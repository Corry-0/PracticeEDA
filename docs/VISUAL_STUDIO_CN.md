# Visual Studio 编译与运行

1. 打开 `build/PracticeEDA.sln`；尚未生成时运行 `scripts/build.ps1 -ConfigureOnly`。
2. 选择 Debug/x64 或 Release/x64，将 PracticeEDA 设为启动项目。
3. Ctrl+Shift+B 编译，F5 直接进入数字电路编辑器。

解决方案包含数字电路应用 PracticeEDA、数字核心库 logic_core、逻辑/组件/导入/图形/进阶核心/分析/PCB/布线八个核心测试程序，以及 CMake 辅助目标。存在 Python 和指定外部工具时，CTest 还注册独立网表格式与 KiCad 验证。GUI 冒烟测试另含模块场景，当前已知失败项见 [测试报告](TEST_REPORT_CN.md)。

头文件位于 `include/`，源文件位于 `src/`；图标资源显示在“图标资源”筛选器下，来源于 `resources/wxwidgets/`，编译后不依赖外部资源路径。原生工程示例为 `.logic.json`，另有 `.component.json`、`.circ` 和受限实体PCB映射示例。

```powershell
.\scripts\build.ps1 -Configuration Debug -GuiTest
.\scripts\build.ps1 -Configuration Release -GuiTest -Package
```

需要 Visual Studio 2022“使用 C++ 的桌面开发”、Windows SDK、CMake 3.20+、MSVC x64 的 wxWidgets 3.2+。脚本优先发现仓库旁的 `../wxWidgets`；也可传 `-WxRoot D:\SDK\wxWidgets`。

测试输出分别位于 `build/qa-debug` 与 `build/qa-release`；可运行包位于 `dist/PracticeEDA`。CMake 自动生成 `.vcxproj` 与筛选器，请在 `CMakeLists.txt` 中维护源码清单。

支持 `CMakePresets.json` 中的 vs2022-x64 配置及 debug/release 构建和测试预设。图标已内嵌程序；将运行包复制到其他电脑不需要复制本机 wxWidgets samples。
