#include "Digital.h"
#include "json.hpp"
#include <iostream>
#include <limits>
using namespace eda;
using namespace eda::logic;
using json = nlohmann::json;
namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "invalid shape accepted");
}
} // namespace
int main() {
    try {
        Project p = halfAdder();
        Simulator original(p, p.main);
        auto netlist = original.netlist();
        auto table = truthTable(p, p.main).csv();
        for (const auto &kind : {"Line", "Curve", "Rectangle", "Ellipse", "Circle"}) {
            Shape s;
            s.id = p.id("s");
            s.kind = kind;
            s.a = {-50, -30};
            s.b = {50, 70};
            s.control = {0, -100};
            s.appearance = true;
            p.circuits[0].shapes.push_back(s);
        }
        p.validate();
        check(serialize(deserialize(serialize(p))) == serialize(p), "shape roundtrip");
        check(Simulator(p, p.main).netlist() == netlist, "artwork changed connectivity");
        check(truthTable(p, p.main).csv() == table, "artwork changed simulation");
        Project imported;
        auto name = importComponent(imported, exportComponent(p, p.main));
        check(imported.circuit(name).shapes.size() == 5, "component artwork lost");
        importComponent(imported, exportComponent(p, p.main));
        imported.validate();
        check(imported.circuits.size() == 3, "repeat import lost definitions");
        Part instance;
        instance.id = imported.id("p");
        instance.kind = "Subcircuit";
        instance.circuit = name;
        imported.circuit("main").parts.push_back(instance);
        Project pasted;
        paste(pasted, pasted.circuit("main"), copy(imported, imported.circuit("main"), {instance.id}),
              {10, 20});
        pasted.validate();
        check(pasted.circuit(pasted.circuit("main").parts[0].circuit).shapes.size() == 5,
              "cross-project instance copy lost artwork");
        const auto shape = p.circuits[0].shapes[1];
        auto ids = paste(p, p.circuits[0], copy(p, p.circuits[0], {shape.id}), {40, 30});
        p.validate();
        check(ids.size() == 1 && !ids.count(shape.id), "shape clipboard IDs");
        auto copied = p.circuits[0].shapes.back();
        check(distance(copied.a, shape.a + Point{40, 30}) < .01 &&
                  distance(copied.control, shape.control + Point{40, 30}) < .01,
              "shape clipboard offset");
        History h;
        h.reset(p);
        auto before = h.project;
        erase(h.project.circuits[0], ids);
        h.commit(before);
        check(h.project.circuits[0].shapes.size() == 5, "delete shape");
        check(h.undo() && serialize(h.project) == serialize(before), "undo shape");
        check(h.redo() && h.project.circuits[0].shapes.size() == 5, "redo shape");
        auto old = json::parse(serialize(Project{}));
        old["circuits"][0].erase("shapes");
        check(deserialize(old.dump()).circuits[0].shapes.empty(), "old v1 compatibility");
        auto corrupt = json::parse(serialize(p));
        auto malformed = [&](const char *key, const json &value) {
            auto bad = corrupt;
            bad["circuits"][0]["shapes"][0][key] = value;
            rejects([&] { deserialize(bad.dump()); });
        };
        malformed("kind", "Unknown");
        malformed("stroke", 0);
        malformed("stroke", 21);
        malformed("a", json::array({1000001, 0}));
        malformed("b", json::array({-50, -30}));
        malformed("filled", "false");
        malformed("appearance", 1);
        malformed("id", p.circuits[0].parts[0].id);
        auto badCircle = p;
        badCircle.circuits[0].shapes[4].b.x += 20;
        rejects([&] { badCircle.validate(); });
        auto invalid = p;
        invalid.circuits[0].shapes.back().stroke = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { invalid.validate(); });
        Shape line{"test", "Line", {0, 0}, {100, 0}};
        check(shapeHit(line, {50, 3}, 3) && !shapeHit(line, {50, 20}, 3), "line hit");
        Shape curve{"test", "Curve", {0, 0}, {100, 0}, {50, 100}};
        check(shapeHit(curve, {50, 50}, 2) && !shapeHit(curve, {50, 0}, 2), "curve hit");
        Shape rectangle{"test", "Rectangle", {100, 100}, {0, 0}};
        check(shapeHit(rectangle, {100, 50}, 2) && !shapeHit(rectangle, {50, 50}, 2), "rectangle outline");
        rectangle.filled = true;
        check(shapeHit(rectangle, {50, 50}, 2), "rectangle fill");
        Shape circle{"test", "Circle", {0, 0}, {100, 100}};
        check(shapeHit(circle, {100, 50}, 2) && !shapeHit(circle, {50, 50}, 2), "circle outline");
        circle.filled = true;
        check(shapeHit(circle, {50, 50}, 2), "circle fill");
        std::cout << "Shapes: " << checks << " checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
