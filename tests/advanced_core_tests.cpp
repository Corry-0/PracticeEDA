// 高级数字逻辑回归：用实际输入/时钟/连线验证行为，尤其覆盖实例隔离与双向开关。
#include "Digital.h"
#include <iostream>
using namespace eda;
using namespace eda::logic;
namespace {
int count = 0;
void check(bool condition, const std::string &reason) {
    ++count;
    if (!condition)
        throw std::runtime_error(reason);
}
struct CircuitCase {
    Project p;
    std::string add(const std::string &kind, int width = 1, uint32_t value = 0,
                    const std::string &circuit = "main") {
        Part part;
        part.id = p.id("p");
        part.label = part.id;
        part.kind = kind;
        part.width = width;
        part.value = value;
        p.circuit(circuit).parts.push_back(part);
        return part.id;
    }
    Part &part(const std::string &id, const std::string &circuit = "main") {
        return *p.circuit(circuit).part(id);
    }
    void wire(const std::string &a, const std::string &ap, const std::string &b, const std::string &bp,
              const std::string &circuit = "main") {
        p.circuit(circuit).wires.push_back({p.id("w"), {a, ap}, {b, bp}, {}});
    }
};
bool value(const Simulator &sim, const std::string &id, const std::string &pin, uint32_t expected) {
    auto signal = sim.read({id, pin});
    return signal.defined() && signal.value == expected;
}
void gatesAndBuses() {
    // 三输入逐脚反相的真值表不依赖元件排列顺序。
    CircuitCase c;
    auto a = c.add("Input"), b = c.add("Input"), d = c.add("Input"), g = c.add("And");
    c.part(g).params = {{"inputs", 3}, {"invertMask", 2}};
    c.wire(a, "Y", g, "A");
    c.wire(b, "Y", g, "B");
    c.wire(d, "Y", g, "I2");
    Simulator sim(c.p, "main");
    for (uint32_t row = 0; row < 8; ++row) {
        sim.setInput(a, row & 1);
        sim.setInput(b, (row >> 1) & 1);
        sim.setInput(d, row >> 2);
        sim.settle();
        check(value(sim, g, "Y", row == 5), "multi-input gate and per-pin inversion");
    }
    c.part(g).kind = "Xor";
    c.part(g).params["invertMask"] = 0;
    c.part(g).params["xorOne"] = 1;
    Simulator one(c.p, "main");
    one.setInput(a, 1);
    one.setInput(b, 1);
    one.setInput(d, 1);
    one.settle();
    check(value(one, g, "Y", 0), "exactly-one XOR differs from odd parity");

    CircuitCase buses;
    auto input = buses.add("Input", 8, 0xa5), split = buses.add("Splitter", 8),
         join = buses.add("Joiner", 8), ext = buses.add("Extender", 8);
    buses.part(split).bitGroups = {{4, 5, 6, 7}, {3, 2, 1, 0}};
    buses.part(join).bitGroups = buses.part(split).bitGroups;
    buses.part(ext).params = {{"inputWidth", 4}, {"signExtend", 1}};
    buses.wire(input, "Y", split, "A");
    buses.wire(split, "B0", join, "B0");
    buses.wire(split, "B1", join, "B1");
    buses.wire(split, "B0", ext, "A");
    Simulator bs(buses.p, "main");
    bs.settle();
    check(value(bs, join, "Y", 0xa5), "arbitrary bit mapping round trip");
    check(value(bs, ext, "Y", 0xfa), "four-bit sign extension");
    auto round = deserialize(serialize(buses.p));
    check(round.circuit("main").part(split)->bitGroups == buses.part(split).bitGroups,
          "bit mappings persist");

    CircuitCase mux;
    auto m = mux.add("Mux", 8), select = mux.add("Input", 2);
    mux.part(m).params["selectBits"] = 2;
    for (int i = 0; i < 4; ++i) {
        auto source = mux.add("Constant", 8, uint32_t(11 + i));
        mux.wire(source, "Y", m, i == 0 ? "A" : i == 1 ? "B" : "I" + std::to_string(i));
    }
    mux.wire(select, "Y", m, "S");
    Simulator ms(mux.p, "main");
    for (uint32_t i = 0; i < 4; ++i) {
        ms.setInput(select, i);
        ms.settle();
        check(value(ms, m, "Y", 11 + i), "configurable mux selects every channel");
    }
}
void timingAndMemory() {
    CircuitCase c;
    auto clock = c.add("Clock"), slow = c.add("Clock"), enable = c.add("Constant", 1, 1),
         counter = c.add("Counter", 4), other = c.add("Counter", 4);
    c.part(slow).params = {{"lowTicks", 2}, {"highTicks", 2}};
    c.part(counter).params["counterMax"] = 9;
    c.wire(clock, "Y", counter, "CLK");
    c.wire(slow, "Y", other, "CLK");
    c.wire(enable, "Y", counter, "EN");
    c.wire(enable, "Y", other, "EN");
    Simulator sim(c.p, "main");
    sim.settle();
    for (int i = 0; i < 8; ++i)
        sim.tick();
    check(value(sim, counter, "Q", 4) && value(sim, other, "Q", 2), "independent clock periods");
    for (int i = 0; i < 12; ++i)
        sim.tick();
    check(value(sim, counter, "Q", 0), "modulo-ten counter wraps");

    CircuitCase imported;
    auto ic = imported.add("Input"), ct = imported.add("Input", 1, 1), ld = imported.add("Input", 1, 1),
         initial = imported.add("Constant", 4, 6), external = imported.add("Counter", 4);
    imported.part(external).params = {{"counterMax", 9}, {"loadable", 1}, {"logisimCounter", 1}};
    imported.wire(ic, "Y", external, "CLK");
    imported.wire(ct, "Y", external, "EN");
    imported.wire(ld, "Y", external, "LOAD");
    imported.wire(initial, "Y", external, "D");
    Simulator importedSim(imported.p, "main");
    importedSim.settle();
    check(value(importedSim, external, "CARRY", 1), "Logisim decrement terminal is zero");
    importedSim.setInput(ic, 1);
    importedSim.settle();
    check(value(importedSim, external, "Q", 9), "Logisim CT and LD decrement with wrap");
    importedSim.setInput(ic, 0);
    importedSim.setInput(ct, 0);
    importedSim.settle();
    importedSim.setInput(ic, 1);
    importedSim.settle();
    check(value(importedSim, external, "Q", 6), "Logisim LD loads only when CT is low");

    CircuitCase falling;
    auto clk = falling.add("Input"), data = falling.add("Input", 8, 77), reg = falling.add("Register", 8);
    falling.part(reg).params["falling"] = 1;
    falling.wire(clk, "Y", reg, "CLK");
    falling.wire(data, "Y", reg, "D");
    Simulator fs(falling.p, "main");
    fs.settle();
    fs.setInput(clk, 1);
    fs.settle();
    check(value(fs, reg, "Q", 0), "falling-edge register ignores rising edge");
    fs.setInput(clk, 0);
    fs.settle();
    check(value(fs, reg, "Q", 77), "falling-edge register samples falling edge");

    CircuitCase memory;
    auto address = memory.add("Input", 10, 999), ram = memory.add("RAM", 16),
         d = memory.add("Input", 16, 0xabcd), we = memory.add("Input", 1, 1),
         mc = memory.add("Input");
    memory.part(ram).params["addressBits"] = 10;
    memory.part(ram).data.resize(1024);
    memory.part(ram).data[999] = 12;
    memory.wire(address, "Y", ram, "ADDR");
    memory.wire(d, "Y", ram, "D");
    memory.wire(we, "Y", ram, "WE");
    memory.wire(mc, "Y", ram, "CLK");
    Simulator mem(memory.p, "main");
    mem.settle();
    check(value(mem, ram, "Q", 12) && mem.memory(ram).size() == 1024, "configurable RAM capacity and initial data");
    mem.setInput(mc, 1);
    mem.settle();
    check(mem.memory(ram)[999].value == 0xabcd, "real runtime memory observation");
    auto moment = mem.time();
    auto snapshot = mem.memory(ram);
    check(mem.time() == moment && snapshot[999].value == 0xabcd, "memory inspection preserves execution");
    mem.reset();
    mem.settle();
    check(mem.memory(ram)[999].value == 12, "reset restores initial image");
}
void switchesAndIo() {
    // 从开关 B 端驱动 A 端，直接证明确实是双向开关。
    CircuitCase c;
    auto enable = c.add("Input"), strong = c.add("Input", 8, 0x35), pull = c.add("PullUp", 8),
         transistor = c.add("Transistor", 8);
    c.wire(enable, "Y", transistor, "G");
    c.wire(strong, "Y", transistor, "B");
    c.wire(pull, "Y", transistor, "A");
    Simulator sim(c.p, "main");
    sim.settle();
    check(value(sim, transistor, "A", 255), "open switch leaves weak pull-up");
    sim.setInput(enable, 1);
    sim.settle();
    check(value(sim, transistor, "A", 0x35) && value(sim, transistor, "B", 0x35),
          "reverse conduction and strong-over-weak resolution");
    sim.setInput(enable, 0);
    sim.settle();
    check(value(sim, transistor, "A", 255) && value(sim, transistor, "B", 0x35), "switch opens again without stale backdrive");
    size_t received = 0;
    sim.setTraceSink([&](const Trace &) { ++received; });
    for (unsigned i = 0; i < 2050; ++i) {
        sim.setInput(strong, i & 255);
        sim.settle();
    }
    check(received > 2000 && sim.trace().size() == 2000, "continuous sink outlives display buffer");

    CircuitCase transmission;
    auto en = transmission.add("Input", 1, 1), nen = transmission.add("Input"),
         v = transmission.add("Input", 1, 1), tg = transmission.add("TransmissionGate");
    transmission.wire(en, "Y", tg, "EN");
    transmission.wire(nen, "Y", tg, "NEN");
    transmission.wire(v, "Y", tg, "B");
    Simulator ts(transmission.p, "main");
    ts.settle();
    check(value(ts, tg, "A", 1), "transmission gate conducts in reverse");
    ts.setInput(nen, 1);
    ts.settle();
    check(ts.read({tg, "A"}).text() == "Z", "transmission gate complementary control opens");

    CircuitCase io;
    auto keyboard = io.add("Keyboard"), tty = io.add("TTY"), clk = io.add("Input"),
         one = io.add("Constant", 1, 1), zero = io.add("Constant");
    io.wire(clk, "Y", keyboard, "CLK");
    io.wire(clk, "Y", tty, "CLK");
    io.wire(one, "Y", keyboard, "READ");
    io.wire(one, "Y", tty, "EN");
    io.wire(zero, "Y", keyboard, "CLEAR");
    io.wire(zero, "Y", tty, "CLEAR");
    io.wire(keyboard, "DATA", tty, "DATA");
    Simulator ios(io.p, "main");
    ios.settle();
    ios.typeText(keyboard, "Hi");
    ios.settle();
    check(value(ios, keyboard, "DATA", 'H') && value(ios, keyboard, "AVAILABLE", 1), "keyboard queues actual text");
    for (int i = 0; i < 2; ++i) {
        ios.setInput(clk, 1);
        ios.settle();
        ios.setInput(clk, 0);
        ios.settle();
    }
    check(ios.text(tty) == "Hi" && value(ios, keyboard, "AVAILABLE", 0), "TTY captures data on clock edges");

    CircuitCase bits;
    auto source = bits.add("Input", 8, 0xa5), adder = bits.add("BitAdder", 8),
         finder = bits.add("BitFinder", 8), parity = bits.add("EvenParity", 8),
         selector = bits.add("BitSelector", 8), index = bits.add("Constant", 1, 1), joy = bits.add("Joystick", 8);
    bits.part(finder).params["mode"] = 1;
    bits.part(selector).params["outputWidth"] = 4;
    bits.wire(source, "Y", adder, "A");
    bits.wire(source, "Y", finder, "A");
    bits.wire(source, "Y", parity, "A");
    bits.wire(source, "Y", selector, "A");
    bits.wire(index, "Y", selector, "S");
    Simulator bitSim(bits.p, "main");
    bitSim.setInput(joy, (22U << 16) | 11);
    bitSim.settle();
    check(value(bitSim, adder, "Y", 4) && value(bitSim, finder, "Y", 7) && value(bitSim, finder, "V", 1),
          "population count and highest set-bit search");
    check(value(bitSim, parity, "Y", 1) && value(bitSim, selector, "Y", 10), "parity and grouped bit selection");
    check(value(bitSim, joy, "X", 11) && value(bitSim, joy, "Y", 22), "joystick independent coordinates");
}
void geometryAndHierarchy() {
    CircuitCase c;
    auto gate = c.add("And");
    c.part(gate).rotation = 1;
    auto pins = ports(c.p, c.part(gate));
    check(pins[0].id == "A" && pins[0].at.x == 10 && pins[0].at.y == -70,
          "rotation preserves stable ID and rotates port geometry");
    Circuit child;
    child.name = "memory-block";
    c.p.circuits.push_back(child);
    auto address = c.add("Input", 8, 0, child.name), rom = c.add("ROM", 8, 0, child.name),
         output = c.add("Output", 8, 0, child.name);
    c.part(rom, child.name).data = {73};
    c.wire(address, "Y", rom, "ADDR", child.name);
    c.wire(rom, "Q", output, "A", child.name);
    c.p.circuit(child.name).interfaceLayout[address] = {-90, 30};
    auto instance = c.add("Subcircuit");
    c.part(instance).circuit = child.name;
    auto zero = c.add("Constant", 8);
    c.wire(zero, "Y", instance, address);
    auto childPins = ports(c.p, c.part(instance));
    check(childPins[0].at.x == -90 && childPins[0].at.y == 30, "custom interface layout applies to instance");
    Simulator sim(c.p, "main");
    sim.settle();
    check(value(sim, instance + "/" + rom, "Q", 73) && sim.memory(instance + "/" + rom)[0].value == 73,
          "nested instance signals and memory use actual execution state");
    auto clip = copy(c.p, c.p.circuit("main"), {instance});
    Project pasted;
    auto selected = paste(pasted, pasted.circuit("main"), clip, {20, 20});
    const auto &newInstance = *pasted.circuit("main").part(*selected.begin());
    const auto &newDefinition = pasted.circuit(newInstance.circuit);
    check(newDefinition.interfaceLayout.size() == 1 &&
              newDefinition.part(newDefinition.interfaceLayout.begin()->first),
          "cross-project paste remaps custom interface IDs");
}
void catalogAndRandom() {
    // 每一个已登记原语都必须能建网和求值；缺少求值分支不能靠库列表伪装成功。
    for (const auto &info : library()) {
        CircuitCase c;
        c.add(info.kind);
        Simulator sim(c.p, "main");
        check(sim.settle(), "catalog device settles: " + info.kind);
    }
    CircuitCase random;
    auto clk = random.add("Input"), en = random.add("Constant", 1, 1), generator = random.add("Random", 32);
    random.part(generator).params["seed"] = 27;
    random.wire(clk, "Y", generator, "CLK");
    random.wire(en, "Y", generator, "EN");
    Simulator sim(random.p, "main");
    sim.settle();
    sim.setInput(clk, 1);
    sim.settle();
    auto first = sim.read({generator, "Y"});
    sim.reset();
    sim.settle();
    sim.setInput(clk, 1);
    sim.settle();
    check(first.defined() && first.value != 0 && first == sim.read({generator, "Y"}),
          "random seed replays deterministically after reset");
}
} // namespace
int main() {
    try {
        gatesAndBuses();
        timingAndMemory();
        switchesAndIo();
        geometryAndHierarchy();
        catalogAndRandom();
        std::cout << "advanced core: " << count << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "advanced core failure after " << count << " checks: " << error.what() << '\n';
        return 1;
    }
}
