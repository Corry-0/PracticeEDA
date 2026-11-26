# Logisim 2.7.1 `.circ` 导入

## 使用方法

1. 在 **文件 → 导入 → Logisim 2.7 电路** 中选择 `.circ`；顶部“导入”下拉按钮提供相同入口，也可通过“文件 → 打开”或命令行 `PracticeEDA.exe example.circ` 读取。目标是经典 Logisim 2.7.1、工程版本 `1.0` 的 XML，包含本地中文发行版；Logisim-evolution 和外部 JAR 库不属于本次兼容范围。
2. 导入后打开 `<main>` 指定的电路，其他电路定义可从电路下拉框切换。点击画布 ERC 图标或按 `Ctrl+Shift+E` 查看诊断，单击列表项定位对象。
3. 无法映射的元件变成带有“外部电路 导入：”前缀的普通文字占位，保留名称、来源库、原坐标、原属性及失败原因。已支持的元件和导线仍可编辑、连接和仿真。可手动替换占位，修复后删除对应文字；支持撤销/重做。
4. **保存** 会要求选择新的文件路径，继续保存为 `PracticeEDA.logic` **version 1** 的 `.logic.json`。重新打开、复制或保存原生工程后，文字占位及其诊断仍保留。

导入失败（XML 损坏、无效坐标、主电路不存在、重复电路名、规模超限等）不会替换当前工程。单个元件不支持或参数不合法则保留占位，不中止整个导入。`.logic.json` 的加载和保存规则不变。

## 0.5 映射范围

使用 `<lib name="…" desc="#Wiring">` 等声明解析库身份，**不依赖固定库编号**。同名的外部库元件不会被当成内置元件。

| Logisim 库 | 支持的元件和主要限制 |
| --- | --- |
| Wiring | Pin 输入/输出 1–32 位、Constant、Power、Ground、Clock 独立高/低时长、Probe、同作用域 Tunnel、零/符号 Bit Extender、可判定单向用途的 Splitter/Joiner |
| Gates | AND/OR/NAND/NOR/XOR/XNOR 的 2–32 输入、逐脚反相和 XOR 奇偶/恰一语义；NOT、Buffer、Controlled Buffer。开集/开漏输出等无法保持的模式明确诊断 |
| Plexers | 参数化 MUX/DEMUX/Decoder，选择位数 1–8；Priority Encoder 的无输入零输出模式；不支持的浮空未选输出、控制模式明确诊断 |
| Memory | D/T/JK/SR、Register、Counter 的支持边沿与控制；ROM/RAM 地址 1–16 位、数据 1–32 位。RAM 以独立读写端口和支持的控制方式交换 |
| Arithmetic | Add/Subtract/Multiply/Divide/Negate、无符号 Compare、单输入 BitAdder、BitFinder；额外进借位与控制端必须满足可表示条件 |
| Input/Output | 瞬时 Button、LED、4 位 Hex、独立 SevenSegment、按行列选择的 LEDMatrix、支持范围内的 Joystick |
| 子电路 | 多层定义、实例、内部输入输出稳定关联、标准外观与可表示的自定义接口布局 |

ROM 的 `contents` 支持 `addr/data: 地址位宽 数据位宽` 头、十六进制字、十进制重复计数（例如 `4*ff`）及 `#` 行注释；省略内容为全零。经典 RAM 不在工程中保存运行内存，直接导入使用零初值；WE、时钟、数据和地址端应显式接线。

Splitter 在 Logisim 中是双向线路元件，而原生分线/合线器有方向。导入器依据真实连接与驱动判断拆分或合并；无法可靠判定的复杂双向混合用途会诊断，不假定信号方向。未列出的器件/模式不代表支持，特别是外部库、evolution 属性与专业 HDL。对于已经接线却无法表示的辅助控制或额外输出，不会静默丢弃该连接。

## 连线及仿真边界

- 按 外部电路 原始引脚坐标转换成现有模型的显式端点和节点；支持端点相接、T 形接点、重叠线段、元件直接接触和位于导线中间的引脚。没有端点或引脚的单纯交叉不连接。
- 悬空线段保留。零长度和斜向导线跳过并诊断；不根据“看起来接近”猜测连接。未知元件不会提供虚构的引脚。
- 导入布局按原坐标换算到 PracticeEDA；四方向体现在原生 rotation 与端口位置，不保证像素级复原。直接导入的 Logisim Counter 使用 `logisimCounter` 标记保留 CT/LD 的递增、递减和装载组合含义。
- 模型使用 PracticeEDA 的初始值、事件延迟、时钟操作和 X/Z 行为；Pin 的交互三态输入、外部电路 的悬空输入处理、运行状态、字体/外观与工程仿真选项不复刻。每个电路均保留一条总提示，导入成功不代表与 外部电路 完全等价。
- 原有仿真诊断继续报告悬空、位宽冲突、多驱动等问题，导入提示不会因仿真重建而消失。
- XML 不加载外部资源，拒绝 DTD、实体声明和 NUL。复用原生导入校验：文件不超过 32 MB、电路最多 128 个、每电路元件最多 10,000、节点/导线各最多 40,000；转换新增的节点、端口引线和提示也计入限制。

## 示例与验证

打开 `examples/circ-limited.circ`：A/B 经过 XOR、AND，再由 Select 选择结果，时钟驱动寄存器输出 Q。令 A=1、B=0、Select=0，第一个上升沿后 Q=1；切换 Select=1 后，下一个上升沿捕获 Carry=0。该文件保留旧“受限导入”名称；0.5 已扩展算术映射，不能再将它的 Adder 视为必然不支持的演示对象。

新增独立测试 `circ_import_tests`，覆盖门真值表和四方向/尺寸、连接拓扑、总线探针、Tunnel、存储器时序与 ROM 数据、部分导入、坏文件、原生保存往返及占位撤销。运行：

```powershell
cmake -S . -B build-core -DEDA_BUILD_GUI=OFF
cmake --build build-core --config Debug
ctest --test-dir build-core -C Debug --output-on-failure
```

实现位于 `src/CircImport.cpp`，公开导入接口声明于 `include/Digital.h`；导入通过原生校验。XML 解析使用随仓库提供的 TinyXML2 10.0.0（zlib 许可，见 `third_party/LICENSE-tinyxml2.txt`），无需联网下载或安装额外运行库。经典引脚几何和默认属性依据本地 Logisim 2.7.1 安装包内附 Java 源码核对。
