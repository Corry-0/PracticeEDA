#pragma once
#include "LogicSupport.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace eda::logic {
// 本头文件定义数字电路工程的数据模型、四态信号、事件驱动仿真器与自定义元件公共 API。
// 三条核心约定（详见 docs/CODE_GUIDE_CN.md）：
//  1. ID 与显示名分离：Part.id / Port.id 决定连接，label 只负责显示；
//  2. 几何与电气分离：ports() 决定端口，网络只合并显式端点，不按画面交叉推断；
//  3. 定义与实例运行态分离：Project 存结构与初值，Simulator 内部 Node 存展开实例的运行态。

// 元件 kind 是持久化到工程文件中的稳定标识；显示名（name）可以翻译成不同语言。
struct PartInfo {
    std::string kind, name, category;
};
const std::vector<PartInfo> &library();
struct Part {
    // circuit 仅 Subcircuit 使用；value/data 是复位初值，绝非运行中的仿真状态。
    std::string id, kind = "And", label, circuit;
    Point at;
    int width = 1;
    uint32_t value = 0;
    std::vector<uint32_t> data;
    // rotation 使用 0/1/2/3 表示顺时针 0/90/180/270 度；只影响几何，不改变端口 ID。
    int rotation = 0;
    // 可选整数参数向后兼容旧 v1 工程；缺省值由 parameter() 的调用方明确给出。
    std::map<std::string, int64_t> params{};
    // 每组依次列出总线中的位号，组内第一个位映射到分支的最低位。
    std::vector<std::vector<int>> bitGroups{};
};
int64_t parameter(const Part &, const std::string &name, int64_t fallback);
struct Port {
    // 对子电路而言 id 是定义内部的 Input/Output 元件 ID，因此重命名 label 不会弄断连线。
    std::string id;
    bool output;
    int width;
    Point at;
};
// 装饰性几何图形与电气元件相互独立，绝不参与仿真。
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
    // 自定义符号接口坐标，以定义内部 Input/Output 稳定 ID 为键，坐标相对实例中心。
    std::map<std::string, Point> interfaceLayout{};
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
// 自定义元件就是普通子电路，因此总线、状态与 X/Z 等语义与子电路完全一致。
struct ComponentPin {
    std::string name;
    bool output = false;
    int width = 1;
};
// 修改是事务式的：校验失败时调用方的工程保持原样。
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
// 受限的 外部电路(Logisim) 2.7 XML 转换；结果工程仍使用不变的 v1 模型。
// 无法映射的对象与导入说明都转成普通 Text 元件，可随保存/撤销/复制保留。
Project importCirc(const std::string &);
std::vector<Issue> circDiagnostics(const Circuit &);
Project halfAdder();
// 剪贴板包含完整定义，因此跨工程粘贴时实例仍能存活。
std::string copy(const Project &, const Circuit &, const std::set<std::string> &);
std::set<std::string> paste(Project &, Circuit &, const std::string &, Point offset);
// 全工程快照包含元件定义，因此编辑/导入可作为一次操作整体撤销。
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
    // 四个逐位掩码区分：已知 0/1（value/known）、高阻 z、未知 X 与驱动冲突 error。
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
    bool step(); // 推进一个“同一时刻”的事件批次。
    bool settle(size_t budget = 10000);
    void tick(); // 推进一个基础节拍；每个时钟独立按 highTicks/lowTicks/phase 翻转。
    Signal read(const Endpoint &) const;
    // path 可为 p12/p34/p56 等完整实例路径；返回副本，查看内容不推进仿真。
    std::vector<Signal> memory(const std::string &path) const;
    // 键盘字符队列与 TTY 输出属于实例运行态，不写入工程初值。
    void typeText(const std::string &path, const std::string &utf8);
    std::string text(const std::string &path) const;
    // 事件接收器可持续写文件；不受窗口内最近 2000 条显示缓存的限制。
    void setTraceSink(std::function<void(const Trace &)> sink);
    std::vector<Issue> issues() const;
    const std::vector<Trace> &trace() const;
    uint64_t time() const;
    size_t pending() const;
    // 基础节拍数；cycles() 保留旧接口，表示每两个基础节拍一组。
    // 多时钟各有周期，不能把该计数当作某个指定时钟的完整周期数。
    uint64_t ticks() const;
    uint64_t cycles() const;
    // 是否因事件预算耗尽而判定为振荡（为真时 step/settle 不再推进）。
    bool oscillating() const;
    // 导出已编译的连接关系，不推进事件、不改变仿真状态。
    // JSON 保留总线与别名；KiCad S-expression 把总线引脚逐位展开。
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
