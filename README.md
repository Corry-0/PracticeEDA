# PracticeEDA — 数字电路设计与仿真

C++17 / wxWidgets 桌面数字电路编辑器，当前版本 0.5，提供七类数字元件库。启动即进入数字电路设计界面。

个人发布仓库：[Corry-0/PracticeEDA](https://github.com/Corry-0/PracticeEDA)。

支持画布拖拽、60 种元件/注释、元件旋转、参数化逻辑门与选择器、1–32 位总线和任意位分组、独立时钟、可配置 RAM/ROM、子电路实例实时调试、自定义元件及跨工程复用、组合逻辑化简与自动生成电路、网表与 `.circ` 交换、项目保存加载、PNG/打印、撤销重做、剪贴板与错误定位。

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
- `examples/half-adder.component.json`：可通过“项目 → 导入自定义元件”复用的半加器。

工程使用 `.logic.json`，独立元件使用 `.component.json`；格式仍为 version 1，旧示例可读取。支持 Logisim 2.7.1 `.circ` 的[导入与导出](docs/CIRC_IMPORT_CN.md)，无法映射的导入对象保留文字诊断；导出不能保持语义的器件会明确拒绝。真值表支持最多 10 位输入，卡诺图支持最多 4 个变量；化简搜索达到预算时提供等价结果并注明未证明最少项。

## 新增功能

- **帮助 → 操作指南**：原画布旁的七条操作提示迁入独立窗口；保留“数字逻辑使用说明”。
- **真实实例调试**：双击子电路进入所点击的运行实例，面包屑与“返回上级”保留父工程的时间、寄存器及 RAM；“编辑定义（复位）”单独进入可修改模式。
- **元件与存储配置**：门输入数及逐脚反相、选择位数、总线位段、零/符号扩展、时钟高低节拍和相位、下降沿触发、模计数与装载、RAM/ROM 地址容量均可设置；存储器有十六进制初值编辑和实例运行内存查看。
- **组合分析闭环**：真值表输出可编辑为 0、1、设计不关心项；展示化简式、格雷码卡诺图和圈组，生成与/或/非或仅 NAND 的新子电路，并重新仿真逐行验证。
- **调试与交付**：按输出信号选择连续 CSV 日志；最近工程、空白/半加器模板、右键菜单、可选比例 PNG、全部定义批量 PNG、打印预览和打印。

- **画布与属性交互**：输入输出使用更小的符号和五种实时状态色；右侧提供可直接编辑的属性表格、连接错误/警告列表及双击定位；网格点更清晰。

- **图形工具与元件外观**：直线、曲线、矩形、椭圆、圆，支持线宽、填充、控制点调整、复制及撤销。选择“工具 → 编辑元件外观”设计实例符号，选择“工具 → 返回电路编辑”继续内部逻辑。详见[操作说明](docs/CUSTOM_COMPONENTS_NETLIST_CN.md#图形与元件外观)。


- **文件 → 打开 / 导出 Logisim 2.7 `.circ`**：交换支持范围内的元件、参数、连线和层次实例。导出采用真实 Logisim 元件与 Tunnel 网络，会重新安排布局；输入交互值、相位及外观等差异在导出说明中列出，详见 [中文交换说明](docs/CIRC_IMPORT_CN.md)。

- **项目 → 新建自定义元件**：填写元件名称与输入/输出接口（如 `DATA:8`），然后在画布实现内部逻辑。切回 `main`，从“自定义元件 / 子电路”库放置实例；双击实例观察实际运行状态，修改结构请进入定义编辑。自定义符号接口坐标按稳定 ID 保存，可单独安排位置。
- **项目 → 保存当前电路为元件 / 导入自定义元件**：保存独立 `.component.json`，自动携带嵌套依赖。导入不会覆盖已有定义，重名自动添加后缀，支持撤销重做。
- **文件 → 导出网表**：导出当前电路及嵌套实例。`.net` 为 KiCad S-expression 逻辑连接网表，总线按位展开；`.net.json` 保留位宽、实例路径、接口、初值、存储器数据、网络标签和连接别名。导出不会改变运行中的仿真状态。
- **KiCad 实体映射网表**：另选第三种导出格式，将支持的单比特组合门映射为真实 74HC DIP 芯片，同时生成标准 `.net`、配套 `.kicad_pcb` 和引脚映射表。实际加载及范围见 [PCB 交换说明](docs/PCB_EXCHANGE_CN.md)。

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
| `include/CircExchange.h`、`src/CircExport.cpp` | 经典 Logisim 导出、真实元件几何、层次和语义差异说明 |
| `include/LogicAnalysis.h`、`src/LogicAnalysis.cpp` | 布尔化简、卡诺图、CSV 分析表与电路生成/反向验证 |
| `include/PcbExport.h`、`src/PcbExport.cpp` | 受限真实芯片/封装映射，标准网表与配套 PCB |
| `src/CustomComponents.cpp` | 自定义接口创建、依赖打包、导入重命名与 ID 重映射 |
| `include/LogicSupport.h`、`src/LogicSupport.cpp` | 画布几何、导线基础类型、文件读写 |
| `include/WxSupport.h` | wxWidgets 字符和路径转换 |
| `include/DigitalEditor.h`、`src/DigitalEditor.cpp` | 数字编辑器界面、交互与 GUI 检查 |
| `src/LogicEditorTools.cpp` | 初值/运行内存、引脚布局、持续日志、打印及批量图片窗口 |
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
