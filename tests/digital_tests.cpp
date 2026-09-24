// Baseline regression suite: logic truth tables, event timing, storage, hierarchy and file rejection.
#include "Digital.h"
#include <iostream>
using namespace eda;
using namespace eda::logic;
namespace {
int checks = 0;
void check(bool b, const std::string &why) {
    ++checks;
    if (!b)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn, const std::string &why) {
    bool caught = false;
    try {
        fn();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught, why);
}
struct Fixture {
    Project p;
    std::string add(std::string kind, int width = 1, uint32_t value = 0, std::string circuit = "main") {
        Part q;
        q.id = p.id("p");
        q.kind = kind;
        q.width = width;
        q.value = value;
        q.label = q.id;
        p.circuit(circuit).parts.push_back(q);
        return q.id;
    }
    void wire(std::string a, std::string ap, std::string b, std::string bp, std::string c = "main") {
        p.circuit(c).wires.push_back({p.id("w"), {a, ap}, {b, bp}, {}});
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
        check(library().size() >= 40, "component catalog");
        auto demo = halfAdder();
        demo.validate();
        auto table = truthTable(demo, "main");
        check(table.rows ==
                  std::vector<std::vector<int>>{{0, 0, 0, 0}, {0, 1, 1, 0}, {1, 0, 1, 0}, {1, 1, 0, 1}},
              "half adder exhaustive truth table");
        check(table.report().find("Sum = (!A & B) | (A & !B)") != std::string::npos, "SOP expression");
        check(eda::logic::serialize(eda::logic::deserialize(eda::logic::serialize(demo))) ==
                  eda::logic::serialize(demo),
              "roundtrip");
        auto exp = expressionTable("A & !B | !A & B");
        check(exp.rows[3].back() == 0 && exp.rows[2].back() == 1, "expression parser precedence");
        check(expressionTable("!(A | B) ^ C").rows.size() == 8, "nested expressions");
        check(expressionTable("1").rows == std::vector<std::vector<int>>{{1}}, "constant expression");
        rejects([] { expressionTable("A && B"); }, "invalid expression");
        rejects([] { expressionTable("(A"); }, "missing parenthesis");
        for (auto k : {"And", "Or", "Nand", "Nor", "Xor", "Xnor"}) {
            Fixture f;
            auto a = f.add("Input"), b = f.add("Input"), g = f.add(k), o = f.add("Output");
            f.wire(a, "Y", g, "A");
            f.wire(b, "Y", g, "B");
            f.wire(g, "Y", o, "A");
            Simulator s(f.p, "main");
            for (int x = 0; x < 2; ++x)
                for (int y = 0; y < 2; ++y) {
                    s.setInput(a, x);
                    s.setInput(b, y);
                    s.settle();
                    bool expected = std::string(k) == "And"    ? (x && y)
                                    : std::string(k) == "Or"   ? (x || y)
                                    : std::string(k) == "Nand" ? !(x && y)
                                    : std::string(k) == "Nor"  ? !(x || y)
                                    : std::string(k) == "Xor"  ? (x != y)
                                                               : (x == y);
                    auto v = s.read({o, "A"});
                    check(v.defined() && v.value == uint32_t(expected), std::string(k) + " truth table");
                }
        }
        {
            Fixture f;
            auto a = f.add("Input", 32, UINT32_MAX), b = f.add("Input", 32, 1), g = f.add("Add", 32);
            f.wire(a, "Y", g, "A");
            f.wire(b, "Y", g, "B");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({g, "Y"}).value == 0 && s.read({g, "C"}).value == 1, "32-bit carry no overflow UB");
        }
        {
            Fixture f;
            auto a = f.add("Input", 8, 0xa5), split = f.add("Splitter", 8), join = f.add("Joiner", 8);
            f.wire(a, "Y", split, "A");
            for (int i = 0; i < 8; ++i)
                f.wire(split, "B" + std::to_string(i), join, "B" + std::to_string(i));
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({join, "Y"}).defined() && s.read({join, "Y"}).value == 0xa5, "split/join bus");
        }
        {
            Fixture f;
            auto a = f.add("Input", 8), o = f.add("Output", 4);
            f.wire(a, "Y", o, "A");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({o, "A"}).error != 0, "width mismatch");
            check(!s.issues().empty(), "width diagnostic");
        }
        {
            Fixture f;
            auto o = f.add("Output");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({o, "A"}).text() == "Z" && !s.issues().empty(), "floating input");
        }
        {
            Fixture f;
            auto a = f.add("Input", 1, 0), b = f.add("Input", 1, 1), o = f.add("Output");
            f.wire(a, "Y", o, "A");
            f.wire(b, "Y", o, "A");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({o, "A"}).text() == "E", "multiple driver conflict");
            s.setInput(b, 0);
            s.settle();
            check(s.read({o, "A"}).text() == "0", "conflict recovery");
        }
        {
            Fixture f;
            auto data = f.add("Input", 8, 42), en = f.add("Input"), tri = f.add("TriState", 8);
            f.wire(data, "Y", tri, "A");
            f.wire(en, "Y", tri, "EN");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({tri, "Y"}).z == 255, "tri state disabled");
            s.setInput(en, 1);
            s.settle();
            check(s.read({tri, "Y"}).value == 42, "tri state enabled");
        }
        for (auto k : {"DFF", "Register", "TFF", "JKFF", "SRFF", "Counter", "ShiftRegister"}) {
            Fixture f;
            auto clk = f.add("Input"), data = f.add("Input", 1, 1), zero = f.add("Constant"), reg = f.add(k);
            auto pin = std::string(k) == "TFF"       ? "T"
                       : std::string(k) == "JKFF"    ? "J"
                       : std::string(k) == "SRFF"    ? "S"
                       : std::string(k) == "Counter" ? "EN"
                                                     : "D";
            f.wire(data, "Y", reg, pin);
            f.wire(clk, "Y", reg, "CLK");
            if (std::string(k) == "JKFF")
                f.wire(zero, "Y", reg, "K");
            if (std::string(k) == "SRFF")
                f.wire(zero, "Y", reg, "R");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({reg, "Q"}).value == 0, std::string(k) + " holds before edge");
            s.setInput(clk, 1);
            s.settle();
            check(s.read({reg, "Q"}).value == 1, std::string(k) + " rising edge");
            s.setInput(data, 0);
            s.settle();
            check(s.read({reg, "Q"}).value == 1, std::string(k) + " holds while high");
            s.setInput(clk, 0);
            s.settle();
            check(s.read({reg, "Q"}).value == 1, std::string(k) + " ignores falling edge");
            rejects([&] { truthTable(f.p, "main"); }, "reject sequential truth table");
        }
        {
            Fixture f;
            auto clk = f.add("Clock"), data = f.add("Constant", 1, 1), reg = f.add("DFF");
            f.wire(clk, "Y", reg, "CLK");
            f.wire(data, "Y", reg, "D");
            Simulator s(f.p, "main");
            s.settle();
            s.tick();
            check(s.read({reg, "Q"}).value == 1, "clock tick");
            s.reset();
            s.settle();
            check(s.read({reg, "Q"}).value == 0, "reset state");
        }
        {
            Fixture f;
            auto addr = f.add("Input", 8, 12), data = f.add("Input", 8, 73), we = f.add("Input", 1, 1),
                 clk = f.add("Input"), ram = f.add("RAM", 8);
            f.wire(addr, "Y", ram, "ADDR");
            f.wire(data, "Y", ram, "D");
            f.wire(we, "Y", ram, "WE");
            f.wire(clk, "Y", ram, "CLK");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({ram, "Q"}).value == 0, "RAM zero init");
            s.setInput(clk, 1);
            s.settle();
            check(s.read({ram, "Q"}).value == 73, "RAM write edge/read");
            s.setInput(addr, 13);
            s.settle();
            check(s.read({ram, "Q"}).value == 0, "RAM address isolation");
            s.setInput(addr, 12);
            s.settle();
            check(s.read({ram, "Q"}).value == 73, "RAM retains data");
        }
        {
            Fixture f;
            auto addr = f.add("Input", 8, 1), rom = f.add("ROM", 8);
            f.p.circuit("main").part(rom)->data = {3, 77};
            f.wire(addr, "Y", rom, "ADDR");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({rom, "Q"}).value == 77, "ROM initial data");
        }
        {
            Fixture f;
            auto a = f.add("Input", 8, 23), b = f.add("Input", 8, 3), sel = f.add("Input"),
                 mux = f.add("Mux", 8);
            f.wire(a, "Y", mux, "A");
            f.wire(b, "Y", mux, "B");
            f.wire(sel, "Y", mux, "S");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({mux, "Y"}).value == 23, "mux A");
            s.setInput(sel, 1);
            s.settle();
            check(s.read({mux, "Y"}).value == 3, "mux B");
        }
        {
            Fixture f;
            auto a = f.add("Input", 8, 23), b = f.add("Input", 8, 0), div = f.add("Divide", 8);
            f.wire(a, "Y", div, "A");
            f.wire(b, "Y", div, "B");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({div, "Y"}).error == 255, "divide zero error");
        }
        {
            Fixture f;
            auto a = f.add("Input", 1, 1), o = f.add("Output");
            auto j = f.p.id("j"), k = f.p.id("j");
            auto &c = f.p.circuit("main");
            c.nodes.push_back({j, "BUS", {}});
            c.nodes.push_back({k, "BUS", {}});
            f.wire(a, "Y", j, "");
            f.wire(k, "", o, "A");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({o, "A"}).value == 1, "same-name tunnels");
        }
        // Hierarchical register instances must keep independent state, including after serialization.
        {
            Fixture f;
            Circuit child;
            child.name = "reg";
            f.p.circuits.push_back(child);
            auto d = f.add("Input", 1, 0, "reg"), clk = f.add("Input", 1, 0, "reg"),
                 q = f.add("Output", 1, 0, "reg"), reg = f.add("DFF", 1, 0, "reg");
            f.wire(d, "Y", reg, "D", "reg");
            f.wire(clk, "Y", reg, "CLK", "reg");
            f.wire(reg, "Q", q, "A", "reg");
            auto a = f.add("Input", 1, 1), b = f.add("Input"), ck = f.add("Input"), one = f.add("Subcircuit"),
                 two = f.add("Subcircuit");
            f.p.circuit("main").part(one)->circuit = "reg";
            f.p.circuit("main").part(two)->circuit = "reg";
            f.wire(a, "Y", one, d);
            f.wire(b, "Y", two, d);
            f.wire(ck, "Y", one, clk);
            f.wire(ck, "Y", two, clk);
            auto project = eda::logic::deserialize(eda::logic::serialize(f.p));
            Simulator s(project, "main");
            s.settle();
            s.setInput(ck, 1);
            s.settle();
            check(s.read({one, q}).value == 1 && s.read({two, q}).value == 0,
                  "independent hierarchical state");
            auto clip = copy(f.p, f.p.circuit("main"), {one, a, ck});
            auto pasted = paste(f.p, f.p.circuit("main"), clip, {30, 30});
            check(!pasted.empty(), "cross-project hierarchy clipboard");
            f.p.validate();
            Simulator pastedSim(f.p, "main");
            pastedSim.settle();
            check(true, "pasted subcircuit port mapping valid");
            f.p.circuit("reg").parts.push_back(*f.p.circuit("main").part(one));
            f.p.circuit("reg").parts.back().id = f.p.id("p");
            rejects([&] { f.p.validate(); }, "recursive hierarchy rejected");
        }
        {
            auto bad = demo;
            bad.nextId = 1;
            rejects([&] { bad.validate(); }, "stale id rejected");
            bad = demo;
            bad.circuits[0].wires[0].a.pin = "bad";
            rejects([&] { bad.validate(); }, "dangling endpoint rejected");
            bad = demo;
            bad.circuits[0].parts[0].width = 33;
            rejects([&] { bad.validate(); }, "invalid width rejected");
            rejects([] { eda::logic::deserialize("{}"); }, "malformed project rejected");
        }
        {
            eda::logic::History h;
            h.reset(demo);
            auto before = h.project;
            erase(h.project.circuits[0], {h.project.circuits[0].parts[0].id});
            h.commit(before);
            check(h.dirty() && h.undo() && !h.dirty(), "delete undo dirty state");
            check(h.redo(), "redo");
            auto fragment =
                copy(demo, demo.circuits[0], {demo.circuits[0].parts[0].id, demo.circuits[0].parts[2].id});
            Project target;
            auto selected = paste(target, target.circuits[0], fragment, {40, 40});
            check(target.circuits[0].parts.size() == 2 && target.circuits[0].wires.size() == 1 &&
                      selected.size() == 3,
                  "clipboard excludes external wires");
        }
        {
            Fixture f;
            auto a = f.add("Input", 1, 1), g = f.add("Not");
            f.wire(a, "Y", g, "A");
            Simulator s(f.p, "main");
            check(s.time() == 0 && s.pending() > 0, "initial queue");
            check(s.step() && s.time() == 1, "single event batch");
            check(s.settle() && s.pending() == 0 && s.read({g, "Y"}).value == 0, "settle queue");
            check(!s.trace().empty(), "trace events");
        }
        {
            Fixture f;
            auto enable = f.add("Input"), gate = f.add("Nand");
            f.wire(enable, "Y", gate, "B");
            f.wire(gate, "Y", gate, "A");
            Simulator s(f.p, "main");
            check(s.settle() && s.read({gate, "Y"}).value == 1, "feedback converges with controlling input");
            s.setInput(enable, 1);
            check(!s.settle(30), "oscillation stops at event budget");
            auto issues = s.issues();
            check(std::any_of(issues.begin(), issues.end(),
                              [](auto &i) { return i.message.find("事件预算") != std::string::npos; }),
                  "oscillation diagnostic");
            s.reset();
            check(s.settle(), "reset recovers oscillation");
        }
        for (auto test : std::vector<std::pair<std::string, uint32_t>>{
                 {"Subtract", 249}, {"Multiply", 30}, {"Divide", 0}, {"ShiftLeft", 0}, {"ShiftRight", 0}}) {
            Fixture f;
            auto a = f.add("Input", 8, 3), b = f.add("Input", 8, 10), g = f.add(test.first, 8);
            f.wire(a, "Y", g, "A");
            f.wire(b, "Y", g, "B");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({g, "Y"}).defined() && s.read({g, "Y"}).value == test.second,
                  test.first + " arithmetic result");
            if (test.first == "Subtract")
                check(s.read({g, "C"}).value == 1, "subtractor borrow");
        }
        {
            Fixture f;
            auto a = f.add("Input", 32, UINT32_MAX), b = f.add("Input", 32, 1), g = f.add("Compare", 32);
            f.wire(a, "Y", g, "A");
            f.wire(b, "Y", g, "B");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({g, "GT"}).value == 1 && s.read({g, "LT"}).value == 0 &&
                      s.read({g, "EQ"}).value == 0,
                  "unsigned 32-bit comparator");
        }
        {
            Fixture f;
            auto a = f.add("Input", 8, 219), select = f.add("Input", 1, 1), d = f.add("Demux", 8);
            f.wire(a, "Y", d, "A");
            f.wire(select, "Y", d, "S");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({d, "Y0"}).value == 0 && s.read({d, "Y1"}).value == 219, "demultiplexer routing");
        }
        {
            Fixture f;
            auto a = f.add("Input", 2, 3), d = f.add("Decoder"), e = f.add("Encoder");
            f.wire(a, "Y", d, "A");
            for (int i = 0; i < 4; ++i)
                f.wire(d, "Y" + std::to_string(i), e, "A" + std::to_string(i));
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({d, "Y3"}).value == 1 && s.read({e, "Y"}).value == 3 && s.read({e, "V"}).value == 1,
                  "decoder/priority encoder");
        }
        {
            Fixture f;
            auto a = f.add("Input", 1, 1), e = f.add("Extender", 32);
            f.wire(a, "Y", e, "A");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({e, "Y"}).defined() && s.read({e, "Y"}).value == 1, "zero extension 1 to 32");
        }
        {
            Fixture f;
            auto a = f.add("Constant", 1, 0), g = f.add("And");
            f.wire(a, "Y", g, "A");
            Simulator s(f.p, "main");
            s.settle();
            check(s.read({g, "Y"}).defined() && s.read({g, "Y"}).value == 0,
                  "known zero controls AND despite floating input");
        }
        {
            auto fragment = copy(demo, demo.circuits[0], {demo.circuits[0].wires[0].id});
            Project target;
            paste(target, target.circuits[0], fragment, {10, 20});
            check(target.circuits[0].nodes.size() == 2 && target.circuits[0].wires.size() == 1 &&
                      target.circuits[0].parts.empty(),
                  "wire-only clipboard detaches endpoint nodes");
            Fixture f;
            auto a = f.add("Input", 1, 1), o = f.add("Output");
            auto j = f.p.id("j"), k = f.p.id("j");
            f.p.circuits[0].nodes = {{j, "BUS", {}}, {k, "BUS", {}}};
            f.wire(a, "Y", j, "");
            f.wire(k, "", o, "A");
            auto clip = copy(f.p, f.p.circuits[0], {a, o, j, k});
            Project pasted;
            paste(pasted, pasted.circuits[0], clip, {30, 40});
            Simulator s(pasted, "main");
            s.settle();
            check(s.read({pasted.circuits[0].parts[1].id, "A"}).value == 1,
                  "copied tunnel names retain connectivity");
        }
        {
            // Deliberately non-monotonic IDs catch double-remapping of imported port IDs.
            Project p;
            Circuit child;
            child.name = "leaf";
            child.parts = {{"p10", "Input", "A", "", {}, 1, 1, {}}, {"p1", "Output", "Y", "", {}, 1, 0, {}}};
            child.wires = {{"w11", {"p10", "Y"}, {"p1", "A"}, {}}};
            p.circuits.push_back(child);
            p.nextId = 12;
            p.circuits[0].parts = {{"p12", "Subcircuit", "", "leaf", {}, 1, 0, {}},
                                   {"p13", "Input", "I", "", {}, 1, 1, {}},
                                   {"p14", "Output", "O", "", {}, 1, 0, {}}};
            p.circuits[0].wires = {{"w15", {"p13", "Y"}, {"p12", "p10"}, {}},
                                   {"w16", {"p12", "p1"}, {"p14", "A"}, {}}};
            p.nextId = 17;
            auto clip = copy(p, p.circuits[0], {"p12", "p13", "p14"});
            Project dest;
            paste(dest, dest.circuits[0], clip, {0, 0});
            Simulator s(dest, "main");
            s.settle();
            check(s.read({dest.circuits[0].parts[2].id, "A"}).value == 1,
                  "cross-project subcircuit ID collisions");
            check(truthTable(dest, "main").rows == std::vector<std::vector<int>>{{0, 0}, {1, 1}},
                  "hierarchical combinational truth table");
            auto json = eda::logic::serialize(demo);
            auto at = json.find("\"width\": 1");
            json.replace(at, 10, "\"width\": 1.5");
            rejects([&] { eda::logic::deserialize(json); }, "fractional bit width rejected");
        }
        // Exercise evaluation of every exposed component; catches missing pin/evaluator branches.
        for (auto &info : library()) {
            Fixture f;
            auto id = f.add(info.kind);
            Simulator s(f.p, "main");
            check(s.settle(), info.kind + " evaluates without exception");
        }
        if (argc > 1)
            writeAtomic(std::filesystem::u8path(argv[1]), eda::logic::serialize(demo));
#ifdef LOGIC_EXAMPLES_DIR
        for (const auto &name :
             {"half-adder.logic.json", "bus-register.logic.json", "hierarchical-adder.logic.json"}) {
            auto project =
                eda::logic::deserialize(readFile(std::filesystem::u8path(LOGIC_EXAMPLES_DIR) / name));
            Simulator s(project, project.main);
            check(s.settle() && s.issues().empty(), std::string(name) + " example runs cleanly");
            if (std::string(name) == "bus-register.logic.json") {
                s.tick();
                auto &parts = project.circuit("main").parts;
                auto q = std::find_if(parts.begin(), parts.end(), [](auto &p) { return p.kind == "Output"; });
                check(q != parts.end() && s.read({q->id, "A"}).value == 37,
                      "bus register example captures data");
            } else
                check(truthTable(project, project.main).rows == table.rows,
                      std::string(name) + " example truth table");
        }
#endif
        std::cout << "PASS " << checks << " digital checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
