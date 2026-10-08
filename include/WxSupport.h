#pragma once
#include "LogicSupport.h"
#include <wx/choice.h>
#include <wx/graphics.h>
#include <wx/listbox.h>
#include <wx/wx.h>

namespace eda {
// 模型内部字符串使用 UTF-8；Windows 文件系统路径必须使用原生 UTF-16。
inline wxString U(const std::string &s) {
    return wxString::FromUTF8(s);
}
inline std::string utf8(const wxString &s) {
    return s.ToStdString(wxConvUTF8);
}
inline std::filesystem::path fsPath(const wxString &s) {
#ifdef _WIN32
    return std::filesystem::path(s.ToStdWstring());
#else
    return std::filesystem::path(utf8(s));
#endif
}
} // namespace eda
