# PracticeEDA — 数字电路设计与仿真

C++17 / wxWidgets 桌面数字电路编辑器，提供七类数字元件库。启动即进入数字电路设计界面。

个人发布仓库：[Corry-0/PracticeEDA](https://github.com/Corry-0/PracticeEDA)。

支持画布拖拽、44 种元件/注释、1–32 位总线、节点连接、事件驱动仿真、时钟与触发器、RAM/ROM、子电路封装调用、工程师自定义元件及跨工程复用、网表导出、项目保存加载、真值表和表达式分析、PNG 导出、撤销重做、剪贴板与错误定位。

工具栏、分类树和常用元件栏已加入图标，复用了 `wxWidgets/samples` 中的资源，并补充数字逻辑符号。所有图标内嵌程序，支持高 DPI 缩放。

## 运行

直接启动 `build/Debug/PracticeEDA.exe`、`build/Release/PracticeEDA.exe` 或分发包 `dist/PracticeEDA/PracticeEDA.exe`。也可双击 `DigitalLogic.cmd`。旧的 `--logic` 启动参数仍可使用。

文件 → 打开半加器示例，可验证输入、逻辑门和真值表。更多示例：

- `examples/half-adder.logic.json`：半加器。
- `examples/bus-register.logic.json`：8 位总线、时钟和寄存器。
- `examples/hierarchical-adder.logic.json`：子电路封装与调用。
- `examples/circ-limited.circ`：外部电路 2.7 受限导入示例，含逻辑门、复用器、寄存器和未支持元件。
- `examples/drawn-half-adder.component.json`：带自定义符号外观的可运行半加器。
- `examples/half-adder.component.json`：可通过“项目 → 导入自定义元件”复用的半加器。

工程使用 `.logic.json`，独立元件使用 `.component.json`；支持 外部电路 2.7 `.circ` 的[受限导入](docs/CIRC_IMPORT_CN.md)，无法映射的组件会保留为可编辑文字并列入诊断，保存格式不变。真值表最多 10 位输入，表达式输出为未最小化的标准与或式。

## 新增功能

- **画布与属性交互**：输入输出使用更小的符号和五种实时状态色；右侧提供可直接编辑的属性表格、连接错误/警告列表及双击定位；网格点更清晰。

- **图形工具与元件外观**：直线、曲线、矩形、椭圆、圆，支持线宽、填充、控制点调整、复制及撤销。选择“工具 → 编辑元件外观”设计实例符号，选择“工具 → 返回电路编辑”继续内部逻辑。详见[操作说明](docs/CUSTOM_COMPONENTS_NETLIST_CN.md#图形与元件外观)。


- **文件 → 打开 → 外部电路 2.7 受限导入**：支持部分 Wiring、Gates、Plexers、Memory 和坐标连线，导入后另存为 `.logic.json`。具体支持范围见 [中文导入说明](docs/CIRC_IMPORT_CN.md)。

- **项目 → 新建自定义元件**：填写元件名称与输入/输出接口（如 `DATA:8`），然后在画布实现内部逻辑。切回 `main`，从“自定义元件 / 子电路”库放置实例；双击实例可编辑定义。
- **项目 → 保存当前电路为元件 / 导入自定义元件**：保存独立 `.component.json`，自动携带嵌套依赖。导入不会覆盖已有定义，重名自动添加后缀，支持撤销重做。
- **文件 → 导出网表**：导出当前电路及嵌套实例。`.net` 为 KiCad S-expression 逻辑连接网表，总线按位展开；`.net.json` 保留位宽、实例路径、接口、初值、存储器数据、网络标签和连接别名。导出不会改变运行中的仿真状态。

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
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## 结构

| 位置 | 内容 |
| --- | --- |
| `include/Digital.h`、`src/Digital.cpp` | 数字电路、仿真、子电路、文件格式、历史与分析 |
| `src/CircImport.cpp`、`tests/circ_import_tests.cpp` | 外部电路 2.7 XML 受限转换、原生校验复用及导入回归 |
| `src/CustomComponents.cpp` | 自定义接口创建、依赖打包、导入重命名与 ID 重映射 |
| `include/LogicSupport.h`、`src/LogicSupport.cpp` | 画布几何、导线基础类型、文件读写 |
| `include/WxSupport.h` | wxWidgets 字符和路径转换 |
| `include/DigitalEditor.h`、`src/DigitalEditor.cpp` | 数字编辑器界面、交互与 GUI 检查 |
| `include/LogicIcons.h`、`src/LogicIcons.cpp` | 内嵌示例资源、数字符号图标 |
| `src/App.cpp` | 唯一入口，直接创建数字编辑器 |
| `resources/wxwidgets/` | 从 wxWidgets samples 复制的原始图标数据 |
| `tests/digital_tests.cpp` | 数字逻辑及基础功能回归 |
| `tests/component_netlist_tests.cpp`、`tests/verify_netlists.py` | 自定义元件、仿真状态保护、网表结构与逐位连接比对 |

使用详情见 [数字逻辑使用说明](docs/DIGITAL_LOGIC_CN.md)，编译见 [Visual Studio 指南](docs/VISUAL_STUDIO_CN.md)，资源来源与许可见 [图标说明](resources/ICONS.md)。

源码维护入口见 [代码阅读指南](docs/CODE_GUIDE_CN.md)，本次验证记录见 [测试报告](docs/TEST_REPORT_CN.md)。

## 四人答辩材料

- [材料入口与演示说明](docs/defense/00_阅读与演示说明.md)
- [源码详细讲解](docs/defense/01_源码详细讲解.md)
- [四人分工与排练](docs/defense/02_四人分工与排练.md)
- [逐页答辩讲稿](docs/defense/03_答辩逐页讲稿.md)
- [方案对比与迭代证据](docs/defense/04_方案对比与迭代证据.md)
- [25 页网页展示文件](docs/defense/ppt/index.html)：下载或克隆后用浏览器打开，支持离线展示、交接页与演讲者备注。
- [整套材料 ZIP](docs/PracticeEDA_四人答辩材料.zip)

## 项目来源

本仓库保留 [LEVE1012/ElectronicComponent_Practice-](https://github.com/LEVE1012/ElectronicComponent_Practice-) 的原型提交历史，在此基础上整理和扩展为当前 PracticeEDA。历史提交作者与第三方资源署名保持原样；第三方许可文本见 `third_party/`，图标来源见 [资源说明](resources/ICONS.md)。

旧模拟电路、PCB、直流分析、制造导出、对应示例和原型代码已移除。分发包静态链接 wxWidgets，Windows 目标机器仍需 Microsoft Visual C++ x64 运行库。
