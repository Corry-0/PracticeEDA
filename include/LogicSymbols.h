#pragma once
#include "Digital.h"
#include "WxSupport.h"

namespace eda::logic {
wxColour signalColour(const Signal &);
void drawShape(wxGraphicsContext &, const Shape &, bool selected = false, bool preview = false,
               double zoom = 1);
// 画布、放置预览与导出共用同一渲染器。外部引脚位置完全来自 ports()，
// 因此修改外观永远不会改变已保存的连线。
void drawSymbol(wxGraphicsContext &, const Project &, const Part &,
                const std::function<Signal(const Port &)> &read, bool selected = false, bool error = false,
                bool preview = false);
} // namespace eda::logic
