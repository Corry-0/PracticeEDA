#pragma once
#include "Digital.h"
#include "WxSupport.h"

namespace eda::logic {
wxColour signalColour(const Signal &);
void drawShape(wxGraphicsContext &, const Shape &, bool selected = false, bool preview = false,
               double zoom = 1);
// Canvas, placement preview and export share this renderer. External pin positions
// come exclusively from ports(), so changing artwork never changes saved wiring.
void drawSymbol(wxGraphicsContext &, const Project &, const Part &,
                const std::function<Signal(const Port &)> &read, bool selected = false, bool error = false,
                bool preview = false);
} // namespace eda::logic
