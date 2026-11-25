#include "http.h"

#include "core/version.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace vigil::net {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
};

std::string lastError(const char* stage) {
    return std::string(stage) + " failed (error " + std::to_string(GetLastError()) + ")";
}

}  // namespace

bool get(const std::string& url, Response& out, int timeoutMs) {
    out = Response{};
    const std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    wchar_t extra[2048] = {};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { out.error = "invalid URL"; return false; }
    if (uc.nScheme != INTERNET_SCHEME_HTTPS) { out.error = "only HTTPS is allowed"; return false; }

    const std::wstring agent = widen(std::string("Vigil/") + VIGIL_VERSION_STRING);
    Handle session{WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) { out.error = lastError("WinHttpOpen"); return false; }
    WinHttpSetTimeouts(session.h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    Handle connect{WinHttpConnect(session.h, host, uc.nPort, 0)};
    if (!connect.h) { out.error = lastError("connect"); return false; }
    const std::wstring object = std::wstring(path) + extra;
    Handle request{WinHttpOpenRequest(connect.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.h) { out.error = lastError("open request"); return false; }
    DWORD noCookies = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &noCookies, sizeof(noCookies));
    if (!WinHttpSendRequest(request.h, L"Accept: application/json\r\n", static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        out.error = lastError("request");
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                        &size, WINHTTP_NO_HEADER_INDEX);
    out.status = static_cast<int>(status);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail) || avail == 0) break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, buf.data(), avail, &read) || read == 0) break;
        out.body.append(buf.data(), read);
        if (out.body.size() > 1u << 20) break;  // responses are small; never buffer more than 1 MB
    }
    return true;
}

}  // namespace vigil::net
