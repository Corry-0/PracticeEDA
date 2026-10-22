# 元件主体与边框端口设计参考

- 日期：2026-10-22。
- 生成方式：内置 `image_gen.imagegen`，非 CLI/API 回退。
- 型号：当前工具不暴露型号选择，未指定或宣称使用 image2.5。
- 输出：`component-library-border-pins.png`，单张整套元件设计参考图。
- 用途：参考整体轮廓、主体配色和边框端子；最终电气端口语义保留源码定义，界面以原生矢量实现。

## 完整生成提示词

```text
Use case: scientific-educational
Asset type: a single coherent component-library redesign reference board for a Windows digital logic circuit editor, to guide native vector implementation.
Primary request: redesign ALL components into compact, precise, legible symbols whose circular connection terminals sit DIRECTLY ON the body outline. Absolutely no external pin leads, no wire stubs, no long input/output tails.
Style: flat technical EDA symbols, white background, thin deep navy outlines, subtle very pale blue fills, green for active signals. No perspective, no shadows, no 3D, no decorative texture. Carefully aligned contact sheet, 6 columns by 10 rows, consistent scale and gutters. Tiny legible English component kind caption above every cell. Each cell is one body with small terminal dots centered on its border. Pin labels inside the body. Input and Output are especially small square/circular value badges showing 0 or 1, with exactly one terminal at right/left edge.
Arrange the following 60 cells in this order:
Row 1: Input, Output, Constant, Clock, Power, Ground.
Row 2: Splitter, Joiner, Extender, Probe, PullUp, PullDown.
Row 3: Transistor, TransmissionGate, Not, Buffer, And, Or.
Row 4: Nand, Nor, Xor, Xnor, TriState, ControlledInverter.
Row 5: EvenParity, OddParity, Mux, Demux, Decoder, Encoder.
Row 6: Add, Subtract, Multiply, Divide, Negate, Compare.
Row 7: ShiftLeft, ShiftRight, BitSelector, BitAdder, BitFinder, DFF.
Row 8: TFF, JKFF, SRFF, Register, Counter, ShiftRegister.
Row 9: RAM, ROM, Random, Button, LED, Hex.
Row 10: Keyboard, TTY, Joystick, SevenSegment, LEDMatrix, Subcircuit.
Use recognizable AND D-shapes, OR curved bodies, NOT/Buffer triangles, NAND/NOR/NOT inversion bubbles inset at the body edge so external contact remains on the body envelope. Multiplexers are trapezoids with selection terminal ON bottom border, data inputs on left, outputs on right. Memory/arithmetic blocks are rectangles with concise functional marks and internal pin names, all terminal dots on rectangle edges. Splitter/Joiner are compact labeled blocks with a fan motif INSIDE. SevenSegment is a tall dark seven-segment display with eight border terminals. LEDMatrix is a dark matrix body with two border terminals. Power and Ground use compact badge outlines with supply/ground motif inside and a right-border terminal.
Constraints: every connection terminal must touch its own body border; no leads outside any body. No real circuit connections between cells, no watermark. This board documents geometry; preserve distinguishable traditional logic functions.
```
