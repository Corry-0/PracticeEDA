# 外部电路 2.7 `.circ` 受限导入

## 使用方法

1. 在 **文件 → 打开** 中选择 `.circ`（也支持命令行 `PracticeEDA.exe example.circ`）。文件必须为 外部电路 2.7、工程版本 `1.0` 的 XML；外部电路-evolution 等版本暂不支持。
2. 导入后打开 `<main>` 指定的电路，其他电路定义可从电路下拉框切换。查看右下方连接诊断面板，双击提示定位对象。
3. 无法映射的元件变成带有“外部电路 导入：”前缀的普通文字占位，保留名称、来源库、原坐标、原属性及失败原因。已支持的元件和导线仍可编辑、连接和仿真。可手动替换占位，修复后删除对应文字；支持撤销/重做。
4. **保存** 会要求选择新的文件路径。工程仍保存为 `PracticeEDA.logic` **version 1** 的 `.logic.json`，不写回 `.circ`。即使手动选择 `.circ` 名称，也会追加 `.logic.json`。重新打开、复制或保存工程后，文字占位及其诊断仍保留。

导入失败（XML 损坏、无效坐标、主电路不存在、重复电路名、规模超限等）不会替换当前工程。单个元件不支持或参数不合法则保留占位，不中止整个导入。`.logic.json` 的加载和保存规则不变。

## 首版映射范围

使用 `<lib name="…" desc="#Wiring">` 等声明解析库身份，**不依赖固定库编号**。同名的外部库元件不会被当成内置元件。

| 外部电路 库 | 支持的元件和限制 |
| --- | --- |
| Wiring | Pin → 输入/输出（1–32 位）；Constant、Power、Ground；Clock（单比特，高低各 1 tick）；Probe（由连接推断位宽）；同电路同名同宽 Tunnel；Bit Extender（1 位输入、零扩展） |
| Gates | 双输入 AND / OR / NAND / NOR / XOR / XNOR；NOT、Buffer、Controlled Buffer；1–32 位，四方向，标准尺寸；不支持输入反相、开集/开漏输出。外部电路 常规门缺省 **5 输入**，需要将输入数设为 2 |
| Plexers | 2:1 Multiplexer、1:2 Demultiplexer、2:4 Decoder；四方向及两种选择端位置；enable=false 或使能脚未连接；未选输出为零（不支持 tristate=true） |
| Memory | D / T / J-K / S-R Flip-Flop、Register：上升沿，辅助控制脚未连接，触发器反相输出未连接；ROM：8 位地址、1–32 位数据、最多 256 字；RAM：同样的位宽范围，bus=separate 独立读写端口，片选/输出使能/清零脚未连接 |

ROM 的 `contents` 支持 `addr/data: 8 数据位宽` 头、十六进制字、十进制重复计数（例如 `4*ff`）及 `#` 行注释；省略内容为全零。RAM 使用现有模型的零初值，不恢复运行中的内存状态。RAM 的 WE、时钟、数据和地址端请显式接线。

Splitter 的双向分线、Priority Encoder、Counter、Shift Register、双向/异步 RAM、外部库、Arithmetic、I/O、自定义子电路实例等暂不映射，会列入诊断。子电路**定义**分别保留，但实例不会自动展开或猜测自定义外观引脚。

## 连线及仿真边界

- 按 外部电路 原始引脚坐标转换成现有模型的显式端点和节点；支持端点相接、T 形接点、重叠线段、元件直接接触和位于导线中间的引脚。没有端点或引脚的单纯交叉不连接。
- 悬空线段保留。零长度和斜向导线跳过并诊断；不根据“看起来接近”猜测连接。未知元件不会提供虚构的引脚。
- 布局按四倍缩放并适配 PracticeEDA 固定形状的端口，不保证像素级复原；旋转仅用于还原原始电气端点，导入后的符号采用 PracticeEDA 的标准朝向。
- 模型使用 PracticeEDA 的初始值、事件延迟、时钟操作和 X/Z 行为；Pin 的交互三态输入、外部电路 的悬空输入处理、运行状态、字体/外观与工程仿真选项不复刻。每个电路均保留一条总提示，导入成功不代表与 外部电路 完全等价。
- 原有仿真诊断继续报告悬空、位宽冲突、多驱动等问题，导入提示不会因仿真重建而消失。
- XML 不加载外部资源，拒绝 DTD、实体声明和 NUL。复用原生导入校验：文件不超过 32 MB、电路最多 128 个、每电路元件最多 10,000、节点/导线各最多 40,000；转换新增的节点、端口引线和提示也计入限制。

## 示例与验证

打开 `examples/circ-limited.circ`：A/B 经过 XOR、AND，再由 Select 选择结果，时钟驱动寄存器输出 Q。令 A=1、B=0、Select=0，第一个上升沿后 Q=1；切换 Select=1 后，下一个上升沿捕获 Carry=0。图中有一个故意保留的 Arithmetic/Adder，导入后用于演示占位和定位诊断。

新增独立测试 `circ_import_tests`，覆盖门真值表和四方向/尺寸、连接拓扑、总线探针、Tunnel、存储器时序与 ROM 数据、部分导入、坏文件、原生保存往返及占位撤销。运行：

```powershell
cmake -S . -B build-core -DEDA_BUILD_GUI=OFF
cmake --build build-core --config Debug
ctest --test-dir build-core -C Debug --output-on-failure
```

实现位于 `src/CircImport.cpp`，末尾通过 `deserialize(serialize(project))` 复用原生模型和导入校验。XML 解析使用随仓库提供的 TinyXML2 10.0.0（zlib 许可，见 `third_party/LICENSE-tinyxml2.txt`）；无需联网下载或安装额外运行库。引脚几何和默认属性依据 外部电路 2.7.1 安装包内附的 Java 源码核对。
