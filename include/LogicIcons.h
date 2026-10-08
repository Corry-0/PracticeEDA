#pragma once
#include <string>
#include <wx/bmpbndl.h>

namespace eda::logic {
// 内嵌 wxWidgets 示例资源与原生数字符号图标；运行时不依赖任何外部文件。
wxBitmapBundle icon(const std::string &kind);
} // namespace eda::logic
