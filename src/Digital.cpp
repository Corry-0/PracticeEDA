// Digital model, persistence and event simulation. Geometry never determines electrical connectivity.
#include "Digital.h"
#include "json.hpp"
#include <deque>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <unordered_map>

namespace eda::logic {
using json = nlohmann::json;
namespace {
uint32_t mask(int w) {
    return w == 32 ? UINT32_MAX : (uint32_t(1) << w) - 1;
}
uint64_t integer(const json &j, uint64_t maximum) {
    if (!j.is_number_integer() || (!j.is_number_unsigned() && j.get<int64_t>() < 0))
        throw std::runtime_error("工程中的数值必须是非负整数");
    auto n = j.get<uint64_t>();
    if (n > maximum)
        throw std::runtime_error("工程中的数值超出范围");
    return n;
}
bool sequential(const std::string &k) {
    return k == "DFF" || k == "TFF" || k == "JKFF" || k == "SRFF" || k == "Register" || k == "Counter" ||
           k == "ShiftRegister" || k == "RAM" || k == "Clock";
}
bool source(const std::string &k) {
    return k == "Input" || k == "Button" || k == "Clock" || k == "Constant" || k == "Power" || k == "Ground";
}
std::string portName(const Part &p) {
    return p.label.empty() ? p.id : p.label;
}
json point(Point p) {
    return {p.x, p.y};
}
Point point(const json &p) {
    if (!p.is_array() || p.size() != 2)
        throw std::runtime_error("坐标格式错误");
    return {p.at(0).get<double>(), p.at(1).get<double>()};
}
json endpointJson(const Endpoint &e) {
    return {e.object, e.pin};
}
Endpoint endpointJson(const json &j) {
    if (!j.is_array() || j.size() != 2)
        throw std::runtime_error("端点格式错误");
    return {j.at(0).get<std::string>(), j.at(1).get<std::string>()};
}
} // namespace
const std::vector<PartInfo> &library() {
    static const std::vector<PartInfo> v = {{"Input", "输入引脚", "线路 (Wiring)"},
                                            {"Output", "输出引脚", "线路 (Wiring)"},
                                            {"Constant", "常量", "线路 (Wiring)"},
                                            {"Clock", "时钟", "线路 (Wiring)"},
                                            {"Power", "电源 1", "线路 (Wiring)"},
                                            {"Ground", "接地 0", "线路 (Wiring)"},
                                            {"Splitter", "总线分线器", "线路 (Wiring)"},
                                            {"Joiner", "总线合并器", "线路 (Wiring)"},
                                            {"Extender", "位扩展器 (零扩展)", "线路 (Wiring)"},
                                            {"Probe", "探针", "线路 (Wiring)"},
                                            {"Not", "非门 NOT", "逻辑门 (Gates)"},
                                            {"Buffer", "缓冲器", "逻辑门 (Gates)"},
                                            {"And", "与门 AND", "逻辑门 (Gates)"},
                                            {"Or", "或门 OR", "逻辑门 (Gates)"},
                                            {"Nand", "与非门 NAND", "逻辑门 (Gates)"},
                                            {"Nor", "或非门 NOR", "逻辑门 (Gates)"},
                                            {"Xor", "异或门 XOR", "逻辑门 (Gates)"},
                                            {"Xnor", "同或门 XNOR", "逻辑门 (Gates)"},
                                            {"TriState", "三态缓冲器", "逻辑门 (Gates)"},
                                            {"Mux", "2:1 多路选择器", "复用器 (Plexers)"},
                                            {"Demux", "1:2 解复用器", "复用器 (Plexers)"},
                                            {"Decoder", "2:4 译码器", "复用器 (Plexers)"},
                                            {"Encoder", "4:2 优先编码器", "复用器 (Plexers)"},
                                            {"Add", "加法器", "运算器 (Arithmetic)"},
                                            {"Subtract", "减法器", "运算器 (Arithmetic)"},
                                            {"Multiply", "乘法器", "运算器 (Arithmetic)"},
                                            {"Divide", "除法器", "运算器 (Arithmetic)"},
                                            {"Negate", "取负器", "运算器 (Arithmetic)"},
                                            {"Compare", "比较器", "运算器 (Arithmetic)"},
                                            {"ShiftLeft", "逻辑左移", "运算器 (Arithmetic)"},
                                            {"ShiftRight", "逻辑右移", "运算器 (Arithmetic)"},
                                            {"DFF", "D 触发器", "存储 (Memory)"},
                                            {"TFF", "T 触发器", "存储 (Memory)"},
                                            {"JKFF", "JK 触发器", "存储 (Memory)"},
                                            {"SRFF", "SR 触发器", "存储 (Memory)"},
                                            {"Register", "寄存器", "存储 (Memory)"},
                                            {"Counter", "计数器", "存储 (Memory)"},
                                            {"ShiftRegister", "移位寄存器", "存储 (Memory)"},
                                            {"RAM", "RAM (256 字)", "存储 (Memory)"},
                                            {"ROM", "ROM (256 字)", "存储 (Memory)"},
                                            {"Button", "按钮 (双击切换)", "输入/输出 (Input/Output)"},
                                            {"LED", "LED", "输入/输出 (Input/Output)"},
                                            {"Hex", "十六进制显示", "输入/输出 (Input/Output)"},
                                            {"Text", "文字标签", "基本 (Base)"}};
    return v;
}
Part *Circuit::part(const std::string &id) {
    for (auto &p : parts)
        if (p.id == id)
            return &p;
    return nullptr;
}
const Part *Circuit::part(const std::string &id) const {
    for (auto &p : parts)
        if (p.id == id)
            return &p;
    return nullptr;
}
std::string Project::id(const std::string &prefix) {
    return prefix + std::to_string(nextId++);
}
Circuit &Project::circuit(const std::string &n) {
    for (auto &c : circuits)
        if (c.name == n)
            return c;
    throw std::runtime_error("子电路不存在：" + n);
}
const Circuit &Project::circuit(const std::string &n) const {
    for (auto &c : circuits)
        if (c.name == n)
            return c;
    throw std::runtime_error("子电路不存在：" + n);
}
std::vector<Port> ports(const Project &project, const Part &p) {
    std::vector<Port> v;
    auto in = [&](std::string n, int w = 0) { v.push_back({n, false, w ? w : p.width, {}}); };
    auto out = [&](std::string n, int w = 0) { v.push_back({n, true, w ? w : p.width, {}}); };
    const auto &k = p.kind;
    if (k == "Subcircuit") {
        for (const auto &q : project.circuit(p.circuit).parts) {
            if (q.kind == "Input")
                in(q.id, q.width);
            if (q.kind == "Output")
                out(q.id, q.width);
        }
    } else if (source(k))
        out("Y");
    else if (k == "Output" || k == "LED" || k == "Hex" || k == "Probe")
        in("A");
    else if (k == "Text") {
    } else if (k == "Splitter") {
        in("A");
        for (int i = 0; i < p.width; ++i)
            out("B" + std::to_string(i), 1);
    } else if (k == "Joiner") {
        for (int i = 0; i < p.width; ++i)
            in("B" + std::to_string(i), 1);
        out("Y");
    } else if (k == "Extender") {
        in("A", 1);
        out("Y");
    } else if (k == "Mux") {
        in("A");
        in("B");
        in("S", 1);
        out("Y");
    } else if (k == "Demux") {
        in("A");
        in("S", 1);
        out("Y0");
        out("Y1");
    } else if (k == "Decoder") {
        in("A", 2);
        for (int i = 0; i < 4; ++i)
            out("Y" + std::to_string(i), 1);
    } else if (k == "Encoder") {
        for (int i = 0; i < 4; ++i)
            in("A" + std::to_string(i), 1);
        out("Y", 2);
        out("V", 1);
    } else if (k == "TriState") {
        in("A");
        in("EN", 1);
        out("Y");
    } else if (k == "RAM" || k == "ROM") {
        in("ADDR", 8);
        if (k == "RAM") {
            in("D");
            in("WE", 1);
            in("CLK", 1);
        }
        out("Q");
    } else if (sequential(k)) {
        if (k == "DFF" || k == "Register")
            in("D");
        if (k == "TFF")
            in("T");
        if (k == "JKFF") {
            in("J");
            in("K");
        }
        if (k == "SRFF") {
            in("S");
            in("R");
        }
        if (k == "Counter")
            in("EN", 1);
        if (k == "ShiftRegister")
            in("D", 1);
        in("CLK", 1);
        out("Q");
    } else {
        in("A");
        if (k != "Not" && k != "Buffer" && k != "Negate")
            in("B");
        if (k == "Compare") {
            out("LT", 1);
            out("EQ", 1);
            out("GT", 1);
        } else {
            out("Y");
            if (k == "Add" || k == "Subtract")
                out("C", 1);
        }
    }
    int ni = 0, no = 0;
    for (auto &q : v)
        (q.output ? no : ni)++;
    int ii = 0, oi = 0;
    for (auto &q : v)
        q.at = {q.output ? 70.0 : -70.0,
                20.0 * ((q.output ? oi++ : ii++) - ((q.output ? no : ni) - 1) * 0.5)};
    return v;
}
double partHeight(const Project &p, const Part &part) {
    auto v = ports(p, part);
    size_t in = 0, out = 0;
    for (auto &q : v)
        (q.output ? out : in)++;
    return std::max(60.0, 20.0 * std::max(in, out) + 20);
}
Point endpoint(const Project &p, const Circuit &c, const Endpoint &e) {
    if (auto part = c.part(e.object))
        for (auto &port : ports(p, *part))
            if (port.id == e.pin)
                return part->at + port.at;
    for (auto &n : c.nodes)
        if (n.id == e.object && e.pin.empty())
            return n.at;
    throw std::runtime_error("无效的连接端点：" + e.key());
}
std::vector<Point> wirePoints(const Project &p, const Circuit &c, const Wire &w) {
    std::vector<Point> v{endpoint(p, c, w.a)};
    v.insert(v.end(), w.bends.begin(), w.bends.end());
    v.push_back(endpoint(p, c, w.b));
    return orthogonal(v);
}
void erase(Circuit &c, const std::set<std::string> &ids) {
    c.parts.erase(
        std::remove_if(c.parts.begin(), c.parts.end(), [&](const Part &p) { return ids.count(p.id); }),
        c.parts.end());
    c.nodes.erase(
        std::remove_if(c.nodes.begin(), c.nodes.end(), [&](const Junction &n) { return ids.count(n.id); }),
        c.nodes.end());
    c.wires.erase(std::remove_if(c.wires.begin(), c.wires.end(),
                                 [&](const Wire &w) {
                                     return ids.count(w.id) || ids.count(w.a.object) || ids.count(w.b.object);
                                 }),
                  c.wires.end());
}
void Project::validate() const {
    // Validate all definitions, including unused ones, before loading or compiling a project.
    if (circuits.empty() || circuits.size() > 128)
        throw std::runtime_error("电路数量应为 1–128");
    circuit(main);
    std::set<std::string> names, ids;
    uint64_t largest = 0;
    auto idCheck = [&](const std::string &s) {
        if (s.empty() || s.find_first_of("/:\r\n") != std::string::npos || !ids.insert(s).second)
            throw std::runtime_error("对象 ID 重复或非法");
        auto first = s.find_first_of("0123456789");
        if (first != std::string::npos) {
            try {
                largest = std::max(largest, uint64_t(std::stoull(s.substr(first))));
            } catch (...) {
                throw std::runtime_error("对象 ID 数值错误");
            }
        }
    };
    auto pos = [](Point at) {
        if (!std::isfinite(at.x) || !std::isfinite(at.y) || std::abs(at.x) > 1000000 ||
            std::abs(at.y) > 1000000)
            throw std::runtime_error("坐标超出范围");
    };
    for (auto &c : circuits) {
        if (c.name.empty() || !names.insert(c.name).second)
            throw std::runtime_error("电路名称重复或为空");
        if (c.parts.size() > 10000 || c.wires.size() > 40000 || c.nodes.size() > 40000)
            throw std::runtime_error("电路规模超出限制");
        std::set<std::string> labels;
        for (auto &p : c.parts) {
            idCheck(p.id);
            pos(p.at);
            if (p.width < 1 || p.width > 32)
                throw std::runtime_error("位宽必须为 1–32");
            if (p.kind == "Clock" && p.width != 1)
                throw std::runtime_error("时钟的位宽必须为 1");
            if (p.kind != "Subcircuit" &&
                std::none_of(library().begin(), library().end(), [&](auto &q) { return q.kind == p.kind; }))
                throw std::runtime_error("不支持的元件：" + p.kind);
            if (p.kind == "Subcircuit")
                circuit(p.circuit);
            if (p.data.size() > 256)
                throw std::runtime_error("存储器最多 256 字");
            if ((p.kind == "Input" || p.kind == "Output") && !labels.insert(portName(p)).second)
                throw std::runtime_error("输入输出名称不能重复");
        }
        for (auto &n : c.nodes) {
            idCheck(n.id);
            pos(n.at);
        }
        for (auto &w : c.wires) {
            idCheck(w.id);
            endpoint(*this, c, w.a);
            endpoint(*this, c, w.b);
            for (auto b : w.bends)
                pos(b);
        }
    }
    if (nextId <= largest || nextId > 1000000000000ULL)
        throw std::runtime_error("nextId 无效");
    std::set<std::string> stack, done;
    std::function<void(const std::string &)> visit = [&](const std::string &n) {
        if (stack.count(n))
            throw std::runtime_error("子电路递归引用：" + n);
        if (done.count(n))
            return;
        if (stack.size() > 32)
            throw std::runtime_error("子电路嵌套超过 32 层");
        stack.insert(n);
        for (auto &p : circuit(n).parts)
            if (p.kind == "Subcircuit")
                visit(p.circuit);
        stack.erase(n);
        done.insert(n);
    };
    for (auto &c : circuits)
        visit(c.name);
}
std::string serialize(const Project &p) {
    json j = {{"format", "PracticeEDA.logic"},
              {"version", 1},
              {"name", p.name},
              {"main", p.main},
              {"nextId", p.nextId},
              {"circuits", json::array()}};
    for (auto &c : p.circuits) {
        json q = {
            {"name", c.name}, {"parts", json::array()}, {"nodes", json::array()}, {"wires", json::array()}};
        for (auto &a : c.parts)
            q["parts"].push_back({{"id", a.id},
                                  {"kind", a.kind},
                                  {"label", a.label},
                                  {"circuit", a.circuit},
                                  {"at", point(a.at)},
                                  {"width", a.width},
                                  {"value", a.value},
                                  {"data", a.data}});
        for (auto &n : c.nodes)
            q["nodes"].push_back({{"id", n.id}, {"label", n.label}, {"at", point(n.at)}});
        for (auto &w : c.wires) {
            json bends = json::array();
            for (auto b : w.bends)
                bends.push_back(point(b));
            q["wires"].push_back(
                {{"id", w.id}, {"a", endpointJson(w.a)}, {"b", endpointJson(w.b)}, {"bends", bends}});
        }
        j["circuits"].push_back(q);
    }
    return j.dump(2);
}
Project deserialize(const std::string &text) {
    if (text.size() > 32 * 1024 * 1024)
        throw std::runtime_error("工程文件超过 32 MB");
    auto j = json::parse(text);
    if (j.at("format") != "PracticeEDA.logic" || j.at("version") != 1)
        throw std::runtime_error("不是受支持的数字逻辑工程（.logic.json）");
    Project p;
    p.name = j.at("name");
    p.main = j.at("main");
    p.nextId = integer(j.at("nextId"), 1000000000000ULL);
    p.circuits.clear();
    for (auto &q : j.at("circuits")) {
        Circuit c;
        c.name = q.at("name");
        for (auto &a : q.at("parts")) {
            Part x;
            x.id = a.at("id");
            x.kind = a.at("kind");
            x.label = a.at("label");
            x.circuit = a.at("circuit");
            x.at = point(a.at("at"));
            x.width = int(integer(a.at("width"), 32));
            x.value = uint32_t(integer(a.at("value"), UINT32_MAX));
            for (auto &word : a.at("data")) {
                x.data.push_back(uint32_t(integer(word, UINT32_MAX)));
            }
            c.parts.push_back(x);
        }
        for (auto &n : q.at("nodes"))
            c.nodes.push_back({n.at("id"), n.at("label"), point(n.at("at"))});
        for (auto &w : q.at("wires")) {
            Wire x;
            x.id = w.at("id");
            x.a = endpointJson(w.at("a"));
            x.b = endpointJson(w.at("b"));
            for (auto &b : w.at("bends"))
                x.bends.push_back(point(b));
            c.wires.push_back(x);
        }
        p.circuits.push_back(c);
    }
    p.validate();
    return p;
}
Project halfAdder() {
    Project p;
    p.name = "半加器 · XOR / AND";
    auto &c = p.circuits[0];
    auto add = [&](const std::string &k, const std::string &l, Point at) {
        Part q;
        q.id = p.id("p");
        q.kind = k;
        q.label = l;
        q.at = at;
        c.parts.push_back(q);
        return q.id;
    };
    auto a = add("Input", "A", {130, 140}), b = add("Input", "B", {130, 340});
    auto x = add("Xor", "XOR", {410, 130}), g = add("And", "AND", {410, 350});
    auto s = add("Output", "Sum", {670, 130}), cy = add("Output", "Carry", {670, 350});
    auto wire = [&](Endpoint u, Endpoint v, std::vector<Point> bends) {
        c.wires.push_back({p.id("w"), u, v, bends});
    };
    wire({a, "Y"}, {x, "A"}, {{240, 140}, {240, 120}});
    wire({b, "Y"}, {x, "B"}, {{280, 340}, {280, 140}});
    wire({a, "Y"}, {g, "A"}, {{240, 140}, {240, 340}});
    wire({b, "Y"}, {g, "B"}, {{280, 340}, {280, 360}});
    wire({x, "Y"}, {s, "A"}, {});
    wire({g, "Y"}, {cy, "A"}, {});
    return p;
}
std::string copy(const Project &p, const Circuit &c, const std::set<std::string> &ids) {
    Project clip = p;
    Circuit dest;
    dest.name = c.name;
    for (auto &q : c.parts)
        if (ids.count(q.id))
            dest.parts.push_back(q);
    for (auto &q : c.nodes)
        if (ids.count(q.id))
            dest.nodes.push_back(q);
    std::map<std::string, Endpoint> detached;
    auto include = [&](Endpoint e) {
        if (ids.count(e.object))
            return e;
        if (detached.count(e.key()))
            return detached.at(e.key());
        auto id = clip.id("j");
        dest.nodes.push_back({id, "", endpoint(p, c, e)});
        return detached[e.key()] = {id, ""};
    };
    for (auto w : c.wires)
        if (ids.count(w.id) || (ids.count(w.a.object) && ids.count(w.b.object))) {
            w.a = include(w.a);
            w.b = include(w.b);
            dest.wires.push_back(w);
        }
    clip.circuit(c.name) = std::move(dest);
    clip.main = c.name;
    return serialize(clip);
}
std::set<std::string> paste(Project &p, Circuit &target, const std::string &text, Point offset) {
    // Definitions and interface IDs must move together when pasting across project boundaries.
    auto clip = deserialize(text);
    auto targetName = target.name;
    auto picked = clip.circuit(clip.main);
    if (picked.parts.empty() && picked.nodes.empty())
        return {};
    std::map<std::string, std::string> definitions;
    std::vector<Circuit> imports;
    std::function<std::string(const std::string &)> import = [&](const std::string &name) {
        if (definitions.count(name))
            return definitions.at(name);
        std::string newName = name;
        int i = 1;
        auto exists = [&](const std::string &n) {
            return std::any_of(p.circuits.begin(), p.circuits.end(), [&](auto &c) { return c.name == n; }) ||
                   std::any_of(imports.begin(), imports.end(), [&](auto &c) { return c.name == n; }) ||
                   std::any_of(definitions.begin(), definitions.end(),
                               [&](auto &d) { return d.second == n; });
        };
        while (exists(newName))
            newName = name + "_copy" + std::to_string(i++);
        definitions[name] = newName;
        Circuit c = clip.circuit(name);
        c.name = newName;
        std::map<std::string, std::string> ids;
        for (auto &q : c.parts) {
            auto oldId = q.id;
            ids[oldId] = p.id("p");
            q.id = ids.at(oldId);
            if (q.kind == "Subcircuit")
                q.circuit = import(q.circuit);
        }
        // Port IDs are remapped below using the imported definition's original port order.
        for (size_t k = 0; k < c.parts.size(); ++k)
            if (c.parts[k].kind == "Subcircuit") {
                const auto &oldPart = clip.circuit(name).parts[k];
                const auto &oldDef = clip.circuit(oldPart.circuit);
                const auto it = std::find_if(imports.begin(), imports.end(),
                                             [&](auto &d) { return d.name == c.parts[k].circuit; });
                std::map<std::string, std::string> portIds;
                for (size_t z = 0; z < oldDef.parts.size(); ++z)
                    portIds[oldDef.parts[z].id] = it->parts[z].id;
                for (auto &w : c.wires) {
                    if (w.a.object == oldPart.id)
                        w.a.pin = portIds.at(w.a.pin);
                    if (w.b.object == oldPart.id)
                        w.b.pin = portIds.at(w.b.pin);
                }
            }
        for (auto &n : c.nodes) {
            auto old = n.id;
            n.id = p.id("j");
            ids[old] = n.id;
        }
        for (auto &w : c.wires) {
            w.id = p.id("w");
            w.a.object = ids.at(w.a.object);
            w.b.object = ids.at(w.b.object);
        }
        imports.push_back(c);
        return newName;
    };
    for (auto &q : picked.parts)
        if (q.kind == "Subcircuit") {
            auto old = q.circuit;
            q.circuit = import(old);
            const auto &orig = clip.circuit(old);
            auto it =
                std::find_if(imports.begin(), imports.end(), [&](auto &d) { return d.name == q.circuit; });
            std::map<std::string, std::string> portIds;
            for (size_t i = 0; i < orig.parts.size(); ++i)
                portIds[orig.parts[i].id] = it->parts[i].id;
            for (auto &w : picked.wires) {
                if (w.a.object == q.id)
                    w.a.pin = portIds.at(w.a.pin);
                if (w.b.object == q.id)
                    w.b.pin = portIds.at(w.b.pin);
            }
        }
    p.circuits.insert(p.circuits.end(), imports.begin(), imports.end());
    auto &dest = p.circuit(targetName);
    std::map<std::string, std::string> ids;
    std::set<std::string> result;
    for (auto q : picked.parts) {
        auto old = q.id;
        q.id = p.id("p");
        ids[old] = q.id;
        q.at = q.at + offset;
        if (q.kind == "Input" || q.kind == "Output")
            q.label = portName(q) + "_" + q.id;
        dest.parts.push_back(q);
        result.insert(q.id);
    }
    std::map<std::string, std::string> labels;
    for (auto n : picked.nodes) {
        auto old = n.id;
        n.id = p.id("j");
        ids[old] = n.id;
        n.at = n.at + offset;
        if (!n.label.empty()) {
            if (!labels.count(n.label))
                labels[n.label] = n.label + "_copy_" + n.id;
            n.label = labels.at(n.label);
        }
        dest.nodes.push_back(n);
        result.insert(n.id);
    }
    for (auto w : picked.wires) {
        w.id = p.id("w");
        w.a.object = ids.at(w.a.object);
        w.b.object = ids.at(w.b.object);
        for (auto &b : w.bends)
            b = b + offset;
        dest.wires.push_back(w);
        result.insert(w.id);
    }
    p.validate();
    return result;
}
void History::reset(Project p, bool isSaved) {
    project = std::move(p);
    past.clear();
    future.clear();
    saved.clear();
    if (isSaved)
        markSaved();
}
void History::commit(const Project &before) {
    project.validate();
    if (serialize(before) == serialize(project))
        return;
    past.push_back(before);
    if (past.size() > 100)
        past.erase(past.begin());
    future.clear();
}
bool History::undo() {
    if (past.empty())
        return false;
    future.push_back(project);
    project = std::move(past.back());
    past.pop_back();
    return true;
}
bool History::redo() {
    if (future.empty())
        return false;
    past.push_back(project);
    project = std::move(future.back());
    future.pop_back();
    return true;
}
void History::markSaved() {
    saved = serialize(project);
}
bool History::dirty() const {
    return saved != serialize(project);
}
Signal Signal::number(uint32_t n, int w) {
    return {w, n & mask(w), mask(w), 0, 0};
}
Signal Signal::unknown(int w) {
    return {w, 0, 0, 0, 0};
}
Signal Signal::floating(int w) {
    return {w, 0, 0, mask(w), 0};
}
Signal Signal::failure(int w) {
    return {w, 0, 0, 0, mask(w)};
}
bool Signal::defined() const {
    return known == mask(width);
}
bool Signal::operator==(const Signal &b) const {
    return width == b.width && value == b.value && known == b.known && z == b.z && error == b.error;
}
std::string Signal::text() const {
    if (defined()) {
        if (width == 1)
            return value ? "1" : "0";
        std::ostringstream o;
        o << "0x" << std::uppercase << std::hex << std::setw((width + 3) / 4) << std::setfill('0') << value;
        return o.str();
    }
    std::string s;
    for (int i = width - 1; i >= 0; --i) {
        auto b = uint32_t(1) << i;
        s += (error & b) ? 'E' : (known & b) ? ((value & b) ? '1' : '0') : (z & b) ? 'Z' : 'X';
    }
    return s;
}
struct Simulator::Impl {
    // A Node is a flattened primitive instance; two instances never share register/RAM storage.
    struct Node {
        Part part;
        std::string path, object;
        std::map<std::string, int> nets;
        std::map<std::string, Signal> outputs;
        Signal state = Signal::number(0, 1), previousClock = Signal::number(0, 1);
        std::vector<Signal> memory;
    };
    struct NetState {
        int width = 1;
        bool mismatch = false;
        Signal signal = Signal::floating(1);
        std::vector<std::pair<size_t, std::string>> drivers;
        std::set<size_t> users;
        std::set<std::string> objects;
    };
    Project project;
    std::string root;
    std::vector<Node> nodes;
    std::vector<NetState> nets;
    std::map<std::string, int> keys, roots;
    std::vector<int> parent, widths;
    std::map<uint64_t, std::set<size_t>> queue;
    std::vector<Trace> traces;
    uint64_t now = 0;
    bool oscillating = false;
    std::set<std::string> oscillators;
    int key(const std::string &s, int w = 0) {
        auto it = keys.find(s);
        if (it != keys.end()) {
            if (w)
                widths[it->second] = w;
            return it->second;
        }
        int i = int(parent.size());
        parent.push_back(i);
        widths.push_back(w);
        keys[s] = i;
        return i;
    }
    int find(int i) { return parent[i] == i ? i : parent[i] = find(parent[i]); }
    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b)
            parent[b] = a;
    }
    void flatten(const Circuit &c, const std::string &prefix, const std::string &top, bool nested) {
        // Union explicit endpoints and same-name junctions within this instance only.
        if (nodes.size() > 20000 || keys.size() > 250000)
            throw std::runtime_error("展开后的电路过大");
        std::map<std::string, int> labels;
        for (auto &p : c.parts) {
            auto path = prefix + p.id;
            auto object = top.empty() ? p.id : top;
            auto pp = ports(project, p);
            for (auto &port : pp)
                key(path + ":" + port.id, port.width);
            if (p.kind == "Subcircuit") {
                auto &child = project.circuit(p.circuit);
                flatten(child, path + "/", object, true);
                for (auto &q : child.parts) {
                    if (q.kind == "Input")
                        unite(key(path + ":" + q.id), key(path + "/" + q.id + ":Y"));
                    if (q.kind == "Output")
                        unite(key(path + ":" + q.id), key(path + "/" + q.id + ":A"));
                }
            } else if (!(nested && (p.kind == "Input" || p.kind == "Output")) && p.kind != "Text") {
                Node n;
                n.part = p;
                n.path = path;
                n.object = object;
                for (auto &port : pp)
                    n.nets[port.id] = key(path + ":" + port.id);
                nodes.push_back(n);
            }
        }
        for (auto &n : c.nodes) {
            int k = key(prefix + n.id + ":");
            if (!n.label.empty()) {
                if (labels.count(n.label))
                    unite(k, labels[n.label]);
                else
                    labels[n.label] = k;
            }
        }
        for (auto &w : c.wires) {
            int a = key(prefix + w.a.key()), b = key(prefix + w.b.key());
            unite(a, b);
            unite(key(prefix + w.id + ":"), a);
        }
    }
    Impl(const Project &p, std::string c) : project(p), root(std::move(c)) {
        project.validate();
        flatten(project.circuit(root), "", "", false);
        std::map<int, int> remap;
        for (auto &k : keys) {
            int r = find(k.second);
            if (!remap.count(r)) {
                remap[r] = int(nets.size());
                nets.push_back({});
            }
            roots[k.first] = remap[r];
        }
        std::vector<int> netWidths(nets.size(), 0);
        for (auto &k : keys) {
            auto &n = nets[roots[k.first]];
            int w = widths[k.second];
            auto &prev = netWidths[roots[k.first]];
            if (w) {
                if (prev && prev != w)
                    n.mismatch = true;
                prev = std::max(prev, w);
                n.width = prev;
            }
            auto sep = k.first.find_first_of("/:");
            n.objects.insert(k.first.substr(0, sep));
        }
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto &n = nodes[i];
            for (auto &port : ports(project, n.part)) {
                int net = roots.at(n.path + ":" + port.id);
                n.nets[port.id] = net;
                if (port.output)
                    nets[net].drivers.push_back({i, port.id});
                else
                    nets[net].users.insert(i);
            }
        }
        reset();
    }
    void reset() {
        queue.clear();
        traces.clear();
        now = 0;
        oscillating = false;
        oscillators.clear();
        for (auto &net : nets)
            net.signal = net.mismatch ? Signal::failure(net.width) : Signal::floating(net.width);
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto &n = nodes[i];
            n.state = Signal::number(n.part.value, n.part.width);
            n.previousClock = Signal::number(0, 1);
            n.outputs.clear();
            n.memory.assign(256, Signal::number(0, n.part.width));
            for (size_t j = 0; j < n.part.data.size(); ++j)
                n.memory[j] = Signal::number(n.part.data[j], n.part.width);
            for (auto &port : ports(project, n.part))
                if (port.output)
                    n.outputs[port.id] = Signal::floating(port.width);
            queue[1].insert(i);
        }
    }
    Signal input(const Node &n, const std::string &pin) const { return nets.at(n.nets.at(pin)).signal; }
    void evaluate(Node &n) {
        auto &k = n.part.kind;
        int w = n.part.width;
        auto a = [&](const std::string &pin) { return input(n, pin); };
        auto put = [&](const std::string &pin, Signal s) { n.outputs[pin] = s; };
        auto number = [&](const std::string &pin, uint32_t value, int width = 0) {
            put(pin, Signal::number(value, width ? width : w));
        };
        if (source(k)) {
            number("Y", k == "Power" ? mask(w) : k == "Ground" ? 0 : n.state.value);
            return;
        }
        if (k == "Output" || k == "LED" || k == "Hex" || k == "Probe")
            return;
        if (k == "Splitter") {
            auto s = a("A");
            for (int i = 0; i < w; ++i)
                put("B" + std::to_string(i),
                    {1, (s.value >> i) & 1, (s.known >> i) & 1, (s.z >> i) & 1, (s.error >> i) & 1});
            return;
        }
        if (k == "Joiner") {
            Signal s = Signal::unknown(w);
            for (int i = 0; i < w; ++i) {
                auto b = a("B" + std::to_string(i));
                s.value |= (b.value & 1) << i;
                s.known |= (b.known & 1) << i;
                s.z |= (b.z & 1) << i;
                s.error |= (b.error & 1) << i;
            }
            put("Y", s);
            return;
        }
        if (k == "Extender") {
            auto s = a("A");
            s.width = w;
            s.known |= mask(w) & ~1U;
            put("Y", s);
            return;
        }
        if (k == "TriState") {
            auto en = a("EN");
            put("Y", en.defined() ? (en.value ? a("A") : Signal::floating(w)) : Signal::unknown(w));
            return;
        }
        if (k == "Mux") {
            auto s = a("S");
            auto x = a("A"), y = a("B");
            put("Y", s.defined() ? (s.value ? y : x) : (x == y ? x : Signal::unknown(w)));
            return;
        }
        if (k == "Demux") {
            auto s = a("S");
            for (int i = 0; i < 2; ++i)
                put("Y" + std::to_string(i), !s.defined()             ? Signal::unknown(w)
                                             : s.value == uint32_t(i) ? a("A")
                                                                      : Signal::number(0, w));
            return;
        }
        if (k == "Decoder") {
            auto s = a("A");
            for (int i = 0; i < 4; ++i)
                put("Y" + std::to_string(i),
                    s.defined() ? Signal::number(s.value == uint32_t(i), 1) : Signal::unknown(1));
            return;
        }
        if (k == "Encoder") {
            int value = 0;
            bool valid = false, known = true;
            for (int i = 0; i < 4; ++i) {
                auto s = a("A" + std::to_string(i));
                if (!s.defined())
                    known = false;
                if (s.defined() && s.value) {
                    value = i;
                    valid = true;
                }
            }
            put("Y", known ? Signal::number(value, 2) : Signal::unknown(2));
            put("V", known ? Signal::number(valid, 1) : Signal::unknown(1));
            return;
        }
        if (k == "ROM" || k == "RAM") {
            auto addr = a("ADDR");
            if (k == "RAM") {
                auto clk = a("CLK");
                bool rising =
                    n.previousClock.defined() && !n.previousClock.value && clk.defined() && clk.value;
                n.previousClock = clk;
                if (rising) {
                    auto we = a("WE");
                    if (addr.defined()) {
                        if (we.defined() && we.value)
                            n.memory[addr.value] = a("D");
                        else if (!we.defined())
                            n.memory[addr.value] = Signal::unknown(w);
                    } else if (!we.defined() || we.value)
                        std::fill(n.memory.begin(), n.memory.end(), Signal::unknown(w));
                }
            }
            put("Q", addr.defined() ? n.memory[addr.value] : Signal::unknown(w));
            return;
        }
        if (sequential(k)) {
            auto clk = a("CLK");
            bool rising = n.previousClock.defined() && !n.previousClock.value && clk.defined() && clk.value;
            n.previousClock = clk;
            if (rising) {
                if (k == "DFF" || k == "Register") {
                    n.state = a("D");
                    n.state.z = 0;
                } else if (k == "Counter") {
                    auto en = a("EN");
                    if (!en.defined())
                        n.state = Signal::unknown(w);
                    else if (en.value && n.state.defined())
                        n.state = Signal::number(n.state.value + 1, w);
                } else if (k == "ShiftRegister") {
                    auto d = a("D");
                    n.state = {w, ((n.state.value << 1) | (d.value & 1)) & mask(w),
                               ((n.state.known << 1) | (d.known & 1)) & mask(w), 0,
                               ((n.state.error << 1) | (d.error & 1)) & mask(w)};
                } else {
                    auto x = a(k == "TFF" ? "T" : k == "JKFF" ? "J" : "S");
                    auto y = k == "TFF" ? x : a(k == "JKFF" ? "K" : "R");
                    Signal next = Signal::unknown(w);
                    for (int i = 0; i < w; ++i) {
                        uint32_t b = uint32_t(1) << i;
                        if (!(x.known & b) || !(y.known & b))
                            continue;
                        bool xv = x.value & b, yv = y.value & b;
                        if (k == "SRFF" && xv && yv) {
                            next.error |= b;
                            continue;
                        }
                        if (xv != yv) {
                            next.known |= b;
                            if (xv)
                                next.value |= b;
                        } else {
                            next.known |= n.state.known & b;
                            next.error |= n.state.error & b;
                            next.value |= ((xv ? (~n.state.value) : n.state.value) & n.state.known & b);
                        }
                    }
                    n.state = next;
                }
            }
            put("Q", n.state);
            return;
        }
        auto x = a("A");
        if (k == "Buffer") {
            put("Y", x);
            return;
        }
        if (k == "Not") {
            put("Y", {w, (~x.value) & x.known, x.known, 0, x.error});
            return;
        }
        if (k == "Negate") {
            put("Y", x.defined() ? Signal::number(0U - x.value, w) : Signal::unknown(w));
            return;
        }
        auto y = a("B");
        if (k == "And" || k == "Nand" || k == "Or" || k == "Nor" || k == "Xor" || k == "Xnor") {
            Signal s = Signal::unknown(w);
            if (k == "And" || k == "Nand") {
                auto zero = (x.known & ~x.value) | (y.known & ~y.value);
                s.known = (x.known & y.known) | zero;
                s.value = x.value & y.value & s.known;
            } else if (k == "Or" || k == "Nor") {
                auto one = (x.known & x.value) | (y.known & y.value);
                s.known = (x.known & y.known) | one;
                s.value = (x.value | y.value) & s.known;
            } else {
                s.known = x.known & y.known;
                s.value = (x.value ^ y.value) & s.known;
            }
            s.error = (x.error | y.error) & ~s.known;
            if (k == "Nand" || k == "Nor" || k == "Xnor")
                s.value = (~s.value) & s.known;
            put("Y", s);
            return;
        }
        if (!x.defined() || !y.defined()) {
            for (auto &out : n.outputs)
                out.second = Signal::unknown(out.second.width);
            return;
        }
        if (k == "Add") {
            uint64_t sum = uint64_t(x.value) + y.value;
            number("Y", uint32_t(sum));
            number("C", uint32_t(sum >> w), 1);
        } else if (k == "Subtract") {
            number("Y", x.value - y.value);
            number("C", x.value < y.value, 1);
        } else if (k == "Multiply")
            number("Y", uint32_t(uint64_t(x.value) * y.value));
        else if (k == "Divide")
            put("Y", y.value ? Signal::number(x.value / y.value, w) : Signal::failure(w));
        else if (k == "Compare") {
            number("LT", x.value < y.value, 1);
            number("EQ", x.value == y.value, 1);
            number("GT", x.value > y.value, 1);
        } else if (k == "ShiftLeft")
            number("Y", y.value >= 32 ? 0 : x.value << y.value);
        else if (k == "ShiftRight")
            number("Y", y.value >= 32 ? 0 : x.value >> y.value);
    }
    Signal resolve(const NetState &net) {
        if (net.mismatch)
            return Signal::failure(net.width);
        Signal result = Signal::floating(net.width);
        for (int bit = 0; bit < net.width; ++bit) {
            uint32_t b = uint32_t(1) << bit;
            bool zero = false, one = false, unknown = false, error = false;
            for (auto &d : net.drivers) {
                auto s = nodes[d.first].outputs.at(d.second);
                if (s.error & b)
                    error = true;
                else if (s.known & b) {
                    if (s.value & b)
                        one = true;
                    else
                        zero = true;
                } else if (!(s.z & b))
                    unknown = true;
            }
            if (zero || one || unknown || error)
                result.z &= ~b;
            if (error || (zero && one))
                result.error |= b;
            else if (!unknown && (zero || one)) {
                result.known |= b;
                if (one)
                    result.value |= b;
            }
        }
        return result;
    }
    bool step() {
        // Evaluate the entire time batch against old net values, then publish all changed nets.
        // Updating nets inside the first loop would make clock sampling depend on component order.
        if (queue.empty() || oscillating)
            return false;
        auto it = queue.begin();
        now = it->first;
        auto batch = it->second;
        queue.erase(it);
        std::set<int> affected;
        for (auto i : batch) {
            auto &n = nodes[i];
            auto before = n.outputs;
            evaluate(n);
            for (auto &out : n.outputs)
                if (before[out.first] != out.second) {
                    affected.insert(n.nets[out.first]);
                    traces.push_back({now, n.path, out.first, out.second.text()});
                }
        }
        if (traces.size() > 2000)
            traces.erase(traces.begin(), traces.begin() + ptrdiff_t(traces.size() - 2000));
        for (auto i : affected) {
            auto &net = nets[i];
            auto s = resolve(net);
            if (s != net.signal) {
                net.signal = s;
                for (auto user : net.users)
                    queue[now + 1].insert(user);
            }
        }
        return true;
    }
};
Simulator::Simulator(const Project &p, const std::string &c) : impl(std::make_unique<Impl>(p, c)) {}
Simulator::~Simulator() = default;
Simulator::Simulator(Simulator &&) noexcept = default;
Simulator &Simulator::operator=(Simulator &&) noexcept = default;
void Simulator::reset() {
    impl->reset();
}
void Simulator::setInput(const std::string &id, uint32_t value) {
    for (size_t i = 0; i < impl->nodes.size(); ++i) {
        auto &n = impl->nodes[i];
        if (n.path == id && (n.part.kind == "Input" || n.part.kind == "Button")) {
            n.state = Signal::number(value, n.part.width);
            impl->queue[impl->now + 1].insert(i);
            return;
        }
    }
    throw std::runtime_error("不是可操作的输入：" + id);
}
bool Simulator::step() {
    return impl->step();
}
bool Simulator::settle(size_t budget) {
    size_t count = 0;
    while (!impl->queue.empty() && !impl->oscillating && count++ < budget)
        impl->step();
    if (!impl->queue.empty()) {
        impl->oscillating = true;
        for (auto &b : impl->queue)
            for (auto i : b.second)
                impl->oscillators.insert(impl->nodes[i].object);
        return false;
    }
    return !impl->oscillating;
}
void Simulator::tick() {
    for (size_t i = 0; i < impl->nodes.size(); ++i) {
        auto &n = impl->nodes[i];
        if (n.part.kind == "Clock") {
            n.state = Signal::number(n.state.value ? 0 : 1, n.part.width);
            impl->queue[impl->now + 1].insert(i);
        }
    }
    settle();
}
Signal Simulator::read(const Endpoint &e) const {
    auto it = impl->roots.find(e.key());
    return it == impl->roots.end() ? Signal::unknown(1) : impl->nets[it->second].signal;
}
std::vector<Issue> Simulator::issues() const {
    std::vector<Issue> result;
    for (auto &n : impl->nets) {
        std::string message;
        if (n.mismatch)
            message = "总线位宽不匹配";
        else if (n.signal.error)
            message = "驱动冲突或非法运算 (E)";
        else if (n.signal.z && !n.users.empty())
            message = "输入悬空 / 高阻态 (Z)";
        else if (!n.signal.defined() && !n.users.empty())
            message = "输入包含未知值 (X)";
        if (!message.empty())
            for (auto &object : n.objects)
                result.push_back({n.mismatch || n.signal.error ? "error" : "warning", object, message});
    }
    for (auto &obj : impl->oscillators)
        result.push_back({"error", obj, "事件预算耗尽：电路振荡或传播过长；请复位后检查反馈"});
    return result;
}
const std::vector<Trace> &Simulator::trace() const {
    return impl->traces;
}
uint64_t Simulator::time() const {
    return impl->now;
}
size_t Simulator::pending() const {
    size_t n = 0;
    for (auto &b : impl->queue)
        n += b.second.size();
    return n;
}
std::string Simulator::netlist(bool kicad) const {
    // Read the same union-find result used by simulation. Crossing drawings do not create nets.
    // This method intentionally reads topology only, never live signals, clocks or event queues.
    json result = {{"format", "PracticeEDA.netlist"}, {"version", 1},
                   {"project", impl->project.name},   {"circuit", impl->root},
                   {"components", json::array()},     {"nets", json::array()}};
    for (size_t i = 0; i < impl->nets.size(); ++i) {
        const auto &net = impl->nets[i];
        if (net.mismatch)
            throw std::runtime_error("网表导出失败：存在位宽不一致的连接，请先修复电路");
        result["nets"].push_back({{"id", i + 1},
                                  {"name", "N" + std::to_string(i + 1)},
                                  {"width", net.width},
                                  {"aliases", json::array()},
                                  {"labels", json::array()},
                                  {"pins", json::array()}});
    }
    for (const auto &entry : impl->roots)
        result["nets"][entry.second]["aliases"].push_back(entry.first);
    // Keep engineer-assigned labels with their instance scope; equal labels in different instances differ.
    std::function<void(const Circuit &, const std::string &)> addLabels = [&](const Circuit &c,
                                                                              const std::string &prefix) {
        for (const auto &n : c.nodes)
            if (!n.label.empty())
                result["nets"][impl->roots.at(prefix + n.id + ":")]["labels"].push_back(
                    {{"path", prefix + n.id}, {"name", n.label}});
        for (const auto &p : c.parts)
            if (p.kind == "Subcircuit")
                addLabels(impl->project.circuit(p.circuit), prefix + p.id + "/");
    };
    addLabels(impl->project.circuit(impl->root), "");
    for (size_t i = 0; i < impl->nodes.size(); ++i) {
        const auto &node = impl->nodes[i];
        const auto &part = node.part;
        const auto ref = "U" + std::to_string(i + 1);
        json component = {{"ref", ref},          {"path", node.path},    {"kind", part.kind},
                          {"label", part.label}, {"width", part.width},  {"value", part.value},
                          {"data", part.data},   {"pins", json::array()}};
        for (const auto &pin : ports(impl->project, part)) {
            const int net = node.nets.at(pin.id);
            component["pins"].push_back({{"id", pin.id},
                                         {"width", pin.width},
                                         {"direction", pin.output ? "output" : "input"},
                                         {"net", net + 1}});
            result["nets"][net]["pins"].push_back({{"ref", ref}, {"pin", pin.id}});
        }
        result["components"].push_back(std::move(component));
    }
    if (!kicad)
        return result.dump(2) + "\n";

    // KiCad's generic S-expression connectivity uses scalar pins. Export every bus bit explicitly.
    // These are logical primitives without PCB footprints or analog SPICE models.
    auto quoted = [](const std::string &s) { return json(s).dump(); };
    std::ostringstream out;
    out << "(export (version \"D\")\n  (design (source " << quoted(impl->root)
        << ") (tool \"PracticeEDA\"))\n  (components\n";
    for (const auto &c : result["components"]) {
        out << "    (comp (ref " << c["ref"].dump() << ") (value " << c["kind"].dump()
            << ")\n      (fields (field (name \"InstancePath\") " << c["path"].dump()
            << ") (field (name \"Label\") " << c["label"].dump() << ") (field (name \"Width\") "
            << quoted(c["width"].dump()) << ") (field (name \"InitialValue\") " << quoted(c["value"].dump())
            << ") (field (name \"MemoryData\") " << quoted(c["data"].dump()) << ")))\n";
    }
    out << "  )\n  (nets\n";
    size_t code = 0;
    for (const auto &n : result["nets"]) {
        const int width = n["width"].get<int>();
        for (int bit = 0; bit < width; ++bit) {
            const auto suffix = width == 1 ? "" : "[" + std::to_string(bit) + "]";
            out << "    (net (code " << ++code << ") (name " << quoted(n["name"].get<std::string>() + suffix)
                << ")";
            for (const auto &pin : n["pins"])
                out << "\n      (node (ref " << pin["ref"].dump() << ") (pin "
                    << quoted(pin["pin"].get<std::string>() + suffix) << "))";
            out << ")\n";
        }
    }
    out << "  )\n)\n";
    return out.str();
}

// Truth-table analysis uses fresh simulators, so exploring input combinations cannot change the editor.
Table truthTable(const Project &p, const std::string &circuit) {
    p.validate();
    std::set<std::string> visited;
    std::function<void(const Circuit &)> check = [&](const Circuit &c) {
        if (!visited.insert(c.name).second)
            return;
        for (auto &q : c.parts) {
            if (sequential(q.kind))
                throw std::runtime_error("真值表只支持组合电路；当前包含时钟或存储元件");
            if (q.kind == "Subcircuit")
                check(p.circuit(q.circuit));
        }
    };
    check(p.circuit(circuit));
    std::vector<Part> inputs, outputs;
    Table table;
    for (auto &q : p.circuit(circuit).parts) {
        if (q.kind == "Input" || q.kind == "Button")
            inputs.push_back(q);
        if (q.kind == "Output")
            outputs.push_back(q);
    }
    auto names = [](const Part &q, std::vector<std::string> &names) {
        for (int i = 0; i < q.width; ++i)
            names.push_back(portName(q) + (q.width == 1 ? "" : "[" + std::to_string(i) + "]"));
    };
    for (auto &q : inputs)
        names(q, table.inputs);
    for (auto &q : outputs)
        names(q, table.outputs);
    if (table.inputs.size() > 10 || table.outputs.empty() || table.outputs.size() > 128)
        throw std::runtime_error("分析需至少一个输出引脚，输入总位数最多 10、输出总位数最多 128");
    Simulator sim(p, circuit);
    for (size_t row = 0; row < (size_t(1) << table.inputs.size()); ++row) {
        sim.reset();
        std::vector<int> values;
        size_t index = 0;
        for (auto &q : inputs) {
            uint32_t v = 0;
            for (int i = 0; i < q.width; ++i) {
                int b = int((row >> (table.inputs.size() - 1 - index++)) & 1);
                v |= uint32_t(b) << i;
                values.push_back(b);
            }
            sim.setInput(q.id, v);
        }
        if (!sim.settle())
            throw std::runtime_error("组合电路不收敛，无法生成真值表");
        for (auto &q : outputs) {
            auto s = sim.read({q.id, "A"});
            if (!s.defined())
                throw std::runtime_error("输出 " + portName(q) + " 存在 X/Z/E，请先修复连线");
            for (int i = 0; i < q.width; ++i)
                values.push_back((s.value >> i) & 1);
        }
        table.rows.push_back(values);
    }
    return table;
}
std::string Table::report() const {
    std::ostringstream out;
    for (auto &n : inputs)
        out << n << '\t';
    out << "|\t";
    for (auto &n : outputs)
        out << n << '\t';
    out << '\n';
    for (auto &row : rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            if (i == inputs.size())
                out << "|\t";
            out << row[i] << '\t';
        }
        out << '\n';
    }
    out << "\n等价表达式（标准与或式，未做最小化；! 非，& 与，| 或）\n";
    for (size_t col = 0; col < outputs.size(); ++col) {
        out << outputs[col] << " = ";
        std::vector<std::string> terms;
        for (auto &row : rows)
            if (row[inputs.size() + col]) {
                std::string term;
                for (size_t i = 0; i < inputs.size(); ++i) {
                    if (i)
                        term += " & ";
                    if (!row[i])
                        term += '!';
                    term += inputs[i];
                }
                terms.push_back("(" + term + ")");
            }
        if (terms.empty())
            out << '0';
        else if (terms.size() == rows.size())
            out << '1';
        else
            for (size_t i = 0; i < terms.size(); ++i) {
                if (i)
                    out << " | ";
                out << terms[i];
            }
        out << '\n';
    }
    return out.str();
}
std::string Table::csv() const {
    std::ostringstream out;
    bool first = true;
    auto cell = [&](std::string s) {
        if (!first)
            out << ',';
        first = false;
        out << '"';
        for (char c : s) {
            if (c == '"')
                out << '"';
            out << c;
        }
        out << '"';
    };
    for (auto &s : inputs)
        cell(s);
    for (auto &s : outputs)
        cell(s);
    out << '\n';
    for (auto &r : rows) {
        for (size_t i = 0; i < r.size(); ++i) {
            if (i)
                out << ',';
            out << r[i];
        }
        out << '\n';
    }
    return out.str();
}
Table expressionTable(const std::string &text) {
    if (text.size() > 4096)
        throw std::runtime_error("表达式太长");
    std::set<std::string> variables;
    struct Parser {
        const std::string &s;
        size_t at = 0;
        std::set<std::string> &vars;
        const std::map<std::string, int> &values;
        void space() {
            while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at])))
                ++at;
        }
        bool take(char c) {
            space();
            if (at < s.size() && s[at] == c) {
                ++at;
                return true;
            }
            return false;
        }
        int atom(int depth = 0) {
            if (depth > 128)
                throw std::runtime_error("表达式嵌套过深");
            if (take('!') || take('~'))
                return !atom(depth + 1);
            if (take('(')) {
                int n = expr(depth + 1);
                if (!take(')'))
                    throw std::runtime_error("缺少右括号");
                return n;
            }
            space();
            if (at < s.size() && (s[at] == '0' || s[at] == '1'))
                return s[at++] - '0';
            size_t start = at;
            if (at < s.size() && (std::isalpha(static_cast<unsigned char>(s[at])) || s[at] == '_')) {
                ++at;
                while (at < s.size() && (std::isalnum(static_cast<unsigned char>(s[at])) || s[at] == '_'))
                    ++at;
            }
            if (at == start)
                throw std::runtime_error("表达式中存在无效字符/操作数");
            auto v = s.substr(start, at - start);
            vars.insert(v);
            auto it = values.find(v);
            return it == values.end() ? 0 : it->second;
        }
        int ands(int d) {
            int n = atom(d);
            while (take('&') || take('*'))
                n = n & atom(d);
            return n;
        }
        int xors(int d) {
            int n = ands(d);
            while (take('^'))
                n = n ^ ands(d);
            return n;
        }
        int expr(int d = 0) {
            int n = xors(d);
            while (take('|') || take('+'))
                n = n | xors(d);
            return n;
        }
        int parse() {
            int n = expr();
            space();
            if (at != s.size())
                throw std::runtime_error("表达式尾部无效");
            return n;
        }
    };
    std::map<std::string, int> values;
    Parser{text, 0, variables, values}.parse();
    if (variables.size() > 10)
        throw std::runtime_error("表达式最多支持 10 个变量");
    Table t;
    t.inputs.assign(variables.begin(), variables.end());
    t.outputs = {"Y"};
    for (size_t i = 0; i < (size_t(1) << t.inputs.size()); ++i) {
        std::vector<int> row;
        for (size_t b = 0; b < t.inputs.size(); ++b) {
            int v = int((i >> (t.inputs.size() - 1 - b)) & 1);
            values[t.inputs[b]] = v;
            row.push_back(v);
        }
        row.push_back(Parser{text, 0, variables, values}.parse());
        t.rows.push_back(row);
    }
    return t;
}
} // namespace eda::logic
