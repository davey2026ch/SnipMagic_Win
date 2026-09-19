#pragma once
#include "util.h"
#include <atomic>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")

namespace netutil {

struct CancelFlag {
    std::atomic<bool> cancelled{false};
    void Cancel() { cancelled.store(true); }
    bool IsCancelled() const { return cancelled.load(); }
};

// 当前进行中的 WinHTTP 请求句柄，取消时强制关闭以立刻打断阻塞
inline std::atomic<void*>& ActiveHttpRequest() {
    static std::atomic<void*> h{nullptr};
    return h;
}
inline void AbortActiveHttp() {
    void* p = ActiveHttpRequest().exchange(nullptr);
    if (p) WinHttpCloseHandle(reinterpret_cast<HINTERNET>(p));
}

struct HttpResponse {
    int status = 0;
    std::string body;          // UTF-8 text
    std::vector<BYTE> binary;  // raw bytes
    std::wstring error;
    bool ok() const { return status >= 200 && status < 300 && error.empty(); }
};

inline std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// 去掉首尾空白，保存与调用前都应走这里
inline std::wstring TrimToken(const std::wstring& s) {
    return util::TrimToken(s);
}

inline bool TextToClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) { CloseClipboard(); return false; }
    void* p = GlobalLock(hMem);
    if (!p) { GlobalFree(hMem); CloseClipboard(); return false; }
    memcpy(p, text.c_str(), bytes);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

inline void DeletePathRecursive(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(path.c_str()); // 永久删除，不进回收站
        return;
    }
    WIN32_FIND_DATAW fd = {};
    std::wstring pattern = path + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            DeletePathRecursive(path + L"\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(path.c_str());
}

inline bool WriteFileBytes(const std::wstring& path, const void* data, size_t len) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = TRUE;
    if (len > 0) {
        size_t off = 0;
        const BYTE* p = static_cast<const BYTE*>(data);
        while (off < len) {
            DWORD chunk = static_cast<DWORD>((std::min)(len - off, static_cast<size_t>(1 << 20)));
            if (!WriteFile(h, p + off, chunk, &written, nullptr) || written != chunk) {
                ok = FALSE;
                break;
            }
            off += written;
        }
    }
    CloseHandle(h);
    return ok == TRUE;
}

inline bool ReadFileBytes(const std::wstring& path, std::vector<BYTE>& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > 200LL * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(sz.QuadPart));
    size_t off = 0;
    while (off < out.size()) {
        DWORD chunk = static_cast<DWORD>((std::min)(out.size() - off, static_cast<size_t>(1 << 20)));
        DWORD got = 0;
        if (!ReadFile(h, out.data() + off, chunk, &got, nullptr) || got == 0) break;
        off += got;
    }
    CloseHandle(h);
    out.resize(off);
    return true;
}

inline std::wstring MakeTempDir(const wchar_t* prefix) {
    wchar_t base[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, base);
    wchar_t name[MAX_PATH] = {};
    swprintf_s(name, L"%s%s%u_%u", base, prefix ? prefix : L"st_",
               GetCurrentProcessId(), GetTickCount());
    CreateDirectoryW(name, nullptr);
    return name;
}

inline std::wstring UrlFileName(const std::wstring& url) {
    size_t q = url.find_first_of(L"?#");
    std::wstring u = (q == std::wstring::npos) ? url : url.substr(0, q);
    size_t s = u.find_last_of(L"/\\");
    return (s == std::wstring::npos) ? u : u.substr(s + 1);
}

// ---- URL parse ----
struct UrlParts {
    bool https = true;
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path; // includes query
    bool valid = false;
};

inline UrlParts ParseUrl(const std::wstring& url) {
    UrlParts p;
    size_t schemeEnd = url.find(L"://");
    if (schemeEnd == std::wstring::npos) return p;
    std::wstring scheme = url.substr(0, schemeEnd);
    p.https = (_wcsicmp(scheme.c_str(), L"https") == 0);
    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find(L'/', hostStart);
    std::wstring hostPort = (pathStart == std::wstring::npos) ? url.substr(hostStart)
                                                              : url.substr(hostStart, pathStart - hostStart);
    p.path = (pathStart == std::wstring::npos) ? L"/" : url.substr(pathStart);
    size_t colon = hostPort.find(L':');
    if (colon == std::wstring::npos) {
        p.host = hostPort;
        p.port = p.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    } else {
        p.host = hostPort.substr(0, colon);
        p.port = static_cast<INTERNET_PORT>(_wtoi(hostPort.c_str() + colon + 1));
        if (p.port == 0) p.port = p.https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    }
    p.valid = !p.host.empty();
    return p;
}

inline bool HttpRequest(const std::wstring& method,
                        const std::wstring& url,
                        const std::wstring& bearerToken,
                        const std::string& jsonBody,
                        const std::vector<BYTE>* binaryBody,
                        bool omitContentType,
                        HttpResponse& out,
                        CancelFlag* cancel,
                        bool wantBinary,
                        const wchar_t* forceContentType = nullptr) {
    out = HttpResponse();
    UrlParts up = ParseUrl(url);
    if (!up.valid) {
        out.error = L"URL 无效";
        return false;
    }
    if (cancel && cancel->IsCancelled()) {
        out.error = L"已取消";
        return false;
    }

    HINTERNET hSession = WinHttpOpen(L"ScreenshotTool/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        out.error = L"WinHttpOpen 失败";
        return false;
    }
    WinHttpSetTimeouts(hSession, 15000, 15000, 30000, 60000);
    // 附件下载常带 302 跳转，放开重定向限制（默认禁 https→http）
#ifndef WINHTTP_REDIRECT_POLICY_ALWAYS
#define WINHTTP_REDIRECT_POLICY_ALWAYS 2
#endif
    DWORD redir = WINHTTP_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    HINTERNET hConnect = WinHttpConnect(hSession, up.host.c_str(), up.port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        out.error = L"连接失败";
        return false;
    }

    DWORD flags = up.https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method.c_str(), up.path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        out.error = L"创建请求失败";
        return false;
    }

    DWORD sec = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sec, sizeof(sec));

    // 预签名 PUT：不要 Authorization、尽量不要多余头，否则对象存储会 403
    std::wstring headers;
    std::wstring token = TrimToken(bearerToken);
    if (!token.empty()) {
        headers += L"Authorization: Bearer " + token + L"\r\n";
    }
    if (forceContentType && forceContentType[0]) {
        headers += L"Content-Type: ";
        headers += forceContentType;
        headers += L"\r\n";
    } else if (!omitContentType) {
        if (!jsonBody.empty() && method != L"GET") {
            headers += L"Content-Type: application/json\r\n";
        } else if (binaryBody) {
            headers += L"Content-Type: application/octet-stream\r\n";
        }
    }

    const void* bodyPtr = nullptr;
    DWORD bodyLen = 0;
    std::string localJson = jsonBody;
    if (binaryBody) {
        bodyPtr = binaryBody->data();
        bodyLen = static_cast<DWORD>(binaryBody->size());
    } else if (!localJson.empty()) {
        bodyPtr = localJson.data();
        bodyLen = static_cast<DWORD>(localJson.size());
    }

    BOOL sent = WinHttpSendRequest(hRequest,
                                   headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                   headers.empty() ? 0 : static_cast<DWORD>(-1L),
                                   WINHTTP_NO_REQUEST_DATA, 0, bodyLen, 0);
    ActiveHttpRequest().store(hRequest);
    auto releaseHandle = [&]() {
        void* p = ActiveHttpRequest().exchange(nullptr);
        if (p == hRequest) WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
    };
    if (!sent) {
        out.error = L"发送请求失败";
        releaseHandle();
        return false;
    }

    if (bodyLen > 0) {
        DWORD written = 0;
        if (!WinHttpWriteData(hRequest, bodyPtr, bodyLen, &written) || written != bodyLen) {
            out.error = L"上传请求体失败";
            releaseHandle();
            return false;
        }
    }

    if (cancel && cancel->IsCancelled()) {
        out.error = L"已取消";
        releaseHandle();
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        out.error = cancel && cancel->IsCancelled() ? L"已取消" : L"接收响应失败";
        releaseHandle();
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    out.status = static_cast<int>(statusCode);

    std::vector<BYTE> raw;
    for (;;) {
        if (cancel && cancel->IsCancelled()) {
            out.error = L"已取消";
            break;
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail == 0) break;
        size_t old = raw.size();
        raw.resize(old + avail);
        DWORD got = 0;
        if (!WinHttpReadData(hRequest, raw.data() + old, avail, &got)) break;
        raw.resize(old + got);
        if (got == 0) break;
    }

    releaseHandle();

    if (!out.error.empty() && out.error == L"已取消") return false;

    if (wantBinary) {
        out.binary = std::move(raw);
    } else {
        out.body.assign(raw.begin(), raw.end());
    }
    return true;
}

inline bool PostJson(const std::wstring& url, const std::wstring& token,
                     const std::string& json, HttpResponse& out, CancelFlag* cancel = nullptr) {
    return HttpRequest(L"POST", url, token, json, nullptr, false, out, cancel, false);
}

inline bool GetJson(const std::wstring& url, const std::wstring& token,
                    HttpResponse& out, CancelFlag* cancel = nullptr) {
    return HttpRequest(L"GET", url, token, {}, nullptr, false, out, cancel, false);
}

// PUT raw binary — omitContentType=true 时不带 Content-Type（签名 URL 需要）
inline bool PutBinary(const std::wstring& url, const std::wstring& token,
                      const std::vector<BYTE>& data, bool omitContentType,
                      HttpResponse& out, CancelFlag* cancel = nullptr,
                      const wchar_t* forceContentType = nullptr) {
    return HttpRequest(L"PUT", url, token, {}, &data, omitContentType, out, cancel, false, forceContentType);
}

inline bool GetBinary(const std::wstring& url, HttpResponse& out, CancelFlag* cancel = nullptr) {
    return HttpRequest(L"GET", url, {}, {}, nullptr, false, out, cancel, true);
}

// ---- Minimal JSON ----
class Json {
public:
    enum class Type { Null, Bool, Number, String, Object, Array };
    Type type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<std::pair<std::string, Json>> obj;
    std::vector<Json> arr;

    const Json* Find(const char* key) const {
        if (type != Type::Object || !key) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }
    const Json* At(size_t i) const {
        if (type != Type::Array || i >= arr.size()) return nullptr;
        return &arr[i];
    }
    std::string AsStr() const {
        if (type == Type::String) return str;
        return {};
    }
    std::wstring AsWStr() const { return Utf8ToWide(AsStr()); }
    double AsNum() const { return type == Type::Number ? num : 0.0; }
    int AsInt() const { return static_cast<int>(AsNum()); }
    bool AsBool() const { return type == Type::Bool ? b : false; }
    bool IsNull() const { return type == Type::Null; }
};

namespace detail {
inline void SkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}
inline bool ParseString(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (i + 3 < s.size()) {
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = s[i++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                        else return false;
                    }
                    // UTF-8 encode BMP (basic); surrogate pairs simplified
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                }
                break;
            }
            default: out.push_back(e); break;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}
inline bool ParseValue(const std::string& s, size_t& i, Json& out);
inline bool ParseObject(const std::string& s, size_t& i, Json& out) {
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    out.type = Json::Type::Object;
    out.obj.clear();
    SkipWs(s, i);
    if (i < s.size() && s[i] == '}') { ++i; return true; }
    for (;;) {
        SkipWs(s, i);
        std::string key;
        if (!ParseString(s, i, key)) return false;
        SkipWs(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        Json val;
        if (!ParseValue(s, i, val)) return false;
        out.obj.emplace_back(key, std::move(val));
        SkipWs(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        return false;
    }
}
inline bool ParseArray(const std::string& s, size_t& i, Json& out) {
    if (i >= s.size() || s[i] != '[') return false;
    ++i;
    out.type = Json::Type::Array;
    out.arr.clear();
    SkipWs(s, i);
    if (i < s.size() && s[i] == ']') { ++i; return true; }
    for (;;) {
        Json val;
        if (!ParseValue(s, i, val)) return false;
        out.arr.push_back(std::move(val));
        SkipWs(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        return false;
    }
}
inline bool ParseValue(const std::string& s, size_t& i, Json& out) {
    SkipWs(s, i);
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '{') return ParseObject(s, i, out);
    if (c == '[') return ParseArray(s, i, out);
    if (c == '"') {
        out.type = Json::Type::String;
        return ParseString(s, i, out.str);
    }
    if (s.compare(i, 4, "true") == 0) { i += 4; out.type = Json::Type::Bool; out.b = true; return true; }
    if (s.compare(i, 5, "false") == 0) { i += 5; out.type = Json::Type::Bool; out.b = false; return true; }
    if (s.compare(i, 4, "null") == 0) { i += 4; out.type = Json::Type::Null; return true; }
    size_t start = i;
    if (s[i] == '-' || s[i] == '+') ++i;
    while (i < s.size() && (isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' ||
                            s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
        // stop at second +/- not in exponent — good enough for API payloads
        if ((s[i] == '+' || s[i] == '-') && i > start && s[i - 1] != 'e' && s[i - 1] != 'E') break;
        ++i;
    }
    if (i == start) return false;
    out.type = Json::Type::Number;
    out.num = atof(s.substr(start, i - start).c_str());
    return true;
}
} // namespace detail

inline Json Parse(const std::string& text) {
    Json root;
    size_t i = 0;
    detail::ParseValue(text, i, root);
    return root;
}

// MinerU / Volc 混合错误：HTTP 401、msgCode A02、或 body.success=false
inline std::wstring ExtractApiError(const HttpResponse& resp) {
    Json j = Parse(resp.body);
    std::wstring msg;
    if (const Json* err = j.Find("error")) {
        if (const Json* m = err->Find("message")) msg = m->AsWStr();
        if (msg.empty() && err->type == Json::Type::String) msg = err->AsWStr();
    }
    if (msg.empty()) {
        if (const Json* m = j.Find("msg")) msg = m->AsWStr();
    }
    if (msg.empty()) {
        if (const Json* m = j.Find("message")) msg = m->AsWStr();
    }
    if (msg.empty()) {
        if (const Json* m = j.Find("err_msg")) msg = m->AsWStr();
    }
    std::wstring code;
    if (const Json* err = j.Find("error")) {
        if (const Json* c = err->Find("code")) code = c->AsWStr();
    }
    if (code.empty()) {
        if (const Json* c = j.Find("msgCode")) code = c->AsWStr();
    }
    if (code.empty()) {
        if (const Json* c = j.Find("code")) {
            if (c->type == Json::Type::Number) code = std::to_wstring(c->AsInt());
            else code = c->AsWStr();
        }
    }
    bool apiFail = false;
    if (const Json* s = j.Find("success")) {
        if (s->type == Json::Type::Bool && !s->b) apiFail = true;
    }
    bool tokenIssue = (resp.status == 401) ||
                      (!code.empty() && code.size() >= 3 && code[0] == L'A' && code[1] == L'0' && code[2] == L'2');
    std::wstring err;
    if (tokenIssue) err = L"鉴权失败：请检查 Token / API Key 是否有效（勿带空格）";
    if (resp.status == 403 && err.empty()) {
        err = L"HTTP 403：无权限、密钥无效，或请求参数不正确";
    }
    if (apiFail && err.empty()) err = L"接口返回失败";
    if (!code.empty()) {
        if (!err.empty()) err += L"；";
        err += L"code=" + code;
    }
    if (!msg.empty()) {
        if (!err.empty()) err += L"；";
        err += msg;
    }
    if (err.empty()) err = L"HTTP " + std::to_wstring(resp.status);
    return err;
}

inline std::string JsonEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\b': o += "\\b"; break;
        case '\f': o += "\\f"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", c);
                o += buf;
            } else o.push_back(static_cast<char>(c));
        }
    }
    return o;
}

} // namespace netutil
