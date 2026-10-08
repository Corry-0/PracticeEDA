#include "CircExchange.h"
#include <iostream>
#include <sstream>
using namespace eda;
using namespace eda::logic;
namespace {
int checks = 0;
void check(bool value, const std::string &message) { ++checks; if (!value) throw std::runtime_error(message); }
Part &add(Project &p, Circuit &c, const std::string &kind, int width, const std::string &label = "") {
    Part q; q.id = p.id("p"); q.kind = kind; q.width = width; q.label = label; q.at = {100.0 + 180 * c.parts.size(), 100};
    c.parts.push_back(q); return c.parts.back();
}
void wire(Project &p, Circuit &c, Endpoint a, Endpoint b) { c.wires.push_back({p.id("w"), a, b, {}}); }
const Part &label(const Circuit &c, const std::string &name) { for (const auto &q : c.parts) if (q.label == name) return q; throw std::runtime_error("missing " + name); }
std::string constant(Project &p, Circuit &c, int width, uint32_t value) { auto &q = add(p, c, "Constant", width); q.value = value; return q.id; }
std::string output(Project &p, Circuit &c, Endpoint source, int width, const std::string &name) { auto id = add(p, c, "Output", width, name).id; wire(p, c, source, {id, "A"}); return id; }
void halt(Project &p, Circuit &c) { output(p, c, {constant(p, c, 1, 1), "Y"}, 1, "halt"); }
uint32_t result(const Project &p) { Simulator s(p, p.main); s.settle(); auto value = s.read({label(p.circuit(p.main), "result").id, "A"}); check(value.defined(), "result defined: " + value.text()); return value.value; }
void exchange(const Project &p, uint32_t expected, const std::string &name, const std::filesystem::path &folder) {
    auto before = serialize(p), xml = exportCirc(p);
    if (!folder.empty()) writeAtomic(folder / (name + ".circ"), xml);
    check(serialize(p) == before, "export is read-only");
    check(xml.find("PracticeEDA.logic") == std::string::npos && xml.find("<comp") != std::string::npos, "real XML components rather than native JSON");
    auto imported = importCirc(xml); imported.validate();
    if (!folder.empty()) writeAtomic(folder / (name + ".imported.logic.json"), serialize(imported));
    check(result(p) == expected && result(imported) == expected, name + " native and standard XML agree");
    for (const auto &c : imported.circuits)
        check(circDiagnostics(c).size() == 1, name + " no unexpected unsupported diagnostics");
    if (!folder.empty()) { writeAtomic(folder / (name + ".circ"), xml); writeAtomic(folder / (name + ".expected.txt"), std::to_string(expected)); }
}
void staticTests(const std::filesystem::path &folder) {
    for (int rotation = 0; rotation < 4; ++rotation) {
        Project p; auto &c = p.circuits[0];
        auto &g = add(p, c, "And", 8); g.rotation = rotation; g.params["inputs"] = 3; g.params["invertMask"] = 2; auto gate = g.id;
        wire(p, c, {constant(p, c, 8, 0xff), "Y"}, {gate, "A"}); wire(p, c, {constant(p, c, 8, 0x0f), "Y"}, {gate, "B"}); wire(p, c, {constant(p, c, 8, 0xaa), "Y"}, {gate, "I2"});
        output(p, c, {gate, "Y"}, 8, "result"); halt(p, c);
        exchange(p, 0xa0, "gate-rotation-" + std::to_string(rotation), folder);
    }
    for (auto kind : {"Mux", "Decoder", "Encoder"}) {
        Project p; auto &c = p.circuits[0]; auto &g = add(p, c, kind, 1); g.params["selectBits"] = 3; auto id = g.id;
        std::string k = kind;
        if (k == "Mux") {
            for (int i = 0; i < 8; ++i) wire(p, c, {constant(p, c, 1, i == 5), "Y"}, {id, i == 0 ? "A" : i == 1 ? "B" : "I" + std::to_string(i)});
            wire(p, c, {constant(p, c, 3, 5), "Y"}, {id, "S"}); output(p, c, {id, "Y"}, 1, "result");
        } else if (k == "Decoder") {
            wire(p, c, {constant(p, c, 3, 5), "Y"}, {id, "A"}); output(p, c, {id, "Y5"}, 1, "result");
        } else {
            for (int i = 0; i < 8; ++i) wire(p, c, {constant(p, c, 1, i == 5), "Y"}, {id, "A" + std::to_string(i)});
            output(p, c, {id, "Y"}, 3, "result");
        }
        halt(p, c); exchange(p, k == "Encoder" ? 5 : 1, "select-" + k, folder);
    }
    for (auto kind : {"Add", "Subtract", "Multiply", "Divide", "Compare", "BitAdder", "BitFinder"}) {
        Project p; auto &c = p.circuits[0]; auto id = add(p, c, kind, 8).id; std::string k = kind;
        wire(p, c, {constant(p, c, 8, 12), "Y"}, {id, "A"});
        if (k != "BitAdder" && k != "BitFinder") wire(p, c, {constant(p, c, 8, 3), "Y"}, {id, "B"});
        int width = k == "Compare" ? 1 : k == "BitAdder" ? 4 : k == "BitFinder" ? 3 : 8;
        output(p, c, {id, k == "Compare" ? "GT" : "Y"}, width, "result"); halt(p, c);
        uint32_t expected = k == "Add" ? 15 : k == "Subtract" ? 9 : k == "Multiply" ? 36 : k == "Divide" ? 4 : k == "Compare" ? 1 : 2;
        exchange(p, expected, "arithmetic-" + k, folder);
    }
    for (bool split : {true, false}) {
        Project p; auto &c = p.circuits[0]; auto &q = add(p, c, split ? "Splitter" : "Joiner", 8); q.bitGroups = {{7, 0, 5, 2}, {6, 1, 4, 3}}; auto id = q.id;
        if (split) { wire(p, c, {constant(p, c, 8, 0xa5), "Y"}, {id, "A"}); output(p, c, {id, "B0"}, 4, "result"); }
        else { wire(p, c, {constant(p, c, 4, 15), "Y"}, {id, "B0"}); wire(p, c, {constant(p, c, 4, 0), "Y"}, {id, "B1"}); output(p, c, {id, "Y"}, 8, "result"); }
        halt(p, c); exchange(p, split ? 15 : 0xa5, split ? "reverse-split" : "reverse-join", folder);
    }
    for (auto kind : {"ROM", "RAM"}) {
        Project p; auto &c = p.circuits[0]; auto &q = add(p, c, kind, 8); q.params["addressBits"] = 4; q.data = {0, 0, 0, 0xa5}; auto id = q.id;
        wire(p, c, {constant(p, c, 4, 3), "Y"}, {id, "ADDR"});
        if (std::string(kind) == "RAM") { wire(p, c, {constant(p, c, 8, 0x5a), "Y"}, {id, "D"}); wire(p, c, {constant(p, c, 1, 0), "Y"}, {id, "WE"}); wire(p, c, {constant(p, c, 1, 0), "Y"}, {id, "CLK"}); }
        output(p, c, {id, "Q"}, 8, "result"); halt(p, c); exchange(p, 0xa5, "memory-" + std::string(kind), folder);
    }
    for (auto kind : {"ShiftLeft", "ShiftRight", "EvenParity", "OddParity", "BitSelector", "ControlledInverter"}) {
        Project p; auto &c = p.circuits[0]; auto &q = add(p, c, kind, 8); auto id = q.id; std::string k = kind;
        if (k == "BitSelector") q.params["outputWidth"] = 2;
        if (k == "ControlledInverter") q.params["activeLow"] = 1;
        wire(p, c, {constant(p, c, 8, 0xa5), "Y"}, {id, "A"});
        if (k == "ShiftLeft" || k == "ShiftRight") wire(p, c, {constant(p, c, 8, 2), "Y"}, {id, "B"});
        if (k == "BitSelector") wire(p, c, {constant(p, c, 2, 1), "Y"}, {id, "S"});
        if (k == "ControlledInverter") wire(p, c, {constant(p, c, 1, 0), "Y"}, {id, "EN"});
        output(p, c, {id, "Y"}, k == "EvenParity" || k == "OddParity" ? 1 : k == "BitSelector" ? 2 : 8, "result"); halt(p, c);
        uint32_t expected = k == "ShiftLeft" ? 0x94 : k == "ShiftRight" ? 0x29 : k == "EvenParity" ? 1 : k == "OddParity" ? 0 : k == "BitSelector" ? 1 : 0x5a;
        exchange(p, expected, "extra-" + k, folder);
    }
}
void hierarchyTest(const std::filesystem::path &folder) {
    Project p;
    createComponent(p, "底层非门", {{"输入", false, 8}, {"输出", true, 8}});
    auto &leaf = p.circuit("底层非门"); auto in = leaf.parts[0].id, out = leaf.parts[1].id; auto gate = add(p, leaf, "Not", 8).id;
    wire(p, leaf, {in, "Y"}, {gate, "A"}); wire(p, leaf, {gate, "Y"}, {out, "A"});
    leaf.interfaceLayout[in] = {0, -80}; leaf.interfaceLayout[out] = {0, 80};
    createComponent(p, "中层", {{"输入", false, 8}, {"输出", true, 8}});
    auto &middle = p.circuit("中层"); auto mi = middle.parts[0].id, mo = middle.parts[1].id;
    auto &child = add(p, middle, "Subcircuit", 1); child.circuit = "底层非门"; child.rotation = 1; auto childId = child.id;
    wire(p, middle, {mi, "Y"}, {childId, in}); wire(p, middle, {childId, out}, {mo, "A"});
    auto &main = p.circuit("main"); auto &top = add(p, main, "Subcircuit", 1); top.circuit = "中层"; top.rotation = 3; auto topId = top.id;
    wire(p, main, {constant(p, main, 8, 0xa5), "Y"}, {topId, mi}); output(p, main, {topId, mo}, 8, "result"); halt(p, main);
    exchange(p, 0x5a, "three-level-custom-interface", folder);
    auto packaged = exportComponent(p, "中层"); Project imported; auto name = importComponent(imported, packaged);
    check(!imported.circuit(name).interfaceLayout.empty() || !imported.circuit("底层非门").interfaceLayout.empty(), "component interface layout survives ID remapping");
    imported.validate();
}
void counterTest(const std::filesystem::path &folder) {
    Project p; auto &c = p.circuits[0]; auto clock = add(p, c, "Clock", 1).id;
    auto &q = add(p, c, "Counter", 4); q.params["counterMax"] = 3; auto id = q.id;
    wire(p, c, {clock, "Y"}, {id, "CLK"}); wire(p, c, {constant(p, c, 1, 1), "Y"}, {id, "EN"}); wire(p, c, {constant(p, c, 1, 0), "Y"}, {id, "CLR"});
    output(p, c, {id, "Q"}, 4, "result"); output(p, c, {id, "CARRY"}, 1, "halt");
    auto imported = importCirc(exportCirc(p)); Simulator original(p, "main"), other(imported, "main"); original.settle(); other.settle();
    for (int i = 0; i < 8; ++i) { auto a = original.read({label(p.circuit("main"), "result").id, "A"}); auto b = other.read({label(imported.circuit("main"), "result").id, "A"}); check(a == b, "counter edge sequence preserved"); original.tick(); other.tick(); }
    if (!folder.empty()) { writeAtomic(folder / "counter-sequence.circ", exportCirc(p)); writeAtomic(folder / "counter-sequence.expected.txt", "0\n1\n2\n3"); }
}
void sequenceTests(const std::filesystem::path &folder) {
    for(const auto &kind:std::vector<std::string>{"Register","DFF","TFF","JKFF","SRFF","ShiftRegister","RAM"}) {
        Project p;auto &c=p.circuits[0];auto clock=add(p,c,"Clock",1).id;
        auto &counter=add(p,c,"Counter",4);counter.params["counterMax"]=3;auto count=counter.id;
        auto zero=constant(p,c,1,0),one=constant(p,c,1,1);
        wire(p,c,{clock,"Y"},{count,"CLK"});wire(p,c,{one,"Y"},{count,"EN"});wire(p,c,{zero,"Y"},{count,"CLR"});
        int width=kind=="TFF"||kind=="JKFF"||kind=="SRFF"?1:kind=="ShiftRegister"?4:8;
        auto &part=add(p,c,kind,width);auto id=part.id;
        if(kind=="RAM"){part.params["addressBits"]=4;part.data={0xa5};}
        wire(p,c,{clock,"Y"},{id,"CLK"});
        if(kind=="RAM") {
            wire(p,c,{constant(p,c,4,0),"Y"},{id,"ADDR"});wire(p,c,{one,"Y"},{id,"WE"});wire(p,c,{constant(p,c,8,0x5a),"Y"},{id,"D"});
        }else {
            wire(p,c,{zero,"Y"},{id,"CLR"});
            if(kind=="TFF")wire(p,c,{one,"Y"},{id,"T"});
            else if(kind=="JKFF"||kind=="SRFF") {wire(p,c,{one,"Y"},{id,kind=="JKFF"?"J":"S"});wire(p,c,{zero,"Y"},{id,kind=="JKFF"?"K":"R"});}
            else wire(p,c,{constant(p,c,kind=="ShiftRegister"?1:width,kind=="ShiftRegister"?1:0xa5),"Y"},{id,"D"});
        }
        output(p,c,{id,"Q"},width,"result");output(p,c,{count,"CARRY"},1,"halt");
        auto xml=exportCirc(p);auto imported=importCirc(xml);Simulator a(p,"main"),b(imported,"main");a.settle();b.settle();
        for(int tick=0;tick<6;++tick){check(a.read({label(c,"result").id,"A"})==b.read({label(imported.circuit("main"),"result").id,"A"}),kind+" sequential round trip");a.tick();b.tick();}
        if(!folder.empty()) {
            writeAtomic(folder/("sequence-"+kind+".circ"),xml);
            writeAtomic(folder/("sequence-"+kind+".expected.txt"),kind=="RAM"?"165\n90":kind=="ShiftRegister"?"0\n1\n3\n7":kind=="TFF"?"0\n1\n0\n1":kind=="Register"||kind=="DFF"?"0\n165":"0\n1");
        }
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        std::filesystem::path folder = argc > 1 ? std::filesystem::u8path(argv[1]) : std::filesystem::path{};
        if (!folder.empty()) std::filesystem::create_directories(folder);
        staticTests(folder); hierarchyTest(folder); counterTest(folder); sequenceTests(folder);
        std::cout << "circ exchange: " << checks << " checks passed\n"; return 0;
    } catch (const std::exception &error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
