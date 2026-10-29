# PracticeEDA — 数字电路设计与仿真

C++17 / wxWidgets 桌面数字电路编辑器，当前版本 0.5，提供七类数字元件库。启动即进入数字电路设计界面。

个人发布仓库：[Corry-0/PracticeEDA](https://github.com/Corry-0/PracticeEDA)。

支持画布拖拽、60 种元件/注释、元件旋转、参数化逻辑门与选择器、1–32 位总线和任意位分组、独立时钟、可配置 RAM/ROM、子电路实例实时调试、自定义元件及跨工程复用、组合逻辑化简与自动生成电路、网表导出、`.circ` 导入、项目保存加载、PNG 导出、撤销重做、剪贴板与错误定位。

工具栏、分类树和常用元件栏已加入图标，复用了 `wxWidgets/samples` 中的资源，并补充数字逻辑符号。所有图标内嵌程序，支持高 DPI 缩放。

## 运行

直接启动 `build/Debug/PracticeEDA.exe`、`build/Release/PracticeEDA.exe` 或分发包 `dist/PracticeEDA/PracticeEDA.exe`。也可双击 `DigitalLogic.cmd`。旧的 `--logic` 启动参数仍可使用。

文件 → 打开半加器示例，可验证输入、逻辑门和真值表。更多示例：

- `examples/half-adder.logic.json`：半加器。
- `examples/bus-register.logic.json`：8 位总线、时钟和寄存器。
- `examples/hierarchical-adder.logic.json`：子电路封装与调用。
- `examples/defense-hierarchy.logic.json`：多层左右实例，独立寄存器与 RAM，可从模板创建。
- `examples/half-adder-physical.*`：实体芯片映射、标准网表及配套未布线 KiCad 板。
- `examples/circ-limited.circ`：外部电路 2.7 受限导入示例，含逻辑门、复用器、寄存器和未支持元件。
- `examples/drawn-half-adder.component.json`：带自定义符号外观的可运行半加器。
- `examples/half-adder.component.json`：可通过“文件 → 导入 → 自定义元件”复用的半加器。

工程使用 `.logic.json`，独立元件使用 `.component.json`；格式仍为 version 1，旧示例可读取。支持 Logisim 2.7.1 `.circ` 的[导入](docs/CIRC_IMPORT_CN.md)，无法映射的对象保留文字诊断。真值表支持最多 10 位输入，卡诺图支持最多 4 个变量；化简搜索达到预算时提供等价结果并注明未证明最少项。

## 新增功能

- **案例 → 实验案例**：选择半加器、全加器、8 位总线寄存器、4 位计数器、RAM 读写或层次化半加器，查看目的、步骤和预期结果；一键打开可编辑副本。说明窗口可与画布同时操作，Ctrl+S 另存工程，原始案例保留。案例管理集中在 `src/ExperimentCases.cpp`。
- **帮助 → 操作指南**：原画布旁的七条操作提示迁入独立窗口；保留“数字逻辑使用说明”。
- **真实实例调试**：双击子电路进入所点击的运行实例，面包屑与“返回上级”保留父工程的时间、寄存器及 RAM；“编辑定义（复位）”单独进入可修改模式。
- **元件与存储配置**：门输入数及逐脚反相、选择位数、总线位段、零/符号扩展、时钟高低节拍和相位、下降沿触发、模计数与装载、RAM/ROM 地址容量均可设置；存储器有十六进制初值编辑和实例运行内存查看。
- **组合分析闭环**：真值表输出可编辑为 0、1、设计不关心项；展示化简式、格雷码卡诺图和圈组，生成与/或/非或仅 NAND 的新子电路，并重新仿真逐行验证。
- **调试与交付**：按输出信号选择连续 CSV 日志；最近工程、空白/半加器模板、右键菜单、可选比例 PNG、全部定义批量 PNG。
- **文件操作分组**：“文件 → 导入”集中提供 `.circ`、自定义元件和真值表 CSV；“文件 → 导出”集中提供完整/批量 PNG、最近分析 CSV、逻辑网表 JSON、KiCad PCB 及自定义元件。顶部工具栏从左到右为新建/打开/保存、相邻的导入/导出下拉按钮、选择/导线及常用元件。

- **画布与属性交互**：全部内置元件采用边框端子，移除外伸引线；元件库图标、画布、放置预览和导出共用绘制。输入输出使用 28×28 主体和五种实时状态色；放置一次后自动回到选择模式，靠近引脚仍可直接布线。
- **局部智能布线**：新建导线按引脚方向出线并绕开主体，不同网络保持走线间距；移动或新放元件后只调整受影响的导线。支持 `Ctrl+Shift+R` 重排选中导线、选中后拖动线段、`Alt+单击` / `Tab` 切换重叠候选，以及交叉断口。打开旧工程保持原布局，纯走线调整保留仿真状态。详见 [布线说明](docs/ROUTING_CN.md)。
- **左侧工作区**：实例层次按内容收缩，元件库下方为可调高度的“属性 / 值”表格；支持方向下拉、逐输入反相和放置前的工具参数。子电路选择框旁的加号和删除按钮分别对应 `Ctrl+Shift+N`、`Ctrl+Shift+Delete`，主电路和被引用的定义受删除保护。
- **画布 ERC 提示**：取消固定诊断面板和聚合气泡；每个问题对象外围显示一枚小图标，红色圆形叉号表示错误，黄色三角感叹号表示警告，同时保留对象颜色提示。图标自动避让主体、标签、引脚和导线，悬停查看规则及修复建议，点击打开该对象的问题列表；单击列表项缩放定位并高亮。也可用 `Ctrl+Shift+E` 打开列表；不显示迷你地图热点。现有规则覆盖范围见 [ERC 自查报告](docs/audits/2026-10-22-erc/自查报告.md)。

- **图形工具与元件外观**：直线、曲线、矩形、椭圆、圆，支持线宽、填充、控制点调整、复制及撤销。选择“工具 → 编辑元件外观”设计实例符号，选择“工具 → 返回电路编辑”继续内部逻辑。详见[操作说明](docs/CUSTOM_COMPONENTS_NETLIST_CN.md#图形与元件外观)。


- **文件 → 导入 → Logisim 2.7 电路**：导入支持范围内的元件、参数、连线和层次实例。导入后使用 `.logic.json` 保存，详见 [中文导入说明](docs/CIRC_IMPORT_CN.md)。“文件 → 打开”和命令行仍可读取 `.circ`。

- **项目 → 新建自定义元件**：填写元件名称与输入/输出接口（如 `DATA:8`），然后在画布实现内部逻辑。切回 `main`，从“自定义元件 / 子电路”库放置实例；双击实例观察实际运行状态，修改结构请进入定义编辑。自定义符号接口坐标按稳定 ID 保存，可单独安排位置。
- **文件 → 导出 → 当前电路为自定义元件 / 文件 → 导入 → 自定义元件**：保存独立 `.component.json`，自动携带嵌套依赖。导入不会覆盖已有定义，重名自动添加后缀，支持撤销重做。
- **文件 → 导出 → 逻辑网表 JSON**：保存为 `circuit.json`，包含当前电路及嵌套实例的连接、位宽、器件参数、初值、存储器数据、网络标签和连接别名，供程序读取和分析。导出保留运行中的仿真状态；继续编辑设计请保存 `.logic.json` 工程。
- **文件 → 导出 → KiCad PCB**：将支持的单比特组合门映射为真实 74HC DIP 芯片，保存 `circuit.kicad_pcb`，同时生成 `circuit.net` 和 `circuit.mapping.json`。在 KiCad PCB 编辑器打开板文件继续布局和布线。实际加载及范围见 [PCB 交换说明](docs/PCB_EXCHANGE_CN.md)。

具体格式、使用边界与示例见 [自定义元件与网表说明](docs/CUSTOM_COMPONENTS_NETLIST_CN.md)。

## 构建和验证

需要 Visual Studio 2022 C++ 桌面开发工具、CMake 3.20+、MSVC x64 wxWidgets 3.2+。请先准备 wxWidgets 静态库，可放在仓库同级的 `wxWidgets/` 目录，或通过 `-WxRoot` 指定安装目录。构建产物与本地依赖不包含在源码仓库中。

```sh
git clone https://github.com/Corry-0/PracticeEDA.git
cd PracticeEDA
```

```powershell
.\scripts\build.ps1 -Configuration Debug -GuiTest
.\scripts\build.ps1 -Configuration Release -GuiTest -Package
```

Visual Studio 直接打开 `build/PracticeEDA.sln`，选择 Debug/x64 或 Release/x64，按 F5。核心测试也可脱离 GUI 构建：

```sh
cmake -S . -B build-core -DEDA_BUILD_GUI=OFF
cmake --build build-core --config Debug
ctest --test-dir build-core -C Debug --output-on-failure
```

## 结构

| 位置 | 内容 |
| --- | --- |
| `include/Digital.h`、`src/Digital.cpp` | 数字电路、仿真、子电路、文件格式、历史与分析 |
| `src/CircImport.cpp`、`tests/circ_import_tests.cpp` | 外部电路 2.7 XML 受限转换、原生校验复用及导入回归 |
| `include/LogicAnalysis.h`、`src/LogicAnalysis.cpp` | 布尔化简、卡诺图、CSV 分析表与电路生成/反向验证 |
| `include/PcbExport.h`、`src/PcbExport.cpp` | 受限真实芯片/封装映射，标准网表与配套 PCB |
| `src/CustomComponents.cpp` | 自定义接口创建、依赖打包、导入重命名与 ID 重映射 |
| `include/LogicSupport.h`、`src/LogicSupport.cpp` | 画布几何、导线基础类型、文件读写 |
| `include/WxSupport.h` | wxWidgets 字符和路径转换 |
| `include/DigitalEditor.h`、`src/DigitalEditor.cpp` | 数字编辑器界面、交互与 GUI 检查 |
| `src/LogicEditorTools.cpp` | 初值/运行内存、引脚布局、持续日志及批量图片窗口 |
| `src/LogicDiagnostics.cpp` | ERC 颜色提示、外围警示图标避让、悬停详情与按需问题窗口 |
| `include/LogicRouting.h`、`src/LogicRouting.cpp` | 正交避障、不同网络间距、局部重排和交叉计算 |
| `src/LogicWireTools.cpp`、`tests/routing_tests.cpp` | 画布布线交互、线段拖动、候选切换和路由回归 |
| `include/LogicIcons.h`、`src/LogicIcons.cpp` | 内嵌示例资源、数字符号图标 |
| `src/App.cpp` | 唯一入口，直接创建数字编辑器 |
| `resources/wxwidgets/` | 从 wxWidgets samples 复制的原始图标数据 |
| `tests/digital_tests.cpp` | 数字逻辑及基础功能回归 |
| `tests/advanced_core_tests.cpp`、`tests/analysis_tests.cpp` | 参数化元件、双向开关/弱驱动、实例内存与组合分析闭环 |
| `tests/component_netlist_tests.cpp`、`tests/verify_netlists.py` | 自定义元件、仿真状态保护、网表结构与逐位连接比对 |

使用详情见 [数字逻辑使用说明](docs/DIGITAL_LOGIC_CN.md)，编译见 [Visual Studio 指南](docs/VISUAL_STUDIO_CN.md)，资源来源与许可见 [图标说明](resources/ICONS.md)。

源码维护入口见 [代码阅读指南](docs/CODE_GUIDE_CN.md)，本次验证记录见 [测试报告](docs/TEST_REPORT_CN.md)。

## 四人答辩材料

材料已按 2026-10-08 当前源码和实际 Debug / Release 回归同步，重点展示真实多层实例调试、组合综合与文件交换；25 页，常人元、闫浩楠、杨曙启、黄歆贻四人主讲，18 分钟讲解加 2 分钟机动。

- [材料入口与演示说明](docs/defense/00_阅读与演示说明.md)
- [源码详细讲解](docs/defense/01_源码详细讲解.md)
- [四人分工与排练](docs/defense/02_四人分工与排练.md)
- [逐页答辩讲稿](docs/defense/03_答辩逐页讲稿.md)
- [方案对比与迭代证据](docs/defense/04_方案对比与迭代证据.md)
- [当前进度与现场演示清单](docs/defense/05_当前进度与现场演示.md)
- [25 页网页展示文件](docs/defense/ppt/index.html)：下载或克隆后用浏览器打开，支持离线展示、交接页与演讲者备注。
- [可编辑 PPTX](docs/defense/ppt/PracticeEDA_四人答辩_2026-10-08_正式版.pptx)：同一内容源，25 页原生文字/表格，每页含备注。
- [整套材料 ZIP](docs/PracticeEDA_四人答辩材料.zip)

## 项目来源

本仓库保留 [LEVE1012/ElectronicComponent_Practice-](https://github.com/LEVE1012/ElectronicComponent_Practice-) 的原型提交历史，在此基础上整理和扩展为当前 PracticeEDA。历史提交作者与第三方资源署名保持原样；第三方许可文本见 `third_party/`，图标来源见 [资源说明](resources/ICONS.md)。

当前以数字逻辑为主，PCB 仅提供上述受限实体映射，板未布线；不含模拟电路求解和制造验收。分发包静态链接 wxWidgets，Windows 目标机器仍需 Microsoft Visual C++ x64 运行库。
