// Feature regressions exercise public APIs, malformed files, live state and exported connectivity.
#include "Digital.h"
#include "json.hpp"
#include <iostream>
using namespace eda;
using namespace eda::logic;
using json = nlohmann::json;
namespace {
int checks = 0;
void check(bool passed, const std::string &message) {
    ++checks;
    if (!passed)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action, const std::string &message) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, message);
}
std::string add(Project &p, const std::string &c, const std::string &kind, int width = 1, uint32_t value = 0,
                const std::string &definition = "") {
    Part part;
    part.id = p.id("p");
    part.kind = kind;
    part.label = part.id;
    part.width = width;
    part.value = value;
    part.circuit = definition;
    p.circuit(c).parts.push_back(part);
    return part.id;
}
void wire(Project &p, const std::string &c, Endpoint a, Endpoint b) {
    p.circuit(c).wires.push_back({p.id("w"), std::move(a), std::move(b), {}});
}
std::string pin(const Project &p, const std::string &c, const std::string &label) {
    for (const auto &q : p.circuit(c).parts)
        if (q.label == label)
            return q.id;
    throw std::runtime_error("missing test pin " + label);
}
int netOf(const json &j, const std::string &alias) {
    for (const auto &net : j.at("nets"))
        for (const auto &a : net.at("aliases"))
            if (a == alias)
                return net.at("id").get<int>();
    throw std::runtime_error("missing net alias " + alias);
}
void artifacts(const Simulator &s, const std::filesystem::path &dir, const std::string &name) {
    writeAtomic(dir / (name + ".net.json"), s.netlist());
    writeAtomic(dir / (name + ".net"), s.netlist(true));
}
} // namespace

int main(int argc, char **argv) {
    try {
        auto dir = argc > 1 ? std::filesystem::u8path(argv[1]) : std::filesystem::path("qa-features");
        std::filesystem::create_directories(dir);
        Project p;
        createComponent(p, "自定义与门", {{"A", false, 1}, {"B", false, 1}, {"Y", true, 1}});
        const auto a = pin(p, "自定义与门", "A"), b = pin(p, "自定义与门", "B"),
                   y = pin(p, "自定义与门", "Y");
        const auto gate = add(p, "自定义与门", "And");
        wire(p, "自定义与门", {a, "Y"}, {gate, "A"});
        wire(p, "自定义与门", {b, "Y"}, {gate, "B"});
        wire(p, "自定义与门", {gate, "Y"}, {y, "A"});
        check(truthTable(p, "自定义与门").rows ==
                  std::vector<std::vector<int>>{{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 1}},
              "custom AND truth table");
        auto before = serialize(p);
        for (const auto &pins :
             std::vector<std::vector<ComponentPin>>{{},
                                                    {{"", false, 1}},
                                                    {{"A", false, 0}},
                                                    {{"A", false, 33}},
                                                    {{"A", false, 1}, {"A", true, 1}},
                                                    std::vector<ComponentPin>(65, {"A", false, 1})}) {
            rejects([&] { createComponent(p, "bad", pins); }, "reject invalid interface");
            check(serialize(p) == before, "invalid creation is atomic");
        }
        rejects([&] { createComponent(p, "自定义与门", {{"A", false, 1}}); }, "duplicate definition");
        rejects([&] { createComponent(p, "  ", {{"A", false, 1}}); }, "blank definition");
        rejects([&] { exportComponent(p, "main"); }, "cannot package missing interface");
        auto bytes = exportComponent(p, "自定义与门");
        check(serialize(p) == before, "component export is read-only");
        check(json::parse(bytes)["project"]["circuits"].size() == 1, "exclude unrelated definitions");
        writeAtomic(dir / std::filesystem::u8path("自定义与门.component.json"), bytes);
        check(readFile(dir / std::filesystem::u8path("自定义与门.component.json")) == bytes,
              "Unicode file roundtrip");

        History history;
        history.reset(p);
        const auto imported = importComponent(history.project, bytes);
        history.commit(p);
        check(imported != "自定义与门", "name collision renamed");
        check(history.undo() && serialize(history.project) == before, "import undo");
        check(history.redo(), "import redo");
        check(truthTable(history.project, imported).rows == truthTable(p, "自定义与门").rows,
              "import retains logic");
        auto bad = json::parse(bytes);
        bad["version"] = 99;
        rejects([&] { importComponent(p, bad.dump()); }, "reject unknown component version");
        rejects([&] { importComponent(p, "{"); }, "reject truncated component");
        rejects([&] { importComponent(p, serialize(p)); }, "reject project masquerading as component");
        bad = json::parse(bytes);
        bad["project"]["circuits"][0]["wires"][0]["a"][0] = "missing";
        rejects([&] { importComponent(p, bad.dump()); }, "reject broken endpoint");
        bad = json::parse(bytes);
        auto &recursive = bad["project"]["circuits"][0]["parts"][3];
        recursive["kind"] = "Subcircuit";
        recursive["circuit"] = "自定义与门";
        bad["project"]["circuits"][0]["wires"] = json::array();
        rejects([&] { importComponent(p, bad.dump()); }, "reject recursive component");
        check(serialize(p) == before, "failed imports preserve project and IDs");
        Project full;
        for (int i = 1; i < 128; ++i) {
            Circuit c;
            c.name = "definition" + std::to_string(i);
            full.circuits.push_back(c);
        }
        const auto fullBefore = serialize(full);
        rejects([&] { importComponent(full, bytes); }, "project capacity checked on import");
        check(serialize(full) == fullBefore, "capacity rejection preserves project");

        // Nested interfaces refer to definition IDs. Verify import remaps them at every level.
        auto hierarchy = deserialize(
            readFile(std::filesystem::u8path(LOGIC_EXAMPLES_DIR) / "hierarchical-adder.logic.json"));
        Project target = halfAdder();
        const auto targetBefore = serialize(target);
        const auto nested = importComponent(target, exportComponent(hierarchy, hierarchy.main));
        check(json::parse(serialize(target))["circuits"][0] == json::parse(targetBefore)["circuits"][0],
              "import preserves existing objects and wires exactly");
        check(truthTable(target, nested).rows == truthTable(hierarchy, hierarchy.main).rows,
              "nested import truth table");
        check(truthTable(target, target.main).rows == truthTable(halfAdder(), "main").rows,
              "existing circuit preserved");
        auto roundtrip = deserialize(serialize(target));
        check(truthTable(roundtrip, nested).rows == truthTable(hierarchy, hierarchy.main).rows,
              "custom project roundtrip");
        artifacts(Simulator(target, nested), dir, "hierarchy");

        // Two imported register instances share the definition but never the running state.
        Project stateful;
        createComponent(stateful, "Latch8", {{"D", false, 8}, {"CLK", false, 1}, {"Q", true, 8}});
        auto reg = add(stateful, "Latch8", "Register", 8);
        wire(stateful, "Latch8", {pin(stateful, "Latch8", "D"), "Y"}, {reg, "D"});
        wire(stateful, "Latch8", {pin(stateful, "Latch8", "CLK"), "Y"}, {reg, "CLK"});
        wire(stateful, "Latch8", {reg, "Q"}, {pin(stateful, "Latch8", "Q"), "A"});
        Project run;
        auto def = importComponent(run, exportComponent(stateful, "Latch8"));
        auto d1 = add(run, "main", "Input", 8, 37), d2 = add(run, "main", "Input", 8, 91);
        auto clk = add(run, "main", "Clock");
        auto s1 = add(run, "main", "Subcircuit", 1, 0, def), s2 = add(run, "main", "Subcircuit", 1, 0, def);
        auto o1 = add(run, "main", "Output", 8), o2 = add(run, "main", "Output", 8);
        wire(run, "main", {d1, "Y"}, {s1, pin(run, def, "D")});
        wire(run, "main", {d2, "Y"}, {s2, pin(run, def, "D")});
        for (const auto &s : {s1, s2})
            wire(run, "main", {clk, "Y"}, {s, pin(run, def, "CLK")});
        wire(run, "main", {s1, pin(run, def, "Q")}, {o1, "A"});
        wire(run, "main", {s2, pin(run, def, "Q")}, {o2, "A"});
        Simulator live(run, "main");
        live.settle();
        live.tick();
        check(live.read({o1, "A"}).value == 37 && live.read({o2, "A"}).value == 91,
              "instance state isolation");
        live.setInput(d1, 5); // Keep pending events while exporting, to catch accidental settle/reset.
        const auto time = live.time(), pending = live.pending(), traces = live.trace().size();
        const auto q1 = live.read({o1, "A"}), q2 = live.read({o2, "A"});
        const auto netlist = live.netlist();
        artifacts(live, dir, "registers");
        check(live.time() == time && live.pending() == pending && live.trace().size() == traces &&
                  live.read({o1, "A"}) == q1 && live.read({o2, "A"}) == q2,
              "exports preserve live simulation");
        check(netlist == live.netlist(), "deterministic netlist");
        live.settle();
        live.tick();
        live.tick();
        check(live.read({o1, "A"}).value == 5 && live.read({o2, "A"}).value == 91,
              "simulation continues after exports");
        auto j = json::parse(netlist);
        check(netOf(j, s1 + ":" + pin(run, def, "Q")) == netOf(j, o1 + ":A"), "hierarchical output merged");
        check(netOf(j, o1 + ":A") != netOf(j, o2 + ":A"), "separate instance outputs remain distinct");

        // Same-name junctions connect locally. Identical drawing positions alone do not connect.
        Project labels;
        auto input = add(labels, "main", "Input", 32, UINT32_MAX), output = add(labels, "main", "Output", 32);
        auto isolated = add(labels, "main", "Output", 32);
        auto j1 = labels.id("j"), j2 = labels.id("j");
        labels.circuit("main").nodes = {{j1, "BUS", {0, 0}}, {j2, "BUS", {100, 0}}};
        wire(labels, "main", {input, "Y"}, {j1, ""});
        wire(labels, "main", {j2, ""}, {output, "A"});
        labels.name = "中文 \"project\" \\ newline\n";
        labels.circuit("main").part(input)->label = "引脚 \"A\" \\ \n";
        Simulator labeled(labels, "main");
        labeled.settle();
        j = json::parse(labeled.netlist());
        check(netOf(j, input + ":Y") == netOf(j, output + ":A"), "label connectivity exported");
        check(netOf(j, isolated + ":A") != netOf(j, output + ":A"), "no geometric phantom connections");
        check(labeled.read({output, "A"}).value == UINT32_MAX &&
                  labeled.read({isolated, "A"}).z == UINT32_MAX,
              "export matches 32-bit simulation and floating input");
        check(j["nets"][netOf(j, input + ":Y") - 1]["labels"].size() == 2, "retain engineering net labels");
        artifacts(labeled, dir, "bus32-escaping");
        // Import the labeled circuit, then instantiate it twice: BUS must stay local to each instance.
        Project scoped;
        auto scopedDef = importComponent(scoped, exportComponent(labels, "main"));
        auto left = add(scoped, "main", "Subcircuit", 1, 0, scopedDef);
        auto right = add(scoped, "main", "Subcircuit", 1, 0, scopedDef);
        auto scopedInput = scoped.circuit(scopedDef).parts.front().id;
        Simulator scopedSimulation(scoped, "main");
        auto scopedJson = json::parse(scopedSimulation.netlist());
        check(netOf(scopedJson, left + ":" + scopedInput) != netOf(scopedJson, right + ":" + scopedInput),
              "same label in separate instances does not short nets");
        artifacts(scopedSimulation, dir, "scoped-labels");
        labels.circuit("main").part(output)->width = 8;
        Simulator mismatch(labels, "main");
        rejects([&] { mismatch.netlist(); }, "reject incompatible bus widths in JSON");
        rejects([&] { mismatch.netlist(true); }, "reject incompatible bus widths in KiCad");
        artifacts(Simulator(Project{}, "main"), dir, "empty");
        for (const auto &info : library()) {
            Project catalog;
            auto id = add(catalog, "main", info.kind);
            if (info.kind == "RAM" || info.kind == "ROM")
                catalog.circuit("main").part(id)->data = {1, 2, 3};
            Simulator s(catalog, "main");
            j = json::parse(s.netlist());
            check(j["components"].size() == (info.kind == "Text" ? 0 : 1), "catalog export " + info.kind);
            if (info.kind != "Text")
                check(j["components"][0]["pins"].size() ==
                          ports(catalog, *catalog.circuit("main").part(id)).size(),
                      "all primitive pins exported " + info.kind);
            artifacts(s, dir, "catalog-" + info.kind);
        }
        writeAtomic(dir / "PASS.txt", "PASS " + std::to_string(checks) + " feature checks\n");
        std::cout << "PASS " << checks << " feature checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
