// 只观察现有程序的真实行为，不把“复现了漏检”算作 ERC 验收通过。
#include "Digital.h"
#include "json.hpp"
#include <iostream>

using namespace eda;
using namespace eda::logic;
using Json = nlohmann::json;

namespace {
struct Fixture {
    Project project;
    std::string add(const std::string &kind, uint32_t value = 0, int width = 1,
                    const std::string &circuit = "main") {
        Part part;
        part.id = project.id("p");
        part.label = part.id;
        part.kind = kind;
        part.width = width;
        part.value = value;
        const auto id = part.id;
        project.circuit(circuit).parts.push_back(std::move(part));
        return id;
    }
    std::string node(const std::string &name = "") {
        auto id = project.id("j");
        project.circuit("main").nodes.push_back({id, name, {0, 0}});
        return id;
    }
    void wire(Endpoint a, Endpoint b) {
        project.circuit("main").wires.push_back({project.id("w"), std::move(a), std::move(b), {}});
    }
};
Json issueJson(const std::vector<Issue> &issues) {
    auto result = Json::array();
    for (const auto &issue : issues)
        result.push_back({{"severity", issue.severity}, {"object", issue.object}, {"message", issue.message}});
    return result;
}
struct Audit {
    std::filesystem::path folder;
    Json results = Json::array();
    int unexpected = 0;
    template<class Build>
    void run(const std::string &id, const std::string &purpose, Build build,
             bool expectRejected = false) {
        Fixture f;
        build(f);
        Json row = {{"id", id}, {"purpose", purpose}, {"expectedStructuralRejection", expectRejected}};
        writeAtomic(folder / (id + ".logic.json"), serialize(f.project));
        try {
            f.project.validate();
            row["acceptedByProjectValidate"] = true;
            Simulator sim(f.project, "main");
            row["settled"] = sim.settle();
            auto issues = sim.issues();
            const auto notes = circDiagnostics(f.project.circuit("main"));
            issues.insert(issues.end(), notes.begin(), notes.end());
            row["diagnostics"] = issueJson(issues);
            row["panelWouldShowPass"] = issues.empty();
            row["signals"] = Json::object();
            for (const auto &part : f.project.circuit("main").parts)
                for (const auto &port : ports(f.project, part))
                    row["signals"][part.id + ":" + port.id] = sim.read({part.id, port.id}).text();
            if (expectRejected || !row["settled"].get<bool>())
                ++unexpected;
        } catch (const std::exception &error) {
            row["acceptedByProjectValidate"] = false;
            row["rejection"] = error.what();
            if (!expectRejected)
                ++unexpected;
        }
        std::cout << id << ": " << (row.value("acceptedByProjectValidate", false)
                         ? std::to_string(row["diagnostics"].size()) + " diagnostics" : "rejected") << '\n';
        results.push_back(std::move(row));
    }
};
Fixture parallelBuffers(uint32_t secondValue) {
    Fixture f;
    auto a = f.add("Input"), b = f.add("Input", secondValue);
    auto first = f.add("Buffer"), second = f.add("Buffer"), load = f.add("Output");
    f.wire({a, "Y"}, {first, "A"});
    f.wire({b, "Y"}, {second, "A"});
    f.wire({first, "Y"}, {load, "A"});
    f.wire({second, "Y"}, {load, "A"});
    return f;
}
}

int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: erc_audit <output-directory>");
        Audit audit;
        audit.folder = std::filesystem::u8path(argv[1]);
        std::filesystem::create_directories(audit.folder);
        audit.run("healthy-half-adder", "正常数字电路基线", [](Fixture &f) { f.project = halfAdder(); });
        audit.run("unconnected-input", "未连接的必选输入端", [](Fixture &f) { f.add("Output"); });
        audit.run("input-only-net", "两个输入相连但没有驱动", [](Fixture &f) {
            auto a = f.add("Output"), b = f.add("Output");
            f.wire({a, "A"}, {b, "A"});
        });
        audit.run("unconnected-output", "单端输出网络，没有负载", [](Fixture &f) { f.add("Constant"); });
        audit.run("dangling-wire-end", "已驱动输出连到导线自由端", [](Fixture &f) {
            auto source = f.add("Constant"), end = f.node();
            f.wire({source, "Y"}, {end, ""});
        });
        audit.run("wire-without-pins", "仅两个节点组成的悬空导线", [](Fixture &f) {
            auto a = f.node(), b = f.node();
            f.wire({a, ""}, {b, ""});
        });
        audit.run("equal-push-pull-outputs", "两个普通缓冲器输出同电平直接相连", [](Fixture &f) {
            f = parallelBuffers(0);
        });
        audit.run("opposite-push-pull-outputs", "两个普通缓冲器输出异电平直接相连", [](Fixture &f) {
            f = parallelBuffers(1);
        });
        audit.run("power-ground-short", "Power 与 Ground 的输出直接相连", [](Fixture &f) {
            auto power = f.add("Power"), ground = f.add("Ground");
            f.wire({power, "Y"}, {ground, "Y"});
        });
        audit.run("power-logic-output-tied", "Power 与普通逻辑输出同电平直连", [](Fixture &f) {
            auto power = f.add("Power"), source = f.add("Constant", 1), buffer = f.add("Buffer"), load = f.add("Output");
            f.wire({source, "Y"}, {buffer, "A"});
            f.wire({buffer, "Y"}, {power, "Y"});
            f.wire({power, "Y"}, {load, "A"});
        });
        audit.run("vcc-label-driven-by-ground", "名为 VCC 的普通标签网络由 Ground 驱动；标签并非电源类型", [](Fixture &f) {
            auto ground = f.add("Ground"), label = f.node("VCC"), load = f.add("Output");
            f.wire({ground, "Y"}, {label, ""});
            f.wire({label, ""}, {load, "A"});
        });
        audit.run("mixed-power-labels", "VCC/GND 两个普通标签在同一网络，无专用电源域信息", [](Fixture &f) {
            auto power = f.add("Power"), vcc = f.node("VCC"), gnd = f.node("GND"), load = f.add("Output");
            f.wire({power, "Y"}, {vcc, ""});
            f.wire({vcc, ""}, {gnd, ""});
            f.wire({gnd, ""}, {load, "A"});
        });
        audit.run("similar-labels", "两个正常驱动的独立网络使用 CLK/clk 相似标签", [](Fixture &f) {
            for (const auto *name : {"CLK", "clk"}) {
                auto source = f.add("Constant"), label = f.node(name), load = f.add("Output");
                f.wire({source, "Y"}, {label, ""});
                f.wire({label, ""}, {load, "A"});
            }
        });
        audit.run("different-labels-one-net", "同一网络上存在两个不同普通标签，现实现接受其作为别名", [](Fixture &f) {
            auto source = f.add("Constant"), a = f.node("NET_A"), b = f.node("NET_B"), load = f.add("Output");
            f.wire({source, "Y"}, {a, ""});
            f.wire({a, ""}, {b, ""});
            f.wire({b, ""}, {load, "A"});
        });
        audit.run("floating-label", "孤立且没有同名远端连接的网络标签", [](Fixture &f) { f.node("UNUSED_NET"); });
        audit.run("width-mismatch", "连接双方总线位宽不一致", [](Fixture &f) {
            auto source = f.add("Input", 0, 8), load = f.add("Output", 0, 4);
            f.wire({source, "Y"}, {load, "A"});
        });
        audit.run("optional-clear-unconnected", "可选 CLR 不接线的正常例外", [](Fixture &f) {
            auto source = f.add("Constant"), reg = f.add("Register"), load = f.add("Output");
            f.wire({source, "Y"}, {reg, "D"});
            f.wire({source, "Y"}, {reg, "CLK"});
            f.wire({reg, "Q"}, {load, "A"});
        });
        audit.run("optional-clear-wired-undriven", "CLR 已显式接线，但其网络没有驱动", [](Fixture &f) {
            auto source = f.add("Constant"), reg = f.add("Register"), load = f.add("Output"), end = f.node();
            f.wire({source, "Y"}, {reg, "D"});
            f.wire({source, "Y"}, {reg, "CLK"});
            f.wire({reg, "Q"}, {load, "A"});
            f.wire({reg, "CLR"}, {end, ""});
        });
        audit.run("disabled-tristate", "三态输出关闭时，接收端为 Z；当前警告取决于运行态", [](Fixture &f) {
            auto data = f.add("Constant", 1), enable = f.add("Constant"), tri = f.add("TriState"), load = f.add("Output");
            f.wire({data, "Y"}, {tri, "A"});
            f.wire({enable, "Y"}, {tri, "EN"});
            f.wire({tri, "Y"}, {load, "A"});
        });
        audit.run("ambiguous-pin-diagnostics", "同一与门两个输入均悬空，诊断是否给出具体脚名", [](Fixture &f) { f.add("And"); });
        audit.run("unused-invalid-electrical-definition", "未被实例化的定义内有悬空必选输入", [](Fixture &f) {
            Circuit unused;
            unused.name = "unused";
            f.project.circuits.push_back(unused);
            f.add("Output", 0, 1, "unused");
        });
        audit.run("duplicate-ordinary-labels", "两个普通器件的显示名称均为 U1；显示名称不等于实体位号", [](Fixture &f) {
            auto source = f.add("Constant"), a = f.add("Buffer"), b = f.add("Buffer");
            f.project.circuit("main").part(a)->label = "U1";
            f.project.circuit("main").part(b)->label = "U1";
            f.wire({source, "Y"}, {a, "A"});
            f.wire({source, "Y"}, {b, "A"});
        });
        audit.run("duplicate-interface-labels", "重复输入/输出接口名称属于结构校验", [](Fixture &f) {
            auto a = f.add("Input"), b = f.add("Output");
            f.project.circuit("main").part(a)->label = "DUPLICATE";
            f.project.circuit("main").part(b)->label = "DUPLICATE";
        }, true);
        audit.run("invalid-wire-endpoint", "引用不存在的对象/引脚属于结构校验", [](Fixture &f) {
            auto source = f.add("Constant");
            f.wire({source, "Y"}, {"missing", "A"});
        }, true);
        audit.run("missing-subcircuit-definition", "子电路引用不存在的定义属于结构校验", [](Fixture &f) {
            auto id = f.add("Subcircuit");
            f.project.circuit("main").part(id)->circuit = "missing";
        }, true);
        audit.run("recursive-subcircuit", "递归定义属于结构校验", [](Fixture &f) {
            auto id = f.add("Subcircuit");
            f.project.circuit("main").part(id)->circuit = "main";
        }, true);
        // 同一拓扑，只改变运行态，再观察短接诊断是否消失。
        auto switching = parallelBuffers(0);
        Simulator sim(switching.project, "main");
        sim.settle();
        Json dynamic = {{"initial", issueJson(sim.issues())}};
        sim.setInput(switching.project.circuit("main").parts[1].id, 1);
        sim.settle();
        dynamic["oppositeValues"] = issueJson(sim.issues());
        sim.setInput(switching.project.circuit("main").parts[1].id, 0);
        sim.settle();
        dynamic["sameValuesAgain"] = issueJson(sim.issues());
        // 这些字段在当前模型中没有对应成员；不是已实现的实体规则检查。
        auto metadata = Json::parse(serialize(halfAdder()));
        metadata["circuits"][0]["parts"][0]["reference"] = "U1";
        metadata["circuits"][0]["parts"][0]["footprint"] = "";
        metadata["circuits"][0]["parts"][0]["unit"] = 1;
        metadata["circuits"][0]["parts"][0]["unitCount"] = 4;
        auto loaded = Json::parse(serialize(deserialize(metadata.dump())));
        Json physical = Json::object();
        for (const auto *field : {"reference", "footprint", "unit", "unitCount"})
            physical[field] = loaded["circuits"][0]["parts"][0].contains(field);
        const Json report = {{"date", "2026-10-22"},
                             {"scope", "Behavior audit, not an ERC conformance pass"},
                             {"scenarioCount", audit.results.size()},
                             {"unexpectedExecutionFailures", audit.unexpected},
                             {"scenarios", audit.results},
                             {"sameTopologyDriverStateChanges", dynamic},
                             {"physicalMetadataSurvivesRoundtrip", physical}};
        writeAtomic(audit.folder / "results.json", report.dump(2) + "\n");
        std::cout << "Audit scenarios: " << audit.results.size() << "; unexpected execution failures: "
                  << audit.unexpected << "; this is NOT an ERC conformance pass.\n";
        return audit.unexpected ? 1 : 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
