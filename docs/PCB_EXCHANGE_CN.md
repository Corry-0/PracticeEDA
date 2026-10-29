# PCB 实体映射与外部加载

本功能为课程演示提供真实引脚和封装的组合逻辑交换。逻辑连接报告仍可单独导出；实体映射另有明确范围，不能把 Register、RAM 的抽象端口直接当作芯片焊盘。

## 导出与使用

打开半加器，选择“文件 → 导出 → KiCad PCB”，保存 `circuit.kicad_pcb`，程序同时生成标准 S-expression 网表 `circuit.net` 和引脚映射 `circuit.mapping.json`。顶部工具栏的“导出”下拉菜单提供相同入口。导出前先检查实体器件是否支持，配套文件已存在时提示是否覆盖。

可直接用 KiCad PCB 编辑器打开 `.kicad_pcb` 板文件，无需依赖本机封装库才能显示已有焊盘。`.net` 为实体连接网表，`.mapping.json` 记录逻辑实例与实际焊盘的对应关系；三个文件使用同一文件名前缀。示例见 `examples/half-adder-physical.*`；答辩材料 `sample/pcb/` 还保留加载结果和独立网络比对证据。

板上元件先按网格放置，没有铜线和过孔布线。正式设计需继续布局、布线、电源/信号检查和 DRC，不能直接宣称制造通过。

## 映射范围

仅接受单比特、默认二输入且无逐脚反相的 AND / NAND / OR / XOR，以及 NOT。嵌套子电路先展开；Input/Output映射为信号/地两针连接器，常量0/1映射GND/VCC。总线、时序器件、NOR/XNOR以及没有核对实体引脚的配置明确拒绝。

|逻辑元件|实体芯片|第一单元引脚|电源|
|---|---|---|---|
|AND|74HC08|A=1、B=2、Y=3|VCC=14、GND=7|
|NAND|74HC00|A=1、B=2、Y=3|同上|
|OR|74HC32|A=1、B=2、Y=3|同上|
|XOR|74HC86|A=1、B=2、Y=3|同上|
|NOT|74HC04|A=1、Y=2|同上|

每个逻辑门单独使用一片 DIP-14，便于解释和核对；尚未做多门共用芯片打包。未用输入接地，未用输出不接；每片添加100nF电源去耦，另有5V电源连接器。封装为DIP-14、2.54mm连接器与5mm脚距通孔电容，已在板文件中写出焊盘几何。

引脚依据 TI 官方器件资料核对：[SN74HC08](https://www.ti.com/lit/ds/symlink/sn74hc08.pdf)、[SN74HC00](https://www.ti.com/lit/ds/symlink/sn74hc00.pdf)、[SN74HC32](https://www.ti.com/lit/ds/symlink/sn74hc32.pdf)、[SN74HC86](https://www.ti.com/lit/ds/symlink/sn74hc86.pdf)、[SN74HC04](https://www.ti.com/lit/ds/symlink/sn74hc04.pdf)。器件采购型号与实际封装仍需在后续设计中核对。

## 已执行的外部验证

2026-10-08，实际 KiCad CLI 10.0.7 加载半加器配套 `.kicad_pcb`，导出 IPC-D-356。`tests/verify_pcb_exchange.py` 独立解析标准 `.net`，并与映射表和 KiCad 导出的全部已分配焊盘逐项比较。

- 9个实体元件，6个网络，36个已分配焊盘全部一致。
- `standardNetlistMatchesLoadedBoard: true`。
- `directNetlistGuiImportTested: false`，本次未自动点击GUI直接导入 `.net`。
- `routed: false`，未执行布线或制造验收。

因此可复核的结论是“配套PCB被实际软件加载，标准网表与其真实焊盘连接一致”。逻辑报告的独立语法检查、标准网表与配套板对照、GUI直接网表导入是不同验证，不互相替代。

## 实现与复现

`src/PcbExport.cpp::exportPcb` 从独立编译的逻辑网络生成实体映射，不操作编辑器当前运行状态。`tests/pcb_export_tests.cpp` 检查引脚、电源、去耦、数量、状态保持和不支持对象拒绝；实际外部检查在配置 `EDA_KICAD_CLI` 且存在Python时由CTest注册。

```powershell
cmake -S . -B build -DEDA_KICAD_CLI="你的KiCad目录/bin/kicad-cli.exe"
cmake --build build --config Release
ctest --test-dir build -C Release -R "pcb_export_tests|kicad_pcb_validation" --output-on-failure
```

KiCad只用于外部验证，不是PracticeEDA的运行依赖，也不捆绑进分发包。
