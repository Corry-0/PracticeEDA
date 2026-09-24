#pragma once
#include "LogicSupport.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace eda::logic {
// Stable kind identifiers are persisted in project files; display names may be translated.
struct PartInfo {
    std::string kind, name, category;
};
const std::vector<PartInfo> &library();
struct Part {
    // circuit is used only by Subcircuit; value/data are reset values, never live simulation state.
    std::string id, kind = "And", label, circuit;
    Point at;
    int width = 1;
    uint32_t value = 0;
    std::vector<uint32_t> data;
};
struct Port {
    // For subcircuits id is the internal Input/Output part ID, so renaming a label cannot break wires.
    std::string id;
    bool output;
    int width;
    Point at;
};
// Decorative geometry is separate from electrical parts and never participates in simulation.
struct Shape {
    std::string id, kind = "Line";
    Point a, b, control;
    double stroke = 2;
    bool filled = false, appearance = false;
};
bool shapeKind(const std::string &);
std::pair<Point, Point> shapeBounds(const Shape &);
bool shapeHit(const Shape &, Point, double tolerance);
struct Circuit {
    std::string name = "main";
    std::vector<Part> parts;
    std::vector<Junction> nodes;
    std::vector<Wire> wires;
    std::vector<Shape> shapes;
    Part *part(const std::string &id);
    const Part *part(const std::string &id) const;
};
struct Project {
    std::string name = "数字逻辑工程", main = "main";
    uint64_t nextId = 1;
    std::vector<Circuit> circuits{Circuit{}};
    std::string id(const std::string &prefix);
    Circuit &circuit(const std::string &name);
    const Circuit &circuit(const std::string &name) const;
    void validate() const;
};
// Custom components use ordinary subcircuits, so buses, state and X/Z retain their existing semantics.
struct ComponentPin {
    std::string name;
    bool output = false;
    int width = 1;
};
// Mutations are transactional: validation failure leaves the caller's project unchanged.
void createComponent(Project &, const std::string &name, const std::vector<ComponentPin> &pins);
std::string exportComponent(const Project &, const std::string &circuit);
std::string importComponent(Project &, const std::string &bytes);
std::vector<Port> ports(const Project &, const Part &);
double partHeight(const Project &, const Part &);
Point endpoint(const Project &, const Circuit &, const Endpoint &);
std::vector<Point> wirePoints(const Project &, const Circuit &, const Wire &);
void erase(Circuit &, const std::set<std::string> &);
std::string serialize(const Project &);
Project deserialize(const std::string &);
// Restricted 外部电路 2.7 XML conversion; the resulting project uses the unchanged v1 model.
// Unmapped objects and import notes are ordinary Text parts, retained by save/undo/copy.
Project importCirc(const std::string &);
std::vector<Issue> circDiagnostics(const Circuit &);
Project halfAdder();
// Clipboard includes whole definitions so instances survive cross-project paste.
std::string copy(const Project &, const Circuit &, const std::set<std::string> &);
std::set<std::string> paste(Project &, Circuit &, const std::string &, Point offset);
// Full-project snapshots include component definitions, so edits/imports undo as one operation.
class History {
  public:
    Project project;
    void reset(Project p, bool saved = true);
    void commit(const Project &before);
    bool undo();
    bool redo();
    void markSaved();
    bool dirty() const;

  private:
    std::vector<Project> past, future;
    std::string saved;
};
struct Signal {
    // Per-bit masks distinguish known 0/1, high impedance (z), unknown and conflicting drivers (error).
    int width = 1;
    uint32_t value = 0, known = 0, z = 0, error = 0;
    static Signal number(uint32_t value, int width);
    static Signal unknown(int width);
    static Signal floating(int width);
    static Signal failure(int width);
    bool defined() const;
    std::string text() const;
    bool operator==(const Signal &) const;
    bool operator!=(const Signal &s) const { return !(*this == s); }
};
struct Trace {
    uint64_t time;
    std::string object, pin, value;
};
class Simulator {
  public:
    Simulator(const Project &, const std::string &circuit);
    ~Simulator();
    Simulator(Simulator &&) noexcept;
    Simulator &operator=(Simulator &&) noexcept;
    void reset();
    void setInput(const std::string &id, uint32_t value);
    bool step(); // One simultaneous event-time batch.
    bool settle(size_t budget = 10000);
    void tick(); // Toggle every clock, then settle.
    Signal read(const Endpoint &) const;
    std::vector<Issue> issues() const;
    const std::vector<Trace> &trace() const;
    uint64_t time() const;
    size_t pending() const;
    // Export the compiled connectivity without advancing events or changing simulation state.
    // JSON preserves buses and aliases; KiCad S-expressions expand bus pins into individual bits.
    std::string netlist(bool kicad = false) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
struct Table {
    std::vector<std::string> inputs, outputs;
    std::vector<std::vector<int>> rows;
    std::string report() const;
    std::string csv() const;
};
Table truthTable(const Project &, const std::string &circuit);
Table expressionTable(const std::string &expression);
} // namespace eda::logic
