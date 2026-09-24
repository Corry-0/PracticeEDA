#pragma once
#include <string>
#include <wx/bmpbndl.h>

namespace eda::logic {
// Bundled wxWidgets sample assets and native digital symbols; no runtime file dependency.
wxBitmapBundle icon(const std::string &kind);
} // namespace eda::logic
