# PCB 实体映射与外部加载

本功能为课程演示提供真实引脚和封装的组合逻辑交换。逻辑连接报告仍可单独导出；实体映射另有明确范围，不能把 Register、RAM 的抽象端口直接当作芯片焊盘。

## 导出与使用

打开半加器，选择“文件 → 导出 → KiCad PCB”，保存 `circuit.kicad_pcb`，程序同时生成标准 S-expression 网表 `circuit.net` 和引脚映射 `circuit.mapping.json`。顶部工具栏的“导出”下拉菜单提供相同入口。导出前先检查实体器件是否支持，配套文件已存在时提示是否覆盖。

用 KiCad 10 PCB 编辑器打开 `.kicad_pcb`。板文件已嵌入 KiCad 原库封装的焊盘、钻孔、丝印及边界，打开时无需从本机库重新加载封装。`.net` 为实体连接网表，`.mapping.json` 记录逻辑实例、芯片单元及实际焊盘的对应关系；三个文件使用同一文件名前缀。“PCB 网表”菜单可单独导出 KiCad 或 OrCAD PCB II 格式，映射一致。示例见 `examples/half-adder-physical.*`。

也可以在 KiCad 的空白板上用“导入网表”读取 KiCad 格式的 `.net`。网表和配套板使用相同的元件 ID，KiCad 可用默认的唯一 ID 模式匹配封装。连接器的 ID 取自端口路径，芯片的 ID 取自该封装中第一个门的实例路径。向已有板导入时，“删除网表中没有元件的封装”默认未勾选，旧板上的无对应封装会保留。

板上元件先按网格放置，没有铜线和过孔布线。正式设计需继续布局、布线、电源/信号检查和 DRC，不能直接宣称制造通过。

## 映射范围

仅接受单比特、默认二输入且无逐脚反相的 AND / NAND / OR / XOR，以及 NOT。嵌套子电路先展开；Input/Output映射为信号/地两针连接器，常量0/1映射GND/VCC。总线、时序器件、NOR/XNOR以及没有核对实体引脚的配置明确拒绝。

|逻辑元件|实体芯片|单片门数|物理引脚|
|---|---|---|---|
|AND / NAND / OR / XOR|74HC08 / 74HC00 / 74HC32 / 74HC86|4|A：1、2 → 3；B：4、5 → 6；C：9、10 → 8；D：12、13 → 11|
|NOT|74HC04|6|A：1 → 2；B：3 → 4；C：5 → 6；D：9 → 8；E：11 → 10；F：13 → 12|

同型号的逻辑门按电路顺序分配到同一颗芯片的不同单元，用完单元后再分配下一颗。例如五个 AND 门对应两颗 74HC08，第一颗使用 A～D，第二颗使用 A。映射清单的 `units` 记录每个单元对应的实例路径和引脚。每个顶层输入输出端口对应一个信号连接器。

每颗芯片的 14 脚接 VCC、7 脚接 GND，未用门的输入接地、输出悬空。VCC 和 GND 作为网络导出，程序不生成独立供电排针或去耦电容。芯片使用 `Package_DIP:DIP-14_W7.62mm`。74HC08 和 74HC32 参照 KiCad 标准库中同引脚的 74LS08、74LS32 符号，物料值分别是 74HC08、74HC32。封装来源见 [KiCad 标准封装说明](../third_party/kicad-footprints/README.md)。

引脚依据 TI 官方器件资料核对：[SN74HC08](https://www.ti.com/lit/ds/symlink/sn74hc08.pdf)、[SN74HC00](https://www.ti.com/lit/ds/symlink/sn74hc00.pdf)、[SN74HC32](https://www.ti.com/lit/ds/symlink/sn74hc32.pdf)、[SN74HC86](https://www.ti.com/lit/ds/symlink/sn74hc86.pdf)、[SN74HC04](https://www.ti.com/lit/ds/symlink/sn74hc04.pdf)。器件采购型号与实际封装仍需在后续设计中核对。

## 已执行的外部验证

使用 KiCad CLI 10.0.7 加载两个用例的 `.kicad_pcb` 并导出 IPC-D-356。`tests/verify_pcb_exchange.py` 对照原库封装几何，并核对板上的元件 ID 和焊盘连接。

- 半加器包含 2 颗芯片和 4 个连接器，共 6 个网络、30 个已分配焊盘。
- 多门用例将 5 个 AND 门和 7 个 NOT 门分别装入两颗 74HC08 和两颗 74HC04，并映射 14 个信号连接器。

## 实现与复现

`src/PcbExport.cpp` 从独立编译的逻辑网络生成两种实体网表及配套板文件。`tests/pcb_export_tests.cpp` 检查芯片单元分配、封装数量、焊盘接线、OrCAD 网表和导出前后的电路状态。配置 `EDA_KICAD_CLI` 且系统安装 Python 后，CTest 会注册 KiCad 加载测试。

```powershell
cmake -S . -B build -DEDA_KICAD_CLI="你的KiCad目录/bin/kicad-cli.exe"
cmake --build build --config Release
ctest --test-dir build -C Release -R "pcb_export_tests|kicad_pcb_validation" --output-on-failure
```

KiCad只用于外部验证，不是PracticeEDA的运行依赖，也不捆绑进分发包。
