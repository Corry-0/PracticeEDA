#include "Digital.h"
#include <iostream>
#include <sstream>
using namespace eda;
using namespace eda::logic;
namespace {
int checks = 0;
void check(bool ok, const std::string &message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action, const std::string &message) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, message);
}
std::string xy(int x, int y) {
    return "(" + std::to_string(x) + "," + std::to_string(y) + ")";
}
std::string a(const std::string &name, const std::string &value) {
    return "<a name='" + name + "' val='" + value + "'/>";
}
std::string comp(const std::string &name, int x, int y, const std::string &attrs = "", int lib = 0) {
    return "<comp name='" + name + "' lib='" + std::to_string(lib) + "' loc='" + xy(x, y) + "'>" + attrs +
           "</comp>";
}
std::string wire(int x, int y, int xx, int yy) {
    return "<wire from='" + xy(x, y) + "' to='" + xy(xx, yy) + "'/>";
}
std::string project(const std::string &body, const std::string &more = "") {
    return "<?xml version='1.0'?><project source='2.7.1' version='1.0'>"
           "<lib name='0' desc='#Wiring'/><lib name='1' desc='#Gates'/>"
           "<lib name='2' desc='#Plexers'/><lib name='4' desc='#Memory'/>"
           "<main name='main'/><circuit name='main'>" +
           body + "</circuit>" + more + "</project>";
}
const Part &part(const Project &p, const std::string &label) {
    for (auto &q : p.circuit("main").parts)
        if (q.label == label)
            return q;
    throw std::runtime_error("missing part " + label);
}
uint32_t read(Simulator &s, const Part &q, const std::string &pin = "A") {
    auto result = s.read({q.id, pin});
    check(result.defined(), "signal must be defined: " + q.label + "/" + pin + " = " + result.text());
    return result.value;
}
void gateTests() {
    for (auto name : {"AND Gate", "OR Gate", "NAND Gate", "NOR Gate", "XOR Gate", "XNOR Gate"})
        for (auto facing : {"east", "west", "north", "south"})
            for (int size : {30, 50, 70}) {
                std::string n = name, f = facing;
                int length = size + ((n == "XOR Gate" || n == "XNOR Gate") ? 10 : 0) +
                             ((n == "NAND Gate" || n == "NOR Gate" || n == "XNOR Gate") ? 10 : 0);
                auto input = [&](const char *label, int offset) {
                    int x = f == "east" ? -length : f == "west" ? length : offset;
                    int y = f == "north" ? length : f == "south" ? -length : offset;
                    return comp("Pin", 200 + x, 200 + y, a("label", label));
                };
                auto p = importLogisim(
                    project(input("A", size == 30 ? -10 : -20) + input("B", size == 30 ? 10 : 20) +
                            comp(n, 200, 200,
                                 a("inputs", "2") + a("facing", f) + a("size", std::to_string(size)), 1) +
                            comp("Pin", 200, 200, a("output", "true") + a("label", "Y"))));
                check(logisimDiagnostics(p.circuit("main")).size() == 1, "only general import note");
                Simulator s(p, "main");
                for (int x = 0; x < 2; ++x)
                    for (int y = 0; y < 2; ++y) {
                        s.setInput(part(p, "A").id, x);
                        s.setInput(part(p, "B").id, y);
                        s.settle();
                        bool expected = n == "AND Gate"    ? x && y
                                        : n == "OR Gate"   ? x || y
                                        : n == "NAND Gate" ? !(x && y)
                                        : n == "NOR Gate"  ? !(x || y)
                                        : n == "XOR Gate"  ? x != y
                                                           : x == y;
                        check(read(s, part(p, "Y")) == uint32_t(expected), n + " " + f + " truth table");
                    }
            }
}
void connectivityTests() {
    auto p = importLogisim(project(comp("Constant", 0, 50, a("value", "1")) +
                                   comp("Pin", 100, 50, a("label", "horizontal") + a("output", "true")) +
                                   comp("Constant", 50, 0, a("value", "0")) +
                                   comp("Pin", 50, 100, a("label", "vertical") + a("output", "true")) +
                                   wire(0, 50, 100, 50) + wire(50, 0, 50, 100)));
    Simulator cross(p, "main");
    cross.settle();
    check(read(cross, part(p, "horizontal")) == 1 && read(cross, part(p, "vertical")) == 0,
          "bare crossing does not connect");
    p = importLogisim(project(comp("Constant", 0, 0, a("width", "8") + a("value", "0xa5")) +
                              wire(0, 0, 100, 0) + wire(100, 0, 0, 0) + wire(30, 0, 80, 0) +
                              wire(50, 0, 50, 50) + comp("Probe", 50, 50, a("label", "probe")) +
                              comp("Pin", 70, 0, a("width", "8") + a("output", "true") + a("label", "mid"))));
    Simulator tee(p, "main");
    tee.settle();
    check(part(p, "probe").width == 8 && read(tee, part(p, "probe")) == 0xa5,
          "T-junction, overlapping wires and probe bus inference");
    check(read(tee, part(p, "mid")) == 0xa5, "port in wire interior connects");
    p = importLogisim(project(comp("Constant", 0, 0, a("value", "1")) +
                              comp("Tunnel", 0, 0, a("label", "bus")) +
                              comp("Tunnel", 100, 0, a("label", "bus")) +
                              comp("Pin", 100, 0, a("output", "true") + a("label", "out"))));
    Simulator tunnel(p, "main");
    tunnel.settle();
    check(read(tunnel, part(p, "out")) == 1, "tunnel alias connects");
}
void wiringAndPlexerTests() {
    struct Direction {
        std::string name;
        Point muxA, muxB, muxS, demuxA, demuxB, demuxS, decoder0, decoderStep;
    };
    // Offsets from Logisim 2.7's port definitions, not PracticeEDA symbol geometry.
    for (const auto &d : std::vector<Direction>{
             {"east", {-30, -10}, {-30, 10}, {-20, 20}, {30, -10}, {30, 10}, {20, 20}, {20, -40}, {0, 10}},
             {"west", {30, -10}, {30, 10}, {20, 20}, {-30, -10}, {-30, 10}, {-20, 20}, {-20, -40}, {0, 10}},
             {"north", {-10, 30}, {10, 30}, {-20, 20}, {-10, -30}, {10, -30}, {-20, -20}, {0, -20}, {10, 0}},
             {"south",
              {-10, -30},
              {10, -30},
              {-20, -20},
              {-10, 30},
              {10, 30},
              {-20, 20},
              {0, 20},
              {10, 0}}}) {
        auto at = [&](const std::string &name, Point pt, const std::string &attrs) {
            return comp(name, 200 + int(pt.x), 200 + int(pt.y), attrs);
        };
        for (auto selloc : {"bl", "tr"}) {
            bool top = std::string(selloc) == "tr";
            auto select = d.muxS;
            if (top) {
                if (d.name == "east" || d.name == "west")
                    select.y *= -1;
                else
                    select.x *= -1;
            }
            auto p = importLogisim(
                project(comp("Multiplexer", 200, 200,
                             a("facing", d.name) + a("selloc", selloc) + a("width", "8"), 2) +
                        at("Constant", d.muxA, a("width", "8") + a("value", "0x5a")) +
                        at("Constant", d.muxB, a("width", "8") + a("value", "0xa5")) +
                        at("Pin", select, a("label", "S")) +
                        at("Pin", {}, a("width", "8") + a("output", "true") + a("label", "Y"))));
            Simulator mux(p, "main");
            mux.settle();
            check(read(mux, part(p, "Y")) == 0x5a, "mux select 0 " + d.name);
            mux.setInput(part(p, "S").id, 1);
            mux.settle();
            check(read(mux, part(p, "Y")) == 0xa5, "mux select 1 " + d.name);
            select = d.demuxS;
            if (top) {
                if (d.name == "east" || d.name == "west")
                    select.y *= -1;
                else
                    select.x *= -1;
            }
            p = importLogisim(
                project(comp("Demultiplexer", 200, 200, a("facing", d.name) + a("selloc", selloc), 2) +
                        at("Constant", {}, a("value", "1")) + at("Pin", select, a("label", "S")) +
                        at("Pin", d.demuxA, a("output", "true") + a("label", "Y0")) +
                        at("Pin", d.demuxB, a("output", "true") + a("label", "Y1"))));
            Simulator demux(p, "main");
            demux.settle();
            check(read(demux, part(p, "Y0")) == 1 && read(demux, part(p, "Y1")) == 0, "demux select 0");
            demux.setInput(part(p, "S").id, 1);
            demux.settle();
            check(read(demux, part(p, "Y0")) == 0 && read(demux, part(p, "Y1")) == 1, "demux select 1");
            auto body =
                comp("Decoder", 200, 200, a("facing", d.name) + a("select", "2") + a("selloc", selloc), 2) +
                at("Pin", {}, a("width", "2") + a("label", "S"));
            auto first = d.decoder0;
            if (top)
                first = first + d.decoderStep * ((d.name == "east" || d.name == "west") ? 4 : -4);
            for (int i = 0; i < 4; ++i)
                body += at("Pin", first + d.decoderStep * i,
                           a("output", "true") + a("label", "Y" + std::to_string(i)));
            p = importLogisim(project(body));
            Simulator decoder(p, "main");
            for (int sel = 0; sel < 4; ++sel) {
                decoder.setInput(part(p, "S").id, sel);
                decoder.settle();
                for (int i = 0; i < 4; ++i)
                    check(read(decoder, part(p, "Y" + std::to_string(i))) == uint32_t(i == sel),
                          "decoder one-hot");
            }
        }
    }
    for (auto kind : {"NOT Gate", "Buffer", "Controlled Buffer"}) {
        auto body = comp(kind, 100, 100, a("label", "gate"), 1) +
                    comp("Constant", std::string(kind) == "NOT Gate" ? 70 : 80, 100, a("value", "1"));
        if (std::string(kind) == "Controlled Buffer")
            body += comp("Constant", 90, 110);
        auto p = importLogisim(project(body));
        Simulator s(p, "main");
        s.settle();
        check(read(s, part(p, "gate"), "Y") == uint32_t(std::string(kind) != "NOT Gate"),
              "unary/controlled gate");
    }
    auto p = importLogisim(
        project(comp("Bit Extender", 100, 0,
                     a("in_width", "1") + a("out_width", "8") + a("type", "zero") + a("label", "ext")) +
                comp("Power", 60, 0) + comp("Ground", 0, 100, a("label", "gnd"))));
    Simulator s(p, "main");
    s.settle();
    check(read(s, part(p, "ext"), "Y") == 1 && read(s, part(p, "gnd"), "Y") == 0,
          "power/ground and zero extender");
    auto remapped = project(comp("Constant", 0, 0, a("label", "C")));
    remapped.replace(remapped.find("name='0'"), 8, "name='42'");
    remapped.replace(remapped.find("lib='0'"), 7, "lib='42'");
    check(part(importLogisim(remapped), "C").kind == "Constant", "library IDs resolved through desc");
}
void memoryTests() {
    for (auto kind : {"Register", "D Flip-Flop", "T Flip-Flop", "J-K Flip-Flop", "S-R Flip-Flop"}) {
        std::string n = kind;
        bool reg = n == "Register", two = n == "J-K Flip-Flop" || n == "S-R Flip-Flop";
        int dx = reg ? 170 : 160, dy = reg || two ? 200 : 220;
        int cx = reg ? 180 : 160, cy = reg ? 220 : two ? 210 : 200;
        auto body = comp(n, 200, 200, a("width", "1") + a("label", "state"), 4) +
                    comp("Pin", dx, dy, a("label", "D")) + comp("Pin", cx, cy, a("label", "CLK")) +
                    comp("Pin", 200, 200, a("label", "Q") + a("output", "true"));
        if (two)
            body += comp("Constant", 160, 220, a("value", "0"));
        auto p = importLogisim(project(body));
        check(logisimDiagnostics(p.circuit("main")).size() == 1, "memory mapped: " + n);
        Simulator s(p, "main");
        s.settle();
        s.setInput(part(p, "D").id, 1);
        s.settle();
        check(read(s, part(p, "Q")) == 0, "state holds before clock");
        s.setInput(part(p, "CLK").id, 1);
        s.settle();
        check(read(s, part(p, "Q")) == 1, "rising edge stores input: " + n);
    }
    auto p = importLogisim(project(
        comp("ROM", 200, 0, a("label", "rom") + "<a name='contents'>addr/data: 8 8\n3*0 a5 ff</a>", 4) +
        comp("Constant", 60, 0, a("width", "8") + a("value", "3")) +
        comp("Pin", 200, 0, a("width", "8") + a("label", "out") + a("output", "true"))));
    Simulator rom(p, "main");
    rom.settle();
    check(read(rom, part(p, "out")) == 0xa5 && part(p, "rom").data.size() == 5,
          "ROM header and run-length contents");
    p = importLogisim(project(comp("RAM", 200, 0, a("label", "ram") + a("bus", "separate"), 4) +
                              comp("Constant", 60, 0, a("width", "8") + a("value", "3")) +
                              comp("Constant", 60, 20, a("width", "8") + a("value", "0x5a")) +
                              comp("Constant", 90, 40, a("value", "1")) +
                              comp("Pin", 130, 40, a("label", "CLK"))));
    Simulator ram(p, "main");
    ram.settle();
    ram.setInput(part(p, "CLK").id, 1);
    ram.settle();
    check(read(ram, part(p, "ram"), "Q") == 0x5a, "RAM writes separate port on rising edge");
}
void partialAndValidationTests() {
    auto p = importLogisim(project(comp("Constant", 0, 0) + comp("Alien", 100, 0, a("label", "unknown"), 9) +
                                   comp("Pin", 200, 0, a("width", "64")) + wire(0, 0, 100, 0)));
    p.validate();
    check(p.circuit("main").wires.size() >= 2 && logisimDiagnostics(p.circuit("main")).size() == 3,
          "partial import preserves known objects, wires and unsupported diagnostics");
    auto saved = serialize(p);
    check(serialize(deserialize(saved)) == saved, "native v1 round trip unchanged");
    check(logisimDiagnostics(deserialize(saved).circuit("main")).size() == 3,
          "diagnostics survive native save/reload");
    History history;
    history.reset(p, false);
    check(history.dirty(), "import is unsaved");
    history.markSaved();
    auto before = history.project;
    auto notes = logisimDiagnostics(history.project.circuit("main"));
    erase(history.project.circuit("main"), {notes.front().object});
    history.commit(before);
    check(history.undo() && serialize(history.project) == saved && history.redo(),
          "placeholder editing supports undo/redo");
    for (auto body :
         {comp("AND Gate", 0, 0, "", 1), comp("AND Gate", 0, 0, a("inputs", "2") + a("negate0", "true"), 1),
          comp("Register", 0, 0, a("trigger", "falling"), 4),
          comp("Register", 0, 0, "", 4) + wire(-10, 20, 10, 20),
          comp("Register", 0, 0, "", 4) + comp("Constant", -30, 10), comp("RAM", 0, 0, "", 4),
          comp("ROM", 0, 0, a("addrWidth", "16"), 4),
          comp("ROM", 0, 0, "<a name='contents'>addr/data: 8 8\n257*0</a>", 4),
          comp("Multiplexer", 0, 0, a("select", "2"), 2),
          comp("Multiplexer", 0, 0, "", 2) + wire(-10, 20, 10, 20),
          comp("Constant", 0, 0, a("value", "garbage")), comp("Constant", 0, 0, a("width", "-1")),
          comp("Constant", 0, 0, a("futureBehavior", "1")), comp("Constant", 0, 0, "", 99)}) {
        auto q = importLogisim(project(body));
        check(logisimDiagnostics(q.circuit("main")).size() == 2, "unsupported variant diagnosed");
    }
    auto labels = importLogisim(project(comp("Pin", 0, 0, a("label", "重复&amp;标签")) +
                                            comp("Pin", 100, 0, a("label", "重复&amp;标签")),
                                        "<circuit name='child'><comp loc='(0,0)' name='main'/></circuit>"));
    labels.validate();
    check(labels.circuits.size() == 2 && logisimDiagnostics(labels.circuit("main")).size() == 2,
          "unicode/XML entities, duplicate pin labels, multiple circuits");
    check(logisimDiagnostics(labels.circuit("child")).size() == 2,
          "subcircuit instance is explicit placeholder");
    for (auto bad :
         {std::string("<project>"), project("") + "<extra/>",
          project("<comp name='Pin' lib='0' loc='(nan,1)'/>"), project("", "<circuit name='main'/>"),
          std::string("<!DOCTYPE project [<!ENTITY x SYSTEM 'file:///no'>]>") + project(""),
          project("").replace(project("").find("2.7.1"), 5, "3.9.0")})
        rejects([&] { importLogisim(bad); }, "reject invalid project transactionally");
    rejects([] { importLogisim(std::string(32 * 1024 * 1024 + 1, 'x')); }, "size limit");
    auto missingMain = project("");
    missingMain.replace(missingMain.find("name='main'"), 11, "name='absent'");
    rejects([&] { importLogisim(missingMain); }, "reuse main circuit validation");
    auto native = serialize(halfAdder());
    check(serialize(deserialize(native)) == native, "existing native file format preserved");
}
void exampleTest() {
    auto p = importLogisim(readFile(std::filesystem::path(LOGIC_EXAMPLES_DIR) / "logisim-limited.circ"));
    check(logisimDiagnostics(p.circuit("main")).size() == 2,
          "example has unsupported Adder and general note");
    Simulator s(p, "main");
    s.settle();
    s.setInput(part(p, "A").id, 1);
    s.setInput(part(p, "B").id, 0);
    s.settle();
    s.tick();
    check(read(s, part(p, "Q")) == 1, "example mux and register capture sum");
    s.tick();
    s.setInput(part(p, "Select").id, 1);
    s.settle();
    s.tick();
    check(read(s, part(p, "Q")) == 0, "example mux selects carry");
}
} // namespace
int main() {
    try {
        gateTests();
        connectivityTests();
        wiringAndPlexerTests();
        memoryTests();
        partialAndValidationTests();
        exampleTest();
        std::cout << "Logisim import: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
