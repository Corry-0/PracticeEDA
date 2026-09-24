#pragma once
#include "Digital.h"
#include "WxSupport.h"
#include <wx/grid.h>
#include <wx/treectrl.h>

namespace eda::logic {
class LogicCanvas;
// Owns the project/history and simulator; structural edits pass through change() for rollback.
class LogicEditor : public wxFrame {
  public:
    explicit LogicEditor(wxWindow *parent = nullptr);
    bool load(const wxString &path);
    bool save(bool as = false);
    void runSmoke(const std::filesystem::path &folder);
    void closeSmoke();

  private:
    History history;
    std::string current = "main", savedPath;
    std::unique_ptr<Simulator> simulator;
    LogicCanvas *canvas;
    wxTreeCtrl *tree;
    std::map<std::string, int> iconIds;
    wxChoice *circuits;
    wxTextCtrl *search, *diagnosticDetail;
    wxGrid *properties;
    int drawingStroke = 2;
    bool drawingFilled = false, syncingProperties = false;
    std::set<std::string> inspectedSelection;
    wxStaticText *selection, *status, *diagnosticSummary;
    wxListBox *diagnostics;
    wxTimer timer;
    std::vector<Issue> issues;
    Table lastTable;
    bool hasTable = false;
    Circuit &circuit() { return history.project.circuit(current); }
    void rebuild(bool reset = true, bool library = false);
    void populate();
    void inspect();
    void apply();
    void refreshDiagnostics();
    void showDiagnostic(int index, bool locate);
    void change(const std::function<void()> &action);
    void showReport(const std::string &title, const std::string &text);
    void showTable(Table table);
    void copySelection();
    void pasteSelection();
    void removeSelection();
    void newCircuit();
    void newComponent();
    void saveComponent();
    void loadComponent();
    void exportNetlist();
    void encapsulate();
    void renameCircuit();
    void tick();
    void undo(bool redo = false);
    bool discard();
    void fail(const std::exception &);
    void stop();
    void setAppearance(bool enabled);
    friend class LogicCanvas;
};
// View coordinates and temporary gestures live here; committed data remains in the project model.
class LogicCanvas : public wxPanel {
  public:
    explicit LogicCanvas(LogicEditor *owner, wxWindow *parent);
    View view;
    std::set<std::string> selected;
    std::string tool = "Select", placing = "And", subcircuit;
    bool appearance = false;
    void cancel();
    void fit();
    void render(wxGraphicsContext &, wxSize, bool clean = false);
    void exportPng(const wxString &path);
    void place(Point at);
    void route(Point at, bool freeEnd = false);

  private:
    LogicEditor *owner;
    wxTimer rippleTimer;
    std::optional<Endpoint> hoveredPin;
    Point hoverScreen, hoverAt;
    bool mouseInside = false;
    unsigned rippleFrame = 0;
    void updatePinHover();
    void clearPinHover();
    void refreshPinHover();
    void drawPinHover(wxGraphicsContext &);
    std::optional<Project> before;
    std::optional<Endpoint> start;
    std::vector<Point> bends;
    Point mouse, down, lastScreen, boxStart;
    bool dragging = false, panning = false, box = false;
    std::optional<Shape> draft;
    bool curveControl = false;
    int shapeHandle = -1;
    void updateDraft(Point, bool constrain);
    void finishShape();
    std::optional<Endpoint> pinAt(Point at) const;
    Endpoint endpointAt(Point at);
    std::string hit(Point at) const;
    void paint(wxPaintEvent &);
    void leftDown(wxMouseEvent &);
    void leftUp(wxMouseEvent &);
    void motion(wxMouseEvent &);
    void doubleClick(wxMouseEvent &);
    void key(wxKeyEvent &);
    void move(Point delta);
    friend class LogicEditor;
};
} // namespace eda::logic
