# PracticeEDA 0.5 功能与回归测试报告

日期：2026-10-08。环境：Windows、Visual Studio 2022 x64、C++17、wxWidgets静态库；外部验证使用本地经典Logisim 2.7.1和KiCad CLI 10.0.7。所有下列结果均来自本日实际执行，原始日志保存在docs/defense/evidence。

|构建配置|CTest|窗口集成|结果|
|---|---|---|---|
|Debug桌面版|11/11通过|四组本次GUI标志通过|通过|
|Release桌面版|11/11通过|四组本次GUI标志通过|通过|

## 核心与格式测试

|CTest目标|本次输出|覆盖重点|
|---|---|---|
|digital_logic_tests|209项检查|原有门、总线、事件、时序、存储、层次、文件及旧示例回归|
|component_netlist_tests|167项检查|组件依赖、重名、ID、失败回滚、只读导出、逐位连接|
|circ_import_tests|1066项检查|经典元件映射、方向、拓扑、控制、ROM/RAM、诊断与往返|
|shape_tests|28项检查|图形几何、校验、复制、外观与持久化|
|analysis_tests|3688项检查|16种二输入函数、48组随机四输入函数、两种综合、不关心项、常量、CSV引用/往返|
|advanced_core_tests|103项检查|参数门、选择器、位映射、时钟、边沿、计数、实例RAM、弱驱动/双向开关、交互、持续日志|
|circ_exchange_tests|211项检查|导出再导入、算术、总线、多层接口、寄存器/触发器/移位/RAM序列、导出限制|
|pcb_export_tests|属性检查通过|芯片映射、真实引脚、电源/去耦、只读导出、不支持总线拒绝|
|netlist_format_tests|65组文件比对|独立Python解析S-expression，逐网络/逐位与JSON比对|
|logisim_exchange_validation|33/33样例|实际Java加载导出.circ，核对组合和时序输出|
|kicad_pcb_validation|9元件、6网络、36已分配焊盘|KiCad加载配套PCB导出IPC-D-356，与标准.net及映射逐焊盘核对|

测试目标数、运行检查数、文件样例数为不同口径，不相加声称覆盖率。CMake检测到Python后注册独立文件测试；检测到本地Logisim JAR/Java后注册外部交换测试；EDA_KICAD_CLI显式指定后注册实际PCB验证。缺少外部工具时这些检查不会运行，不能照抄本机11/11结论。

## GUI集成

LOGIC-PASS覆盖启动、图标/符号、拖拽、布线、分支、取消、悬停隔离、剪贴板、撤销、属性、保存/加载、Unicode路径、真值表、图片、组件导入与源文件保护。

SHAPES-PASS覆盖绘图阶段、控制点、取消、图层、剪贴板、撤销和持久化。EDITOR-UI-PASS覆盖紧凑I/O、状态色、表格校验/回退、保持运行寄存器、连接错误/警告与定位。

HIERARCHY-UI-PASS在真实编辑器中推进时钟，为left/inner与right/inner写入不同寄存器及RAM值；运行计时器开启时进入多层，断言同一个Simulator、time、ticks保持，暂停后切换实例仍为0x12/0x34；父驱动Input和结构只读；内部Button只改变本实例，serialize工程定义不变。正式画布图保存在ppt/images，两张图分别来自左右实例，不称为完整窗口截图。

## 外部结果与已修复问题

Logisim样例包含四向多输入门、MUX/编解码、逆序拆合、算术/位运算、ROM/RAM初值、计数器序列、Register/D/T/JK/SR/ShiftRegister/RAM序列和三层自定义接口。所有33组由实际Logisim输出与期望比较；验证设置headless及每文件20秒超时，不让模态错误窗口无限阻塞。

辅助Constant曾写裸0，经典数值解析报“属性值(0)对value无效”。已统一使用0x0及带前缀十六进制，重新生成和验证正式样例。BitSelector选择端位宽按分组数量推导；Splitter→Joiner链用迭代方向传播，均已回归。

PCB结果记录standardNetlistMatchesLoadedBoard=true、directNetlistGuiImportTested=false、routed=false。实际验证是加载配套.kicad_pcb，并从IPC-D-356确认标准.net的逐焊盘连接；未自动执行GUI直接导入.net，不宣称布线、DRC或制造验收通过。

## 复现

```powershell
.\scripts\build.ps1 -Configuration Debug -GuiTest
.\scripts\build.ps1 -Configuration Release -GuiTest -Package
```

可用cmake -D参数指定EDA_LOGISIM_JAR、EDA_LOGISIM_JAVA、EDA_KICAD_CLI。外部工具为验证依赖，不随程序分发。核心构建可使用EDA_BUILD_GUI=OFF；在Visual Studio多配置生成器下，构建与CTest都应指定同一配置。

原始产物：build/qa-features/{Debug,Release}、qa-circ-exchange/{Debug,Release}、qa-pcb/{Debug,Release}以及qa-debug/qa-release。对应精选日志和交换样例在答辩材料evidence与sample中。

## 验证边界

本次未自动点击系统原生文件选择器、实体打印机或全部参数对话框；GUI测试直接调用真实窗口处理路径，并非所有人工场景已穷举。演讲文件通过25页渲染/边界/字体/备注与离线网页检查，未在原生PowerPoint人工放映，也未连接实体投影仪。上述检查证明列明范围，不保证所有未来电路或全部Logisim配置均无问题。
