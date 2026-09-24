#include "DigitalEditor.h"
#include <wx/cmdline.h>
#include <wx/imagpng.h>

// One application entry point; smoke-test failures become a nonzero process exit code for build.ps1.
class PracticeApp : public wxApp {
  public:
    int OnRun() override {
        int result = wxApp::OnRun();
        return smokeFailed ? 1 : result;
    }
    void OnInitCmdLine(wxCmdLineParser &parser) override {
        wxApp::OnInitCmdLine(parser);
        parser.AddOption("", "smoke-test", "Run digital GUI integration checks in a directory");
        parser.AddSwitch("", "logic", "Digital editor (accepted for compatibility)");
        parser.AddParam("project", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
    }
    bool OnCmdLineParsed(wxCmdLineParser &parser) override {
        parser.Found("smoke-test", &smoke);
        if (parser.GetParamCount())
            path = parser.GetParam(0);
        return wxApp::OnCmdLineParsed(parser);
    }
    bool OnInit() override {
        if (!wxApp::OnInit())
            return false;
        SetAppName("PracticeEDA");
        wxImage::AddHandler(new wxPNGHandler());
        auto frame = new eda::logic::LogicEditor();
        SetTopWindow(frame);
        frame->Show();
        if (!path.empty())
            frame->load(path);
        if (!smoke.empty())
            CallAfter([frame, this] {
                try {
                    frame->runSmoke(eda::fsPath(smoke));
                } catch (const std::exception &e) {
                    smokeFailed = true;
                    try {
                        eda::writeAtomic(eda::fsPath(smoke) / "FAILED.txt", e.what());
                    } catch (...) {
                    }
                }
                frame->closeSmoke();
            });
        return true;
    }

  private:
    bool smokeFailed = false;
    wxString path, smoke;
};
wxIMPLEMENT_APP(PracticeApp);
