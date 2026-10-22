// 数字电路模型、持久化与事件驱动仿真。几何坐标绝不决定电气连接。
#include "Digital.h"
#include "json.hpp"
#include <deque>
#include <iomanip>
#include <numeric>
#include <limits>
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
           k == "ShiftRegister" || k == "RAM" || k == "Clock" || k == "Random" || k == "Keyboard" ||
           k == "TTY";
}
bool source(const std::string &k) {
    return k == "Input" || k == "Button" || k == "Clock" || k == "Constant" || k == "Power" || k == "Ground" ||
           k == "PullUp" || k == "PullDown";
}
std::string inputId(int i) {
    // 前两脚保持旧工程 A/B；额外脚用 I2...，避免大选择器的 S/Y 与数据脚重名。
    return i < 2 ? std::string(1, char('A' + i)) : "I" + std::to_string(i);
}
bool gate(const std::string &k) {
    return k == "And" || k == "Nand" || k == "Or" || k == "Nor" || k == "Xor" || k == "Xnor";
}
bool switchKind(const std::string &k) { return k == "Transistor" || k == "TransmissionGate"; }
int bitsFor(int maximum) {
    int bits = 1;
    while (bits < 31 && (uint64_t(1) << bits) <= uint64_t(maximum))
        ++bits;
    return bits;
}
std::vector<std::vector<int>> groups(const Part &p) {
    if (!p.bitGroups.empty())
        return p.bitGroups;
    std::vector<std::vector<int>> result;
    for (int i = 0; i < p.width; ++i)
        result.push_back({i});
    return result;
}
std::string portName(const Part &p) {
    return p.label.empty() ? p.id : p.label;
}
Point bodySize(const Part &p, size_t inputs, size_t outputs) {
    const auto &k = p.kind;
    if (k == "Input" || k == "Output")
        return {28, 28};
    if (k == "Constant" || k == "Clock" || k == "Button" || k == "LED" || k == "Probe" ||
        k == "Power" || k == "Ground" || k == "PullUp" || k == "PullDown")
        return {48, 48};
    if (k == "Hex")
        return {104, 54};
    if (k == "LEDMatrix")
        return {90, 90};
    if (k == "SevenSegment")
        return {80, 120};
    if (k == "Not" || k == "Buffer" || k == "TriState")
        return {80, 60};
    if (gate(k))
        return {80, std::max(60.0, 20.0 * inputs + 20)};
    if (k == "Mux" || k == "Demux")
        --inputs; // 选择端位于底边，不占用左侧数据端子的行。
    double height = std::max(60.0, 20.0 * std::max(inputs, outputs) + 40);
    if (k == "Splitter" || k == "Joiner")
        return {80, height};
    return {110, height};
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
int64_t parameter(const Part &p, const std::string &name, int64_t fallback) {
    const auto it = p.params.find(name);
    return it == p.params.end() ? fallback : it->second;
}
// 内置元件目录：kind 是持久化到工程文件的稳定标识，name 是显示名，category 是分类。
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
                                            {"PullUp", "弱上拉", "线路 (Wiring)"},
                                            {"PullDown", "弱下拉", "线路 (Wiring)"},
                                            {"Transistor", "晶体管开关", "线路 (Wiring)"},
                                            {"TransmissionGate", "传输门", "线路 (Wiring)"},
                                            {"Not", "非门 NOT", "逻辑门 (Gates)"},
                                            {"Buffer", "缓冲器", "逻辑门 (Gates)"},
                                            {"And", "与门 AND", "逻辑门 (Gates)"},
                                            {"Or", "或门 OR", "逻辑门 (Gates)"},
                                            {"Nand", "与非门 NAND", "逻辑门 (Gates)"},
                                            {"Nor", "或非门 NOR", "逻辑门 (Gates)"},
                                            {"Xor", "异或门 XOR", "逻辑门 (Gates)"},
                                            {"Xnor", "同或门 XNOR", "逻辑门 (Gates)"},
                                            {"TriState", "三态缓冲器", "逻辑门 (Gates)"},
                                            {"ControlledInverter", "受控反相器", "逻辑门 (Gates)"},
                                            {"EvenParity", "偶校验", "逻辑门 (Gates)"},
                                            {"OddParity", "奇校验", "逻辑门 (Gates)"},
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
                                            {"BitSelector", "位选择器", "复用器 (Plexers)"},
                                            {"BitAdder", "位计数器", "运算器 (Arithmetic)"},
                                            {"BitFinder", "位查找器", "运算器 (Arithmetic)"},
                                            {"DFF", "D 触发器", "存储 (Memory)"},
                                            {"TFF", "T 触发器", "存储 (Memory)"},
                                            {"JKFF", "JK 触发器", "存储 (Memory)"},
                                            {"SRFF", "SR 触发器", "存储 (Memory)"},
                                            {"Register", "寄存器", "存储 (Memory)"},
                                            {"Counter", "计数器", "存储 (Memory)"},
                                            {"ShiftRegister", "移位寄存器", "存储 (Memory)"},
                                            {"RAM", "RAM (256 字)", "存储 (Memory)"},
                                            {"ROM", "ROM (256 字)", "存储 (Memory)"},
                                            {"Random", "随机数发生器", "存储 (Memory)"},
                                            {"Button", "按钮 (双击切换)", "输入/输出 (Input/Output)"},
                                            {"LED", "LED", "输入/输出 (Input/Output)"},
                                            {"Hex", "十六进制显示", "输入/输出 (Input/Output)"},
                                            {"Keyboard", "键盘", "输入/输出 (Input/Output)"},
                                            {"TTY", "终端 TTY", "输入/输出 (Input/Output)"},
                                            {"Joystick", "摇杆", "输入/输出 (Input/Output)"},
                                            {"SevenSegment", "七段数码管（独立引脚）", "输入/输出 (Input/Output)"},
                                            {"LEDMatrix", "LED 矩阵", "输入/输出 (Input/Output)"},
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
// 返回某个元件的全部端口（名称/方向/位宽/坐标）。
// 这是端口定义的唯一来源：绘图、命中测试、连线锚点、仿真建网全部复用它。
// 子电路的端口来自其定义内部的 Input/Output 元件（递归读取定义）。
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
        const auto g = groups(p);
        for (size_t i = 0; i < g.size(); ++i)
            out("B" + std::to_string(i), int(g[i].size()));
    } else if (k == "Joiner") {
        const auto g = groups(p);
        for (size_t i = 0; i < g.size(); ++i)
            in("B" + std::to_string(i), int(g[i].size()));
        out("Y");
    } else if (k == "Extender") {
        in("A", int(parameter(p, "inputWidth", 1)));
        out("Y");
    } else if (k == "Mux") {
        int bits = int(parameter(p, "selectBits", 1));
        for (int i = 0; i < (1 << bits); ++i)
            in(inputId(i));
        in("S", bits);
        out("Y");
    } else if (k == "Demux") {
        in("A");
        int bits = int(parameter(p, "selectBits", 1));
        in("S", bits);
        for (int i = 0; i < (1 << bits); ++i)
            out("Y" + std::to_string(i));
    } else if (k == "Decoder") {
        int bits = int(parameter(p, "selectBits", 2));
        in("A", bits);
        for (int i = 0; i < (1 << bits); ++i)
            out("Y" + std::to_string(i), 1);
    } else if (k == "Encoder") {
        int bits = int(parameter(p, "selectBits", 2));
        for (int i = 0; i < (1 << bits); ++i)
            in("A" + std::to_string(i), 1);
        out("Y", bits);
        out("V", 1);
    } else if (k == "TriState" || k == "ControlledInverter") {
        in("A");
        in("EN", 1);
        out("Y");
    } else if (switchKind(k)) {
        // A/B 均为无方向的电气端点；求值器动态联通网络，不产生伪造的单向驱动。
        in("A");
        in("B");
        if (k == "Transistor")
            in("G", 1);
        else {
            in("EN", 1);
            in("NEN", 1);
        }
    } else if (k == "BitSelector") {
        in("A");
        // 选择的是“分组编号”，不是原总线位号；两位一组的8位总线只需2位选择。
        int groupWidth = int(parameter(p,"outputWidth",1));
        in("S", int(parameter(p, "selectBits", bitsFor((p.width+groupWidth-1)/groupWidth-1))));
        out("Y", int(parameter(p, "outputWidth", 1)));
    } else if (k == "BitAdder" || k == "BitFinder" || k == "EvenParity" || k == "OddParity") {
        in("A");
        out("Y", k == "BitAdder" ? bitsFor(p.width) : k == "BitFinder" ? bitsFor(p.width - 1) : 1);
        if (k == "BitFinder")
            out("V", 1);
    } else if (k == "SevenSegment") {
        for (const auto *pin : {"a", "b", "c", "d", "e", "f", "g", "dp"})
            in(pin, 1);
    } else if (k == "LEDMatrix") {
        in("ROWS", int(parameter(p, "rows", 8)));
        in("COLS", int(parameter(p, "cols", 8)));
    } else if (k == "Joystick") {
        out("X");
        out("Y");
    } else if (k == "Keyboard") {
        in("CLK", 1);
        in("READ", 1);
        in("CLEAR", 1);
        out("DATA", 8);
        out("AVAILABLE", 1);
    } else if (k == "TTY") {
        in("DATA", 8);
        in("CLK", 1);
        in("EN", 1);
        in("CLEAR", 1);
    } else if (k == "RAM" || k == "ROM") {
        in("ADDR", int(parameter(p, "addressBits", 8)));
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
        if (k == "Counter" && parameter(p, "loadable", 0)) {
            in("D");
            in("LOAD", 1);
        }
        if (k == "Random")
            in("EN", 1);
        if (k == "ShiftRegister")
            in("D", 1);
        in("CLK", 1);
        in("CLR", 1);          // 异步清零：高电平有效，与时钟无关
        out(k == "Random" ? "Y" : "Q");
        if (k == "Counter")
            out("CARRY", 1);   // 终端计数输出（计到位宽最大值时为 1），用于级联
    } else {
        if (gate(k)) {
            for (int i = 0; i < int(parameter(p, "inputs", 2)); ++i)
                in(inputId(i));
        } else {
            in("A");
            if (k != "Not" && k != "Buffer" && k != "Negate")
                in("B");
        }
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
    const bool bottomSelect = k == "Mux" || k == "Demux";
    for (auto &q : v)
        if (!(bottomSelect && q.id == "S") && !(k == "TriState" && q.id == "EN"))
            (q.output ? no : ni)++;
    // bodySize() 接受全部输入数量，再为底边选择端留出空间。
    const auto size = bodySize(p, ni + (bottomSelect ? 1 : 0), no);
    int ii = 0, oi = 0;
    for (auto &q : v) {
        q.at = {q.output ? size.x / 2 : -size.x / 2,
                20.0 * ((q.output ? oi++ : ii++) - ((q.output ? no : ni) - 1) * 0.5)};
        const bool simple = gate(k) || k == "Not" || k == "Buffer" || k == "TriState" ||
                            size.y <= 54 || k == "LEDMatrix";
        if (!simple)
            q.at.y += 10; // 功能块顶端留出独立的标题区。
        if (k == "SevenSegment")
            q.at.y = 12 * (ii - 1 - (ni - 1) * .5) + 8;
        if ((bottomSelect && q.id == "S") || (k == "TriState" && q.id == "EN")) {
            --ii;
            q.at = {0, k == "TriState" ? 15.0 : size.y / 2 - 12 * 55.0 / 94};
        }
        if (!q.output && (k == "Or" || k == "Nor" || k == "Xor" || k == "Xnor")) {
            double t = 2 * q.at.y / size.y;
            q.at.x = -40 + 14 * (1 - t * t); // 与 OR 后沿二次曲线完全一致。
        }
        if (k == "Mux" || k == "Demux" || k == "Extender") {
            if ((k == "Mux" && q.output) || (k != "Mux" && !q.output && q.id != "S"))
                q.at.x = (q.output ? 1 : -1) * 39;
        }
        if (p.kind == "Subcircuit") {
            const auto &layout = project.circuit(p.circuit).interfaceLayout;
            const auto it = layout.find(q.id);
            if (it != layout.end())
                q.at = it->second;
        }
        for (int turn = 0; turn < p.rotation; ++turn)
            q.at = {-q.at.y, q.at.x};
    }
    return v;
}
double partHeight(const Project &p, const Part &part) {
    return partSize(p, part).y;
}
Point partSize(const Project &p, const Part &part) {
    auto v = ports(p, part);
    size_t in = 0, out = 0;
    for (auto &q : v)
        (q.output ? out : in)++;
    return bodySize(part, in, out);
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
    for (const auto &id : ids)
        c.interfaceLayout.erase(id);
    c.shapes.erase(
        std::remove_if(c.shapes.begin(), c.shapes.end(), [&](const Shape &s) { return ids.count(s.id); }),
        c.shapes.end());
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
// 加载/编译前统一校验整个工程：规模上限、ID 唯一且合法、位宽/坐标范围、
// 子电路引用存在且无递归、输入/输出名称不重复等。
void Project::validate() const {
    // 在加载或编译工程前，校验所有定义（包括未被使用的定义）。
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
        if (c.shapes.size() > 10000)
            throw std::runtime_error("图形数量超出限制");
        for (const auto &s : c.shapes) {
            idCheck(s.id);
            pos(s.a);
            pos(s.b);
            pos(s.control);
            if (!shapeKind(s.kind) || !std::isfinite(s.stroke) || s.stroke < 1 || s.stroke > 20)
                throw std::runtime_error("图形类型或线宽无效");
            if (distance(s.a, s.b) < .01 ||
                (s.kind != "Line" && s.kind != "Curve" &&
                 (std::abs(s.a.x - s.b.x) < .01 || std::abs(s.a.y - s.b.y) < .01)))
                throw std::runtime_error("图形尺寸不能为零");
            if (s.kind == "Circle" && std::abs(std::abs(s.a.x - s.b.x) - std::abs(s.a.y - s.b.y)) > .01)
                throw std::runtime_error("圆的宽高必须相等");
        }
        std::set<std::string> labels;
        for (auto &p : c.parts) {
            idCheck(p.id);
            pos(p.at);
            if (p.width < 1 || p.width > 32)
                throw std::runtime_error("位宽必须为 1–32");
            if (p.kind == "Clock" && p.width != 1)
                throw std::runtime_error("时钟的位宽必须为 1");
            if (p.rotation < 0 || p.rotation > 3)
                throw std::runtime_error("元件方向必须为 0–3");
            if (p.params.size() > 64)
                throw std::runtime_error("元件参数过多");
            const auto range = [&](const std::string &name, int64_t fallback, int64_t lo, int64_t hi) {
                auto value = parameter(p, name, fallback);
                if (value < lo || value > hi)
                    throw std::runtime_error("元件参数 " + name + " 超出范围");
            };
            range("inputs", 2, 2, 32);
            range("invertMask", 0, 0, UINT32_MAX);
            range("selectBits", p.kind == "Decoder" || p.kind == "Encoder" ? 2 : 1, 1, 8);
            range("inputWidth", 1, 1, 32);
            range("outputWidth", 1, 1, 32);
            range("addressBits", 8, 1, 16);
            range("highTicks", 1, 1, 1000000);
            range("lowTicks", 1, 1, 1000000);
            range("phase", 0, 0, 1000000000);
            range("counterMax", mask(p.width), 0, mask(p.width));
            range("rows", 8, 1, 32);
            range("cols", 8, 1, 32);
            range("mode", 0, 0, 3);
            range("seed", 1, 0, UINT32_MAX);
            for (const auto *flag : {"signExtend", "falling", "loadable", "momentary", "activeLow", "xorOne",
                                     "logisimCounter"})
                range(flag, 0, 0, 1);
            if (p.kind == "Extender" && parameter(p, "inputWidth", 1) > p.width)
                throw std::runtime_error("位扩展器的输入位宽不能大于输出位宽");
            if (p.kind == "Joystick" && p.width > 16)
                throw std::runtime_error("摇杆每个坐标支持 1–16 位");
            if (p.bitGroups.size() > 32)
                throw std::runtime_error("总线分组最多 32 组");
            std::set<int> assigned;
            for (const auto &group : p.bitGroups) {
                if (group.empty() || group.size() > 32)
                    throw std::runtime_error("总线分组必须包含 1–32 个位号");
                for (int bit : group) {
                    if (bit < 0 || bit >= p.width)
                        throw std::runtime_error("总线分组位号超出总线位宽");
                    if (p.kind == "Joiner" && !assigned.insert(bit).second)
                        throw std::runtime_error("合线器的同一个目标位不能被重复映射");
                }
            }
            if (p.kind != "Subcircuit" &&
                std::none_of(library().begin(), library().end(), [&](auto &q) { return q.kind == p.kind; }))
                throw std::runtime_error("不支持的元件：" + p.kind);
            if (p.kind == "Subcircuit")
                circuit(p.circuit);
            const size_t capacity = size_t(1) << int(parameter(p, "addressBits", 8));
            if (p.data.size() > capacity)
                throw std::runtime_error("存储器初值超出配置的地址容量");
            if ((p.kind == "Input" || p.kind == "Output") && !labels.insert(portName(p)).second)
                throw std::runtime_error("输入输出名称不能重复");
        }
        for (const auto &entry : c.interfaceLayout) {
            const auto *pin = c.part(entry.first);
            if (!pin || (pin->kind != "Input" && pin->kind != "Output"))
                throw std::runtime_error("外观引脚布局引用了不存在的输入/输出接口");
            pos(entry.second);
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
// 把工程序列化为版本化的 .logic.json 文本（version=1）。
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
        q["shapes"] = json::array();
        q["interfaceLayout"] = json::object();
        for (const auto &entry : c.interfaceLayout)
            q["interfaceLayout"][entry.first] = point(entry.second);
        for (const auto &s : c.shapes)
            q["shapes"].push_back({{"id", s.id},
                                   {"kind", s.kind},
                                   {"a", point(s.a)},
                                   {"b", point(s.b)},
                                   {"control", point(s.control)},
                                   {"stroke", s.stroke},
                                   {"filled", s.filled},
                                   {"appearance", s.appearance}});
        for (auto &a : c.parts)
            q["parts"].push_back({{"id", a.id},
                                  {"kind", a.kind},
                                  {"label", a.label},
                                  {"circuit", a.circuit},
                                  {"at", point(a.at)},
                                  {"width", a.width},
                                  {"value", a.value},
                                   {"data", a.data},
                                   {"rotation", a.rotation},
                                   {"params", a.params},
                                   {"bitGroups", a.bitGroups}});
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
// 从 JSON 文本反序列化工程，逐字段严格校验后调用 validate()。
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
        if (q.contains("interfaceLayout")) {
            if (!q.at("interfaceLayout").is_object())
                throw std::runtime_error("外观引脚布局格式错误");
            for (auto it = q.at("interfaceLayout").begin(); it != q.at("interfaceLayout").end(); ++it)
                c.interfaceLayout[it.key()] = point(it.value());
        }
        if (q.contains("shapes")) {
            if (!q.at("shapes").is_array())
                throw std::runtime_error("图形列表格式错误");
            for (const auto &a : q.at("shapes")) {
                Shape s;
                s.id = a.at("id");
                s.kind = a.at("kind");
                s.a = point(a.at("a"));
                s.b = point(a.at("b"));
                s.control = point(a.at("control"));
                s.stroke = a.at("stroke");
                s.filled = a.at("filled");
                s.appearance = a.at("appearance");
                c.shapes.push_back(s);
            }
        }
        for (auto &a : q.at("parts")) {
            Part x;
            x.id = a.at("id");
            x.kind = a.at("kind");
            x.label = a.at("label");
            x.circuit = a.at("circuit");
            x.at = point(a.at("at"));
            x.width = int(integer(a.at("width"), 32));
            x.value = uint32_t(integer(a.at("value"), UINT32_MAX));
            if (a.contains("rotation"))
                x.rotation = int(integer(a.at("rotation"), 3));
            if (a.contains("params")) {
                if (!a.at("params").is_object() || a.at("params").size() > 64)
                    throw std::runtime_error("元件参数格式错误");
                for (auto it = a.at("params").begin(); it != a.at("params").end(); ++it) {
                    if (!it.value().is_number_integer() ||
                        (it.value().is_number_unsigned() && it.value().get<uint64_t>() > INT64_MAX))
                        throw std::runtime_error("元件参数必须为有符号 64 位整数");
                    x.params[it.key()] = it.value().get<int64_t>();
                }
            }
            if (a.contains("bitGroups")) {
                const auto &mapping = a.at("bitGroups");
                if (!mapping.is_array() || mapping.size() > 32)
                    throw std::runtime_error("总线分组格式错误");
                for (const auto &group : mapping) {
                    if (!group.is_array() || group.empty() || group.size() > 32)
                        throw std::runtime_error("总线分组格式错误");
                    std::vector<int> bits;
                    for (const auto &bit : group)
                        bits.push_back(int(integer(bit, 31)));
                    x.bitGroups.push_back(std::move(bits));
                }
            }
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
// 把选中的对象连同其依赖的子电路定义打包成一个小工程并序列化；
// 这样跨工程粘贴时子电路依赖能一起携带，悬空端点转成自由节点。
std::string copy(const Project &p, const Circuit &c, const std::set<std::string> &ids) {
    Project clip = p;
    Circuit dest;
    dest.name = c.name;
    for (const auto &entry : c.interfaceLayout)
        if (ids.count(entry.first))
            dest.interfaceLayout.insert(entry);
    for (const auto &s : c.shapes)
        if (ids.count(s.id))
            dest.shapes.push_back(s);
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
// 把剪贴板内容粘贴到目标电路：重新分配全部 ID、重名定义加后缀、
// 重映射子电路接口 ID，并整体施加偏移。
std::set<std::string> paste(Project &p, Circuit &target, const std::string &text, Point offset) {
    // 跨工程粘贴时，定义与接口 ID 必须一起迁移。
    auto clip = deserialize(text);
    auto targetName = target.name;
    auto picked = clip.circuit(clip.main);
    if (picked.parts.empty() && picked.nodes.empty() && picked.shapes.empty())
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
        for (auto &s : c.shapes)
            s.id = p.id("s");
        std::map<std::string, std::string> ids;
        for (auto &q : c.parts) {
            auto oldId = q.id;
            ids[oldId] = p.id("p");
            q.id = ids.at(oldId);
            if (q.kind == "Subcircuit")
                q.circuit = import(q.circuit);
        }
        std::map<std::string, Point> layout;
        for (const auto &entry : c.interfaceLayout)
            layout[ids.at(entry.first)] = entry.second;
        c.interfaceLayout = std::move(layout);
        // 下面的端口 ID 使用导入定义的原始端口顺序进行重映射。
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
    for (auto s : picked.shapes) {
        s.id = p.id("s");
        s.a = s.a + offset;
        s.b = s.b + offset;
        s.control = s.control + offset;
        dest.shapes.push_back(s);
        result.insert(s.id);
    }
    for (auto q : picked.parts) {
        auto old = q.id;
        q.id = p.id("p");
        ids[old] = q.id;
        q.at = q.at + offset;
        if (picked.interfaceLayout.count(old))
            dest.interfaceLayout[q.id] = picked.interfaceLayout.at(old);
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
// 撤销/重做采用“全工程快照 + 双栈”实现：past 最多 100 步，future 保存被撤销的状态。
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
// 四态信号按位编码：value 保存 0/1，known 标记已知位，z 标记高阻位，error 标记冲突位。
// 约定：1=高电平，0=低电平，X=未知，Z=高阻，E=冲突/非法运算。
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
// 仿真器内部实现：把层次化电路展平成扁平实例（Node）与网络（NetState），
// 用并查集合并显式端点与同名节点，再用按时间排序的事件队列驱动求值。
struct Simulator::Impl {
    // Node 是展平后的原语实例；两个实例绝不共享寄存器/RAM 存储。
    struct Node {
        Part part;
        std::string path, object;
        std::map<std::string, int> nets;
        std::map<std::string, Signal> outputs;
        Signal state = Signal::number(0, 1), previousClock = Signal::number(0, 1);
        std::vector<Signal> memory;
        std::deque<uint8_t> keyboard;
        std::string terminal;
        uint32_t inputValue = 0, randomState = 1;
    };
    struct NetState {
        int width = 1;
        bool mismatch = false;
        bool optionalOnly = true;  // 该网络只接到可悬空的控制引脚（CLR），未接线不算悬空
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
    std::function<void(const Trace &)> traceSink;
    bool hasSwitches = false;
    uint64_t now = 0;
    uint64_t ticks = 0;          // 累计时钟半周期数（tick() 被调用的次数）
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
    // 递归展平电路：为每个原语生成一个 Node，并把显式端点/同名节点并入同一网络。
    void flatten(const Circuit &c, const std::string &prefix, const std::string &top, bool nested) {
        // 只在本实例内合并显式端点与同名的节点（label）。
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
            auto sep = k.first.find(':');
            n.objects.insert(k.first.substr(0, sep));
        }
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto &n = nodes[i];
            hasSwitches = hasSwitches || switchKind(n.part.kind);
            for (auto &port : ports(project, n.part)) {
                int net = roots.at(n.path + ":" + port.id);
                n.nets[port.id] = net;
                if (port.output)
                    nets[net].drivers.push_back({i, port.id});
                else {
                    nets[net].users.insert(i);
                    if (port.id != "CLR" && port.id != "CLEAR")
                        nets[net].optionalOnly = false;
                }
            }
        }
        // 容量上限按展开后的实例求和，避免少量定义被大量实例化后耗尽内存。
        size_t memoryWords = 0;
        for (const auto &node : nodes)
            if (node.part.kind == "RAM" || node.part.kind == "ROM") {
                memoryWords += size_t(1) << int(parameter(node.part, "addressBits", 8));
                if (memoryWords > 4194304)
                    throw std::runtime_error("展开后的存储器总容量超过 4194304 字");
            }
        reset();
    }
    void reset() {
        queue.clear();
        traces.clear();
        now = 0;
        ticks = 0;
        oscillating = false;
        oscillators.clear();
        for (auto &net : nets)
            net.signal = net.mismatch ? Signal::failure(net.width) : Signal::floating(net.width);
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto &n = nodes[i];
            n.state = Signal::number(n.part.value, n.part.width);
            n.inputValue = n.part.value;
            n.randomState = uint32_t(parameter(n.part, "seed", n.part.value ? n.part.value : 1));
            if (!n.randomState)
                n.randomState = 1;
            n.previousClock = Signal::number(0, 1);
            n.outputs.clear();
            n.keyboard.clear();
            n.terminal.clear();
            n.memory.clear();
            if (n.part.kind == "RAM" || n.part.kind == "ROM") {
                n.memory.assign(size_t(1) << int(parameter(n.part, "addressBits", 8)),
                                Signal::number(0, n.part.width));
                for (size_t j = 0; j < n.part.data.size(); ++j)
                    n.memory[j] = Signal::number(n.part.data[j], n.part.width);
            }
            if (n.part.kind == "Clock")
                n.state = Signal::number(clockValue(n, 0), 1);
            for (auto &port : ports(project, n.part))
                if (port.output)
                    n.outputs[port.id] = Signal::floating(port.width);
            queue[1].insert(i);
        }
    }
    Signal input(const Node &n, const std::string &pin) const { return nets.at(n.nets.at(pin)).signal; }
    uint32_t clockValue(const Node &n, uint64_t tick) const {
        uint64_t low = uint64_t(parameter(n.part, "lowTicks", 1));
        uint64_t high = uint64_t(parameter(n.part, "highTicks", 1));
        uint64_t phase = uint64_t(parameter(n.part, "phase", 0));
        return ((tick + phase + (n.part.value & 1 ? low : 0)) % (low + high)) >= low;
    }
    bool edge(Node &n, Signal clk) {
        const auto &old = n.previousClock;
        const bool falling = parameter(n.part, "falling", 0) != 0;
        bool result = old.defined() && clk.defined() &&
                      (falling ? old.value && !clk.value : !old.value && clk.value);
        n.previousClock = clk;
        return result;
    }
    void record(Trace event) {
        traces.push_back(event);
        if (traceSink)
            traceSink(event);
    }
    // 按元件 kind 求值：读取输入网络值，把结果写入 n.outputs。
    // 组合门按位运算；时序元件只在明确的 0→1 上升沿采样。
    void evaluate(Node &n) {
        auto &k = n.part.kind;
        int w = n.part.width;
        auto a = [&](const std::string &pin) { return input(n, pin); };
        auto put = [&](const std::string &pin, Signal s) { n.outputs[pin] = s; };
        auto number = [&](const std::string &pin, uint32_t value, int width = 0) {
            put(pin, Signal::number(value, width ? width : w));
        };
        if (source(k)) {
            number("Y", k == "Power" || k == "PullUp" ? mask(w)
                        : k == "Ground" || k == "PullDown" ? 0
                                                          : n.state.value);
            return;
        }
        if (k == "Output" || k == "LED" || k == "Hex" || k == "Probe" || k == "SevenSegment" ||
            k == "LEDMatrix" || switchKind(k))
            return;
        if (k == "Joystick") {
            number("X", n.inputValue & 0xffffU);
            number("Y", n.inputValue >> 16);
            return;
        }
        if (k == "Keyboard" || k == "TTY") {
            bool trigger = edge(n, a("CLK"));
            auto clear = a("CLEAR");
            if (clear.defined() && clear.value) {
                n.keyboard.clear();
                n.terminal.clear();
            } else if (trigger) {
                auto enable = a(k == "Keyboard" ? "READ" : "EN");
                if (enable.defined() && enable.value) {
                    if (k == "Keyboard") {
                        if (!n.keyboard.empty())
                            n.keyboard.pop_front();
                    } else {
                        auto data = a("DATA");
                        if (data.defined()) {
                            char ch = char(data.value & 255);
                            if (ch == '\b') {
                                if (!n.terminal.empty())
                                    n.terminal.pop_back();
                            } else if (ch == '\f')
                                n.terminal.clear();
                            else if (ch != '\0' && n.terminal.size() < 1048576)
                                n.terminal += ch;
                        }
                    }
                }
            }
            if (k == "Keyboard") {
                number("DATA", n.keyboard.empty() ? 0 : n.keyboard.front(), 8);
                number("AVAILABLE", !n.keyboard.empty(), 1);
            }
            return;
        }
        if (k == "Splitter") {
            auto s = a("A");
            const auto g = groups(n.part);
            for (size_t i = 0; i < g.size(); ++i) {
                Signal result = Signal::unknown(int(g[i].size()));
                for (size_t j = 0; j < g[i].size(); ++j) {
                    auto from = unsigned(g[i][j]);
                    result.value |= ((s.value >> from) & 1U) << j;
                    result.known |= ((s.known >> from) & 1U) << j;
                    result.z |= ((s.z >> from) & 1U) << j;
                    result.error |= ((s.error >> from) & 1U) << j;
                }
                put("B" + std::to_string(i), result);
            }
            return;
        }
        if (k == "Joiner") {
            Signal s = Signal::number(0, w);
            const auto g = groups(n.part);
            for (size_t i = 0; i < g.size(); ++i) {
                auto b = a("B" + std::to_string(i));
                for (size_t j = 0; j < g[i].size(); ++j) {
                    uint32_t target = uint32_t(1) << g[i][j];
                    s.known &= ~target;
                    s.value |= ((b.value >> j) & 1U) << g[i][j];
                    s.known |= ((b.known >> j) & 1U) << g[i][j];
                    s.z |= ((b.z >> j) & 1U) << g[i][j];
                    s.error |= ((b.error >> j) & 1U) << g[i][j];
                }
            }
            put("Y", s);
            return;
        }
        if (k == "Extender") {
            auto s = a("A");
            int oldWidth = s.width;
            s.width = w;
            uint32_t added = mask(w) & ~mask(oldWidth);
            if (parameter(n.part, "signExtend", 0)) {
                uint32_t sign = uint32_t(1) << (oldWidth - 1);
                if (s.value & sign)
                    s.value |= added;
                if (s.known & sign)
                    s.known |= added;
                if (s.z & sign)
                    s.z |= added;
                if (s.error & sign)
                    s.error |= added;
            } else
                s.known |= added;
            put("Y", s);
            return;
        }
        if (k == "TriState" || k == "ControlledInverter") {
            auto en = a("EN");
            auto value = a("A");
            if (k == "ControlledInverter") {
                value.value = (~value.value) & value.known;
                value.z = 0;
            }
            bool enabled = bool(en.value) != bool(parameter(n.part, "activeLow", 0));
            put("Y", en.defined() ? (enabled ? value : Signal::floating(w)) : Signal::unknown(w));
            return;
        }
        if (k == "Mux") {
            auto s = a("S");
            auto result = a("A");
            if (s.defined())
                result = a(inputId(int(s.value)));
            else
                for (int i = 1; i < (1 << int(parameter(n.part, "selectBits", 1))); ++i)
                    if (a(inputId(i)) != result) {
                        result = Signal::unknown(w);
                        break;
                    }
            put("Y", result);
            return;
        }
        if (k == "Demux") {
            auto s = a("S");
            for (int i = 0; i < (1 << int(parameter(n.part, "selectBits", 1))); ++i)
                put("Y" + std::to_string(i), !s.defined()             ? Signal::unknown(w)
                                             : s.value == uint32_t(i) ? a("A")
                                                                      : Signal::number(0, w));
            return;
        }
        if (k == "Decoder") {
            auto s = a("A");
            for (int i = 0; i < (1 << int(parameter(n.part, "selectBits", 2))); ++i)
                put("Y" + std::to_string(i),
                    s.defined() ? Signal::number(s.value == uint32_t(i), 1) : Signal::unknown(1));
            return;
        }
        if (k == "Encoder") {
            int value = 0;
            bool valid = false, known = true;
            int bits = int(parameter(n.part, "selectBits", 2));
            for (int i = 0; i < (1 << bits); ++i) {
                auto s = a("A" + std::to_string(i));
                if (!s.defined())
                    known = false;
                if (s.defined() && s.value) {
                    value = i;
                    valid = true;
                }
            }
            put("Y", known ? Signal::number(value, bits) : Signal::unknown(bits));
            put("V", known ? Signal::number(valid, 1) : Signal::unknown(1));
            return;
        }
        if (k == "BitSelector" || k == "BitAdder" || k == "BitFinder" || k == "EvenParity" ||
            k == "OddParity") {
            auto value = a("A");
            int outputWidth = n.outputs.at("Y").width;
            if (k == "BitSelector") {
                auto select = a("S");
                Signal result = Signal::unknown(outputWidth);
                if (select.defined()) {
                    uint64_t shift = uint64_t(select.value) * uint64_t(outputWidth);
                    result = Signal::number(0, outputWidth);
                    if (shift < uint64_t(w)) {
                        uint32_t covered = mask(std::min(outputWidth, w - int(shift)));
                        result.value = (value.value >> shift) & covered;
                        result.known = ((value.known >> shift) & covered) | (mask(outputWidth) & ~covered);
                        result.z = (value.z >> shift) & covered;
                        result.error = (value.error >> shift) & covered;
                    }
                }
                put("Y", result);
            } else if (!value.defined()) {
                put("Y", Signal::unknown(outputWidth));
                if (k == "BitFinder")
                    put("V", Signal::unknown(1));
            } else if (k == "BitFinder") {
                int mode = int(parameter(n.part, "mode", 0));
                uint32_t candidates = (mode >= 2 ? ~value.value : value.value) & mask(w);
                int found = -1;
                for (int i = 0; i < w; ++i) {
                    int bit = mode & 1 ? w - 1 - i : i;
                    if ((candidates >> bit) & 1U) {
                        found = bit;
                        break;
                    }
                }
                number("Y", found < 0 ? 0 : uint32_t(found), outputWidth);
                number("V", found >= 0, 1);
            } else {
                uint32_t bits = value.value;
                unsigned count = 0;
                while (bits) {
                    bits &= bits - 1;
                    ++count;
                }
                number("Y", k == "BitAdder" ? count : k == "OddParity" ? count % 2 : !(count % 2),
                       outputWidth);
            }
            return;
        }
        if (k == "ROM" || k == "RAM") {
            auto addr = a("ADDR");
            if (k == "RAM") {
                auto clk = a("CLK");
                if (edge(n, clk)) {
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
            bool trigger = edge(n, clk); // 清零期间也记录时钟历史，避免解除清零后出现虚假边沿。
            auto clr = a("CLR");
            if (clr.defined() && clr.value) {
                n.state = Signal::number(0, w);  // 异步清零：与时钟无关，优先级高于时钟沿
                if (k == "Random") {
                    n.randomState = uint32_t(parameter(n.part, "seed", n.part.value ? n.part.value : 1));
                    if (!n.randomState)
                        n.randomState = 1;
                }
            } else if (trigger) {
                if (k == "DFF" || k == "Register") {
                    n.state = a("D");
                    n.state.z = 0;
                } else if (k == "Counter") {
                    auto en = a("EN");
                    Signal load = parameter(n.part, "loadable", 0) ? a("LOAD") : Signal::number(0, 1);
                    const bool external = parameter(n.part, "logisimCounter", 0) != 0;
                    // 经典 Logisim 的 CT/LD 组合同时表示计数方向和装载；原生 LOAD 则优先装载。
                    // 导入器显式打标后才切换语义，避免已有原生工程悄悄改变行为。
                    if (external && (!load.defined() || !en.defined()))
                        n.state = Signal::unknown(w);
                    else if (external && en.value && load.value)
                        n.state = n.state.defined()
                                      ? Signal::number(n.state.value == 0
                                                           ? uint32_t(parameter(n.part, "counterMax", mask(w)))
                                                           : n.state.value - 1,
                                                       w)
                                      : Signal::unknown(w);
                    else if (load.defined() && load.value) {
                        n.state = a("D");
                        n.state.z = 0;
                    } else if (!load.defined() || !en.defined())
                        n.state = Signal::unknown(w);
                    else if (en.value && n.state.defined())
                        n.state = Signal::number(n.state.value >= uint32_t(parameter(n.part, "counterMax", mask(w)))
                                                     ? 0
                                                     : n.state.value + 1,
                                                 w);
                } else if (k == "Random") {
                    auto en = a("EN");
                    if (!en.defined())
                        n.state = Signal::unknown(w);
                    else if (en.value) {
                        // 固定种子的 xorshift32 便于课程演示重放，实例之间不共享随机状态。
                        n.randomState ^= n.randomState << 13;
                        n.randomState ^= n.randomState >> 17;
                        n.randomState ^= n.randomState << 5;
                        n.state = Signal::number(n.randomState, w);
                    }
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
            put(k == "Random" ? "Y" : "Q", n.state);
            if (k == "Counter") {
                uint32_t terminal = uint32_t(parameter(n.part, "counterMax", mask(w)));
                bool cleared = false;
                if (parameter(n.part, "logisimCounter", 0)) {
                    const auto enable = a("EN");
                    const auto load = parameter(n.part, "loadable", 0) ? a("LOAD") : Signal::number(0, 1);
                    if (enable.defined() && enable.value && load.defined() && load.value)
                        terminal = 0;
                    cleared = clr.defined() && clr.value;
                }
                put("CARRY", Signal::number(!cleared && n.state.defined() && n.state.value == terminal, 1));
            }
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
        if (gate(k)) {
            bool andGate = k == "And" || k == "Nand", orGate = k == "Or" || k == "Nor";
            Signal s = Signal::number(andGate ? mask(w) : 0, w);
            std::vector<Signal> values;
            uint32_t inversions = uint32_t(parameter(n.part, "invertMask", 0));
            int count = int(parameter(n.part, "inputs", 2));
            for (int i = 0; i < count; ++i) {
                auto value = a(inputId(i));
                if ((inversions >> i) & 1U) {
                    value.value = (~value.value) & value.known;
                    value.z = 0;
                }
                values.push_back(value);
                Signal next = Signal::unknown(w);
                if (andGate) {
                    uint32_t zero = (s.known & ~s.value) | (value.known & ~value.value);
                    next.known = (s.known & value.known) | zero;
                    next.value = s.value & value.value & next.known;
                } else if (orGate) {
                    uint32_t one = (s.known & s.value) | (value.known & value.value);
                    next.known = (s.known & value.known) | one;
                    next.value = (s.value | value.value) & next.known;
                } else {
                    next.known = s.known & value.known;
                    next.value = (s.value ^ value.value) & next.known;
                }
                next.error = (s.error | value.error) & ~next.known;
                s = next;
            }
            if (!andGate && !orGate && parameter(n.part, "xorOne", 0)) {
                s = Signal::unknown(w);
                for (int bit = 0; bit < w; ++bit) {
                    uint32_t b = uint32_t(1) << bit;
                    int ones = 0, unknown = 0;
                    bool error = false;
                    for (const auto &value : values) {
                        ones += (value.known & value.value & b) != 0;
                        unknown += (value.known & b) == 0;
                        error = error || (value.error & b);
                    }
                    if (ones >= 2 || !unknown) {
                        s.known |= b;
                        if (ones == 1)
                            s.value |= b;
                    } else if (error)
                        s.error |= b;
                }
            }
            if (k == "Nand" || k == "Nor" || k == "Xnor")
                s.value = (~s.value) & s.known;
            put("Y", s);
            return;
        }
        auto y = a("B");
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
    // 逐位合并同一网络上的多个驱动：
    // 无驱动→Z；单一确定驱动→0/1；多个不同确定驱动或 error→E。
    Signal resolve(const NetState &net) {
        if (net.mismatch)
            return Signal::failure(net.width);
        Signal result = Signal::floating(net.width);
        for (int bit = 0; bit < net.width; ++bit) {
            uint32_t b = uint32_t(1) << bit;
            bool zero = false, one = false, unknown = false, error = false;
            bool strong = false;
            for (const auto &d : net.drivers) {
                const auto &node = nodes[d.first];
                const auto &s = node.outputs.at(d.second);
                if (node.part.kind != "PullUp" && node.part.kind != "PullDown" && !(s.z & b))
                    strong = true;
            }
            for (auto &d : net.drivers) {
                const auto &kind = nodes[d.first].part.kind;
                if (strong && (kind == "PullUp" || kind == "PullDown"))
                    continue;
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
    std::vector<Signal> switchedSignals() {
        // 开关不是驱动器。每批事件根据控制端临时联通 A/B，合并两边的真实驱动后再解析。
        // 静态连线的 roots 保持不变，因此关断可恢复两个独立网络，且观察路径不会漂移。
        std::vector<int> connected(nets.size());
        std::iota(connected.begin(), connected.end(), 0);
        std::function<int(int)> rootOf = [&](int i) {
            return connected[i] == i ? i : connected[i] = rootOf(connected[i]);
        };
        auto connect = [&](int a, int b) {
            a = rootOf(a);
            b = rootOf(b);
            if (a != b)
                connected[b] = a;
        };
        std::vector<std::pair<int, int>> uncertain;
        for (const auto &node : nodes) {
            if (!switchKind(node.part.kind))
                continue;
            int state = 0; // 0 断开，1 导通，-1 控制未知（保守解析所有可能的连通结果）。
            if (node.part.kind == "Transistor") {
                auto control = input(node, "G");
                state = control.defined()
                            ? int(bool(control.value) != bool(parameter(node.part, "activeLow", 0)))
                            : -1;
            } else {
                auto enable = input(node, "EN"), complement = input(node, "NEN");
                if ((enable.defined() && !enable.value) || (complement.defined() && complement.value))
                    state = 0;
                else if (enable.defined() && complement.defined())
                    state = 1;
                else
                    state = -1;
            }
            int a = node.nets.at("A"), b = node.nets.at("B");
            if (state > 0)
                connect(a, b);
            else if (state < 0)
                uncertain.push_back({a, b});
        }
        auto collect = [&]() {
            std::map<int, NetState> merged;
            for (size_t i = 0; i < nets.size(); ++i) {
                auto id = rootOf(int(i));
                auto found = merged.find(id);
                if (found == merged.end()) {
                    NetState group;
                    group.width = nets[i].width;
                    group.mismatch = nets[i].mismatch;
                    merged.emplace(id, std::move(group));
                }
                auto &group = merged.at(id);
                group.mismatch = group.mismatch || nets[i].mismatch || group.width != nets[i].width;
                group.width = std::max(group.width, nets[i].width);
                group.drivers.insert(group.drivers.end(), nets[i].drivers.begin(), nets[i].drivers.end());
            }
            std::map<int, Signal> values;
            for (const auto &entry : merged)
                values[entry.first] = resolve(entry.second);
            std::vector<Signal> result;
            result.reserve(nets.size());
            for (size_t i = 0; i < nets.size(); ++i) {
                auto s = values.at(rootOf(int(i)));
                s.width = nets[i].width;
                uint32_t keep = mask(s.width);
                s.value &= keep;
                s.known &= keep;
                s.z &= keep;
                s.error &= keep;
                result.push_back(s);
            }
            return result;
        };
        auto result = collect();
        if (!uncertain.empty()) {
            for (const auto &link : uncertain)
                connect(link.first, link.second);
            auto possible = collect();
            for (size_t i = 0; i < result.size(); ++i) {
                const auto original = result[i];
                const auto candidate = possible[i];
                uint32_t same = ~(original.value ^ candidate.value) & ~(original.known ^ candidate.known) &
                                ~(original.z ^ candidate.z) & ~(original.error ^ candidate.error) &
                                mask(original.width);
                result[i] = {original.width, original.value & same, original.known & same,
                             original.z & same, original.error & same};
            }
        }
        return result;
    }
    bool step() {
        // 整批事件都基于旧网络值求值，再统一发布所有变化网络。
        // 若在第一个循环里就更新网络，时钟采样会依赖元件排列顺序。
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
                    record({now, n.path, out.first, out.second.text()});
                }
        }
        if (traces.size() > 2000)
            traces.erase(traces.begin(), traces.begin() + ptrdiff_t(traces.size() - 2000));
        std::vector<Signal> switched;
        if (hasSwitches) {
            switched = switchedSignals();
            for (size_t i = 0; i < nets.size(); ++i)
                affected.insert(int(i));
        }
        for (auto i : affected) {
            auto &net = nets[i];
            auto s = hasSwitches ? switched[i] : resolve(net);
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
        if (n.path == id && (n.part.kind == "Input" || n.part.kind == "Button" || n.part.kind == "Joystick")) {
            n.inputValue = value;
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
// 连续推进事件批次直到队列为空或预算耗尽；预算耗尽说明电路振荡或传播过长。
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
// 一个基础节拍是用户定义的离散时钟刻度，与传播事件时间和界面播放毫秒数分离。
// highTicks=lowTicks=1 时保留原来每次 tick 翻转一次的旧工程行为。
void Simulator::tick() {
    ++impl->ticks;
    for (size_t i = 0; i < impl->nodes.size(); ++i) {
        auto &n = impl->nodes[i];
        if (n.part.kind == "Clock") {
            auto value = Signal::number(impl->clockValue(n, impl->ticks), 1);
            if (value != n.state) {
                n.state = value;
                impl->queue[impl->now + 1].insert(i);
            }
        }
    }
    settle();
}
Signal Simulator::read(const Endpoint &e) const {
    auto it = impl->roots.find(e.key());
    return it == impl->roots.end() ? Signal::unknown(1) : impl->nets[it->second].signal;
}
std::vector<Signal> Simulator::memory(const std::string &path) const {
    for (const auto &node : impl->nodes)
        if (node.path == path && (node.part.kind == "RAM" || node.part.kind == "ROM"))
            return node.memory;
    throw std::runtime_error("不是可观察的存储器实例：" + path);
}
void Simulator::typeText(const std::string &path, const std::string &utf8) {
    for (size_t i = 0; i < impl->nodes.size(); ++i) {
        auto &node = impl->nodes[i];
        if (node.path == path && node.part.kind == "Keyboard") {
            if (node.keyboard.size() + utf8.size() > 65536)
                throw std::runtime_error("键盘缓冲区最多 65536 字节");
            for (unsigned char ch : utf8)
                node.keyboard.push_back(ch);
            impl->queue[impl->now + 1].insert(i);
            return;
        }
    }
    throw std::runtime_error("不是键盘实例：" + path);
}
std::string Simulator::text(const std::string &path) const {
    for (const auto &node : impl->nodes)
        if (node.path == path && node.part.kind == "TTY")
            return node.terminal;
    throw std::runtime_error("不是终端实例：" + path);
}
void Simulator::setTraceSink(std::function<void(const Trace &)> sink) {
    impl->traceSink = std::move(sink);
}
std::vector<Issue> Simulator::issues() const {
    std::vector<Issue> result;
    for (auto &n : impl->nets) {
        std::string message;
        if (n.mismatch)
            message = "总线位宽不匹配";
        else if (n.signal.error)
            message = "驱动冲突或非法运算 (E)";
        else if (n.signal.z && !n.users.empty() && !n.optionalOnly)
            message = "输入悬空 / 高阻态 (Z)";
        else if (!n.signal.defined() && !n.users.empty() && !n.optionalOnly)
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
uint64_t Simulator::ticks() const {
    return impl->ticks;
}
uint64_t Simulator::cycles() const {
    return impl->ticks / 2;
}
bool Simulator::oscillating() const {
    return impl->oscillating;
}
size_t Simulator::pending() const {
    size_t n = 0;
    for (auto &b : impl->queue)
        n += b.second.size();
    return n;
}
std::string Simulator::netlist(bool kicad) const {
    // 复用仿真所用的并查集结果。画面交叉不会产生网络。
    // 该方法有意只读拓扑，绝不触碰实时信号、时钟或事件队列。
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
    // 工程师指定的 label 保留其所在实例的作用域；不同实例中的同名 label 彼此不同。
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
                          {"data", part.data},   {"pins", json::array()}, {"rotation", part.rotation},
                          {"params", part.params}, {"bitGroups", part.bitGroups}};
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

    // KiCad 的通用 S-expression 连接使用标量引脚，因此要把每个总线位显式展开。
    // 这些都是逻辑原语，没有 PCB 封装或模拟 SPICE 模型。
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

// 穷举组合电路的所有输入组合生成真值表（输入总位数≤10）。
// 真值表分析使用全新的仿真器，因此遍历输入组合不会改变编辑器状态。
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
        cell("IN:" + s);
    for (auto &s : outputs)
        cell("OUT:" + s);
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
// 手写递归下降解析器求表达式真值表，优先级：非(!/~) > 与(&/*) > 异或(^) > 或(|/+)。
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
