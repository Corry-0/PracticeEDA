# 数字编辑器图标资源

资源来源是本机 `D:/zongshe/wxWidgets/samples`。所需资源已复制到项目并编译进程序，部署时不需要该绝对路径，也不依赖外部图片目录。

| 源目录 | 本项目用途 |
| --- | --- |
| `toolbar/bitmaps/{new,open,save,copy,cut,paste,help}_png.c` 及 `_2x_png.c` | 新建、打开、保存、复制、剪切、粘贴、帮助；同时包含 32/64 像素版本 |
| `richtext/bitmaps/{undo,redo}.xpm` | 撤销、重做 |
| `listctrl/bitmaps/tooltime.xpm` | 时钟元件和时钟步进 |
| `widgets/icons/toggle.xpm` | 数字按钮 |
| `widgets/icons/text.xpm` | 文字注释 |
| `widgets/icons/header.xpm` | 真值表分析 |
| `printing/folder.xpm` | 元件库分类、子电路分类 |
| `drawing/cursor.xpm` | 布线工具 |

`samples` 中未发现专用逻辑门图标。`src/LogicIcons.cpp` 用 wxGraphicsContext 补充绘制 AND/OR/NOT/XOR 及其反相形式、输入/输出、总线、算术、存储等数字符号，并生成 32/64 像素图像。它们用于元件树和常用元件快捷栏。

`icon(kind)` 按稳定类型名查找资源，元件库增加、删除或调整顺序不会引起平行数组越界。GUI 回归检查全部元件图标、分类树映射和快捷栏工具绑定，并输出 `logic-icons.png` 便于视觉检查。

## 许可

wxWidgets `samples/toolbar/bitmaps/README.md` 明确注明上述 PNG 来自 Tango Desktop Project，属于 public domain（公有领域）。XPM 示例资源随 wxWidgets 使用其许可证，项目 `third_party/LICENSE-wxWidgets.txt` 和 `LICENSE-wxWidgets-LGPL.txt` 保留许可文本，分发包同步包含在 `licenses/` 内。复制的资源数据未修改。

## 画布电路符号

按照用户提供的 [原项目图片目录](https://github.com/LEVE1012/ElectronicComponent_Practice-/tree/main/resource/image)，于 2026-09-19 下载 `andGate.png`、`notgate.png`、`orgate.png` 到 `resources/reference/`，用于核对逻辑门轮廓。原图保留不修改，其来源与权利归属沿用原仓库；它们不作为运行时资源，也未并入 wxWidgets 的资源许可声明。

`src/LogicSymbols.cpp` 用 wxGraphicsContext 绘制画布电路符号，包括逻辑门、总线、选择器、时序器件、算术功能块、子电路和数码管。使用矢量路径以适应缩放、选中和错误颜色；程序无需联网或加载参考 PNG。画布、鼠标放置预览、导出图片共用 `drawSymbol()`，全部引脚坐标来自 `ports()`，不会改变既有项目接线。
