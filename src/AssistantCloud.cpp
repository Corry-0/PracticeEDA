#include "Assistant.h"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace eda::logic::ai {
#ifdef _WIN32
namespace {
struct Handle {
    HINTERNET value;
    explicit Handle(HINTERNET h) : value(h) {
        if (!h)
            throw std::runtime_error("无法建立模型服务连接，错误码 " + std::to_string(GetLastError()));
    }
    Handle(const Handle &) = delete;
    ~Handle() { WinHttpCloseHandle(value); }
};
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("配置字符串编码无效");
    std::wstring out(n, L' ');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n);
    return out;
}
void checked(BOOL ok) {
    if (!ok)
        throw std::runtime_error("云端请求失败，Windows 错误码 " + std::to_string(GetLastError()));
}
} // namespace
#endif
Json cloudRequest(const std::string &url, const std::string &model, const std::string &key, Json payload,
                  const std::atomic_bool &cancelled) {
#ifdef _WIN32
    if (key.empty() || key.find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error("请设置有效的 PRACTICEEDA_AI_API_KEY 后重启软件");
    if (model.empty())
        throw std::runtime_error("请填写模型名称");
    auto address = wide(url);
    URL_COMPONENTS u{};
    u.dwStructSize = sizeof(u);
    u.dwHostNameLength = u.dwUrlPathLength = u.dwExtraInfoLength = u.dwUserNameLength = u.dwPasswordLength =
        DWORD(-1);
    checked(WinHttpCrackUrl(address.c_str(), 0, 0, &u));
    if (u.nScheme != INTERNET_SCHEME_HTTPS || u.dwUserNameLength || u.dwPasswordLength || u.dwExtraInfoLength)
        throw std::runtime_error("请输入完整 HTTPS 聊天端点，地址中不能含凭据、查询参数或片段");
    std::wstring host(u.lpszHostName, u.dwHostNameLength), path(u.lpszUrlPath, u.dwUrlPathLength);
    Handle session(WinHttpOpen(L"PracticeEDA", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0));
    checked(WinHttpSetTimeouts(session.value, 5000, 8000, 15000, 45000));
    Handle connection(WinHttpConnect(session.value, host.c_str(), u.nPort, 0));
    Handle request(WinHttpOpenRequest(connection.value, L"POST", path.c_str(), nullptr, nullptr, nullptr,
                                      WINHTTP_FLAG_SECURE));
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    checked(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
    const auto headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + wide(key);
    payload["model"] = model;
    auto body = payload.dump();
    if (body.size() > 2000000)
        throw std::runtime_error("请求上下文过大，请缩小范围");
    if (cancelled)
        throw std::runtime_error("已取消");
    checked(WinHttpSendRequest(request.value, headers.c_str(), DWORD(-1), body.data(), DWORD(body.size()),
                               DWORD(body.size()), 0));
    checked(WinHttpReceiveResponse(request.value, nullptr));
    DWORD status = 0, size = sizeof(status);
    checked(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                                &status, &size, nullptr));
    if (status != 200)
        throw std::runtime_error("模型服务 HTTP " + std::to_string(status) +
                                 "，请检查端点、模型、额度和密钥");
    std::string response;
    char buffer[8192];
    while (!cancelled) {
        DWORD got = 0;
        checked(WinHttpReadData(request.value, buffer, sizeof(buffer), &got));
        if (!got)
            return Json::parse(response);
        response.append(buffer, got);
        if (response.size() > 2000000)
            throw std::runtime_error("模型响应过大");
    }
    throw std::runtime_error("已取消");
#else
    (void)url;
    (void)model;
    (void)key;
    (void)payload;
    (void)cancelled;
    throw std::runtime_error("当前云端连接支持 Windows");
#endif
}
} // namespace eda::logic::ai
