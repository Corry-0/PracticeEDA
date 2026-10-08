// Logisim 2.7.1 标准 XML 交换。连接通过真实 Tunnel 表达，每个网络都有独立标签。
// 这样不会因两套符号尺寸和自动折线不同，把画面交叉误变成电气连接。
#include "CircExchange.h"
#include "tinyxml2.h"
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>

namespace eda::logic {
namespace {
using Attrs = std::map<std::string, std::string>;
std::string xy(Point p) {
    return "(" + std::to_string(int(std::lround(p.x))) + "," + std::to_string(int(std::lround(p.y))) + ")";
}
Point turn(Point p, int rotation) {
    for (int i = 0; i < rotation % 4; ++i) p = {-p.y, p.x};
    return p;
}
std::string facing(int rotation) {
    static const char *names[] = {"east", "south", "west", "north"};
    return names[(rotation % 4 + 4) % 4];
}
std::string hex(uint32_t value) { std::ostringstream out; out << "0x" << std::hex << value; return out.str(); }
struct Device { std::string name, library; Attrs attrs; };

// 将本地属性翻译成 Logisim 公布的稳定属性名；不写入任何私有工程 JSON。
Device device(const Part &p) {
    Device d; d.attrs["label"] = p.label; d.attrs["width"] = std::to_string(p.width);
    d.attrs["facing"] = facing(p.rotation);
    const auto &k = p.kind;
    static const std::map<std::string, std::string> gates = {{"And", "AND Gate"}, {"Or", "OR Gate"}, {"Nand", "NAND Gate"}, {"Nor", "NOR Gate"}, {"Xor", "XOR Gate"}, {"Xnor", "XNOR Gate"}};
    if (gates.count(k)) {
        d.name = gates.at(k); d.library = "#Gates"; d.attrs["inputs"] = std::to_string(parameter(p, "inputs", 2)); d.attrs["size"] = "50";
        if (k == "Xor" || k == "Xnor") d.attrs["xor"] = parameter(p, "xorOne", 0) ? "1" : "odd";
        for (int i = 0; i < parameter(p, "inputs", 2); ++i)
            if ((uint64_t(parameter(p, "invertMask", 0)) >> i) & 1)
                d.attrs["negate" + std::to_string(i)] = "true";
    } else if (k == "Not" || k == "Buffer" || k == "TriState" || k == "ControlledInverter") {
        d.name = k == "Not" ? "NOT Gate" : k == "TriState" ? "Controlled Buffer" : k == "ControlledInverter" ? "Controlled Inverter" : "Buffer"; d.library = "#Gates";
    } else if (k == "Input" || k == "Output" || k == "Constant" || k == "Power" || k == "Ground" || k == "Clock" || k == "Probe") {
        d.name = k == "Input" || k == "Output" ? "Pin" : k; d.library = "#Wiring";
        if (d.name == "Pin") { d.attrs["output"] = k == "Output" ? "true" : "false"; d.attrs["tristate"] = "false"; }
        if (k == "Constant") d.attrs["value"] = hex(p.value);
        if (k == "Clock") { d.attrs["highDuration"] = std::to_string(parameter(p, "highTicks", 1)); d.attrs["lowDuration"] = std::to_string(parameter(p, "lowTicks", 1)); }
    } else if (k == "Extender") {
        d.name = "Bit Extender"; d.library = "#Wiring";
        d.attrs["in_width"] = std::to_string(parameter(p, "inputWidth", 1)); d.attrs["out_width"] = std::to_string(p.width); d.attrs["type"] = parameter(p, "signExtend", 0) ? "sign" : "zero";
        d.attrs.erase("width"); d.attrs.erase("facing");
    } else if (k == "PullUp" || k == "PullDown") {
        d.name = "Pull Resistor"; d.library = "#Wiring"; d.attrs["pull"] = k == "PullUp" ? "1" : "0";
    } else if (k == "Splitter" || k == "Joiner") {
        d.name = "Splitter"; d.library = "#Wiring"; d.attrs["incoming"] = std::to_string(p.width); d.attrs["appear"] = "center";
        auto groups = p.bitGroups;
        if (groups.empty()) { groups.resize(p.width); for (int i = 0; i < p.width; ++i) groups[i].push_back(i); }
        d.attrs["fanout"] = std::to_string(groups.size());
        for (int i = 0; i < p.width; ++i) d.attrs["bit" + std::to_string(i)] = "none";
        for (size_t group = 0; group < groups.size(); ++group) {
            if (!std::is_sorted(groups[group].begin(), groups[group].end())) throw std::runtime_error("内部错误：未展开逆序位分组");
            for (auto bit : groups[group]) d.attrs["bit" + std::to_string(bit)] = std::to_string(group);
        }
        d.attrs.erase("width");
    } else if (k == "BitSelector") {
        d.name = "BitSelector"; d.library = "#Plexers"; d.attrs["group"] = std::to_string(parameter(p, "outputWidth", 1));
    } else if (k == "Mux" || k == "Demux" || k == "Decoder" || k == "Encoder") {
        d.name = k == "Mux" ? "Multiplexer" : k == "Demux" ? "Demultiplexer" : k == "Decoder" ? "Decoder" : "Priority Encoder"; d.library = "#Plexers";
        d.attrs["select"] = std::to_string(parameter(p, "selectBits", k == "Decoder" || k == "Encoder" ? 2 : 1));
        if (k == "Encoder") d.attrs["disabled"] = "0";
        else { d.attrs["enable"] = "false"; d.attrs["tristate"] = "false"; }
        if (k == "Decoder" || k == "Encoder") d.attrs.erase("width");
    } else if (k == "Register" || k == "Counter" || k == "DFF" || k == "TFF" || k == "JKFF" || k == "SRFF" || k == "RAM" || k == "ROM") {
        d.library = "#Memory";
        static const std::map<std::string, std::string> kinds = {{"DFF", "D Flip-Flop"}, {"TFF", "T Flip-Flop"}, {"JKFF", "J-K Flip-Flop"}, {"SRFF", "S-R Flip-Flop"}};
        d.name = kinds.count(k) ? kinds.at(k) : k;
        d.attrs.erase("facing");
        if (k == "RAM" || k == "ROM") {
            d.attrs.erase("width"); d.attrs["addrWidth"] = std::to_string(parameter(p, "addressBits", 8)); d.attrs["dataWidth"] = std::to_string(p.width);
            if (k == "RAM") d.attrs["bus"] = "separate";
            else { std::ostringstream contents; contents << "addr/data: " << parameter(p, "addressBits", 8) << ' ' << p.width << '\n' << std::hex; for (auto value : p.data) contents << value << ' '; d.attrs["contents"] = contents.str(); }
        } else {
            if (p.value) throw std::runtime_error("Logisim 2.7 无法直接保存非零时序初值：" + (p.label.empty() ? p.id : p.label) + "；请将初值设为0或使用显式复位/预置电路");
            d.attrs["trigger"] = parameter(p, "falling", 0) ? "falling" : "rising";
            if (k == "Counter") { uint32_t maximum = p.width == 32 ? UINT32_MAX : (uint32_t(1) << p.width) - 1; d.attrs["max"] = hex(uint32_t(parameter(p, "counterMax", maximum))); d.attrs["ongoal"] = "wrap"; }
        }
    } else if (k == "Add" || k == "Subtract" || k == "Multiply" || k == "Divide" || k == "Negate" || k == "Compare" || k == "BitAdder" || k == "BitFinder") {
        static const std::map<std::string, std::string> names = {{"Add", "Adder"}, {"Subtract", "Subtractor"}, {"Multiply", "Multiplier"}, {"Divide", "Divider"}, {"Negate", "Negator"}, {"Compare", "Comparator"}, {"BitAdder", "BitAdder"}, {"BitFinder", "BitFinder"}};
        d.name = names.at(k); d.library = "#Arithmetic"; d.attrs.erase("facing");
        if (k == "Compare") d.attrs["mode"] = "unsigned";
        if (k == "BitAdder") d.attrs["numInputs"] = "1";
        if (k == "BitFinder") { const char *modes[] = {"low1", "high1", "low0", "high0"}; d.attrs["type"] = modes[std::clamp(int(parameter(p, "mode", 0)), 0, 3)]; }
    } else if (k == "Button" || k == "LED" || k == "Hex" || k == "SevenSegment" || k == "LEDMatrix" || k == "Joystick") {
        static const std::map<std::string, std::string> names = {{"Button", "Button"}, {"LED", "LED"}, {"Hex", "Hex Digit Display"}, {"SevenSegment", "7-Segment Display"}, {"LEDMatrix", "DotMatrix"}, {"Joystick", "Joystick"}};
        d.name = names.at(k); d.library = "#I/O"; d.attrs.erase("facing");
        if (k == "Button" && !parameter(p, "momentary", 0)) throw std::runtime_error("保持切换按钮与 Logisim 瞬时 Button 不等价，请改为 Input 或瞬时按钮：" + p.id);
        if (k == "Hex" && p.width != 4) throw std::runtime_error("Logisim Hex Digit Display 的输入固定4位：" + p.id);
        if (k == "SevenSegment") d.attrs["active"] = parameter(p, "activeLow", 0) ? "false" : "true";
        if (k == "LEDMatrix") { d.attrs["inputtype"] = "select"; d.attrs["matrixrows"] = std::to_string(parameter(p, "rows", 8)); d.attrs["matrixcols"] = std::to_string(parameter(p, "cols", 8)); }
        if (k != "Joystick") d.attrs.erase("width");
    } else if (k == "Text") {
        d.name = "Text"; d.library = "#Base"; d.attrs.clear(); d.attrs["text"] = p.label;
    } else throw std::runtime_error("此器件尚无经过语义核对的 Logisim 2.7 等价实现，未导出：" + k + " / " + p.id);
    return d;
}

// 包装子电路在导出副本中生成，不修改调用者工程，也不改变当前仿真器。
struct Wrapper {
    Project &project; Circuit circuit; std::map<std::string, std::string> pins;
    Wrapper(Project &p, const Part &source, const std::string &suffix) : project(p) {
        circuit.name = "Practice_" + suffix + "_" + source.id;
        while (std::any_of(p.circuits.begin(), p.circuits.end(), [&](const Circuit &c) { return c.name == circuit.name; })) circuit.name += "_";
        auto unrotated = source; unrotated.rotation = 0;
        for (const auto &port : ports(p, unrotated)) {
            Part io; io.id = p.id("p"); io.kind = port.output ? "Output" : "Input"; io.label = port.id; io.width = port.width;
            io.at = {port.output ? 600.0 : 100.0, 100.0 + 80.0 * pins.size()};
            pins[port.id] = io.id; circuit.interfaceLayout[io.id] = port.at; circuit.parts.push_back(io);
        }
    }
    Part &add(std::string kind, int width, std::string label = "") {
        Part p; p.id = project.id("p"); p.kind = std::move(kind); p.width = width; p.label = std::move(label);
        p.at = {300, 100.0 + 80 * circuit.parts.size()}; circuit.parts.push_back(p); return circuit.parts.back();
    }
    void wire(Endpoint a, Endpoint b) { circuit.wires.push_back({project.id("w"), std::move(a), std::move(b), {}}); }
    Endpoint input(const std::string &name) const { return {pins.at(name), "Y"}; }
    Endpoint output(const std::string &name) const { return {pins.at(name), "A"}; }
};

// RAM 工程初值不是经典 .circ 属性。ROM 保存缺省值，位宽1的 RAM 标记每个地址是否写过，
// 输出选择“缺省 ROM / 已写 RAM”，实现初值与运行写入分离，无需隐藏私有格式或启动时钟。
Wrapper initialRam(Project &p, const Part &source) {
    Wrapper w(p, source, "InitializedRAM");
    auto &rom = w.add("ROM", source.width, "初始内容"); rom.data = source.data; rom.params["addressBits"] = parameter(source, "addressBits", 8); auto romId = rom.id;
    auto &ram = w.add("RAM", source.width, "运行写入"); ram.params["addressBits"] = parameter(source, "addressBits", 8); auto ramId = ram.id;
    auto &written = w.add("RAM", 1, "地址已写标记"); written.params["addressBits"] = parameter(source, "addressBits", 8); auto writtenId = written.id;
    auto &one = w.add("Constant", 1); one.value = 1; auto oneId = one.id;
    auto muxId = w.add("Mux", source.width).id;
    for (auto id : {romId, ramId, writtenId}) w.wire(w.input("ADDR"), {id, "ADDR"});
    for (auto id : {ramId, writtenId}) { w.wire(w.input("CLK"), {id, "CLK"}); w.wire(w.input("WE"), {id, "WE"}); }
    w.wire(w.input("D"), {ramId, "D"}); w.wire({oneId, "Y"}, {writtenId, "D"});
    w.wire({romId, "Q"}, {muxId, "A"}); w.wire({ramId, "Q"}, {muxId, "B"}); w.wire({writtenId, "Q"}, {muxId, "S"}); w.wire({muxId, "Y"}, w.output("Q"));
    return w;
}

// Logisim 同一支路总是按总线位号升序排列。逆序/任意顺序分组必须展开成逐位拆合网络。
Wrapper reorderedBits(Project &p, const Part &source) {
    Wrapper w(p, source, "BitMapping");
    bool split = source.kind == "Splitter";
    auto trunkId = w.add(split ? "Splitter" : "Joiner", source.width).id;
    w.wire(split ? w.input("A") : Endpoint{trunkId, "Y"}, split ? Endpoint{trunkId, "A"} : w.output("Y"));
    for (size_t g = 0; g < source.bitGroups.size(); ++g) {
        auto id = w.add(split ? "Joiner" : "Splitter", int(source.bitGroups[g].size())).id;
        auto branch = "B" + std::to_string(g);
        w.wire(split ? Endpoint{id, "Y"} : w.input(branch), split ? w.output(branch) : Endpoint{id, "A"});
        for (size_t bit = 0; bit < source.bitGroups[g].size(); ++bit) {
            Endpoint a{trunkId, "B" + std::to_string(source.bitGroups[g][bit])}, b{id, "B" + std::to_string(bit)};
            w.wire(split ? a : b, split ? b : a);
        }
    }
    return w;
}

Wrapper registerBits(Project &p, const Part &source) {
    if (source.value) throw std::runtime_error("Logisim 2.7 无法保存非零触发器/移位寄存器初态：" + source.id);
    Wrapper w(p, source, "RegisterBits");
    std::map<std::string, std::string> splitters;
    for (const auto &port : ports(p, source))
        if (!port.output && port.width > 1) {
            auto id = w.add("Splitter", port.width).id; splitters[port.id] = id; w.wire(w.input(port.id), {id, "A"});
        }
    auto joiner = w.add("Joiner", source.width).id; w.wire({joiner, "Y"}, w.output("Q"));
    std::string previous;
    for (int bit = 0; bit < source.width; ++bit) {
        auto &ff = w.add(source.kind == "ShiftRegister" ? "DFF" : source.kind, 1); ff.params["falling"] = parameter(source, "falling", 0); auto id = ff.id;
        w.wire(w.input("CLK"), {id, "CLK"}); w.wire(w.input("CLR"), {id, "CLR"}); w.wire({id, "Q"}, {joiner, "B" + std::to_string(bit)});
        if (source.kind == "ShiftRegister") w.wire(bit == 0 ? w.input("D") : Endpoint{previous, "Q"}, {id, "D"});
        else for (const auto &entry : splitters) w.wire({entry.second, "B" + std::to_string(bit)}, {id, entry.first});
        previous = id;
    }
    return w;
}
Wrapper parityBits(Project &p, const Part &source) {
    Wrapper w(p, source, "ParityBits"); auto split = w.add("Splitter", source.width).id; w.wire(w.input("A"), {split, "A"});
    auto &gate = w.add(source.kind == "EvenParity" ? "Xnor" : "Xor", 1); gate.params["inputs"] = std::max(2, source.width); gate.params["xorOne"] = 0; auto id = gate.id;
    for (int bit = 0; bit < source.width; ++bit) w.wire({split, "B" + std::to_string(bit)}, {id, bit == 0 ? "A" : bit == 1 ? "B" : "I" + std::to_string(bit)});
    if (source.width == 1) { auto &zero = w.add("Constant", 1); zero.value = 0; w.wire({zero.id, "Y"}, {id, "B"}); }
    w.wire({id, "Y"}, w.output("Y")); return w;
}
Wrapper activeLowControl(Project &p, const Part &source) {
    Wrapper w(p, source, "ActiveLow"); auto &gate = w.add(source.kind, source.width); gate.params["activeLow"] = 0; auto id = gate.id;
    auto inverse = w.add("Not", 1).id;
    w.wire(w.input("A"), {id, "A"}); w.wire(w.input("EN"), {inverse, "A"}); w.wire({inverse, "Y"}, {id, "EN"}); w.wire({id, "Y"}, w.output("Y")); return w;
}
Wrapper barrelShift(Project &p, const Part &source) {
    Wrapper w(p, source, "BarrelShift"); auto data = w.add("Splitter", source.width).id, amount = w.add("Splitter", source.width).id;
    w.wire(w.input("A"), {data, "A"}); w.wire(w.input("B"), {amount, "A"});
    auto &zero = w.add("Constant", 1); zero.value = 0; auto zeroId = zero.id;
    std::vector<Endpoint> bits;
    for (int bit = 0; bit < source.width; ++bit) bits.push_back({data, "B" + std::to_string(bit)});
    int stages = 0; while ((1 << stages) < source.width) ++stages;
    for (int stage = 0; stage < stages; ++stage) {
        std::vector<Endpoint> next;
        for (int bit = 0; bit < source.width; ++bit) {
            auto mux = w.add("Mux", 1).id; int from = bit + (source.kind == "ShiftRight" ? 1 : -1) * (1 << stage);
            w.wire(bits[bit], {mux, "A"}); w.wire(from >= 0 && from < source.width ? bits[from] : Endpoint{zeroId, "Y"}, {mux, "B"}); w.wire({amount, "B" + std::to_string(stage)}, {mux, "S"}); next.push_back({mux, "Y"});
        }
        bits = std::move(next);
    }
    auto join = w.add("Joiner", source.width).id;
    for (int bit = 0; bit < source.width; ++bit) w.wire(bits[bit], {join, "B" + std::to_string(bit)});
    Endpoint high{amount, "B" + std::to_string(stages)};
    if (source.width - stages > 1) {
        auto &gate = w.add("Or", 1); gate.params["inputs"] = source.width - stages; auto id = gate.id;
        for (int i = stages; i < source.width; ++i) w.wire({amount, "B" + std::to_string(i)}, {id, i == stages ? "A" : i == stages + 1 ? "B" : "I" + std::to_string(i - stages)});
        high = {id, "Y"};
    }
    auto mux = w.add("Mux", source.width).id; auto wordZero = w.add("Constant", source.width).id;
    w.wire({join, "Y"}, {mux, "A"}); w.wire({wordZero, "Y"}, {mux, "B"}); w.wire(high, {mux, "S"}); w.wire({mux, "Y"}, w.output("Y")); return w;
}
Project expanded(const Project &original) {
    Project result = original;
    for (size_t c = 0; c < result.circuits.size(); ++c) for (size_t i = 0; i < result.circuits[c].parts.size(); ++i) {
        const auto source = result.circuits[c].parts[i];
        bool ram = source.kind == "RAM" && std::any_of(source.data.begin(), source.data.end(), [](uint32_t value) { return value != 0; });
        bool reorder = (source.kind == "Splitter" || source.kind == "Joiner") && std::any_of(source.bitGroups.begin(), source.bitGroups.end(), [](const std::vector<int> &g) { return !std::is_sorted(g.begin(), g.end()); });
        bool registers = source.kind == "ShiftRegister" || ((source.kind == "DFF" || source.kind == "TFF" || source.kind == "JKFF" || source.kind == "SRFF") && source.width > 1);
        bool parity = source.kind == "EvenParity" || source.kind == "OddParity";
        bool control = (source.kind == "TriState" || source.kind == "ControlledInverter") && parameter(source, "activeLow", 0);
        bool shift = source.kind == "ShiftLeft" || source.kind == "ShiftRight";
        if (!ram && !reorder && !registers && !parity && !control && !shift) continue;
        auto wrapper = [&] { if (ram) return initialRam(result, source); if (reorder) return reorderedBits(result, source); if (registers) return registerBits(result, source); if (parity) return parityBits(result, source); if (control) return activeLowControl(result, source); return barrelShift(result, source); }();
        auto replacement = source; replacement.kind = "Subcircuit"; replacement.circuit = wrapper.circuit.name; replacement.params.clear(); replacement.data.clear(); replacement.bitGroups.clear();
        for (auto &wire : result.circuits[c].wires) {
            if (wire.a.object == source.id) wire.a.pin = wrapper.pins.at(wire.a.pin);
            if (wire.b.object == source.id) wire.b.pin = wrapper.pins.at(wire.b.pin);
        }
        result.circuits[c].parts[i] = replacement; result.circuits.push_back(std::move(wrapper.circuit));
    }
    result.validate(); return result;
}

// 局部并查集合并显式端点与同名节点。几何交叉不参与，也不跨不同子电路作用域合并。
class Networks {
    std::map<std::string, std::string> parent;
  public:
    std::string find(const std::string &key) { auto [it, inserted] = parent.emplace(key, key); (void)inserted; if (it->second != key) it->second = find(it->second); return it->second; }
    void unite(const std::string &a, const std::string &b) { parent[find(a)] = find(b); }
    explicit Networks(const Circuit &c) {
        for (const auto &w : c.wires) unite(w.a.key(), w.b.key());
        std::map<std::string, std::string> labels;
        for (const auto &n : c.nodes) if (!n.label.empty()) { auto [it, added] = labels.emplace(n.label, Endpoint{n.id, ""}.key()); if (!added) unite(it->second, Endpoint{n.id, ""}.key()); }
    }
};
void attribute(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const std::string &name, const std::string &value) {
    auto a = doc.NewElement("a"); a->SetAttribute("name", name.c_str());
    if (name == "contents") a->SetText(value.c_str()); else a->SetAttribute("val", value.c_str()); parent->InsertEndChild(a);
}
} // namespace

std::string exportCirc(const Project &original) {
    original.validate(); auto p = expanded(original);
    tinyxml2::XMLDocument doc; doc.InsertEndChild(doc.NewDeclaration());
    auto root = doc.NewElement("project"); root->SetAttribute("source", "2.7.1"); root->SetAttribute("version", "1.0"); doc.InsertEndChild(root);
    root->InsertEndChild(doc.NewComment("Standard Logisim 2.7.1 components and Tunnel networks. Layout is rebuilt. Pin interactive values reset according to Logisim project semantics."));
    const std::vector<std::string> libraries = {"#Wiring", "#Gates", "#Plexers", "#Arithmetic", "#Memory", "#I/O", "#Base"};
    std::map<std::string, int> libIds;
    for (size_t i = 0; i < libraries.size(); ++i) { libIds[libraries[i]] = int(i); auto lib = doc.NewElement("lib"); lib->SetAttribute("name", int(i)); lib->SetAttribute("desc", libraries[i].c_str()); root->InsertEndChild(lib); }
    auto main = doc.NewElement("main"); main->SetAttribute("name", p.main.c_str()); root->InsertEndChild(main);
    for (const auto &c : p.circuits) {
        auto circuit = doc.NewElement("circuit"); circuit->SetAttribute("name", c.name.c_str()); root->InsertEndChild(circuit);
        Networks nets(c); std::map<std::string, std::string> netNames;
        std::map<std::string, Point> locations;
        auto makeComponent = [&](const Device &d, Point pos) {
            auto comp = doc.NewElement("comp"); comp->SetAttribute("name", d.name.c_str());
            if (!d.library.empty()) comp->SetAttribute("lib", libIds.at(d.library));
            comp->SetAttribute("loc", xy(pos).c_str()); circuit->InsertEndChild(comp);
            for (const auto &a : d.attrs) attribute(doc, comp, a.first, a.second);
            return comp;
        };
        for (size_t index = 0; index < c.parts.size(); ++index) {
            const auto &part = c.parts[index];
            // 足够宽的网格使大规模选择器与相邻器件的真实端口不会意外重合。
            Point pos{400.0 + 4000.0 * (index % 8), 400.0 + 4000.0 * (index / 8)}; locations[part.id] = pos;
            std::map<std::string, Point> pinLocations;
            std::vector<Point> unused;
            if (part.kind == "Subcircuit") {
                Device d{part.circuit, "", {{"facing", facing(part.rotation)}, {"label", part.label}}}; makeComponent(d, pos);
                auto canonical = part; canonical.rotation = 0;
                for (const auto &port : ports(p, canonical)) pinLocations[port.id] = pos + turn(port.at, part.rotation);
            } else {
                auto d = device(part); makeComponent(d, pos);
                if (part.kind == "Text") continue;
                auto mapping = detail::circDevice(d.name, d.library, d.attrs);
                for (const auto &pin : mapping.pins) pinLocations[part.kind == "Joiner" && pin.second == "A" ? "Y" : pin.second] = pos + pin.first;
                for (auto pin : mapping.unsupportedPins) unused.push_back(pos + pin);
            }
            if (part.kind == "Counter" && parameter(part, "loadable", 0) && !parameter(part, "logisimCounter", 0)) {
                // 本地LOAD优先装载，而Logisim的LD同时控制减计数。将CT改为EN&&!LOAD，
                // 则LD=1时CT必为0，真实Logisim器件才会执行装载而不会误减计数。
                Point notAt = pos + Point{200, 200}, andAt = pos + Point{400, 200};
                makeComponent(Device{"NOT Gate", "#Gates", {{"facing", "east"}}}, notAt);
                makeComponent(Device{"AND Gate", "#Gates", {{"facing", "east"}, {"inputs", "2"}, {"size", "50"}}}, andAt);
                auto sourceKey = nets.find(Endpoint{part.id, "LOAD"}.key());
                auto [loadName, added] = netNames.emplace(sourceKey, "N" + std::to_string(netNames.size())); (void)added;
                auto tie = [&](Point point, const std::string &label) { makeComponent(Device{"Tunnel", "#Wiring", {{"label", label}, {"width", "1"}}}, point); };
                tie(notAt + Point{-30, 0}, loadName->second);
                tie(notAt, "Practice_NotLoad_" + part.id); tie(andAt + Point{-50, 20}, "Practice_NotLoad_" + part.id);
                tie(andAt, "Practice_CountEnable_" + part.id); tie(pos + Point{-30, 10}, "Practice_CountEnable_" + part.id);
                pinLocations["EN"] = andAt + Point{-50, -20};
            }
            for (const auto &port : ports(p, part)) {
                if (!pinLocations.count(port.id)) throw std::runtime_error("导出端口没有 Logisim 对应位置：" + part.kind + " / " + port.id);
                auto key = nets.find(Endpoint{part.id, port.id}.key());
                auto [it, inserted] = netNames.emplace(key, "N" + std::to_string(netNames.size())); (void)inserted;
                Device tunnel{"Tunnel", "#Wiring", {{"label", it->second}, {"width", std::to_string(port.width)}, {"facing", "east"}}};
                makeComponent(tunnel, pinLocations.at(port.id));
            }
            // 本地没有的使能端必须显式绑常量，不能依赖 Logisim 对悬空脚的特殊求值。
            // 寄存器EN/FF使能/存储器CS及OE绑1；进位/预置/清零等绑0；未使用输出保持悬空。
            for (size_t u = 0; u < unused.size(); ++u) {
                auto offset = unused[u] - pos;
                bool output = (part.kind == "DFF" || part.kind == "TFF" || part.kind == "JKFF" || part.kind == "SRFF") && offset.x == 0;
                output |= (part.kind == "Multiply" || part.kind == "Divide") && offset.y > 0;
                output |= part.kind == "Encoder" && u == 1;
                if (output) continue;
                bool high = part.kind == "Register" || (part.kind == "Encoder" && u == 0) ||
                            ((part.kind == "DFF" || part.kind == "TFF" || part.kind == "JKFF" || part.kind == "SRFF") && offset.x == -20) ||
                            ((part.kind == "RAM" || part.kind == "ROM") && (offset.x == -90 || offset.x == -50));
                Device constant{"Constant", "#Wiring", {{"value", hex(high ? 1 : 0)}, {"width", (part.kind == "Multiply" || part.kind == "Divide") ? std::to_string(part.width) : "1"}}};
                makeComponent(constant, unused[u]);
            }
            if (part.kind == "Counter" && !parameter(part, "loadable", 0)) {
                // 默认 Counter 无D/LOAD，经典器件固定具有两脚，显式关闭装载。
                makeComponent(Device{"Constant", "#Wiring", {{"value", "0x0"}, {"width", "1"}}}, pos + Point{-30, -10});
                makeComponent(Device{"Constant", "#Wiring", {{"value", "0x0"}, {"width", std::to_string(part.width)}}}, pos + Point{-30, 0});
            }
        }
        // 每个定义导出自定义外观，端子通过内部 Pin 的真实位置关联，支持任意层数与稳定接口。
        auto appearance = doc.NewElement("appear"); circuit->InsertEndChild(appearance);
        Part instance; instance.kind = "Subcircuit"; instance.circuit = c.name;
        auto interface = ports(p, instance);
        Point anchor{400, 400};
        auto rectangle = doc.NewElement("rect"); rectangle->SetAttribute("x", 340); rectangle->SetAttribute("y", 360); rectangle->SetAttribute("width", 120); rectangle->SetAttribute("height", std::max(80, int(partHeight(p, instance)))); rectangle->SetAttribute("fill", "none"); rectangle->SetAttribute("stroke", "#000000"); appearance->InsertEndChild(rectangle);
        for (const auto &port : interface) {
            auto a = doc.NewElement("circ-port"); auto position = anchor + port.at;
            a->SetAttribute("x", int(std::lround(position.x)) - 4); a->SetAttribute("y", int(std::lround(position.y)) - 4); a->SetAttribute("width", 8); a->SetAttribute("height", 8);
            auto internal = locations.at(port.id); a->SetAttribute("pin", (std::to_string(int(internal.x)) + "," + std::to_string(int(internal.y))).c_str()); appearance->InsertEndChild(a);
        }
        auto a = doc.NewElement("circ-anchor"); a->SetAttribute("x", 394); a->SetAttribute("y", 394); a->SetAttribute("width", 12); a->SetAttribute("height", 12); a->SetAttribute("facing", "east"); appearance->InsertEndChild(a);
    }
    tinyxml2::XMLPrinter printer; doc.Print(&printer); return printer.CStr();
}

std::vector<std::string> circExportWarnings(const Project &p) {
    std::vector<std::string> result{"Logisim 2.7.1 与本程序符号尺寸不同，导出采用网格布局与真实 Tunnel 网络；电气连接及层次保留。"};
    bool input = false, phase = false, ram = false, artwork = false;
    for (const auto &c : p.circuits) {
        artwork |= !c.shapes.empty();
        for (const auto &part : c.parts) {
            input |= part.kind == "Input" && part.value != 0;
            phase |= part.kind == "Clock" && parameter(part, "phase", 0) != 0;
            ram |= part.kind == "RAM" && std::any_of(part.data.begin(), part.data.end(), [](uint32_t value) { return value != 0; });
        }
    }
    if (input) result.push_back("Logisim 工程不保存输入脚的交互值，打开后输入脚复位为0；请在两端分别设置相同测试输入。");
    if (phase) result.push_back("经典 Logisim 工程没有时钟初始相位字段，导出时钟相位从0开始。");
    if (ram) result.push_back("非零RAM初值使用公开元件构成的ROM/RAM/已写标记/MUX子电路保存，返回导入后显示为普通子电路。");
    if (artwork) result.push_back("导出的自定义外观保留接口布局并使用标准矩形符号；装饰性图形不影响电气连接。");
    return result;
}
} // namespace eda::logic
