// Compile the original image arrays into the executable; no resource files are needed at runtime.
#include "LogicIcons.h"
#include "WxSupport.h"
#include "wxwidgets/copy_2x_png.c"
#include "wxwidgets/copy_png.c"
#include "wxwidgets/cursor.xpm"
#include "wxwidgets/cut_2x_png.c"
#include "wxwidgets/cut_png.c"
#include "wxwidgets/folder.xpm"
#include "wxwidgets/header.xpm"
#include "wxwidgets/help_2x_png.c"
#include "wxwidgets/help_png.c"
#include "wxwidgets/new_2x_png.c"
#include "wxwidgets/new_png.c"
#include "wxwidgets/open_2x_png.c"
#include "wxwidgets/open_png.c"
#include "wxwidgets/paste_2x_png.c"
#include "wxwidgets/paste_png.c"
#include "wxwidgets/redo.xpm"
#include "wxwidgets/save_2x_png.c"
#include "wxwidgets/save_png.c"
#include "wxwidgets/text.xpm"
#include "wxwidgets/toggle.xpm"
#include "wxwidgets/tooltime.xpm"
#include "wxwidgets/undo.xpm"
#include <memory>
#include <wx/dcmemory.h>

namespace eda::logic {
namespace {
wxBitmap draw(const std::string &kind, int size) {
    wxBitmap bitmap(size, size, 32);
    wxMemoryDC dc(bitmap);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();
    {
        std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
        if (!g)
            throw std::runtime_error("无法绘制数字元件图标");
        g->Scale(size / 32.0, size / 32.0);
        g->SetPen(wxPen(wxColour("#214a79"), 1.7));
        g->SetBrush(wxBrush(wxColour("#e8f2ff")));
        auto line = [&](double x, double y, double a, double b) { g->StrokeLine(x, y, a, b); };
        auto label = [&](const std::string &word, int font = 10) {
            g->SetFont(wxFont(wxFontInfo(font).Bold().FaceName("Segoe UI")), wxColour("#214a79"));
            double w, h;
            g->GetTextExtent(U(word), &w, &h);
            g->DrawText(U(word), (32 - w) / 2, (32 - h) / 2);
        };
        auto bubble = [&] {
            g->SetBrush(*wxWHITE_BRUSH);
            g->DrawEllipse(24, 13, 6, 6);
        };
        if (kind == "And" || kind == "Nand") {
            auto p = g->CreatePath();
            p.MoveToPoint(8, 6);
            p.AddLineToPoint(16, 6);
            p.AddCurveToPoint(29, 6, 29, 26, 16, 26);
            p.AddLineToPoint(8, 26);
            p.CloseSubpath();
            g->DrawPath(p);
            line(1, 11, 8, 11);
            line(1, 21, 8, 21);
            line(26, 16, 31, 16);
            if (kind == "Nand")
                bubble();
        } else if (kind == "Or" || kind == "Nor" || kind == "Xor" || kind == "Xnor") {
            auto p = g->CreatePath();
            p.MoveToPoint(7, 6);
            p.AddCurveToPoint(18, 5, 24, 8, 28, 16);
            p.AddCurveToPoint(24, 24, 18, 27, 7, 26);
            p.AddCurveToPoint(13, 16, 13, 16, 7, 6);
            p.CloseSubpath();
            g->DrawPath(p);
            line(1, 11, 9, 11);
            line(1, 21, 9, 21);
            line(28, 16, 31, 16);
            if (kind == "Xor" || kind == "Xnor") {
                auto q = g->CreatePath();
                q.MoveToPoint(3, 6);
                q.AddCurveToPoint(9, 16, 9, 16, 3, 26);
                g->StrokePath(q);
            }
            if (kind == "Nor" || kind == "Xnor")
                bubble();
        } else if (kind == "Not" || kind == "Buffer" || kind == "TriState") {
            auto p = g->CreatePath();
            p.MoveToPoint(7, 6);
            p.AddLineToPoint(7, 26);
            p.AddLineToPoint(25, 16);
            p.CloseSubpath();
            g->DrawPath(p);
            line(1, 16, 7, 16);
            line(25, 16, 31, 16);
            if (kind == "Not")
                bubble();
            if (kind == "TriState")
                line(16, 2, 16, 11);
        } else if (kind == "Input" || kind == "Output" || kind == "Constant" || kind == "Probe" ||
                   kind == "Hex") {
            if (kind == "Output")
                g->DrawRoundedRectangle(5, 5, 22, 22, 7);
            else
                g->DrawRectangle(5, 5, 22, 22);
            line(kind == "Input" || kind == "Constant" ? 27 : 0, 16,
                 kind == "Input" || kind == "Constant" ? 32 : 5, 16);
            label(kind == "Input"    ? "1"
                  : kind == "Output" ? "0"
                  : kind == "Hex"    ? "A"
                  : kind == "Probe"  ? "?"
                                     : "01");
        } else if (kind == "LED") {
            g->SetBrush(wxBrush(wxColour("#29c374")));
            g->DrawEllipse(6, 5, 21, 21);
            line(16, 26, 16, 31);
        } else if (kind == "Select") {
            g->SetBrush(wxBrush(wxColour("#214a79")));
            auto p = g->CreatePath();
            p.MoveToPoint(7, 3);
            p.AddLineToPoint(7, 26);
            p.AddLineToPoint(13, 20);
            p.AddLineToPoint(18, 29);
            p.AddLineToPoint(22, 27);
            p.AddLineToPoint(17, 18);
            p.AddLineToPoint(26, 18);
            p.CloseSubpath();
            g->DrawPath(p);
        } else if (kind == "Wire" || kind == "Node" || kind == "Splitter" || kind == "Joiner") {
            if (kind == "Joiner") {
                g->Translate(32, 0);
                g->Scale(-1, 1);
            }
            line(2, 16, 14, 16);
            line(14, 6, 14, 26);
            line(14, 6, 30, 6);
            line(14, 26, 30, 26);
            if (kind == "Node" || kind == "Wire") {
                g->SetBrush(wxBrush(wxColour("#214a79")));
                g->DrawEllipse(11, 13, 6, 6);
            }
            if (kind == "Splitter" || kind == "Joiner") {
                line(2, 19, 10, 19);
                line(2, 13, 10, 13);
            }
        } else if (kind == "Ground" || kind == "Power") {
            line(16, 4, 16, 16);
            line(5, 16, 27, 16);
            line(9, 21, 23, 21);
            line(13, 26, 19, 26);
            if (kind == "Power") {
                line(16, 4, 10, 10);
                line(16, 4, 22, 10);
            }
        } else if (kind == "Run" || kind == "Step") {
            g->SetBrush(wxBrush(wxColour("#29b36b")));
            auto p = g->CreatePath();
            p.MoveToPoint(7, 5);
            p.AddLineToPoint(7, 27);
            p.AddLineToPoint(26, 16);
            p.CloseSubpath();
            g->DrawPath(p);
            if (kind == "Step")
                line(28, 5, 28, 27);
        } else if (kind == "Reset") {
            g->SetBrush(wxBrush(wxColour("#ee905b")));
            g->DrawRoundedRectangle(6, 6, 20, 20, 3);
        } else if (kind == "Fit") {
            g->SetBrush(*wxTRANSPARENT_BRUSH);
            g->DrawRectangle(5, 5, 22, 22);
            line(10, 10, 22, 22);
            line(10, 22, 22, 10);
        } else {
            static const std::map<std::string, std::string> marks = {
                {"Mux", "MUX"},      {"Demux", "DMX"},    {"Decoder", "DEC"},       {"Encoder", "ENC"},
                {"Add", "+"},        {"Subtract", "−"},   {"Multiply", "×"},        {"Divide", "÷"},
                {"Negate", "−x"},    {"Compare", "="},    {"ShiftLeft", "<<"},      {"ShiftRight", ">>"},
                {"DFF", "D"},        {"TFF", "T"},        {"JKFF", "JK"},           {"SRFF", "SR"},
                {"Register", "REG"}, {"Counter", "CTR"},  {"ShiftRegister", "SFT"}, {"RAM", "RAM"},
                {"ROM", "ROM"},      {"Extender", "1:N"}, {"Subcircuit", "SUB"}};
            g->DrawRoundedRectangle(5, 5, 22, 22, 3);
            line(0, 10, 5, 10);
            line(0, 22, 5, 22);
            line(27, 16, 32, 16);
            auto it = marks.find(kind);
            auto word = it == marks.end() ? "?" : it->second;
            label(word, word.size() > 2 ? 7 : 12);
        }
    }
    dc.SelectObject(wxNullBitmap);
    return bitmap;
}
} // namespace
wxBitmapBundle icon(const std::string &kind) {
#define SAMPLE_PNG(key, name)                                                                                \
    if (kind == key)                                                                                         \
    return wxBitmapBundle::FromBitmaps(wxBitmap::NewFromPNGData(name##_png, sizeof(name##_png)),             \
                                       wxBitmap::NewFromPNGData(name##_2x_png, sizeof(name##_2x_png)))
    SAMPLE_PNG("New", new);
    SAMPLE_PNG("Open", open);
    SAMPLE_PNG("Save", save);
    SAMPLE_PNG("Copy", copy);
    SAMPLE_PNG("Cut", cut);
    SAMPLE_PNG("Paste", paste);
    SAMPLE_PNG("Help", help);
#undef SAMPLE_PNG
    if (kind == "Undo")
        return wxBitmapBundle::FromBitmap(wxBitmap(undo_xpm));
    if (kind == "Redo")
        return wxBitmapBundle::FromBitmap(wxBitmap(redo_xpm));
    if (kind == "Clock")
        return wxBitmapBundle::FromBitmap(wxBitmap(tooltime_xpm));
    if (kind == "Button")
        return wxBitmapBundle::FromBitmap(wxBitmap(toggle_xpm));
    if (kind == "Text")
        return wxBitmapBundle::FromBitmap(wxBitmap(text_xpm));
    if (kind == "Table")
        return wxBitmapBundle::FromBitmap(wxBitmap(header_xpm));
    if (kind == "Folder")
        return wxBitmapBundle::FromBitmap(wxBitmap(folder_xpm));
    if (kind == "Wire")
        return wxBitmapBundle::FromBitmap(wxBitmap(cursor_xpm));
    return wxBitmapBundle::FromBitmaps(draw(kind, 32), draw(kind, 64));
}
} // namespace eda::logic
