#pragma once
#include "Digital.h"
#include "WxSupport.h"

namespace eda::logic {
wxColour signalColour(const Signal &);
void drawShape(wxGraphicsContext &, const Shape &, bool selected = false, bool preview = false,
               double zoom = 1);
// 画布、放置预览与导出共用同一渲染器。外部引脚位置完全来自 ports()，
// 连线用稳定端口 ID 保存，改变主体尺寸时端点会随轮廓自动更新。
void drawSymbol(wxGraphicsContext &, const Project &, const Part &,
                const std::function<Signal(const Port &)> &read, bool selected = false, bool error = false,
                bool preview = false, bool annotations = true, bool warning = false);
} // namespace eda::logic
