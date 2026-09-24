// Portable component definitions reuse the validated project format and the existing subcircuit engine.
#include "Digital.h"
#include "json.hpp"

namespace eda::logic {
namespace {
using json = nlohmann::json;

void checkInterface(const Circuit &c) {
    if (std::none_of(c.parts.begin(), c.parts.end(),
                     [](const Part &p) { return p.kind == "Input" || p.kind == "Output"; }))
        throw std::runtime_error("自定义元件至少需要一个输入或输出引脚");
}
} // namespace

void createComponent(Project &project, const std::string &name, const std::vector<ComponentPin> &pins) {
    if (name.empty() || name.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::runtime_error("元件名称不能为空");
    if (pins.empty() || pins.size() > 64)
        throw std::runtime_error("自定义元件接口数量应为 1–64");
    Project candidate = project;
    Circuit c;
    c.name = name;
    int inputs = 0, outputs = 0;
    std::set<std::string> names;
    for (const auto &pin : pins) {
        if (pin.name.empty() || pin.name.find_first_not_of(" \t\r\n") == std::string::npos ||
            !names.insert(pin.name).second)
            throw std::runtime_error("接口名称不能为空或重复");
        Part p;
        p.id = candidate.id("p");
        p.kind = pin.output ? "Output" : "Input";
        p.label = pin.name;
        p.width = pin.width;
        p.at = {pin.output ? 480.0 : 0.0, 100.0 * (pin.output ? outputs++ : inputs++)};
        c.parts.push_back(p);
    }
    candidate.circuits.push_back(std::move(c));
    candidate.validate();
    project = std::move(candidate);
}

std::string exportComponent(const Project &project, const std::string &name) {
    project.validate();
    checkInterface(project.circuit(name));
    // Include only reachable definitions. Unrelated work in the source project is never packaged.
    std::set<std::string> reachable;
    std::function<void(const std::string &)> visit = [&](const std::string &n) {
        if (!reachable.insert(n).second)
            return;
        for (const auto &p : project.circuit(n).parts)
            if (p.kind == "Subcircuit")
                visit(p.circuit);
    };
    visit(name);
    Project package = project;
    package.main = name;
    package.name = name;
    auto &cs = package.circuits;
    cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const Circuit &c) { return !reachable.count(c.name); }),
             cs.end());
    return json{
        {"format", "PracticeEDA.component"}, {"version", 1}, {"project", json::parse(serialize(package))}}
        .dump(2);
}

std::string importComponent(Project &project, const std::string &bytes) {
    if (bytes.size() > 32 * 1024 * 1024)
        throw std::runtime_error("元件文件超过 32 MB");
    auto j = json::parse(bytes);
    if (j.at("format") != "PracticeEDA.component" || j.at("version") != 1)
        throw std::runtime_error("不是受支持的自定义元件文件（.component.json）");
    auto source = deserialize(j.at("project").dump());
    // Repackage on import as well, to reject invalid interfaces and drop unrelated definitions.
    source = deserialize(json::parse(exportComponent(source, source.main)).at("project").dump());
    Project candidate = project;
    std::set<std::string> used;
    for (const auto &c : candidate.circuits)
        used.insert(c.name);
    std::map<std::string, std::string> names, ids;
    for (const auto &c : source.circuits) {
        auto name = c.name;
        for (int i = 1; used.count(name); ++i)
            name = c.name + "_import" + std::to_string(i);
        used.insert(name);
        names[c.name] = name;
        for (const auto &p : c.parts)
            ids[p.id] = candidate.id("p");
        for (const auto &n : c.nodes)
            ids[n.id] = candidate.id("j");
        for (const auto &w : c.wires)
            ids[w.id] = candidate.id("w");
    }
    // IDs are unique across a project. Remap both instance IDs and definition-based interface IDs.
    for (auto c : source.circuits) {
        auto remapEndpoint = [&](Endpoint &e) {
            const auto *part = source.circuit(c.name).part(e.object);
            if (part && part->kind == "Subcircuit")
                e.pin = ids.at(e.pin);
            e.object = ids.at(e.object);
        };
        for (auto &w : c.wires) {
            remapEndpoint(w.a);
            remapEndpoint(w.b);
            w.id = ids.at(w.id);
        }
        c.name = names.at(c.name);
        for (auto &s : c.shapes)
            s.id = candidate.id("s");
        for (auto &p : c.parts) {
            p.id = ids.at(p.id);
            if (p.kind == "Subcircuit")
                p.circuit = names.at(p.circuit);
        }
        for (auto &n : c.nodes)
            n.id = ids.at(n.id);
        candidate.circuits.push_back(std::move(c));
    }
    candidate.validate();
    project = std::move(candidate);
    return names.at(source.main);
}
} // namespace eda::logic
