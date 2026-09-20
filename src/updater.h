#pragma once
// 软件更新模块：
// - 启动 3 秒后自动检测 / 设置界面手动检测（共用 CheckForUpdate）
// - 数据源：gitee 仓库（公开）的最新 Release，附件里带「截图工具.exe」
// - 更新方式：下载新 exe 到临时目录 → 旧 exe 改名让位 → 新 exe 归位 → 重启 → 延迟清理
#include "netutil.h"
#include "version.h"
#include <string>

namespace updater {

// releases/latest：公开仓库无需令牌；仓库无 Release 时返回 404
inline const wchar_t* kReleasesLatestUrl =
    L"https://gitee.com/api/v5/repos/mrpu2020/SnipMagic_Win/releases/latest";

struct UpdateInfo {
    bool available = false;      // 远端版本比本地新
    bool fetched = false;        // 接口访问成功
    std::wstring latestVersion;  // 已去掉 v 前缀，如 1.2.0
    std::wstring assetName;
    std::wstring assetUrl;
    std::wstring notes;          // Release 描述（changelog）
    std::wstring error;
};

inline std::wstring TrimVersion(std::wstring v) {
    while (!v.empty() && (v[0] == L'v' || v[0] == L'V')) v.erase(0, 1);
    return v;
}

// 按 '.' 分段数字比较：1.10.0 > 1.9.9；非数字段按 0；a>b 返回 1，相等 0，a<b 返回 -1
inline int CompareVersions(const std::wstring& a, const std::wstring& b) {
    size_t ia = 0, ib = 0;
    for (;;) {
        unsigned long va = 0, vb = 0;
        while (ia < a.size() && a[ia] != L'.') {
            if (iswdigit(a[ia])) va = va * 10 + (a[ia] - L'0');
            ++ia;
        }
        while (ib < b.size() && b[ib] != L'.') {
            if (iswdigit(b[ib])) vb = vb * 10 + (b[ib] - L'0');
            ++ib;
        }
        if (va != vb) return va > vb ? 1 : -1;
        if (ia >= a.size() && ib >= b.size()) return 0;
        if (ia < a.size()) ++ia;
        if (ib < b.size()) ++ib;
    }
}

// 解析 gitee releases/latest 的 JSON：取 tag_name 与 exe 附件的下载地址
inline bool ParseReleaseJson(const std::string& body, UpdateInfo& out) {
    netutil::Json j = netutil::Parse(body);
    if (j.type != netutil::Json::Type::Object) return false;
    const netutil::Json* tag = j.Find("tag_name");
    if (!tag) return false;
    out.latestVersion = TrimVersion(tag->AsWStr());
    const netutil::Json* assets = j.Find("assets");
    if (assets && assets->type == netutil::Json::Type::Array) {
        for (const auto& a : assets->arr) {
            const netutil::Json* name = a.Find("name");
            const netutil::Json* url = a.Find("browser_download_url");
            if (!name || !url) continue;
            std::wstring n = name->AsWStr();
            if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".exe") == 0) {
                // 优先程序本体的中文名附件，否则取第一个 exe
                if (out.assetUrl.empty() || n.find(APP_NAME) != std::wstring::npos) {
                    out.assetName = n;
                    out.assetUrl = url->AsWStr();
                }
            }
        }
    }
    if (const netutil::Json* b = j.Find("body")) out.notes = b->AsWStr();
    return true;
}

// 检查更新。url 参数供测试注入本地服务地址。返回 false 时 out.error 说明原因。
inline bool CheckForUpdate(UpdateInfo& out, const wchar_t* url = kReleasesLatestUrl) {
    out = UpdateInfo();
    netutil::HttpResponse resp;
    if (!netutil::GetJson(url, L"", resp) || resp.status != 200) {
        if (resp.status == 404) {
            out.error = L"仓库暂未发布任何版本";
        } else if (!resp.error.empty()) {
            out.error = resp.error;
        } else {
            out.error = L"HTTP " + std::to_wstring(resp.status);
        }
        return false;
    }
    if (!ParseReleaseJson(resp.body, out)) {
        out.error = L"响应格式无法解析";
        return false;
    }
    out.fetched = true;
    if (out.assetUrl.empty()) {
        out.error = L"发布版中没有程序附件";
        return false;
    }
    out.available = CompareVersions(out.latestVersion, APP_VERSION) > 0;
    return true;
}

// 下载新版本 exe 到 destPath
inline bool DownloadUpdate(const std::wstring& url, const std::wstring& destPath,
                           std::wstring& err) {
    err.clear();
    netutil::HttpResponse resp;
    netutil::CancelFlag cancel;
    if (!netutil::GetBinary(url, resp, &cancel) || resp.status != 200 || resp.binary.empty()) {
        err = !resp.error.empty() ? resp.error
              : resp.status ? (L"HTTP " + std::to_wstring(resp.status))
                            : L"网络错误";
        return false;
    }
    if (!netutil::WriteFileBytes(destPath, resp.binary.data(), resp.binary.size())) {
        err = L"写入文件失败";
        return false;
    }
    return true;
}

// 新版本下载落点：与当前 exe 同目录的 .new 文件。
// 同卷改名替换不会跨盘复制，且避开 %TEMP%（杀软对临时目录的 exe 更敏感）。
inline std::wstring NewExeStagingPath() {
    wchar_t cur[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, cur, MAX_PATH);
    return std::wstring(cur) + L".new";
}

// 替换核心：旧 exe 改名 → 新 exe 归位（可选重启、可选延迟清理旧文件）。
// 兜底：改名被占用（杀毒/资源管理器/残留进程锁）时，交给 cmd 在本进程
// 退出后完成替换并重启——进程退出后文件锁必然解除。返回 true 表示
// 「替换已发生或必将发生」，调用方应立即退出当前进程。
inline bool SwapUpdateFiles(const std::wstring& curPath, const std::wstring& newExePath,
                            bool restart, bool cleanupOld) {
    std::wstring oldPath = curPath + L".old";
    DeleteFileW(oldPath.c_str());
    // Windows 允许对运行中的 exe 改名
    if (MoveFileExW(curPath.c_str(), oldPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        if (MoveFileExW(newExePath.c_str(), curPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            if (restart) {
                STARTUPINFOW si = {};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi = {};
                std::wstring cmd = L"\"" + curPath + L"\"";
                if (CreateProcessW(curPath.c_str(), cmd.data(), nullptr, nullptr,
                                   FALSE, 0, nullptr, nullptr, &si, &pi)) {
                    if (pi.hProcess) CloseHandle(pi.hProcess);
                    if (pi.hThread) CloseHandle(pi.hThread);
                }
                // 新程序没起来也不算失败：文件已就位，用户可手动启动
            }
            if (cleanupOld) {
                // 本进程退出后旧文件才解锁，交给系统延迟删除
                std::wstring del = L"cmd.exe /c ping -n 4 127.0.0.1 > nul & del /f /q \"" +
                                   oldPath + L"\"";
                STARTUPINFOW si2 = {};
                si2.cb = sizeof(si2);
                si2.dwFlags = STARTF_USESHOWWINDOW;
                si2.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi2 = {};
                CreateProcessW(nullptr, del.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &si2, &pi2);
                if (pi2.hProcess) CloseHandle(pi2.hProcess);
                if (pi2.hThread) CloseHandle(pi2.hThread);
            }
            return true;
        }
        // 第二步失败：把旧文件还原，走兜底
        MoveFileExW(oldPath.c_str(), curPath.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    // 兜底：3 秒后（本进程已退出）move 新文件到位并重启。
    // move /y 同卷执行；start 启动新程序。
    std::wstring cmd = L"cmd.exe /c ping -n 3 127.0.0.1 > nul & move /y \"" +
                       newExePath + L"\" \"" + curPath + L"\"";
    if (restart) cmd += L" & start \"\" \"" + curPath + L"\"";
    STARTUPINFOW si3 = {};
    si3.cb = sizeof(si3);
    si3.dwFlags = STARTF_USESHOWWINDOW;
    si3.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi3 = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si3, &pi3)) {
        return false; // 连兜底都起不来（极端情况）才报失败
    }
    if (pi3.hProcess) CloseHandle(pi3.hProcess);
    if (pi3.hThread) CloseHandle(pi3.hThread);
    return true;
}

// 应用更新并重启新程序。成功后调用方应立即退出当前进程。
inline bool ApplyUpdateAndRestart(const std::wstring& newExePath) {
    wchar_t cur[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, cur, MAX_PATH)) return false;
    return SwapUpdateFiles(cur, newExePath, true, true);
}

} // namespace updater
