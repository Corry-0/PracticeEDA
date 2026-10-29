#include "DigitalEditor.h"
#include <wx/cmdline.h>
#include <wx/imagpng.h>
#include <wx/fileconf.h>

// 唯一的应用入口；冒烟测试失败会变成非零进程退出码，供 build.ps1 判断。
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
        if (!smoke.empty()) {
            auto folder = eda::fsPath(smoke);
            std::filesystem::create_directories(folder);
            // 测试期间的最近文件等设置仅写入证据目录，避免污染用户配置。
            wxConfigBase::Set(new wxFileConfig("PracticeEDA", wxEmptyString,
                wxString((folder / "settings.ini").wstring()), wxEmptyString, wxCONFIG_USE_LOCAL_FILE));
        }
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
