// Logisim 2.7 conversion boundary. Source geometry is used only here; the saved model
// still connects explicit endpoints. TinyXML2 is vendored to keep core tests GUI-free.
#include "Digital.h"
#include "tinyxml2.h"
#include <cctype>
#include <sstream>

namespace eda::logic {
namespace {
using XY = std::pair<int, int>;
using Attrs = std::map<std::string, std::string>;
constexpr const char *notePrefix = "Logisim 导入：";
std::string attr(const tinyxml2::XMLElement *e, const char *key, const char *fallback = "") {
    auto s = e->Attribute(key);
    return s ? s : fallback;
}
std::string get(const Attrs &a, const std::string &key, const std::string &fallback = "") {
    auto it = a.find(key);
    return it == a.end() ? fallback : it->second;
}
uint32_t number(const std::string &s, int base = 10) {
    if (s.empty() || s[0] == '-' || s[0] == '+' || std::isspace(static_cast<unsigned char>(s[0])))
        throw std::runtime_error("非法整数：" + s);
    size_t end = 0;
    auto value = std::stoull(s, &end, base);
    if (end != s.size() || value > UINT32_MAX)
        throw std::runtime_error("整数超出范围：" + s);
    return uint32_t(value);
}
int width(const Attrs &a, const std::string &key = "width", const char *fallback = "1") {
    auto n = number(get(a, key, fallback));
    if (n < 1 || n > 32)
        throw std::runtime_error("位宽必须为 1–32：" + key);
    return int(n);
}
bool boolean(const Attrs &a, const std::string &key, bool fallback) {
    auto s = get(a, key, fallback ? "true" : "false");
    if (s != "true" && s != "false")
        throw std::runtime_error("非法布尔属性：" + key);
    return s == "true";
}
XY location(const std::string &s) {
    std::istringstream in(s);
    char a, b, c;
    long long x, y;
    if (!(in >> a >> x >> b >> y >> c) || a != '(' || b != ',' || c != ')' || (in >> std::ws, !in.eof()) ||
        x < -200000 || x > 200000 || y < -200000 || y > 200000)
        throw std::runtime_error("无效或过大的 Logisim 坐标：" + s);
    return {int(x), int(y)};
}
Point position(XY p) {
    return {p.first * 4.0, p.second * 4.0};
}
Attrs attributes(const tinyxml2::XMLElement *e) {
    Attrs a;
    for (auto n = e->FirstChildElement("a"); n; n = n->NextSiblingElement("a")) {
        auto key = attr(n, "name");
        if (key.empty() || !a.emplace(key, attr(n, "val", n->GetText() ? n->GetText() : "")).second)
            throw std::runtime_error("空或重复的元件属性");
    }
    return a;
}
struct Source {
    std::string name, lib;
    XY at;
    Attrs a;
    Part part;
    std::vector<std::pair<XY, std::string>> pins;
    std::vector<XY> unused;
    std::string reason;
};
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
// Gates/plexers order inputs from top to bottom (left to right for vertical
// facing); this is intentionally not a simple rotation for west/south.
XY axes(int x, int y, const std::string &f) {
    if (f == "west")
        return {-x, y};
    if (f == "north")
        return {y, -x};
    if (f == "south")
        return {y, x};
    return {x, y};
}
void mapPart(Source &s) {
    auto &p = s.part;
    const auto &a = s.a;
    const auto &n = s.name;
    const auto &lib = s.lib;
    auto f = get(a, "facing", "east");
    require(f == "east" || f == "west" || f == "north" || f == "south", "未知方向");
    p.width = width(a);
    p.label = get(a, "label");
    auto absolute = [&](int x, int y) {
        auto d = axes(x, y, f);
        return XY{s.at.first + d.first, s.at.second + d.second};
    };
    auto pin = [&](const std::string &id, int x, int y) { s.pins.push_back({absolute(x, y), id}); };
    auto unused = [&](int x, int y) { s.unused.push_back(absolute(x, y)); };
    std::set<std::string> allowed{"label",    "labelfont", "labelcolor", "labelvisible",
                                  "labelloc", "facing",    "width"};
    auto allow = [&](std::initializer_list<const char *> keys) {
        for (auto key : keys)
            allowed.insert(key);
    };
    if (lib == "#Wiring") {
        if (n == "Pin") {
            allow({"output", "tristate", "pull", "radix"});
            require(get(a, "pull", "none") == "none", "输入引脚上拉/下拉暂不支持");
            p.kind = boolean(a, "output", false) ? "Output" : "Input";
            pin(p.kind == "Input" ? "Y" : "A", 0, 0);
        } else if (n == "Constant" || n == "Clock" || n == "Power" || n == "Ground") {
            p.kind = n;
            allow({"value", "highDuration", "lowDuration"});
            if (n == "Constant")
                p.value = number(get(a, "value", "0x1"), 0);
            if (n == "Clock") {
                require(p.width == 1, "时钟必须为单比特");
                require(number(get(a, "highDuration", "1")) == 1 && number(get(a, "lowDuration", "1")) == 1,
                        "非对称或多周期时钟暂不支持");
            }
            pin("Y", 0, 0);
        } else if (n == "Probe") {
            allow({"radix"});
            p.kind = "Probe";
            pin("A", 0, 0); // Width is inferred from connected ports below.
        } else if (n == "Tunnel") {
            require(!p.label.empty(), "Tunnel 标签不能为空");
            p.kind = "Text"; // Replaced with a named model junction below.
        } else if (n == "Bit Extender") {
            allow({"in_width", "out_width", "type"});
            require(f == "east" && width(a, "in_width", "8") == 1 && get(a, "type", "sign") == "zero",
                    "仅支持 1 位输入的零扩展器");
            p.kind = "Extender";
            p.width = width(a, "out_width", "16");
            pin("A", -40, 0);
            pin("Y", 0, 0);
        } else
            throw std::runtime_error("该 Wiring 元件暂不支持（分线器为双向，不能直接替换为单向元件）");
    } else if (lib == "#Gates") {
        allow({"size", "out", "inputs", "xor", "control"});
        require(get(a, "out", "01") == "01", "开集/开漏输出暂不支持");
        static const std::map<std::string, std::string> gates = {
            {"AND Gate", "And"}, {"OR Gate", "Or"},   {"NAND Gate", "Nand"},
            {"NOR Gate", "Nor"}, {"XOR Gate", "Xor"}, {"XNOR Gate", "Xnor"}};
        if (gates.count(n)) {
            p.kind = gates.at(n);
            require(number(get(a, "inputs", "5")) == 2, "仅支持双输入逻辑门（Logisim 缺省为 5 输入）");
            int size = int(number(get(a, "size", "50")));
            require(size == 30 || size == 50 || size == 70, "不支持的门尺寸");
            int length = size + ((p.kind == "Xor" || p.kind == "Xnor") ? 10 : 0) +
                         ((p.kind == "Nand" || p.kind == "Nor" || p.kind == "Xnor") ? 10 : 0);
            pin("A", -length, size == 30 ? -10 : -20);
            pin("B", -length, size == 30 ? 10 : 20);
            pin("Y", 0, 0);
            for (int i = 0; i < 2; ++i) {
                auto key = "negate" + std::to_string(i);
                allowed.insert(key);
                require(!boolean(a, key, false), "输入端反相暂不支持");
            }
        } else if (n == "NOT Gate" || n == "Buffer" || n == "Controlled Buffer") {
            p.kind = n == "NOT Gate" ? "Not" : n == "Buffer" ? "Buffer" : "TriState";
            int size = n == "NOT Gate" ? int(number(get(a, "size", "30"))) : 20;
            require(size == 20 || size == 30, "不支持的门尺寸");
            pin("A", -size, 0);
            pin("Y", 0, 0);
            if (p.kind == "TriState") {
                auto control = get(a, "control", "right");
                require(control == "right" || control == "left", "未知控制端方向");
                int side = control == "right" ? 10 : -10;
                // Unlike gate input ordering, the handed control rotates with the body.
                if (f == "west" || f == "south")
                    side = -side;
                pin("EN", -10, side);
            }
        } else
            throw std::runtime_error("该 Gates 元件暂不支持");
    } else if (lib == "#Plexers") {
        allow({"select", "selloc", "enable", "tristate", "disabled"});
        auto sel = get(a, "selloc", "bl");
        require(sel == "bl" || sel == "tr", "未知选择端位置");
        bool enable = boolean(a, "enable", true);
        require(!boolean(a, "tristate", false), "三态未选输出暂不支持");
        int side = sel == "bl" ? 1 : -1;
        if (n == "Multiplexer" || n == "Demultiplexer") {
            require(width(a, "select") == 1, "仅支持 2:1 / 1:2 复用器");
            bool mux = n == "Multiplexer";
            p.kind = mux ? "Mux" : "Demux";
            int direction = mux ? -1 : 1;
            pin(mux ? "A" : "Y0", direction * 30, -10);
            pin(mux ? "B" : "Y1", direction * 30, 10);
            int y = (f == "north" || f == "south") ? -side * 20 : side * 20;
            pin("S", direction * 20, y);
            if (enable)
                unused(direction * 10, y);
            pin(mux ? "Y" : "A", 0, 0);
        } else if (n == "Decoder") {
            require(width(a, "select") == 2, "仅支持 2:4 译码器");
            p.kind = "Decoder";
            p.width = 1;
            pin("A", 0, 0);
            bool vertical = f == "north" || f == "south";
            int start = (vertical ? sel == "tr" : sel == "bl") ? -40 : 0;
            for (int i = 0; i < 4; ++i)
                pin("Y" + std::to_string(i), 20, start + i * 10);
            if (enable)
                unused(-10, 0);
        } else
            throw std::runtime_error("该 Plexers 元件暂不支持");
    } else if (lib == "#Memory") {
        allow({"trigger", "addrWidth", "dataWidth", "contents", "bus"});
        require(f == "east", "Memory 元件仅支持原始固定方向");
        require(get(a, "trigger", "rising") == "rising", "仅支持上升沿触发");
        static const std::map<std::string, std::string> flipflops = {{"D Flip-Flop", "DFF"},
                                                                     {"T Flip-Flop", "TFF"},
                                                                     {"J-K Flip-Flop", "JKFF"},
                                                                     {"S-R Flip-Flop", "SRFF"}};
        if (n == "Register") {
            p.kind = "Register";
            p.width = width(a, "width", "8");
            pin("D", -30, 0);
            pin("CLK", -20, 20);
            pin("Q", 0, 0);
            unused(-10, 20);
            unused(-30, 10);
        } else if (flipflops.count(n)) {
            p.kind = flipflops.at(n);
            require(p.width == 1, "触发器必须为单比特");
            if (p.kind == "DFF" || p.kind == "TFF") {
                pin(p.kind == "DFF" ? "D" : "T", -40, 20);
                pin("CLK", -40, 0);
            } else {
                pin(p.kind == "JKFF" ? "J" : "S", -40, 0);
                pin(p.kind == "JKFF" ? "K" : "R", -40, 20);
                pin("CLK", -40, 10);
            }
            pin("Q", 0, 0);
            unused(0, 20);
            unused(-10, 30);
            unused(-30, 30);
            unused(-20, 30);
        } else if (n == "ROM" || n == "RAM") {
            require(width(a, "addrWidth", "8") == 8, "存储器地址宽度仅支持 8 位（256 字）");
            p.kind = n;
            p.width = width(a, "dataWidth", "8");
            pin("ADDR", -140, 0);
            pin("Q", 0, 0);
            unused(-90, 40);
            if (n == "RAM") {
                require(get(a, "bus", "combined") == "separate", "RAM 仅支持 separate 独立读写端口");
                pin("D", -140, 20);
                pin("WE", -110, 40);
                pin("CLK", -70, 40);
                unused(-50, 40);
                unused(-30, 40);
            } else if (a.count("contents")) {
                std::istringstream in(a.at("contents"));
                std::string header, addr, data, token;
                require(bool(in >> header >> addr >> data) && header == "addr/data:" && number(addr) == 8 &&
                            number(data) == uint32_t(p.width),
                        "ROM 内容头与位宽不一致");
                while (in >> token) {
                    if (token[0] == '#') {
                        std::getline(in, token);
                        continue;
                    }
                    auto star = token.find('*');
                    auto count = star == std::string::npos ? 1U : number(token.substr(0, star));
                    auto val = number(star == std::string::npos ? token : token.substr(star + 1), 16);
                    require(count > 0 && count <= 256 - p.data.size(), "ROM 内容超过 256 字或重复计数无效");
                    require(p.width == 32 || val < (uint32_t(1) << p.width), "ROM 字超出数据位宽");
                    p.data.insert(p.data.end(), count, val);
                }
            }
        } else
            throw std::runtime_error("该 Memory 元件暂不支持");
    } else
        throw std::runtime_error("未支持的库或子电路实例：" + lib);
    for (const auto &kv : a)
        require(allowed.count(kv.first) != 0, "未支持的属性：" + kv.first);
}
bool onSegment(XY p, XY a, XY b) {
    return ((a.first == b.first && p.first == a.first) || (a.second == b.second && p.second == a.second)) &&
           p.first >= std::min(a.first, b.first) && p.first <= std::max(a.first, b.first) &&
           p.second >= std::min(a.second, b.second) && p.second <= std::max(a.second, b.second);
}
void note(Project &p, Circuit &c, Point at, const std::string &message) {
    Part q;
    q.id = p.id("p");
    q.kind = "Text";
    q.at = at;
    q.label = notePrefix + message;
    c.parts.push_back(std::move(q));
}
} // namespace

std::vector<Issue> logisimDiagnostics(const Circuit &c) {
    std::vector<Issue> result;
    for (const auto &p : c.parts)
        if (p.kind == "Text" && p.label.rfind(notePrefix, 0) == 0)
            result.push_back({"warning", p.id, p.label});
    return result;
}

Project importLogisim(const std::string &bytes) {
    require(bytes.size() <= 32 * 1024 * 1024, "工程文件超过 32 MB");
    require(bytes.find('\0') == std::string::npos && bytes.find("<!DOCTYPE") == std::string::npos &&
                bytes.find("<!ENTITY") == std::string::npos,
            "不支持 XML DTD、实体声明或 NUL");
    tinyxml2::XMLDocument doc;
    require(doc.Parse(bytes.data(), bytes.size()) == tinyxml2::XML_SUCCESS,
            "Logisim XML 解析失败：" + std::string(doc.ErrorStr() ? doc.ErrorStr() : ""));
    auto root = doc.RootElement();
    require(root && std::string(root->Name()) == "project" && !root->NextSiblingElement(),
            "不是 Logisim 工程");
    auto version = attr(root, "source");
    require((version == "2.7" || version.rfind("2.7.", 0) == 0) && attr(root, "version") == "1.0",
            "受限导入仅支持 Logisim 2.7 的 version=1.0 工程");
    std::map<std::string, std::string> libs;
    for (auto e = root->FirstChildElement("lib"); e; e = e->NextSiblingElement("lib"))
        require(!attr(e, "name").empty() && libs.emplace(attr(e, "name"), attr(e, "desc")).second,
                "库编号为空或重复");
    Project p;
    p.circuits.clear();
    auto main = root->FirstChildElement("main");
    require(main && !main->NextSiblingElement("main"), "必须指定唯一 main 电路");
    p.main = attr(main, "name");
    p.name = p.main + "（Logisim 导入）";
    for (auto e = root->FirstChildElement("circuit"); e; e = e->NextSiblingElement("circuit")) {
        require(p.circuits.size() < 128, "电路数量超过 128");
        Circuit c;
        c.name = attr(e, "name");
        std::vector<Source> sources;
        std::vector<std::pair<XY, XY>> segments;
        for (auto w = e->FirstChildElement("wire"); w; w = w->NextSiblingElement("wire")) {
            require(segments.size() < 40000, "导线数量超过 40000");
            auto a = location(attr(w, "from")), b = location(attr(w, "to"));
            if (a == b || (a.first != b.first && a.second != b.second)) {
                note(p, c, position(a), "已跳过零长度或斜向导线 " + attr(w, "from") + " → " + attr(w, "to"));
                continue;
            }
            segments.push_back({a, b});
        }
        for (auto comp = e->FirstChildElement("comp"); comp; comp = comp->NextSiblingElement("comp")) {
            require(sources.size() < 10000, "元件数量超过 10000");
            Source s;
            s.name = attr(comp, "name");
            s.at = location(attr(comp, "loc"));
            auto lib = libs.find(attr(comp, "lib"));
            s.lib = lib == libs.end() ? attr(comp, "lib", "子电路") : lib->second;
            s.part.id = p.id("p");
            try {
                s.a = attributes(comp);
                mapPart(s);
            } catch (const std::exception &error) {
                s.reason = error.what();
            }
            sources.push_back(std::move(s));
        }
        // Reject whole components with connected controls that cannot be represented.
        // Check all source pins too, including pins of components later rejected.
        std::map<XY, std::set<size_t>> pinOwners;
        for (size_t i = 0; i < sources.size(); ++i) {
            pinOwners[sources[i].at].insert(i);
            for (auto &pin : sources[i].pins)
                pinOwners[pin.first].insert(i);
            for (auto at : sources[i].unused)
                pinOwners[at].insert(i);
        }
        for (auto &s : sources) {
            for (auto at : s.unused) {
                auto owner = pinOwners.find(at);
                bool connected = owner != pinOwners.end() && owner->second.size() > 1;
                for (auto &line : segments) {
                    if (connected)
                        break;
                    connected = onSegment(at, line.first, line.second);
                }
                if (connected)
                    s.reason = "未支持的控制端或反相输出已接线，保留占位以免改变行为";
            }
        }
        // Nodes only at actual endpoints/ports: a bare crossing is NOT a junction.
        std::map<XY, std::string> nodes;
        auto node = [&](XY at) -> std::string {
            auto it = nodes.find(at);
            if (it != nodes.end())
                return it->second;
            require(nodes.size() < 40000, "转换后的节点超过 40000");
            auto id = p.id("j");
            nodes.emplace(at, id);
            c.nodes.push_back({id, "", position(at)});
            return id;
        };
        auto wire = [&](Endpoint a, Endpoint b) {
            require(c.wires.size() < 40000, "转换后的导线超过 40000");
            c.wires.push_back({p.id("w"), std::move(a), std::move(b), {}});
        };
        for (auto &line : segments) {
            node(line.first);
            node(line.second);
        }
        std::set<std::string> labels;
        std::map<std::string, std::pair<std::string, int>> tunnels;
        for (auto &s : sources) {
            if (!s.reason.empty()) {
                // Known ports still split source wires even when their component is a
                // placeholder. No electrical pin is invented on the Text object itself.
                for (auto &pin : s.pins)
                    node(pin.first);
                for (auto at : s.unused)
                    node(at);
                std::string detail;
                for (auto &kv : s.a)
                    detail += " " + kv.first + "=" + kv.second;
                note(p, c, position(s.at),
                     c.name + " / " + s.name + " (" + s.lib + ") @(" + std::to_string(s.at.first) + "," +
                         std::to_string(s.at.second) + ")：" + s.reason + "；原属性：" + detail);
                continue;
            }
            if (s.lib == "#Wiring" && s.name == "Tunnel") {
                auto id = node(s.at);
                auto found = tunnels.find(s.part.label);
                if (found == tunnels.end())
                    tunnels.emplace(s.part.label, std::make_pair(id, s.part.width));
                else if (found->second.second == s.part.width)
                    wire({id, ""}, {found->second.first, ""});
                else
                    note(p, c, position(s.at), "Tunnel " + s.part.label + " 位宽冲突，未合并");
                s.part.at = position(s.at);
                s.part.label = "Tunnel: " + s.part.label;
                c.parts.push_back(s.part);
                continue;
            }
            if (s.part.kind == "Input" || s.part.kind == "Output") {
                auto label = s.part.label.empty() ? s.part.id : s.part.label;
                if (!labels.insert(label).second) {
                    note(p, c, position(s.at) + Point{0, -60}, "重复引脚标签已改名：" + label);
                    do {
                        label += "_" + s.part.id;
                    } while (!labels.insert(label).second);
                    s.part.label = label;
                }
            }
            s.part.at = position(s.at);
            // Place the model's anchor port at the original Logisim anchor.
            for (auto &pin : s.pins)
                if (pin.first == s.at) {
                    for (auto &port : ports(p, s.part))
                        if (port.id == pin.second)
                            s.part.at = position(s.at) - port.at;
                    break;
                }
            c.parts.push_back(s.part);
            for (auto &pin : s.pins)
                wire({s.part.id, pin.second}, {node(pin.first), ""});
        }
        std::map<int, std::map<int, std::string>> rows, columns;
        for (auto &kv : nodes) {
            rows[kv.first.second][kv.first.first] = kv.second;
            columns[kv.first.first][kv.first.second] = kv.second;
        }
        std::set<std::pair<std::string, std::string>> connected;
        for (auto &line : segments) {
            bool horizontal = line.first.second == line.second.second;
            auto &axis = horizontal ? rows.at(line.first.second) : columns.at(line.first.first);
            int low = horizontal ? line.first.first : line.first.second;
            int high = horizontal ? line.second.first : line.second.second;
            if (low > high)
                std::swap(low, high);
            std::string previous;
            for (auto it = axis.lower_bound(low); it != axis.end() && it->first <= high; ++it) {
                if (!previous.empty() && connected.emplace(previous, it->second).second)
                    wire({previous, ""}, {it->second, ""});
                previous = it->second;
            }
        }
        // Infer Probe width through actual connectivity, never by nearest geometry.
        std::map<std::string, std::string> parent;
        std::function<std::string(const std::string &)> find = [&](const std::string &id) {
            auto it = parent.emplace(id, id).first;
            auto rootId = id;
            while (parent.at(rootId) != rootId)
                rootId = parent.at(rootId);
            it->second = rootId;
            return rootId;
        };
        for (auto &w : c.wires) {
            auto a = find(w.a.key()), b = find(w.b.key());
            parent[a] = b;
        }
        std::map<std::string, int> widths;
        for (auto &part : c.parts)
            if (part.kind != "Probe")
                for (auto &port : ports(p, part))
                    widths[find(Endpoint{part.id, port.id}.key())] = port.width;
        for (auto &part : c.parts)
            if (part.kind == "Probe") {
                auto it = widths.find(find(Endpoint{part.id, "A"}.key()));
                if (it != widths.end())
                    part.width = it->second;
            }
        note(p, c, {0, -120},
             "受限转换；引脚初值为 0，时序与 X/Z 按 PracticeEDA "
             "运行；布局已缩放，子电路实例不展开。请检查诊断后另存为 .logic.json。");
        p.circuits.push_back(std::move(c));
    }
    // Reuse the exact native import validation and persistence contract.
    return deserialize(serialize(p));
}
} // namespace eda::logic
