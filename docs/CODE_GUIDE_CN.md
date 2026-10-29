# 代码阅读与维护指南

## 文件职责

| 文件或目录 | 阅读重点 |
| --- | --- |
| `src/App.cpp` | 应用启动、命令行参数、GUI 冒烟测试退出码 |
| `include/Digital.h` | 工程数据结构、稳定 ID、四态信号、仿真与自定义元件公共 API |
| `src/Digital.cpp` | 内置库、端口布局、校验、序列化、剪贴板、历史、层次展平、事件驱动、网表、真值表 |
| `src/CustomComponents.cpp` | 创建接口、依赖闭包、可移植元件文件、名称/ID 重映射、事务式导入 |
| `include/LogicAnalysis.h` / `src/LogicAnalysis.cpp` | 真值表校验、Quine–McCluskey 化简、覆盖搜索、卡诺图、生成电路与独立仿真验证 |
| `src/CircImport.cpp` | Logisim XML 导入、器件几何、层次定义和未支持对象的诊断 |
| `include/PcbExport.h` / `src/PcbExport.cpp` | 受限实体芯片/封装映射、标准网表与配套未布线板 |
| `include/DigitalEditor.h` / `src/DigitalEditor.cpp` | 窗口与画布状态、菜单、输入校验、撤销事务、拖拽和连线、GUI 回归 |
| `src/LogicEditorTools.cpp` | 十六进制内存窗口、接口坐标、连续 CSV、最近工程与批量图片 |
| `src/ExperimentCases.cpp` | 案例目录、说明窗口、候选工程校验、从案例创建副本、GUI 回归 |
| `tests/experiment_case_tests.cpp` | 六个案例的真值表、寄存器边沿、计数器回绕/使能/清零和 RAM 读写验证 |
| `include/LogicSupport.h` / `src/LogicSupport.cpp` | 模型坐标、视口变换、端点与折线、限额读取、原子文件替换 |
| `include/WxSupport.h` | UTF-8 与 wxString 转换，Windows UTF-16 路径 |
| `include/LogicSymbols.h` / `src/LogicSymbols.cpp` | 画布、放置预览、PNG 导出共享的矢量绘图 |
| `include/LogicIcons.h` / `src/LogicIcons.cpp` | 内嵌工具栏资源与高 DPI 图标 |
| `tests/digital_tests.cpp` | 原有逻辑、事件、存储、层次、文件校验回归 |
| `tests/advanced_core_tests.cpp` / `tests/analysis_tests.cpp` | 参数、独立时钟、实际实例状态、双向开关与弱驱动、组合分析闭环 |
| `tests/component_netlist_tests.cpp` | 自定义元件 API、错误回滚、实例隔离、导出与仿真一致性 |
| `tests/verify_netlists.py` | 独立解析 S-expression，逐网络、逐引脚比对 JSON |
| `tests/circ_import_tests.cpp` | Logisim 导入、连线几何、参数和未支持对象回归 |
| `tests/editor_module_tests.cpp` / `scripts/run_module_audit.py` | 真实菜单及窗口模块检查、Debug/Release 日志和失败记录 |
| `tests/pcb_export_tests.cpp` / `tests/verify_pcb_exchange.py` | 实体映射检查及实际KiCad板加载/逐焊盘核对 |
| `CMakeLists.txt` / `CMakePresets.json` | 核心库与 GUI 分离、VS 配置、测试注册和安装文件 |
| `scripts/build.ps1` / `scripts/DigitalLogic.cmd` | 构建测试打包 / 启动分发版或本地 Release 版 |
| `examples/` | 项目和可移植元件数据；JSON 不允许代码注释，说明放在文档内 |
| `resources/` / `third_party/` | 图标及第三方 JSON 库；保留原始数据、版权与许可，不给二进制或上游生成数据插入注释 |

## 必须维持的约定

- **ID 与显示名分离**：`Part.id` 和接口 `Port.id` 决定连接，`label` 只负责显示。自定义接口引用定义内部的 Input/Output ID，导入时这两层 ID 都要一起重映射。
- **几何与电气分离**：`ports()` 决定端口，绘图读取它；网络只合并显式端点与同实例中的同名节点，不根据画面交叉推断连接。
- **端口 ID 与朝向分离**：`Part.rotation` 用 0/1/2/3 表示顺时针四个方向；`ports()` 先应用定义的 `interfaceLayout`，再旋转相对坐标。旋转不能重命名 A/B/Y 或接口 ID。
- **定义与实例状态分离**：`Project` 保存结构及初值，`Simulator::Impl::Node` 保存各个展开实例的运行状态。共享定义不能共享寄存器或 RAM 数据。
- **导航不创建仿真器**：编辑器的 `simulationRoot` 指向当前运行根，`scope` 保存实例路径前缀；进入/返回实例只改变显示范围。`read()`、`memory()`、键盘和摇杆都使用完整实例路径。嵌套 Input/Output 是接口，不是可绕过父电路的独立驱动源。
- **同一时刻批处理**：事件批次的全部元件读取旧网络值，再统一发布变化。如果边求值边更新网络，时序采样会依赖元件排列顺序。
- **修改失败可回滚**：编辑器结构修改通过 `change()`；自定义元件核心接口先修改候选工程，校验后再替换原工程，避免半导入状态。
- **导出只读**：`Simulator::netlist()` 使用已经编译的连接映射；GUI 导出从已提交工程建立独立编译实例，不调用当前仿真器的 `reset()`、`step()`、`settle()` 或 `tick()`。
- **保存先写临时文件**：`writeAtomic()` 写入并检查临时文件后替换目标；导出或校验失败不应破坏已有文件。

## 0.5 参数与算法入口

`Part.params` 保存可选整数参数，调用 `parameter(part,name,fallback)` 显式给出缺省值；旧 v1 文件没有该字段时保留原行为。新增参数也必须同时覆盖校验、端口生成、求值、属性界面与序列化，不能只增加一行属性。门的前两输入保持 A/B，额外输入为 I2、I3 等，避免与大选择器的 S/Y 引脚重名。

`bitGroups` 的每个数组列出原总线位号，数组首项映射到分支最低位。空分组继续按单比特拆/合；合线目标位不能重复，未分配位补零。四态信号的 value/known/z/error 掩码都必须按同样映射迁移，不能只处理整数值。

`Simulator::tick()` 推进一个基础节拍；各时钟按 highTicks、lowTicks、phase 独立求电平。传播队列的整数 time 和界面播放的毫秒间隔是另外两个量，不可混用。每个事件批次先读取旧网络，统一发布新输出；明确的 0→1 或 1→0 才视为有效采样边沿。

晶体管与传输门没有虚构的 A→B 输出。`switchedSignals()` 在每批事件内临时合并已导通的双向网络，再把两侧真实驱动交给 `resolve()`。关断后重新按静态网络求值，避免把反向传来的电平当成永久驱动。逐位解析优先使用非高阻的强驱动；只有缺少强驱动时才使用 PullUp/PullDown。控制未知时保留确定共同结果，其余位为 X。

RAM/ROM 按 addressBits 分配实例内存，普通门不再分配 256 字。单器件最多 65536 字，展开后总容量最多 4194304 字。`memory(path)` 返回运行内容副本；工程的 `Part.data` 只保存复位初值。保存、导航、只读导出不能将某实例的当前内容写回所有实例共用的定义。

`LogicAnalysis.cpp` 先对 0/1/设计不关心项建立蕴含项，再找必需项和覆盖。搜索有预算，达到预算仍返回等价覆盖并设置 `exact=false`；中间项过多会拒绝。生成电路后调用原仿真器重建真值表，逐行验证所有确定输出，再提交新子电路。设计不关心项 `-1` 不能替代仿真的 X/Z/E。

中文注释应解释模块职责、数据约定、输入/输出、优先级、边界和算法选择。核心开关解析、独立时钟、实例内存路径及组合分析已在相应实现处标注；维护时同步更新注释和验证，避免将错误行为写成约定。

本次已在所有自有 C++ 源码/头文件与构建脚本的职责边界、数据约定和关键流程补充注释。显而易见的赋值和循环不逐行复述；第三方库、原始图标和 JSON 示例由文档说明，保留原始格式。
