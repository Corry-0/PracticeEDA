// 外部电路(Logisim) 2.7 的转换边界。源几何坐标只在这里使用；保存后的模型
// 仍然连接显式端点。内置 TinyXML2 使核心测试可以脱离 GUI 运行。
#include "Digital.h"
#include "tinyxml2.h"
#include <cctype>
#include <cmath>
#include <sstream>

namespace eda::logic {
namespace {
using XY = std::pair<int, int>;
using Attrs = std::map<std::string, std::string>;
constexpr const char *notePrefix = "外部电路 导入：";
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
        throw std::runtime_error("无效或过大的 外部电路 坐标：" + s);
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
    // 省略的控制端只有接到规定常量才与本地模型等价；省略输出没有此项。
    std::map<XY, uint32_t> safeControls;
    std::string reason;
};
void require(bool ok, const std::string &why) {
    if (!ok)
        throw std::runtime_error(why);
}
// 门/复用器的输入按从上到下排序（垂直朝向时从左到右）；
// 对 west/south 方向有意不做简单的旋转。
XY axes(int x, int y, const std::string &f) {
    if (f == "west")
        return {-x, y};
    if (f == "north")
        return {y, -x};
    if (f == "south")
        return {y, x};
    return {x, y};
}
// 把一个 外部电路 元件映射为本地 Part（kind/位宽/端口/未用引脚）。
void mapPartLegacy(Source &s) {
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
            pin("A", 0, 0); // 位宽稍后根据相连端口推断。
        } else if (n == "Tunnel") {
            require(!p.label.empty(), "Tunnel 标签不能为空");
            p.kind = "Text"; // 稍后替换为带名称的模型节点。
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
            require(number(get(a, "inputs", "5")) == 2, "仅支持双输入逻辑门（外部电路 缺省为 5 输入）");
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
        } else if (n == "NOT Gate" || n == "Buffer" || n == "Controlled Buffer" || n == "Controlled Inverter") {
            p.kind = n == "NOT Gate" ? "Not" : n == "Buffer" ? "Buffer" : n == "Controlled Inverter" ? "ControlledInverter" : "TriState";
            int size = n == "NOT Gate" || n == "Controlled Inverter" ? int(number(get(a, "size", "30"))) : 20;
            require(size == 20 || size == 30, "不支持的门尺寸");
            pin("A", -size, 0);
            pin("Y", 0, 0);
            if (p.kind == "TriState" || p.kind == "ControlledInverter") {
                auto control = get(a, "control", "right");
                require(control == "right" || control == "left", "未知控制端方向");
                int side = control == "right" ? 10 : -10;
                // 与门的输入排序不同，带方向的使能端随主体一起旋转。
                if (f == "west" || f == "south")
                    side = -side;
                pin("EN", -(size - 10), side);
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

std::string dataPin(int i) {
    return i == 0 ? "A" : i == 1 ? "B" : "I" + std::to_string(i);
}
int direction(const std::string &f) {
    require(f == "east" || f == "south" || f == "west" || f == "north", "未知方向");
    return f == "south" ? 1 : f == "west" ? 2 : f == "north" ? 3 : 0;
}
// 扩展转换以公开端口/属性为依据。无对应运行语义的控制端仍列入 unused，
// 上层仅在该端实际接线时拒绝整个器件，避免忽略控制信号造成“看似导入成功”。
void mapPart(Source &s) {
    auto &p = s.part;
    const auto &a = s.a;
    const auto &n = s.name;
    const auto &lib = s.lib;
    const auto f = get(a, "facing", "east");
    p.rotation = direction(f);
    p.width = width(a);
    p.label = get(a, "label");
    auto at = [&](int x, int y) {
        auto d = axes(x, y, f);
        return XY{s.at.first + d.first, s.at.second + d.second};
    };
    auto pin = [&](const std::string &id, int x, int y) { s.pins.push_back({at(x, y), id}); };
    auto fixed = [&](const std::string &id, int x, int y) {
        s.pins.push_back({{s.at.first + x, s.at.second + y}, id});
    };
    auto unused = [&](int x, int y) { s.unused.push_back(at(x, y)); };
    auto unusedFixed = [&](int x, int y) { s.unused.push_back({s.at.first + x, s.at.second + y}); };
    auto safeFixed = [&](int x, int y, uint32_t value) { unusedFixed(x, y); s.safeControls[{s.at.first + x, s.at.second + y}] = value; };
    std::set<std::string> allowed{"label", "labelfont", "labelcolor", "labelvisible", "labelloc", "facing", "width"};
    auto allow = [&](std::initializer_list<const char *> keys) { for (auto k : keys) allowed.insert(k); };
    static const std::map<std::string, std::string> gates = {
        {"AND Gate", "And"}, {"OR Gate", "Or"}, {"NAND Gate", "Nand"},
        {"NOR Gate", "Nor"}, {"XOR Gate", "Xor"}, {"XNOR Gate", "Xnor"}};
    if (lib == "#Base" && n == "Text") {
        allow({"text", "font", "halign", "valign"}); p.kind = "Text"; p.label = get(a, "text");
    } else if (lib == "#Gates" && gates.count(n)) {
        allow({"size", "out", "inputs", "xor"});
        require(get(a, "out", "01") == "01", "开集/开漏输出暂不支持");
        const int count = int(number(get(a, "inputs", "5")));
        require(count >= 2 && count <= 32, "门输入数必须为 2–32");
        const int size = int(number(get(a, "size", "50")));
        require(size == 30 || size == 50 || size == 70, "不支持的门尺寸");
        p.kind = gates.at(n); p.params["inputs"] = count;
        if (p.kind == "Xor" || p.kind == "Xnor") {
            auto mode = get(a, "xor", "1");
            require(mode == "1" || mode == "odd", "未知 XOR 多输入行为");
            p.params["xorOne"] = mode == "1";
        }
        const int length = size + ((p.kind == "Xor" || p.kind == "Xnor") ? 10 : 0) +
                           ((p.kind == "Nand" || p.kind == "Nor" || p.kind == "Xnor") ? 10 : 0);
        int start = -5, step = 10, skip = 10;
        if (count <= 3) {
            if (size < 40) { start = -5; step = skip = 10; }
            else if (size < 60 || count <= 2) { start = -10; step = skip = 20; }
            else { start = -15; step = skip = 30; }
        } else if (count == 4 && size >= 60) { start = -5; step = 20; skip = 0; }
        uint32_t mask = 0;
        for (int i = 0; i < count; ++i) {
            auto key = "negate" + std::to_string(i); allowed.insert(key);
            bool inverted = boolean(a, key, false);
            if (inverted) mask |= uint32_t(1) << i;
            int y = (count & 1) ? start * (count - 1) + step * i : start * count + step * i + (i >= count / 2 ? skip : 0);
            pin(dataPin(i), -length - (inverted ? 10 : 0), y);
        }
        p.params["invertMask"] = mask; pin("Y", 0, 0);
    } else if (lib == "#Plexers" && (n == "Multiplexer" || n == "Demultiplexer" || n == "Decoder")) {
        allow({"select", "selloc", "enable", "tristate", "disabled"});
        int bits = width(a, "select", n == "Decoder" ? "2" : "1");
        require(bits <= 8, "选择位宽最多 8 位");
        int count = 1 << bits; p.params["selectBits"] = bits;
        auto selloc = get(a, "selloc", "bl");
        require(selloc == "bl" || selloc == "tr", "未知选择端位置");
        const int side = selloc == "bl" ? 1 : -1;
        require(!boolean(a, "tristate", false), "未选择输出为浮空的模式暂不支持");
        bool enabled = boolean(a, "enable", true);
        if (n == "Decoder") {
            p.kind = "Decoder"; p.width = 1; pin("A", 0, 0);
            bool vertical = f == "north" || f == "south";
            int first = count == 2 ? (vertical ? (side == 1 ? 10 : -30) : (side == 1 ? -30 : 10)) :
                        (vertical ? (side == 1 ? 0 : -10 * count) : (side == 1 ? -10 * count : 0));
            int extent = count == 2 ? 10 : 20;
            for (int i = 0; i < count; ++i) {
                if (vertical) fixed("Y" + std::to_string(i), first + i * (count == 2 ? 20 : 10), f == "north" ? -extent : extent);
                else fixed("Y" + std::to_string(i), f == "west" ? -extent : extent, first + i * (count == 2 ? 20 : 10));
            }
            if (enabled) unused(-10, 0);
        } else {
            bool mux = n == "Multiplexer"; int sign = mux ? -1 : 1;
            p.kind = mux ? "Mux" : "Demux";
            int first = count == 2 ? -10 : -5 * count;
            for (int i = 0; i < count; ++i)
                pin(mux ? dataPin(i) : "Y" + std::to_string(i), sign * (count == 2 ? 30 : 40), first + i * (count == 2 ? 20 : 10));
            bool vertical = f == "north" || f == "south";
            int selectY = side * (count == 2 ? 20 : 5 * count);
            pin("S", sign * 20, vertical ? -selectY : selectY);
            if (enabled) unused(sign * 10, vertical ? -selectY : selectY);
            pin(mux ? "Y" : "A", 0, 0);
        }
    } else if (lib == "#Plexers" && n == "BitSelector") {
        allow({"group"}); p.kind = "BitSelector"; p.width = width(a, "width", "8"); p.params["outputWidth"] = width(a, "group", "1");
        pin("A", -30, 0); pin("Y", 0, 0);
        if (f == "north") fixed("S", -10, 10); else if (f == "south") fixed("S", -10, -10); else if (f == "west") fixed("S", 10, 10); else fixed("S", -10, 10);
    } else if (lib == "#Plexers" && n == "Priority Encoder") {
        allow({"select", "disabled"});
        int bits = width(a, "select", "3"); require(bits <= 8, "编码选择位宽最多 8 位");
        p.kind = "Encoder"; p.width = 1; p.params["selectBits"] = bits;
        int count = 1 << bits, first = -5 * count + 10;
        for (int i = 0; i < count; ++i) pin("A" + std::to_string(i), -40, first + 10 * i);
        pin("Y", 0, 0);
        if (f == "north" || f == "south") fixed("V", 10, 0); else fixed("V", 0, 10);
        // 输入使能与级联输出不属于本地 Encoder 接口；接线时需要诊断。
        unused(-20, first + 10 * count); s.safeControls[at(-20, first + 10 * count)] = 1; unused(-20, first - 10);
        require(get(a, "disabled", "0") == "0", "无有效输入时的浮空编码输出暂不支持，请选择零输出模式");
    } else if (lib == "#Wiring" && n == "Bit Extender") {
        allow({"in_width", "out_width", "type"});
        auto type = get(a, "type", "sign"); require(type == "zero" || type == "sign", "仅支持零/符号扩展");
        p.kind = "Extender"; p.width = width(a, "out_width", "16");
        p.params["inputWidth"] = width(a, "in_width", "8"); p.params["signExtend"] = type == "sign";
        // 经典 Bit Extender 没有朝向属性，几何始终为东向。
        p.rotation = 0; fixed("A", -40, 0); fixed("Y", 0, 0);
    } else if (lib == "#Wiring" && n == "Splitter") {
        allow({"incoming", "fanout", "appear"});
        p.kind = "Splitter"; p.width = width(a, "incoming", "2");
        int fanout = int(number(get(a, "fanout", "2"))); require(fanout >= 1 && fanout <= 32, "分支数必须为 1–32");
        p.bitGroups.resize(fanout);
        for (int i = 0; i < p.width; ++i) {
            auto key = "bit" + std::to_string(i); allowed.insert(key);
            auto value = get(a, key, std::to_string(std::min(fanout - 1, i * fanout / p.width)));
            if (value != "none") { int group = int(number(value)); require(group < fanout, "分线位映射越界"); p.bitGroups[group].push_back(i); }
        }
        for (auto &g : p.bitGroups) require(!g.empty(), "空分线支路暂不支持");
        auto appearance = get(a, "appear", "left");
        require(appearance == "left" || appearance == "right" || appearance == "center" || appearance == "legacy", "未知分线器外观");
        int justify = appearance == "right" ? 1 : appearance == "left" ? -1 : 0;
        pin("A", 0, 0);
        if (f == "north" || f == "south") {
            int m = f == "north" ? 1 : -1;
            int x = justify == 0 ? 10 * ((fanout + 1) / 2 - 1) : m * justify < 0 ? -10 : 10 * fanout;
            for (int i = 0; i < fanout; ++i) fixed("B" + std::to_string(i), x - 10 * i, -m * 20);
        } else {
            int m = f == "west" ? -1 : 1;
            int y = justify == 0 ? -10 * (fanout / 2) : m * justify > 0 ? 10 : -10 * fanout;
            for (int i = 0; i < fanout; ++i) fixed("B" + std::to_string(i), m * 20, y + 10 * i);
        }
    } else if (lib == "#Wiring" && n == "Pull Resistor") {
        allow({"pull"}); auto pull = get(a, "pull", "0");
        require(pull == "0" || pull == "1", "仅支持上拉或下拉，错误值拉电阻不映射");
        p.kind = pull == "1" ? "PullUp" : "PullDown"; p.width = 1; pin("Y", 0, 0);
    } else if (lib == "#Wiring" && n == "Clock") {
        allow({"highDuration", "lowDuration", "phase"});
        p.kind = "Clock"; p.width = 1;
        p.params["highTicks"] = number(get(a, "highDuration", "1"));
        p.params["lowTicks"] = number(get(a, "lowDuration", "1"));
        p.params["phase"] = number(get(a, "phase", "0"));
        require(p.params["highTicks"] > 0 && p.params["lowTicks"] > 0, "时钟持续时间必须大于零"); pin("Y", 0, 0);
    } else if (lib == "#Memory" && (n == "Register" || n == "Counter" || n == "D Flip-Flop" || n == "T Flip-Flop" || n == "J-K Flip-Flop" || n == "S-R Flip-Flop")) {
        allow({"trigger", "max", "ongoal"});
        auto trigger = get(a, "trigger", "rising"); require(trigger == "rising" || trigger == "falling", "仅支持上升/下降沿触发");
        p.params["falling"] = trigger == "falling"; p.rotation = 0;
        if (n == "Register" || n == "Counter") {
            p.kind = n; p.width = width(a, "width", "8");
            fixed("D", -30, 0); fixed("CLK", -20, 20); fixed("Q", 0, 0); fixed("CLR", -10, 20);
            if (n == "Counter") {
                p.params["loadable"] = 1;
                p.params["logisimCounter"] = 1;
                p.params["counterMax"] = number(get(a, "max", "0xffffffff"), 0) & (p.width == 32 ? UINT32_MAX : (uint32_t(1) << p.width) - 1);
                require(get(a, "ongoal", "wrap") == "wrap", "计数器达到上限时仅支持回绕");
                fixed("LOAD", -30, -10); fixed("EN", -30, 10); fixed("CARRY", 0, 10);
            } else { safeFixed(-30, 10, 1); }
        } else {
            p.width = 1;
            static const std::map<std::string, std::string> kinds = {{"D Flip-Flop", "DFF"}, {"T Flip-Flop", "TFF"}, {"J-K Flip-Flop", "JKFF"}, {"S-R Flip-Flop", "SRFF"}};
            p.kind = kinds.at(n);
            bool two = p.kind == "JKFF" || p.kind == "SRFF";
            if (two) { fixed(p.kind == "JKFF" ? "J" : "S", -40, 0); fixed(p.kind == "JKFF" ? "K" : "R", -40, 20); fixed("CLK", -40, 10); }
            else { fixed(p.kind == "DFF" ? "D" : "T", -40, 20); fixed("CLK", -40, 0); }
            fixed("Q", 0, 0); fixed("CLR", -10, 30);
            unusedFixed(0, 20); safeFixed(-30, 30, 0); safeFixed(-20, 30, 1);
        }
    } else if (lib == "#Memory" && (n == "ROM" || n == "RAM")) {
        allow({"addrWidth", "dataWidth", "contents", "bus"});
        p.kind = n; p.rotation = 0; p.width = width(a, "dataWidth", "8");
        int bits = width(a, "addrWidth", "8"); require(bits <= 16, "存储器地址位宽最多 16 位"); p.params["addressBits"] = bits;
        fixed("ADDR", -140, 0); fixed("Q", 0, 0); safeFixed(-90, 40, 1);
        if (n == "RAM") {
            require(get(a, "bus", "combined") == "separate", "双向/异步 RAM 总线尚无等价接口，请使用 separate 模式");
            fixed("D", -140, 20); fixed("WE", -110, 40); fixed("CLK", -70, 40); safeFixed(-50, 40, 1); safeFixed(-30, 40, 0);
            require(!a.count("contents"), "Logisim 2.7 的 RAM 不支持工程内持久化 contents");
        } else if (a.count("contents")) {
            std::istringstream in(a.at("contents")); std::string header, address, data, token;
            require(bool(in >> header >> address >> data) && header == "addr/data:" && number(address) == uint32_t(bits) && number(data) == uint32_t(p.width), "ROM 内容头与位宽不一致");
            while (in >> token) {
                if (token[0] == '#') { std::getline(in, token); continue; }
                auto star = token.find('*'); auto count = star == std::string::npos ? 1U : number(token.substr(0, star));
                auto value = number(star == std::string::npos ? token : token.substr(star + 1), 16);
                require(count > 0 && count <= (size_t(1) << bits) - p.data.size(), "ROM 内容超过容量或重复计数无效");
                require(p.width == 32 || value < (uint32_t(1) << p.width), "ROM 字超出数据位宽");
                p.data.insert(p.data.end(), count, value);
            }
        }
    } else if (lib == "#Arithmetic") {
        allow({"mode", "type", "shift", "numInputs"}); p.rotation = 0;
        static const std::map<std::string, std::string> kinds = {{"Adder", "Add"}, {"Subtractor", "Subtract"}, {"Multiplier", "Multiply"}, {"Divider", "Divide"}, {"Negator", "Negate"}, {"Comparator", "Compare"}, {"BitAdder", "BitAdder"}, {"BitFinder", "BitFinder"}};
        require(kinds.count(n) != 0, "该算术器件的端口/位宽暂未建立等价映射");
        p.kind = kinds.at(n); p.width = width(a, "width", "8");
        if (n == "Negator") { fixed("A", -40, 0); fixed("Y", 0, 0); }
        else if (n == "BitAdder") { require(number(get(a, "numInputs", "1")) == 1, "位计数仅支持单组总线输入"); fixed("A", -40, 0); fixed("Y", 0, 0); }
        else if (n == "BitFinder") {
            static const std::map<std::string, int> modes = {{"low1", 0}, {"high1", 1}, {"low0", 2}, {"high0", 3}};
            auto mode = get(a, "type", "low1"); require(modes.count(mode), "未知位查找模式"); p.params["mode"] = modes.at(mode);
            fixed("A", -40, 0); fixed("Y", 0, 0); fixed("V", -20, 20);
        } else {
            fixed("A", -40, -10); fixed("B", -40, 10);
            if (n == "Comparator") {
                require(get(a, "mode", "twosComplement") == "unsigned", "有符号比较暂不映射");
                fixed("GT", 0, -10); fixed("EQ", 0, 0); fixed("LT", 0, 10);
            } else {
                fixed("Y", 0, 0); safeFixed(-20, -20, 0);
                if (n == "Adder" || n == "Subtractor") fixed("C", -20, 20); else unusedFixed(-20, 20);
            }
        }
    } else if (lib == "#I/O" && (n == "Button" || n == "LED" || n == "Hex Digit Display" || n == "7-Segment Display" || n == "DotMatrix" || n == "Joystick")) {
        allow({"color", "oncolor", "offcolor", "bg", "active", "matrixrows", "matrixcols", "inputtype", "persist", "dotshape"});
        p.rotation = 0;
        if (n == "Button") { p.kind = "Button"; p.width = 1; p.params["momentary"] = 1; fixed("Y", 0, 0); }
        else if (n == "LED") { p.kind = "LED"; p.width = 1; require(boolean(a, "active", true), "低有效 LED 尚无等价属性"); fixed("A", 0, 0); }
        else if (n == "Hex Digit Display") { p.kind = "Hex"; p.width = 4; fixed("A", 0, 0); unusedFixed(10, 0); }
        else if (n == "7-Segment Display") {
            p.kind = "SevenSegment"; p.width = 1; p.params["activeLow"] = !boolean(a, "active", true);
            const int x[] = {20, 30, 20, 10, 0, 10, 0, 30}, y[] = {0, 0, 60, 60, 60, 0, 0, 60};
            for (int i = 0; i < 8; ++i) fixed(i == 7 ? "dp" : std::string(1, char('a' + i)), x[i], y[i]);
        } else if (n == "DotMatrix") {
            p.kind = "LEDMatrix"; p.width = 1; int rows = int(number(get(a, "matrixrows", "7"))), cols = int(number(get(a, "matrixcols", "5")));
            require(rows > 1 && cols > 1 && rows <= 32 && cols <= 32 && get(a, "inputtype", "column") == "select", "LED 矩阵仅支持独立行/列选择模式，维数为2–32");
            require(number(get(a, "persist", "0")) == 0, "LED 矩阵余辉暂不映射");
            p.params["rows"] = rows; p.params["cols"] = cols; fixed("COLS", 0, 0); fixed("ROWS", 0, 10);
        } else { p.kind = "Joystick"; p.width = width(a, "width", "4"); require(p.width >= 2 && p.width <= 5, "Logisim 摇杆仅支持2–5位"); fixed("X", 0, 0); fixed("Y", 0, 10); }
    } else {
        mapPartLegacy(s); return;
    }
    for (const auto &kv : a) require(allowed.count(kv.first) != 0, "未支持的属性：" + kv.first);
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

std::vector<Issue> circDiagnostics(const Circuit &c) {
    std::vector<Issue> result;
    for (const auto &p : c.parts)
        if (p.kind == "Text" && p.label.rfind(notePrefix, 0) == 0)
            result.push_back({"warning", p.id, p.label});
    return result;
}

Project importCirc(const std::string &bytes) {
    require(bytes.size() <= 32 * 1024 * 1024, "工程文件超过 32 MB");
    require(bytes.find('\0') == std::string::npos && bytes.find("<!DOCTYPE") == std::string::npos &&
                bytes.find("<!ENTITY") == std::string::npos,
            "不支持 XML DTD、实体声明或 NUL");
    tinyxml2::XMLDocument doc;
    require(doc.Parse(bytes.data(), bytes.size()) == tinyxml2::XML_SUCCESS,
            "外部电路 XML 解析失败：" + std::string(doc.ErrorStr() ? doc.ErrorStr() : ""));
    auto root = doc.RootElement();
    require(root && std::string(root->Name()) == "project" && !root->NextSiblingElement(),
            "不是 外部电路 工程");
    auto version = attr(root, "source");
    require((version == "2.7" || version.rfind("2.7.", 0) == 0) && attr(root, "version") == "1.0",
            "受限导入仅支持 外部电路 2.7 的 version=1.0 工程");
    std::map<std::string, std::string> libs;
    for (auto e = root->FirstChildElement("lib"); e; e = e->NextSiblingElement("lib"))
        require(!attr(e, "name").empty() && libs.emplace(attr(e, "name"), attr(e, "desc")).second,
                "库编号为空或重复");
    Project p;
    p.circuits.clear();
    auto main = root->FirstChildElement("main");
    require(main && !main->NextSiblingElement("main"), "必须指定唯一 main 电路");
    p.main = attr(main, "name");
    p.name = p.main + "（外部电路 导入）";
    // 第一遍建立所有定义和稳定接口 ID。先建壳再解析内容，因而父电路可以位于子电路之前，
    // 同一子电路的多个实例也会引用同一个定义，而不是错误复制运行状态。
    std::map<const tinyxml2::XMLElement *, std::string> componentIds;
    std::set<std::string> circuitNames;
    std::map<std::string, std::string> appearanceWarnings;
    auto rotated = [](Point pt, int turns) {
        for (int i = 0; i < ((turns % 4 + 4) % 4); ++i) pt = {-pt.y, pt.x};
        return pt;
    };
    for (auto e = root->FirstChildElement("circuit"); e; e = e->NextSiblingElement("circuit")) {
        Circuit c; c.name = attr(e, "name");
        require(!c.name.empty() && circuitNames.insert(c.name).second, "空或重复的电路名称");
        require(p.circuits.size() < 128, "电路数量超过 128");
        std::map<XY, std::string> pinIds;
        std::map<int, std::vector<std::pair<XY, std::string>>> edges;
        for (auto comp = e->FirstChildElement("comp"); comp; comp = comp->NextSiblingElement("comp")) {
            auto id = p.id("p"); componentIds.emplace(comp, id);
            auto lib = libs.find(attr(comp, "lib"));
            if (lib == libs.end() || lib->second != "#Wiring" || attr(comp, "name") != "Pin") continue;
            try {
                auto a = attributes(comp); auto xy = location(attr(comp, "loc"));
                Part q; q.id = id; q.kind = boolean(a, "output", false) ? "Output" : "Input";
                q.width = width(a); q.label = get(a, "label"); q.at = position(xy);
                q.rotation = direction(get(a, "facing", "east"));
                c.parts.push_back(q); pinIds.emplace(xy, id); edges[(q.rotation + 2) % 4].push_back({xy, id});
            } catch (const std::exception &) { /* 第二遍负责保留该元件的具体诊断。 */ }
        }
        auto appearance = e->FirstChildElement("appear");
        if (appearance) {
            Point anchor; int anchorFacing = 0; bool foundAnchor = false;
            auto centre = [](const tinyxml2::XMLElement *shape) {
                return Point{shape->DoubleAttribute("x") + shape->DoubleAttribute("width") / 2,
                             shape->DoubleAttribute("y") + shape->DoubleAttribute("height") / 2};
            };
            for (auto a = appearance->FirstChildElement(); a; a = a->NextSiblingElement())
                if (std::string(a->Name()) == "circ-anchor" || std::string(a->Name()) == "circ-origin") {
                    anchor = centre(a); anchorFacing = direction(attr(a, "facing", "east")); foundAnchor = true;
                }
            if (!foundAnchor) appearanceWarnings[c.name] = "自定义外观缺少锚点，实例接口无法可靠定位";
            for (auto a = appearance->FirstChildElement("circ-port"); a; a = a->NextSiblingElement("circ-port")) {
                auto value = attr(a, "pin"); if (value.empty()) continue;
                auto xy = location(value.front() == '(' ? value : "(" + value + ")");
                auto it = pinIds.find(xy);
                if (it == pinIds.end()) { appearanceWarnings[c.name] = "自定义外观端子未找到对应输入/输出引脚"; continue; }
                c.interfaceLayout[it->second] = rotated((centre(a) - anchor) * 4, -anchorFacing);
            }
            if (c.interfaceLayout.size() != c.parts.size())
                appearanceWarnings[c.name] = "自定义外观没有完整覆盖全部接口，相关实例保留诊断";
            // 几何图形是装饰；仅转入支持的基本 SVG，未知图形给出说明但不改变接口网络。
            for (auto a = appearance->FirstChildElement(); a; a = a->NextSiblingElement()) {
                std::string tag = a->Name(); if (tag.rfind("circ-", 0) == 0) continue;
                Shape shape; shape.id = p.id("s"); shape.appearance = true;
                if (tag == "rect" || tag == "ellipse" || tag == "line") {
                    shape.kind = tag == "rect" ? "Rectangle" : tag == "ellipse" ? "Ellipse" : "Line";
                    Point aa, bb;
                    if (tag == "line") { aa = {a->DoubleAttribute("x1"), a->DoubleAttribute("y1")}; bb = {a->DoubleAttribute("x2"), a->DoubleAttribute("y2")}; }
                    else { aa = {a->DoubleAttribute("x"), a->DoubleAttribute("y")}; bb = aa + Point{a->DoubleAttribute("width"), a->DoubleAttribute("height")}; }
                    shape.a = rotated((aa - anchor) * 4, -anchorFacing); shape.b = rotated((bb - anchor) * 4, -anchorFacing);
                    shape.stroke = std::clamp(a->DoubleAttribute("stroke-width", 2) * 2, 1.0, 20.0);
                    shape.filled = attr(a, "fill", "none") != "none"; c.shapes.push_back(shape);
                }
            }
        } else {
            // 经典 Logisim 默认外观：按引脚朝向分到四边，按原理图位置排序；锚点依东/北/西/南优先。
            for (int side = 0; side < 4; ++side) {
                auto &list = edges[side]; bool horizontal = side == 1 || side == 3;
                std::sort(list.begin(), list.end(), [horizontal](const auto &a, const auto &b) {
                    if (horizontal) return std::tie(a.first.first, a.first.second) < std::tie(b.first.first, b.first.second);
                    return std::tie(a.first.second, a.first.first) < std::tie(b.first.second, b.first.first);
                });
            }
            int north = int(edges[3].size()), south = int(edges[1].size()), east = int(edges[0].size()), west = int(edges[2].size());
            int vert = std::max(north, south), horz = std::max(east, west);
            auto dimension = [](int count, int others) { return count < 3 ? 30 : 10 * count + (others ? 10 : 0); };
            auto offset = [](int count, int opposite, int others) {
                int maximum = std::max(count, opposite); int base = maximum < 2 ? (others ? 10 : 15) : maximum == 2 ? 10 : (others ? 10 : 5);
                return base + 10 * ((maximum - count) / 2);
            };
            int w = dimension(vert, horz), h = dimension(horz, vert);
            int offs[4] = {offset(east, west, vert), offset(south, north, horz), offset(west, east, vert), offset(north, south, horz)};
            Point anchor = east ? Point{double(w), double(offs[0])} : north ? Point{double(offs[3]), 0} : west ? Point{0, double(offs[2])} : south ? Point{double(offs[1]), double(h)} : Point{};
            for (int side = 0; side < 4; ++side) for (size_t i = 0; i < edges[side].size(); ++i) {
                Point pos = side == 0 ? Point{double(w), offs[0] + 10.0 * i} : side == 1 ? Point{offs[1] + 10.0 * i, double(h)} : side == 2 ? Point{0, offs[2] + 10.0 * i} : Point{offs[3] + 10.0 * i, 0};
                c.interfaceLayout[edges[side][i].second] = (pos - anchor) * 4;
            }
        }
        p.circuits.push_back(std::move(c));
    }
    for (auto e = root->FirstChildElement("circuit"); e; e = e->NextSiblingElement("circuit")) {
        Circuit c;
        c.name = attr(e, "name");
        c.interfaceLayout = p.circuit(c.name).interfaceLayout;
        c.shapes = p.circuit(c.name).shapes;
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
            s.part.id = componentIds.at(comp);
            try {
                s.a = attributes(comp);
                if (circuitNames.count(s.name) && !comp->Attribute("lib")) {
                    auto warning = appearanceWarnings.find(s.name);
                    require(warning == appearanceWarnings.end(), warning == appearanceWarnings.end() ? "" : warning->second);
                    s.part.kind = "Subcircuit"; s.part.circuit = s.name;
                    s.part.label = get(s.a, "label"); s.part.rotation = direction(get(s.a, "facing", "east"));
                    for (const auto &port : ports(p, s.part)) {
                        auto offset = port.at * .25;
                        s.pins.push_back({{s.at.first + int(std::lround(offset.x)), s.at.second + int(std::lround(offset.y))}, port.id});
                    }
                } else mapPart(s);
            } catch (const std::exception &error) {
                s.reason = error.what();
            }
            sources.push_back(std::move(s));
        }
        // 对“已接线但无法表示的控制端”整体拒绝该元件。
        // 同时检查所有源引脚，包括稍后被拒绝的元件的引脚。
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
                if (connected && s.safeControls.count(at)) {
                    bool safe = false, incompatible = false;
                    for (const auto &driver : sources) for (const auto &pin : driver.pins) {
                        if (pin.first != at || driver.part.id == s.part.id) continue;
                        if (driver.part.kind == "Constant" || driver.part.kind == "Power" || driver.part.kind == "Ground") {
                            uint32_t value = driver.part.kind == "Power" ? 1 : driver.part.kind == "Ground" ? 0 : driver.part.value;
                            if (value == s.safeControls.at(at)) safe = true; else incompatible = true;
                        } else incompatible = true;
                    }
                    if (safe && !incompatible) connected = false;
                }
                if (connected)
                    s.reason = "未支持的控制端或反相输出已接线，保留占位以免改变行为";
            }
        }
        // 只在真实的端点/端口处建节点：单纯的交叉不是节点。
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
                // 即使元件被降级为占位 Text，其已知端口仍会把源导线断开。
                // Text 对象本身不会凭空产生任何电气引脚。
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
            // 把模型锚点端口放到原来的 外部电路 锚点位置。
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
        // 通过实际连通性推断 Probe 位宽，绝不靠最近的几何位置。
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
        // Logisim 分线器是被动双向连通体；本地模型以明确方向求值。
        // 由外部驱动所在侧判定拆线/合线，不能把来自支路的驱动硬套到单向 Splitter 上。
        std::set<std::string> driven;
        for (const auto &part : c.parts)
            if (part.kind != "Splitter")
                for (const auto &port : ports(p, part))
                    if (port.output) driven.insert(find(Endpoint{part.id, port.id}.key()));
        std::vector<Point> mixedSplitters;
        std::set<std::string> oriented;
        // 拆线器可以串接到合线器：把已经确定的方向继续传播，直到没有新证据。
        // 只检查一次外部驱动会把第二级合线器误判为拆线器，导致总线悬空。
        bool changed = true;
        while(changed) {
          changed = false;
          for (auto &part : c.parts) if (part.kind == "Splitter" && !oriented.count(part.id)) {
            bool busDriven = driven.count(find(Endpoint{part.id, "A"}.key())) != 0, branchDriven = false;
            for (size_t i = 0; i < part.bitGroups.size(); ++i)
                branchDriven |= driven.count(find(Endpoint{part.id, "B" + std::to_string(i)}.key())) != 0;
            if (!busDriven && branchDriven) {
                driven.insert(find(Endpoint{part.id, "A"}.key()));
                part.kind = "Joiner";
                for (auto &w : c.wires) {
                    if (w.a.object == part.id && w.a.pin == "A") w.a.pin = "Y";
                    if (w.b.object == part.id && w.b.pin == "A") w.b.pin = "Y";
                }
            } else if (busDriven) {
                if(branchDriven) mixedSplitters.push_back(part.at + Point{0, -80});
                for(size_t i=0;i<part.bitGroups.size();++i)
                    driven.insert(find(Endpoint{part.id,"B"+std::to_string(i)}.key()));
            }
            if(busDriven || branchDriven) { oriented.insert(part.id); changed=true; }
          }
        }
        for (auto at : mixedSplitters)
            note(p, c, at, "分线器两侧均有驱动：混合双向逐位传输不能完全映射，请检查此处");
        parent.clear();
        for (auto &w : c.wires) { auto a = find(w.a.key()), b = find(w.b.key()); parent[a] = b; }
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
             "Logisim 2.7 转换；已保留电气网络、受支持属性和子电路定义/实例。布局按本地符号重建；"
             "传播延迟及 X/Z 细节按本程序运行。请检查逐项诊断后保存。");
        // 被拒绝的输入/输出不能留下悬空外观 ID。
        for (auto it = c.interfaceLayout.begin(); it != c.interfaceLayout.end(); )
            if (!c.part(it->first)) it = c.interfaceLayout.erase(it); else ++it;
        p.circuit(c.name) = std::move(c);
    }
    // 复用完全一致的原生导入校验与持久化契约。
    return deserialize(serialize(p));
}
} // namespace eda::logic
