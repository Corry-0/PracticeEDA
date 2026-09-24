# 代码阅读与维护指南

## 文件职责

| 文件或目录 | 阅读重点 |
| --- | --- |
| `src/App.cpp` | 应用启动、命令行参数、GUI 冒烟测试退出码 |
| `include/Digital.h` | 工程数据结构、稳定 ID、四态信号、仿真与自定义元件公共 API |
| `src/Digital.cpp` | 内置库、端口布局、校验、序列化、剪贴板、历史、层次展平、事件驱动、网表、真值表 |
| `src/CustomComponents.cpp` | 创建接口、依赖闭包、可移植元件文件、名称/ID 重映射、事务式导入 |
| `include/DigitalEditor.h` / `src/DigitalEditor.cpp` | 窗口与画布状态、菜单、输入校验、撤销事务、拖拽和连线、GUI 回归 |
| `include/LogicSupport.h` / `src/LogicSupport.cpp` | 模型坐标、视口变换、端点与折线、限额读取、原子文件替换 |
| `include/WxSupport.h` | UTF-8 与 wxString 转换，Windows UTF-16 路径 |
| `include/LogicSymbols.h` / `src/LogicSymbols.cpp` | 画布、放置预览、PNG 导出共享的矢量绘图 |
| `include/LogicIcons.h` / `src/LogicIcons.cpp` | 内嵌工具栏资源与高 DPI 图标 |
| `tests/digital_tests.cpp` | 原有逻辑、事件、存储、层次、文件校验回归 |
| `tests/component_netlist_tests.cpp` | 自定义元件 API、错误回滚、实例隔离、导出与仿真一致性 |
| `tests/verify_netlists.py` | 独立解析 S-expression，逐网络、逐引脚比对 JSON |
| `CMakeLists.txt` / `CMakePresets.json` | 核心库与 GUI 分离、VS 配置、测试注册和安装文件 |
| `scripts/build.ps1` / `scripts/DigitalLogic.cmd` | 构建测试打包 / 启动分发版或本地 Release 版 |
| `examples/` | 项目和可移植元件数据；JSON 不允许代码注释，说明放在文档内 |
| `resources/` / `third_party/` | 图标及第三方 JSON 库；保留原始数据、版权与许可，不给二进制或上游生成数据插入注释 |

## 必须维持的约定

- **ID 与显示名分离**：`Part.id` 和接口 `Port.id` 决定连接，`label` 只负责显示。自定义接口引用定义内部的 Input/Output ID，导入时这两层 ID 都要一起重映射。
- **几何与电气分离**：`ports()` 决定端口，绘图读取它；网络只合并显式端点与同实例中的同名节点，不根据画面交叉推断连接。
- **定义与实例状态分离**：`Project` 保存结构及初值，`Simulator::Impl::Node` 保存各个展开实例的运行状态。共享定义不能共享寄存器或 RAM 数据。
- **同一时刻批处理**：事件批次的全部元件读取旧网络值，再统一发布变化。如果边求值边更新网络，时序采样会依赖元件排列顺序。
- **修改失败可回滚**：编辑器结构修改通过 `change()`；自定义元件核心接口先修改候选工程，校验后再替换原工程，避免半导入状态。
- **导出只读**：`Simulator::netlist()` 使用已经编译的连接映射；GUI 导出从已提交工程建立独立编译实例，不调用当前仿真器的 `reset()`、`step()`、`settle()` 或 `tick()`。
- **保存先写临时文件**：`writeAtomic()` 写入并检查临时文件后替换目标；导出或校验失败不应破坏已有文件。

本次已在所有自有 C++ 源码/头文件与构建脚本的职责边界、数据约定和关键流程补充注释。显而易见的赋值和循环不逐行复述；第三方库、原始图标和 JSON 示例由文档说明，保留原始格式。
