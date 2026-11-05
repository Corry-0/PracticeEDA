// 验证案例说明中的可观察结果，而不是只验证 JSON 能打开。
#include "Digital.h"
#include <iostream>
using namespace eda;
using namespace eda::logic;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
Project load(const char *file) {
    auto project = deserialize(readFile(std::filesystem::u8path(LOGIC_EXAMPLES_DIR) / file));
    Simulator sim(project, project.main);
    require(sim.settle(), "Experiment did not settle");
    require(sim.issues().empty(), "Experiment has initial diagnostics");
    return project;
}
std::string id(const Project &project, const std::string &label) {
    for (const auto &part : project.circuit(project.main).parts)
        if (part.label == label)
            return part.id;
    throw std::runtime_error("Case label missing: " + label);
}
void expect(Simulator &sim, Endpoint pin, uint32_t value, int width) {
    require(sim.settle(), "Experiment did not settle after input");
    require(sim.read(pin) == Signal::number(value, width), "Experiment output disagrees with instructions");
}
void adder(const char *file, bool full) {
    auto project = load(file);
    Simulator sim(project, project.main);
    const auto a = id(project, "A"), b = id(project, "B");
    for (unsigned row = 0; row < (full ? 8U : 4U); ++row) {
        unsigned av = row & 1U, bv = (row >> 1) & 1U, cv = (row >> 2) & 1U;
        sim.setInput(a, av);
        sim.setInput(b, bv);
        if (full)
            sim.setInput(id(project, "Cin"), cv);
        const auto sum = av + bv + cv;
        expect(sim, {id(project, "Sum"), "A"}, sum & 1U, 1);
        expect(sim, {id(project, full ? "Cout" : "Carry"), "A"}, sum >> 1, 1);
    }
}
} // namespace
int main() {
    try {
        adder("half-adder.logic.json", false);
        adder("full-adder.logic.json", true);
        adder("hierarchical-adder.logic.json", false);
        auto bus = load("bus-register.logic.json");
        Simulator reg(bus, bus.main);
        expect(reg, {"p3", "Q"}, 0, 8);
        reg.tick();
        expect(reg, {"p3", "Q"}, 0x25, 8);
        reg.setInput("p1", 0x3c);
        expect(reg, {"p3", "Q"}, 0x25, 8);
        reg.tick();
        expect(reg, {"p3", "Q"}, 0x25, 8);
        reg.tick();
        expect(reg, {"p3", "Q"}, 0x3c, 8);

        auto counter = load("counter.logic.json");
        Simulator count(counter, counter.main);
        expect(count, {"counter", "Q"}, 0, 4);
        for (unsigned tick = 1; tick <= 32; ++tick) {
            count.tick();
            const auto value = ((tick + 1) / 2) % 16;
            expect(count, {"counter", "Q"}, value, 4);
            expect(count, {"counter", "CARRY"}, value == 15, 1);
        }
        count.setInput("en", 0);
        for (int i = 0; i < 4; ++i)
            count.tick();
        expect(count, {"counter", "Q"}, 0, 4);
        count.setInput("en", 1);
        count.tick();
        expect(count, {"counter", "Q"}, 1, 4);
        count.setInput("clr", 1);
        expect(count, {"counter", "Q"}, 0, 4);

        auto ram = load("ram-read-write.logic.json");
        const auto before = serialize(ram);
        Simulator mem(ram, ram.main);
        expect(mem, {"ram", "Q"}, 0, 8);
        mem.setInput("we", 1);
        mem.settle();
        mem.tick();
        expect(mem, {"ram", "Q"}, 0x2a, 8);
        mem.setInput("we", 0);
        mem.setInput("addr", 1);
        expect(mem, {"ram", "Q"}, 0, 8);
        mem.setInput("data", 0x55);
        mem.setInput("we", 1);
        mem.settle();
        mem.tick();
        expect(mem, {"ram", "Q"}, 0, 8);
        mem.tick();
        expect(mem, {"ram", "Q"}, 0x55, 8);
        mem.setInput("we", 0);
        mem.setInput("addr", 0);
        expect(mem, {"ram", "Q"}, 0x2a, 8);
        mem.setInput("data", 0xff);
        mem.settle();
        mem.tick();
        mem.tick();
        expect(mem, {"ram", "Q"}, 0x2a, 8);
        auto words = mem.memory("ram");
        require(words.size() == 16 && words[0] == Signal::number(0x2a, 8) &&
                    words[1] == Signal::number(0x55, 8), "RAM addresses are not independent");
        require(serialize(ram) == before, "Simulation modified the case template");
        mem.reset();
        expect(mem, {"ram", "Q"}, 0, 8);
        std::cout << "Six experiment cases: truth tables, register edges, counter wrap/enable/clear, RAM read/write/reset passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
