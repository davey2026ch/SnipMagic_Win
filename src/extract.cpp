#pragma once
#include "extract.h"
#include "netutil.h"
#include "settings.h"
#include "ziputil.h"
#include "app.h"
#include "canvas.h"
#include <thread>
#include <commdlg.h>

using namespace Gdiplus;

namespace extract {
namespace {

enum : int {
    IDC_PROG_CANCEL = 4101,
    IDC_PROG_TEXT = 4102,
    IDC_PROG_BAR = 4103,
    WM_APP_TASK_DONE = WM_APP + 40
};

enum : int {
    IDC_RES_EDIT = 4201,
    IDC_RES_MD = 4202,
    IDC_RES_XLSX = 4203,
    IDC_RES_DOC = 4204,
    IDC_RES_COPY = 4205,
    IDC_RES_CLOSE = 4206
};

const wchar_t* kProgClass = L"ScreenshotToolProgressDlg";

static void DecodeImageSize(const std::string& bytes, int& w, int& h) {
    w = 0;
    h = 0;
    if (bytes.size() >= 24 &&
        static_cast<unsigned char>(bytes[0]) == 0x89 && bytes[1] == 'P') {
        auto be = [&](size_t o) {
            return (static_cast<unsigned>(static_cast<unsigned char>(bytes[o])) << 24) |
                   (static_cast<unsigned>(static_cast<unsigned char>(bytes[o + 1])) << 16) |
                   (static_cast<unsigned>(static_cast<unsigned char>(bytes[o + 2])) << 8) |
                   static_cast<unsigned>(static_cast<unsigned char>(bytes[o + 3]));
        };
        w = static_cast<int>(be(16));
        h = static_cast<int>(be(20));
        return;
    }
    if (bytes.size() > 4 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xD8) {
        size_t i = 2;
        while (i + 9 < bytes.size()) {
            if (static_cast<unsigned char>(bytes[i]) != 0xFF) { ++i; continue; }
            unsigned char marker = static_cast<unsigned char>(bytes[i + 1]);
            if (marker == 0xD8 || marker == 0xD9) { i += 2; continue; }
            if (i + 3 >= bytes.size()) break;
            unsigned seg = (static_cast<unsigned char>(bytes[i + 2]) << 8) |
                           static_cast<unsigned char>(bytes[i + 3]);
            if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
                if (i + 8 < bytes.size()) {
                    h = (static_cast<unsigned char>(bytes[i + 5]) << 8) | static_cast<unsigned char>(bytes[i + 6]);
                    w = (static_cast<unsigned char>(bytes[i + 7]) << 8) | static_cast<unsigned char>(bytes[i + 8]);
                }
                return;
            }
            if (seg < 2) break;
            i += 2 + seg;
        }
    }
}

struct ProgressState {
    HWND hwnd = nullptr;
    bool done = false;
    bool cancelledByUser = false;
    netutil::CancelFlag cancel;
    std::wstring status = L"准备中";
    std::wstring cancelText = L"取消识别";
    std::function<void()> work;
    HFONT font = nullptr;
};

struct ResultState {
    HWND hwnd = nullptr;
    bool done = false;
    ExtractResult* result = nullptr;
    HFONT font = nullptr;
};

// ---------- Progress dialog ----------
LRESULT CALLBACK ProgressProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ProgressState* st = reinterpret_cast<ProgressState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = static_cast<ProgressState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->hwnd = hwnd;
        // 必须交给 DefWindowProc：标题文字正是在这一步被存入窗口的，
        // 直接 return TRUE 会导致标题栏永远空白
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(40, 40, 40));
        // 与窗口背景同色（COLOR_WINDOW），否则文字后面会有一条浅灰色带
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_COMMAND:
        if (st && LOWORD(wParam) == IDC_PROG_CANCEL) {
            st->cancelledByUser = true;
            st->cancel.Cancel();
            netutil::AbortActiveHttp();
            SetWindowTextW(GetDlgItem(hwnd, IDC_PROG_TEXT), L"正在取消…");
            EnableWindow(GetDlgItem(hwnd, IDC_PROG_CANCEL), FALSE);
            // 打断后若工作线程仍卡住，1.2 秒后强制关窗，避免一直停在「正在取消」
            SetTimer(hwnd, 100, 1200, [](HWND hw, UINT, UINT_PTR, DWORD) {
                KillTimer(hw, 100);
                ProgressState* s = reinterpret_cast<ProgressState*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
                if (s && s->cancelledByUser) {
                    s->done = true;
                    DestroyWindow(hw);
                }
            });
            return 0;
        }
        return 0;
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlID == IDC_PROG_BAR) {
            Graphics g(dis->hDC);
            RECT rc = dis->rcItem;
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            SolidBrush bg(Color(255, 230, 230, 230));
            g.FillRectangle(&bg, 0, 0, w, h);
            static int phase = 0;
            phase = (phase + 1) % 20;
            int blockW = 28;
            int x = (phase * (w + blockW) / 20) - blockW;
            if (x < 0) x = 0;
            if (x > w - blockW) x = w - blockW;
            SolidBrush fg(Color(255, 0, 0x78, 0xD4));
            g.FillRectangle(&fg, x, 0, blockW, h);
            return TRUE;
        }
        return 0;
    }
    case WM_APP_TASK_DONE:
        if (st) {
            st->done = true;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        if (st) {
            st->cancelledByUser = true;
            st->cancel.Cancel();
        }
        return 0;
    case WM_DESTROY:
        if (st) st->done = true;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void EnsureProgClass(HINSTANCE hi) {
    static bool reg = false;
    if (reg) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ProgressProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
    wc.lpszClassName = kProgClass;
    RegisterClassExW(&wc);
    reg = true;
}

// Animated dots updater via timer in message loop
UINT_PTR ShowProgressAndRun(HWND owner, const wchar_t* title, ProgressState& st) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsureProgClass(hi);
    int w = 420, h = 160;
    RECT wr = {0, 0, w, h};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&wr, style, FALSE);
    int ow = wr.right - wr.left;
    int oh = wr.bottom - wr.top;
    // 弹窗显示在主窗口所在的显示器（多屏时不再固定弹到主屏）
    POINT pos = util::CenterOnMonitorOf(owner, ow, oh);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, kProgClass, title, style,
                                pos.x, pos.y, ow, oh,
                                owner, nullptr, hi, &st);
    if (!hwnd) return 0;
    st.hwnd = hwnd;
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, 0);

    st.font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    HWND text = CreateWindowW(L"STATIC", st.status.c_str(),
                              WS_CHILD | WS_VISIBLE | SS_LEFT,
                              20, 24, w - 40, 28, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROG_TEXT)), hi, nullptr);
    SendMessageW(text, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    HWND bar = CreateWindowW(L"STATIC", L"",
                             WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
                             20, 64, w - 40, 10, hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROG_BAR)), hi, nullptr);

    HWND cancelBtn = CreateWindowW(L"BUTTON", st.cancelText.empty() ? L"取消识别" : st.cancelText.c_str(),
                                   WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   w - 130, 112, 100, 32, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROG_CANCEL)), hi, nullptr);
    SendMessageW(cancelBtn, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);

    // simple marquee animation via subclassed static — paint dots on timer
    static int sAnim = 0;
    SetTimer(hwnd, 99, 300, [](HWND hw, UINT, UINT_PTR, DWORD) {
        ProgressState* s = reinterpret_cast<ProgressState*>(GetWindowLongPtrW(hw, GWLP_USERDATA));
        if (!s) return;
        sAnim = (sAnim + 1) % 4;
        std::wstring dots(sAnim, L'·');
        HWND t = GetDlgItem(hw, IDC_PROG_TEXT);
        if (t && !s->status.empty()) {
            // keep base status + animated dots if not cancelled
            if (!s->cancelledByUser) {
                std::wstring full = s->status + dots;
                SetWindowTextW(t, full.c_str());
            }
        }
        HWND barH = GetDlgItem(hw, IDC_PROG_BAR);
        if (barH) InvalidateRect(barH, nullptr, TRUE);
    });

    // owner-draw moving bar
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ProgressProc));
    // handle WM_DRAWITEM for bar via parent — add here after create using existing proc is hard.
    // We'll paint bar on WM_PAINT of parent: override by handling WM_DRAWITEM in ProgressProc.
    // Already need to add WM_DRAWITEM — patch via re-register isn't needed if we add to proc.
    // The ProgressProc above doesn't handle WM_DRAWITEM yet; subclass not needed if we extend.

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    std::thread worker([&st, hwnd]() {
        if (st.work) st.work();
        PostMessageW(hwnd, WM_APP_TASK_DONE, 0, 0);
    });
    worker.detach();

    MSG msg;
    while (!st.done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0 || r == -1) break;
        if (msg.message == WM_TIMER && msg.wParam == 99 && st.hwnd) {
            // redraw handled in timer proc
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    KillTimer(hwnd, 99);
    if (st.font) {
        DeleteObject(st.font);
        st.font = nullptr;
    }
    return 0;
}

// ---------- MinerU API steps ----------
bool MineruExtractOnBitmap(Bitmap* bmp, const std::wstring& tokenIn,
                           ProgressState& prog, ExtractResult& out) {
    const std::wstring token = util::TrimToken(tokenIn);
    if (!bmp) {
        out.errorMsg = L"没有可识别的图像";
        return false;
    }
    if (token.empty()) {
        out.errorMsg = L"请先在「设置」中填写 MinerU token";
        return false;
    }

    out.workDir = netutil::MakeTempDir(L"mineru_");
    std::wstring imgPath = out.workDir + L"\\screenshot.png";
    if (!util::SaveBitmapToFile(bmp, imgPath, false)) {
        out.errorMsg = L"临时图片保存失败";
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }
    std::vector<BYTE> imgBytes;
    if (!netutil::ReadFileBytes(imgPath, imgBytes) || imgBytes.empty()) {
        out.errorMsg = L"读取临时图片失败";
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }

    if (prog.cancel.IsCancelled()) {
        out.cancelled = true;
        out.errorMsg = L"已取消";
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }

    prog.status = L"正在申请上传地址";
    if (prog.hwnd && IsWindow(prog.hwnd)) {
        SetWindowTextW(GetDlgItem(prog.hwnd, IDC_PROG_TEXT), prog.status.c_str());
    }

    const std::wstring base = L"https://mineru.net/api/v4";
    std::string req =
        "{\"files\":[{\"name\":\"screenshot.png\",\"is_ocr\":true}],"
        "\"model_version\":\"vlm\",\"language\":\"ch\","
        "\"enable_table\":true,\"enable_formula\":false}";

    netutil::HttpResponse resp;
    if (!netutil::PostJson(base + L"/file-urls/batch", token, req, resp, &prog.cancel)) {
        out.cancelled = prog.cancel.IsCancelled();
        out.errorMsg = out.cancelled ? L"已取消" : (resp.error.empty() ? L"创建批次失败" : resp.error);
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }
    if (!resp.ok()) {
        out.errorMsg = netutil::ExtractApiError(resp);
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }

    netutil::Json j = netutil::Parse(resp.body);
    const netutil::Json* data = j.Find("data");
    if (!data) {
        out.errorMsg = L"响应缺少 data";
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }
    const netutil::Json* batch = data->Find("batch_id");
    const netutil::Json* urls = data->Find("file_urls");
    if (!batch || !urls || !urls->At(0)) {
        out.errorMsg = L"响应缺少 batch_id / file_urls";
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }
    std::wstring batchId = batch->AsWStr();
    std::wstring uploadUrl = urls->At(0)->AsWStr();

    if (prog.cancel.IsCancelled()) {
        out.cancelled = true;
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }

    prog.status = L"正在上传图片";
    if (prog.hwnd && IsWindow(prog.hwnd))
        SetWindowTextW(GetDlgItem(prog.hwnd, IDC_PROG_TEXT), prog.status.c_str());

    netutil::HttpResponse up;
    // 不带 Content-Type，纯二进制 PUT
    if (!netutil::PutBinary(uploadUrl, L"", imgBytes, true, up, &prog.cancel)) {
        out.cancelled = prog.cancel.IsCancelled();
        out.errorMsg = out.cancelled ? L"已取消" : L"图片上传失败";
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }
    if (!up.ok() && up.status != 200 && up.status != 204) {
        out.errorMsg = netutil::ExtractApiError(up);
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }

    prog.status = L"正在识别内容";
    if (prog.hwnd && IsWindow(prog.hwnd))
        SetWindowTextW(GetDlgItem(prog.hwnd, IDC_PROG_TEXT), prog.status.c_str());

    std::wstring zipUrl;
    const DWORD start = GetTickCount();
    const DWORD timeoutMs = 300000; // 300s
    while (true) {
        if (prog.cancel.IsCancelled()) {
            out.cancelled = true;
            out.errorMsg = L"已取消";
            netutil::DeletePathRecursive(out.workDir);
            out.workDir.clear();
            return false;
        }
        if (GetTickCount() - start > timeoutMs) {
            out.errorMsg = L"识别超时（300 秒）";
            netutil::DeletePathRecursive(out.workDir);
            out.workDir.clear();
            return false;
        }

        netutil::HttpResponse poll;
        std::wstring pollUrl = base + L"/extract-results/batch/" + batchId;
        if (!netutil::GetJson(pollUrl, token, poll, &prog.cancel)) {
            if (prog.cancel.IsCancelled()) {
                out.cancelled = true;
                netutil::DeletePathRecursive(out.workDir);
                out.workDir.clear();
                return false;
            }
            Sleep(2000);
            continue;
        }
        if (!poll.ok()) {
            out.errorMsg = netutil::ExtractApiError(poll);
            netutil::DeletePathRecursive(out.workDir);
            out.workDir.clear();
            return false;
        }

        netutil::Json pj = netutil::Parse(poll.body);
        const netutil::Json* pdata = pj.Find("data");
        const netutil::Json* er = pdata ? pdata->Find("extract_result") : nullptr;
        const netutil::Json* first = er ? er->At(0) : nullptr;
        if (!first) {
            Sleep(2000);
            continue;
        }
        const netutil::Json* state = first->Find("state");
        std::wstring st = state ? state->AsWStr() : L"";
        if (st == L"done") {
            const netutil::Json* z = first->Find("full_zip_url");
            zipUrl = z ? z->AsWStr() : L"";
            if (zipUrl.empty()) {
                out.errorMsg = L"识别完成但缺少 full_zip_url";
                netutil::DeletePathRecursive(out.workDir);
                out.workDir.clear();
                return false;
            }
            break;
        }
        if (st == L"failed") {
            const netutil::Json* e = first->Find("err_msg");
            out.errorMsg = e && !e->AsWStr().empty() ? e->AsWStr() : L"识别失败";
            netutil::DeletePathRecursive(out.workDir);
            out.workDir.clear();
            return false;
        }
        Sleep(2000);
    }

    if (prog.cancel.IsCancelled()) {
        out.cancelled = true;
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }

    prog.status = L"正在下载结果";
    if (prog.hwnd && IsWindow(prog.hwnd))
        SetWindowTextW(GetDlgItem(prog.hwnd, IDC_PROG_TEXT), prog.status.c_str());

    netutil::HttpResponse zipResp;
    if (!netutil::GetBinary(zipUrl, zipResp, &prog.cancel) || zipResp.binary.empty()) {
        out.cancelled = prog.cancel.IsCancelled();
        out.errorMsg = out.cancelled ? L"已取消" : L"下载识别结果失败";
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }

    std::wstring zipPath = out.workDir + L"\\result.zip";
    netutil::WriteFileBytes(zipPath, zipResp.binary.data(), zipResp.binary.size());
    std::wstring unpack = out.workDir + L"\\unpacked";
    CreateDirectoryW(unpack.c_str(), nullptr);
    if (!ziputil::ExtractZipShell(zipPath, unpack, L"full.md", 15000)) {
        // try find any .md
        std::wstring anyMd = ziputil::FindFileByName(unpack, L"full.md");
        if (anyMd.empty()) {
            out.errorMsg = L"解压识别结果失败";
            netutil::DeletePathRecursive(out.workDir);
            out.workDir.clear();
            return false;
        }
    }

    std::wstring mdPath = ziputil::FindFileByName(unpack, L"full.md");
    if (mdPath.empty()) {
        out.errorMsg = L"结果中未找到 full.md";
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }

    std::vector<BYTE> mdBytes;
    if (!netutil::ReadFileBytes(mdPath, mdBytes)) {
        out.errorMsg = L"读取 full.md 失败";
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
        return false;
    }
    out.markdownUtf8.assign(mdBytes.begin(), mdBytes.end());
    out.markdownWide = netutil::Utf8ToWide(out.markdownUtf8);

    // collect images from md directory
    size_t slash = mdPath.find_last_of(L"\\/");
    std::wstring mdDir = (slash == std::wstring::npos) ? unpack : mdPath.substr(0, slash);
    std::wstring imgDir = mdDir + L"\\images";
    std::vector<std::wstring> imgFiles;
    ziputil::ListDir(imgDir, imgFiles, false);
    if (imgFiles.empty()) {
        // sometimes images sit next to md
        std::vector<std::wstring> sibling;
        ziputil::ListDir(mdDir, sibling, false);
        for (const auto& f : sibling) {
            std::wstring low = f;
            for (auto& c : low) c = static_cast<wchar_t>(towlower(c));
            if (low.size() > 4 &&
                (low.rfind(L".png") == low.size() - 4 || low.rfind(L".jpg") == low.size() - 4 ||
                 low.rfind(L".jpeg") == low.size() - 5 || low.rfind(L".bmp") == low.size() - 4 ||
                 low.rfind(L".gif") == low.size() - 4 || low.rfind(L".webp") == low.size() - 5)) {
                imgFiles.push_back(f);
            }
        }
        for (const auto& f : imgFiles) {
            ExtractImage im;
            im.fileName = f;
            im.relPath = f;
            im.absPath = mdDir + L"\\" + f;
            std::vector<BYTE> bytes;
            if (netutil::ReadFileBytes(im.absPath, bytes)) {
                im.pngOrRaw.assign(bytes.begin(), bytes.end());
                DecodeImageSize(im.pngOrRaw, im.width, im.height);
            }
            out.images.push_back(std::move(im));
        }
    } else {
        for (const auto& f : imgFiles) {
            ExtractImage im;
            im.fileName = f;
            im.relPath = L"images/" + f;
            im.absPath = imgDir + L"\\" + f;
            std::vector<BYTE> bytes;
            if (netutil::ReadFileBytes(im.absPath, bytes)) {
                im.pngOrRaw.assign(bytes.begin(), bytes.end());
                DecodeImageSize(im.pngOrRaw, im.width, im.height);
            }
            out.images.push_back(std::move(im));
        }
    }

    out.success = true;
    // 图片字节已全部进内存，立刻物理删除临时目录（不进回收站）
    if (!out.workDir.empty()) {
        netutil::DeletePathRecursive(out.workDir);
        out.workDir.clear();
    }
    return true;
}

// ---------- helpers for export ----------
// 去掉 Markdown 图片语法 ![alt](path)；整行只有图片时整行删除
inline std::string StripImagesFromLine(const std::string& line) {
    std::string out;
    size_t i = 0;
    while (i < line.size()) {
        if (line[i] == '!' && i + 1 < line.size() && line[i + 1] == '[') {
            size_t br1 = line.find("](", i + 2);
            if (br1 != std::string::npos) {
                size_t end = line.find(')', br1 + 2);
                if (end != std::string::npos) {
                    i = end + 1;
                    continue;
                }
            }
        }
        out.push_back(line[i++]);
    }
    // trim trailing spaces left by removed tags
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '\t')) out.erase(out.begin());
    return out;
}

inline std::string StripMarkdownImagesUtf8(const std::string& md) {
    std::string out;
    size_t i = 0;
    bool first = true;
    while (i <= md.size()) {
        size_t nl = md.find('\n', i);
        if (nl == std::string::npos) nl = md.size();
        std::string line = md.substr(i, nl - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string cleaned = StripImagesFromLine(line);
        bool empty = cleaned.empty();
        // 整行是图片 → 不输出
        if (!empty || (!line.empty() && StripImagesFromLine(line).empty() &&
                       line.find("![") != std::string::npos)) {
            // if original had image and cleaned empty, skip
        }
        if (!(line.find("![") != std::string::npos && cleaned.empty())) {
            if (!first) out.push_back('\n');
            out += cleaned;
            first = false;
        } else {
            // skip image-only line; still need newline structure
            // do not emit blank for pure image lines
        }
        if (nl >= md.size()) break;
        i = nl + 1;
    }
    return out;
}

inline std::wstring StripMarkdownImagesWide(const std::wstring& md) {
    std::wstring out;
    size_t i = 0;
    bool first = true;
    while (i <= md.size()) {
        size_t nl = md.find(L'\n', i);
        if (nl == std::wstring::npos) nl = md.size();
        std::wstring line = md.substr(i, nl - i);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        // strip ![...](...)
        std::wstring cleaned;
        size_t p = 0;
        while (p < line.size()) {
            if (line[p] == L'!' && p + 1 < line.size() && line[p + 1] == L'[') {
                size_t br1 = line.find(L"](", p + 2);
                if (br1 != std::wstring::npos) {
                    size_t end = line.find(L')', br1 + 2);
                    if (end != std::wstring::npos) {
                        p = end + 1;
                        continue;
                    }
                }
            }
            cleaned.push_back(line[p++]);
        }
        while (!cleaned.empty() && (cleaned.back() == L' ' || cleaned.back() == L'\t')) cleaned.pop_back();
        while (!cleaned.empty() && (cleaned.front() == L' ' || cleaned.front() == L'\t')) cleaned.erase(cleaned.begin());

        bool imageOnly = (line.find(L"![") != std::wstring::npos) && cleaned.empty();
        if (!imageOnly) {
            if (!first) out.push_back(L'\n');
            out += cleaned;
            first = false;
        }
        if (nl >= md.size()) break;
        i = nl + 1;
    }
    return out;
}

std::wstring NormalizeEditNewlines(const std::wstring& s) {
    // Win32 EDIT 多行必须用 \r\n，仅 \n 不会换行显示
    std::wstring out;
    out.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\n') {
            if (i == 0 || s[i - 1] != L'\r') out.push_back(L'\r');
            out.push_back(L'\n');
        } else if (c == L'\r') {
            if (i + 1 >= s.size() || s[i + 1] != L'\n') out.push_back(L'\r');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

void CopyAllToClipboard(const ExtractResult& r) {
    std::wstring src = r.markdownWide.empty() ? netutil::Utf8ToWide(r.markdownUtf8) : r.markdownWide;
    netutil::TextToClipboard(StripMarkdownImagesWide(src));
}

void ExportMarkdown(HWND owner, const ExtractResult& r) {
    if (!r.success) return;
    if (!r.hasImages()) {
        std::wstring path = util::OpenSaveDialog(owner, true,
            L"Markdown (*.md)\0*.md\0所有文件\0*.*\0\0",
            L"md", L"提取内容.md", L"导出 Markdown", util::DownloadsDir().c_str());
        if (path.empty()) return;
        netutil::WriteFileBytes(path, r.markdownUtf8.data(), r.markdownUtf8.size());
        MessageBoxW(owner, (L"导出成功！\n" + path).c_str(), L"提示", MB_ICONINFORMATION);
        return;
    }
    // 有图：在所选位置新建「提取内容」文件夹（重名自动加 (2)(3)...），
    // 内含「提取内容.md」+ images 子目录；MD 保留原始相对引用，可直接打开
    std::wstring dir = util::BrowseFolder(owner, L"选择导出位置", util::DownloadsDir().c_str());
    if (dir.empty()) return;
    std::wstring base = dir + L"\\提取内容";
    std::wstring sub = base;
    for (int n = 2; GetFileAttributesW(sub.c_str()) != INVALID_FILE_ATTRIBUTES; ++n)
        sub = base + L" (" + std::to_wstring(n) + L")";
    if (!CreateDirectoryW(sub.c_str(), nullptr)) {
        MessageBoxW(owner, L"创建导出文件夹失败", L"错误", MB_ICONERROR);
        return;
    }
    CreateDirectoryW((sub + L"\\images").c_str(), nullptr);
    std::wstring mdPath = sub + L"\\提取内容.md";
    netutil::WriteFileBytes(mdPath, r.markdownUtf8.data(), r.markdownUtf8.size());
    for (const auto& im : r.images) {
        std::wstring dst = sub + L"\\images\\" + im.fileName;
        netutil::WriteFileBytes(dst, im.pngOrRaw.data(), im.pngOrRaw.size());
    }
    MessageBoxW(owner, (L"导出成功！\n" + sub).c_str(), L"提示", MB_ICONINFORMATION);
}

void ExportExcel(HWND owner, const ExtractResult& r) {
    if (!r.success) return;
    std::wstring path = util::OpenSaveDialog(owner, true,
        L"Excel (*.xlsx)\0*.xlsx\0所有文件\0*.*\0\0",
        L"xlsx", L"提取内容.xlsx", L"导出 Excel", util::DownloadsDir().c_str());
    if (path.empty()) return;

    auto findImgByRel = [&](const std::string& rel) -> const ExtractImage* {
        std::string relN = rel;
        for (auto& ch : relN) if (ch == '\\') ch = '/';
        size_t sl = relN.find_last_of('/');
        std::string base = (sl == std::string::npos) ? relN : relN.substr(sl + 1);
        if (base.empty()) return nullptr;
        for (const auto& im : r.images) {
            std::string fn = netutil::WideToUtf8(im.fileName);
            for (auto& ch : fn) if (ch == '\\') ch = '/';
            size_t s2 = fn.find_last_of('/');
            std::string fname = (s2 == std::string::npos) ? fn : fn.substr(s2 + 1);
            if (fname == base || (!fname.empty() && relN.find(fname) != std::string::npos))
                return &im;
        }
        return nullptr;
    };

    // 按原文顺序：先在对应行放入图片，再写去掉标签后的文字
    std::vector<std::vector<std::string>> rows;
    std::vector<ziputil::XlsxImage> imgs;
    std::string md = r.markdownUtf8;
    size_t i = 0;
    int rowIdx = 0;
    while (i <= md.size()) {
        size_t nl = md.find('\n', i);
        if (nl == std::string::npos) nl = md.size();
        std::string line = md.substr(i, nl - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // 1) 本行出现的图片 → 锚定到 rowIdx
        size_t pos = 0;
        int imgSeq = 0;
        while ((pos = line.find("![", pos)) != std::string::npos) {
            size_t br1 = line.find("](", pos + 2);
            size_t end = (br1 == std::string::npos) ? std::string::npos : line.find(')', br1 + 2);
            if (br1 == std::string::npos || end == std::string::npos) break;
            std::string rel = line.substr(br1 + 2, end - (br1 + 2));
            size_t sp = rel.find_first_of(" \t");
            if (sp != std::string::npos) rel = rel.substr(0, sp);
            const ExtractImage* src = findImgByRel(rel);
            if (src && !src->pngOrRaw.empty()) {
                ziputil::XlsxImage im;
                std::wstring fn = src->fileName;
                std::string name = "image_" + std::to_string(imgs.size() + 1);
                if (fn.find(L".jpg") != std::wstring::npos || fn.find(L".jpeg") != std::wstring::npos)
                    name += ".jpg";
                else
                    name += ".png";
                im.name = name;
                im.data = src->pngOrRaw;
                // 尺寸交给导出时从字节解析，保证宽高比正确
                im.widthPx = src->width;
                im.heightPx = src->height;
                im.atRow = rowIdx;
                imgs.push_back(std::move(im));
                ++imgSeq;
            }
            pos = end + 1;
        }
        (void)imgSeq;

        // 2) 再去掉 image 代码，本行文字写入表格（纯图片行留空占位，保持位置）
        std::string text = StripImagesFromLine(line);
        rows.push_back({text});
        rowIdx++;
        if (nl >= md.size()) break;
        i = nl + 1;
    }
    if (rows.empty()) rows.push_back({""});

    // 未在 md 中引用的图片：按原顺序挂在文末对应位置之后
    int lastRow = rowIdx;
    for (const auto& im : r.images) {
        bool used = false;
        for (const auto& x : imgs) {
            if (x.data.size() == im.pngOrRaw.size() && !x.data.empty() && x.data == im.pngOrRaw) {
                used = true;
                break;
            }
        }
        if (used) continue;
        ziputil::XlsxImage x;
        std::wstring fn = im.fileName;
        x.name = "image_" + std::to_string(imgs.size() + 1);
        if (fn.find(L".jpg") != std::wstring::npos || fn.find(L".jpeg") != std::wstring::npos)
            x.name += ".jpg";
        else
            x.name += ".png";
        x.data = im.pngOrRaw;
        // 宽高由 ExportXlsx 从字节解析
        x.widthPx = im.width;
        x.heightPx = im.height;
        x.atRow = lastRow;
        imgs.push_back(std::move(x));
        ++lastRow;
        rows.push_back({""});
    }

    if (!ziputil::ExportXlsx(path, rows, imgs)) {
        MessageBoxW(owner, L"导出 Excel 失败", L"错误", MB_ICONERROR);
    } else {
        MessageBoxW(owner, (L"导出成功！\n" + path).c_str(), L"提示", MB_ICONINFORMATION);
    }
}

void ExportWord(HWND owner, const ExtractResult& r) {
    if (!r.success) return;
    std::wstring path = util::OpenSaveDialog(owner, true,
        L"Word (*.docx)\0*.docx\0所有文件\0*.*\0\0",
        L"docx", L"提取内容.docx", L"导出 Word", util::DownloadsDir().c_str());
    if (path.empty()) return;

    // 正文按原文排版嵌图；docx 内不会出现 ![...] 标签
    std::vector<std::pair<std::wstring, std::string>> imgs;
    for (const auto& im : r.images) {
        imgs.push_back({im.fileName, im.pngOrRaw});
    }
    if (!ziputil::ExportDocx(path, r.markdownUtf8, imgs)) {
        MessageBoxW(owner, L"导出 Word 失败", L"错误", MB_ICONERROR);
    } else {
        MessageBoxW(owner, (L"导出成功！\n" + path).c_str(), L"提示", MB_ICONINFORMATION);
    }
}

LRESULT CALLBACK ResultProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ResultState* st = reinterpret_cast<ResultState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = static_cast<ResultState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->hwnd = hwnd;
        // 必须交给 DefWindowProc：标题文字正是在这一步被存入窗口的，
        // 直接 return TRUE 会导致标题栏永远空白
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(40, 40, 40));
        // 与窗口背景同色（COLOR_WINDOW），否则文字后面会有一条浅灰色带
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(30, 30, 30));
        SetBkColor(hdc, RGB(255, 255, 255));
        static HBRUSH brEdit = CreateSolidBrush(RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(brEdit);
    }
    case WM_COMMAND: {
        if (!st || !st->result) return 0;
        int id = LOWORD(wParam);
        switch (id) {
        case IDC_RES_MD: ExportMarkdown(hwnd, *st->result); break;
        case IDC_RES_XLSX: ExportExcel(hwnd, *st->result); break;
        case IDC_RES_DOC: ExportWord(hwnd, *st->result); break;
        case IDC_RES_COPY:
            CopyAllToClipboard(*st->result);
            MessageBoxW(hwnd, L"已复制全部内容到剪贴板", L"提示", MB_ICONINFORMATION);
            break;
        case IDC_RES_CLOSE:
            st->done = true;
            DestroyWindow(hwnd);
            break;
        default: break;
        }
        return 0;
    }
    case WM_CLOSE:
        if (st) { st->done = true; DestroyWindow(hwnd); }
        return 0;
    case WM_DESTROY:
        if (st) st->done = true;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void ShowResultDialog(HWND owner, ExtractResult& result) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ResultProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
        wc.lpszClassName = L"ScreenshotToolExtractResultDlg";
        RegisterClassExW(&wc);
        reg = true;
    }

    ResultState st;
    st.result = &result;

    int w = 720, h = 520;
    RECT wr = {0, 0, w, h};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&wr, style, FALSE);
    int ow = wr.right - wr.left;
    int oh = wr.bottom - wr.top;
    // 弹窗显示在主窗口所在的显示器（多屏时不再固定弹到主屏）
    POINT pos = util::CenterOnMonitorOf(owner, ow, oh);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, L"ScreenshotToolExtractResultDlg", L"提取内容",
                                style, pos.x, pos.y, ow, oh,
                                owner, nullptr, hi, &st);
    if (!hwnd) return;
    st.hwnd = hwnd;
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, 0);

    st.font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    // 多行自动换行：Win32 EDIT 需要 \r\n，按 MinerU 原文换行
    std::wstring displayRaw = result.markdownWide.empty()
                                 ? netutil::Utf8ToWide(result.markdownUtf8)
                                 : result.markdownWide;
    // 显示与复制都不含图片代码标签
    std::wstring displayText = NormalizeEditNewlines(StripMarkdownImagesWide(displayRaw));
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", displayText.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_WANTRETURN,
                                12, 12, w - 24, h - 80, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RES_EDIT)), hi, nullptr);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
    SendMessageW(edit, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    // monospace-ish for md
    HFONT fontM = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    if (fontM) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(fontM), TRUE);

    int by = h - 56;
    struct B { int id; const wchar_t* text; int x; int bw; };
    const B btns[] = {
        {IDC_RES_MD, L"导出 Markdown", 12, 120},
        {IDC_RES_XLSX, L"导出 Excel", 140, 100},
        {IDC_RES_DOC, L"导出 Word", 248, 100},
        {IDC_RES_COPY, L"复制全部", 356, 90},
        {IDC_RES_CLOSE, L"关闭", w - 102, 90},
    };
    for (const auto& b : btns) {
        HWND btn = CreateWindowW(L"BUTTON", b.text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 b.x, by, b.bw, 32, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(b.id)), hi, nullptr);
        SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(st.font), TRUE);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (!st.done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0 || r == -1) break;
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (st.font) DeleteObject(st.font);
    if (fontM) DeleteObject(fontM);
}

// ---------- Magic erase ----------
std::unique_ptr<Bitmap> BitmapFromBytes(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!hMem) return nullptr;
    void* p = GlobalLock(hMem);
    if (!p) { GlobalFree(hMem); return nullptr; }
    memcpy(p, bytes.data(), bytes.size());
    GlobalUnlock(hMem);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK) {
        GlobalFree(hMem);
        return nullptr;
    }
    auto bmp = std::make_unique<Bitmap>(stream);
    stream->Release();
    if (bmp->GetLastStatus() != Ok) return nullptr;
    std::unique_ptr<Bitmap> cloned(
        bmp->Clone(0, 0, bmp->GetWidth(), bmp->GetHeight(), PixelFormat32bppARGB));
    return cloned;
}

bool VolcUpload(const std::wstring& apiKey, const std::vector<BYTE>& png,
                std::wstring& fileId, std::wstring& uploadUrl,
                std::wstring& err, netutil::CancelFlag& cancel) {
    const std::wstring base = L"https://mediakit.cn-beijing.volces.com/api/v1";
    const std::wstring key = util::TrimToken(apiKey);
    netutil::HttpResponse resp;
    if (!netutil::PostJson(base + L"/tools-sync/request-media-upload-url", key, "{}", resp, &cancel)) {
        err = cancel.IsCancelled() ? L"已取消" : (resp.error.empty() ? L"申请上传地址失败" : resp.error);
        return false;
    }
    if (!resp.ok()) {
        err = L"申请上传地址失败：" + netutil::ExtractApiError(resp);
        return false;
    }
    netutil::Json j = netutil::Parse(resp.body);
    if (const netutil::Json* s = j.Find("success")) {
        if (s->type == netutil::Json::Type::Bool && !s->AsBool()) {
            err = L"申请上传地址失败：" + netutil::ExtractApiError(resp);
            return false;
        }
    }
    const netutil::Json* result = j.Find("result");
    if (!result) result = &j;
    const netutil::Json* fid = result->Find("file_id");
    const netutil::Json* uurl = result->Find("upload_url");
    if (!fid || !uurl) {
        err = L"上传地址响应缺少 file_id / upload_url";
        return false;
    }
    fileId = fid->AsWStr();
    uploadUrl = uurl->AsWStr();
    if (fileId.empty() || uploadUrl.empty()) {
        err = L"上传地址为空";
        return false;
    }
    // 接口返回的 file_id 可能已带 mediakit:// 前缀，调用侧不要再拼

    // 预签名 PUT：不带 Content-Type / Authorization（实测 200）
    netutil::HttpResponse up;
    netutil::PutBinary(uploadUrl, L"", png, true, up, &cancel);
    if (up.status < 200 || up.status >= 300) {
        netutil::PutBinary(uploadUrl, L"", png, false, up, &cancel, L"application/octet-stream");
    }
    if (up.status < 200 || up.status >= 300) {
        err = L"上传图像失败 HTTP " + std::to_wstring(up.status);
        if (!up.body.empty()) err += L" | " + netutil::Utf8ToWide(up.body.substr(0, 200));
        else if (!up.error.empty()) err += L" | " + up.error;
        return false;
    }
    return true;
}

static std::wstring EnsureMediakitUrl(const std::wstring& fileIdOrUrl) {
    if (fileIdOrUrl.rfind(L"mediakit://", 0) == 0) return fileIdOrUrl;
    return L"mediakit://" + fileIdOrUrl;
}

static bool VolcJsonSuccess(const netutil::Json& j, std::wstring& err, const netutil::HttpResponse& resp) {
    if (const netutil::Json* s = j.Find("success")) {
        if (s->type == netutil::Json::Type::Bool && !s->AsBool()) {
            err = netutil::ExtractApiError(resp);
            return false;
        }
    }
    return true;
}

bool VolcEraseImage(const std::wstring& apiKey, const std::wstring& imageUrl,
                    bool useArea, double x1, double y1, double x2, double y2,
                    const std::wstring& maskUrl,
                    std::vector<BYTE>& outPng, std::wstring& err, netutil::CancelFlag& cancel) {
    const std::wstring base = L"https://mediakit.cn-beijing.volces.com/api/v1";
    const std::wstring key = util::TrimToken(apiKey);
    std::string body = "{\"image_url\":\"" + netutil::JsonEscape(netutil::WideToUtf8(EnsureMediakitUrl(imageUrl))) +
                       "\",\"standard_scene\":\"selected_area_erase\",\"output_format\":\"png\"";
    if (useArea) {
        char area[256];
        sprintf_s(area,
                  ",\"selected_area\":{\"top_left_x\":%.6f,\"top_left_y\":%.6f,"
                  "\"bottom_right_x\":%.6f,\"bottom_right_y\":%.6f}",
                  x1, y1, x2, y2);
        body += area;
    } else if (!maskUrl.empty()) {
        body += ",\"mask_url\":\"" + netutil::JsonEscape(netutil::WideToUtf8(EnsureMediakitUrl(maskUrl))) + "\"";
    }
    body += "}";

    netutil::HttpResponse resp;
    if (!netutil::PostJson(base + L"/tools-sync/erase-image", key, body, resp, &cancel)) {
        err = cancel.IsCancelled() ? L"已取消" : (resp.error.empty() ? L"请求失败" : resp.error);
        return false;
    }
    // 注意：火山可能 HTTP 200 + success:false
    netutil::Json j = netutil::Parse(resp.body);
    if (!resp.ok()) {
        err = L"消除接口失败：" + netutil::ExtractApiError(resp);
        return false;
    }
    if (!VolcJsonSuccess(j, err, resp)) {
        if (err.empty()) err = L"消除接口失败";
        return false;
    }
    const netutil::Json* result = j.Find("result");
    if (!result) result = &j;
    const netutil::Json* img = result->Find("image_url");
    if (!img || img->AsWStr().empty()) {
        err = L"响应缺少 result.image_url";
        return false;
    }
    netutil::HttpResponse down;
    if (!netutil::GetBinary(img->AsWStr(), down, &cancel) || down.binary.empty()) {
        err = cancel.IsCancelled() ? L"已取消" : L"下载结果图失败";
        return false;
    }
    outPng = std::move(down.binary);
    return true;
}

void RunMagicEraseImpl(HWND owner, Document* doc, ProgressState& prog, ExtractResult& unused) {
    (void)unused;
    if (!doc || !doc->base) {
        MessageBoxW(owner, L"没有活动截图", L"魔法消除", MB_ICONWARNING);
        return;
    }
    const std::wstring key = util::TrimToken(Settings().volcApiKey);
    if (key.empty()) {
        MessageBoxW(owner, L"请先在「设置」中填写火山 API Key", L"魔法消除", MB_ICONWARNING);
        return;
    }

    int rx = 0, ry = 0, rw = 0, rh = 0;
    bool hasRegion = doc->GetRegion(rx, ry, rw, rh);

    // collect brush annotations
    std::vector<int> brushIdx;
    RectF brushBounds(0, 0, 0, 0);
    bool hasBrush = false;
    for (size_t i = 0; i < doc->annotations.size(); ++i) {
        auto* a = doc->annotations[i].get();
        if (!a || a->type != AnnType::Brush) continue;
        brushIdx.push_back(static_cast<int>(i));
        RectF b;
        a->GetBounds(b);
        if (!hasBrush) {
            brushBounds = b;
            hasBrush = true;
        } else {
            float L = (std::min)(brushBounds.X, b.X);
            float T = (std::min)(brushBounds.Y, b.Y);
            float R = (std::max)(brushBounds.X + brushBounds.Width, b.X + b.Width);
            float B = (std::max)(brushBounds.Y + brushBounds.Height, b.Y + b.Height);
            brushBounds = RectF(L, T, R - L, B - T);
        }
    }

    if (!hasRegion && !hasBrush) {
        MessageBoxW(owner, L"请先用「选择」框选区域，或用「笔刷」涂抹要消除的内容",
                    L"魔法消除", MB_ICONINFORMATION);
        return;
    }

    // crop + 48px context
    const int pad = 48;
    int ox = 0, oy = 0, cw = 0, ch = 0;
    if (hasRegion) {
        ox = (std::max)(0, rx - pad);
        oy = (std::max)(0, ry - pad);
        int ex = (std::min)(doc->Width(), rx + rw + pad);
        int ey = (std::min)(doc->Height(), ry + rh + pad);
        cw = ex - ox;
        ch = ey - oy;
    } else {
        int bx = static_cast<int>(std::floor(brushBounds.X));
        int by = static_cast<int>(std::floor(brushBounds.Y));
        int bw = static_cast<int>(std::ceil(brushBounds.Width));
        int bh = static_cast<int>(std::ceil(brushBounds.Height));
        ox = (std::max)(0, bx - pad);
        oy = (std::max)(0, by - pad);
        int ex = (std::min)(doc->Width(), bx + bw + pad);
        int ey = (std::min)(doc->Height(), by + bh + pad);
        cw = ex - ox;
        ch = ey - oy;
    }
    if (cw < 2 || ch < 2) {
        MessageBoxW(owner, L"选区无效", L"魔法消除", MB_ICONWARNING);
        return;
    }

    auto composite = doc->RenderComposite();
    if (!composite) {
        MessageBoxW(owner, L"图像合成失败", L"魔法消除", MB_ICONWARNING);
        return;
    }
    auto crop = util::CropBitmap(composite.get(), ox, oy, cw, ch);
    if (!crop) {
        MessageBoxW(owner, L"裁剪失败", L"魔法消除", MB_ICONWARNING);
        return;
    }

    // 火山接口对分辨率有限制（错误 800012：短边过小、长宽比过大都会被拒，
    // 例如细长竖条选区 304×1792）。归一化到安全范围：
    //   长边 ≤ 2000；短边 ≥ 512；长宽比 ≤ 2。
    // 内容固定贴在左上角，不足处在右侧/下方做边缘延展，结果只取内容区域，
    // 因此区域/遮罩坐标只需乘统一缩放比，无需关心补边。
    int sendW = cw, sendH = ch;
    double scale = 1.0;
    {
        const int kMaxSide = 2000;
        int longSide = (std::max)(cw, ch);
        if (longSide > kMaxSide) scale = static_cast<double>(kMaxSide) / longSide;
    }
    int contentW = (std::max)(1, static_cast<int>(std::lround(cw * scale)));
    int contentH = (std::max)(1, static_cast<int>(std::lround(ch * scale)));
    sendW = contentW;
    sendH = contentH;
    {
        const int kMinSide = 512;
        int mn = (std::min)(sendW, sendH);
        int mx = (std::max)(sendW, sendH);
        int target = (std::max)(kMinSide, (mx + 1) / 2); // 长宽比压到 2:1 以内
        if (mn < target) {
            if (sendW <= sendH) sendW = target; else sendH = target;
        }
    }

    std::unique_ptr<Bitmap> send;
    if (sendW == cw && sendH == ch) {
        send.reset(crop.release());
    } else {
        send = std::make_unique<Bitmap>(sendW, sendH, PixelFormat32bppARGB);
        Graphics sg(send.get());
        sg.SetInterpolationMode(scale < 1.0
            ? Gdiplus::InterpolationModeHighQualityBicubic
            : Gdiplus::InterpolationModeNearestNeighbor);
        sg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        // 1) 内容区等比缩放
        sg.DrawImage(crop.get(), Gdiplus::Rect(0, 0, contentW, contentH),
                     0, 0, cw, ch, Gdiplus::UnitPixel);
        // 2) 右侧/下方不足处：拉伸最后一列/一行像素做边缘延展（视觉无缝）。
        //    目的矩形向外多画 2px，靠位图边界裁剪兜底——GDI+ 在位图最右/最下
        //    一列有舍入问题，恰好按 rect 绘制时最后一列可能漏绘（实测）。
        if (contentW < sendW) {
            sg.DrawImage(crop.get(),
                         Gdiplus::Rect(contentW, 0, sendW - contentW + 2, contentH),
                         cw - 1, 0, 1, ch, Gdiplus::UnitPixel);
        }
        if (contentH < sendH) {
            sg.DrawImage(crop.get(),
                         Gdiplus::Rect(0, contentH, contentW, sendH - contentH + 2),
                         0, ch - 1, cw, 1, Gdiplus::UnitPixel);
        }
        if (contentW < sendW && contentH < sendH) {
            sg.DrawImage(crop.get(),
                         Gdiplus::Rect(contentW, contentH,
                                       sendW - contentW + 2, sendH - contentH + 2),
                         cw - 1, ch - 1, 1, 1, Gdiplus::UnitPixel);
        }
    }
    crop.reset();

    std::string cropPng;
    if (!ziputil::BitmapToPngBytes(send.get(), cropPng)) {
        MessageBoxW(owner, L"PNG 编码失败", L"魔法消除", MB_ICONWARNING);
        return;
    }
    std::vector<BYTE> cropBytes(cropPng.begin(), cropPng.end());

    // 全程只提示「正在消除中」
    prog.status = L"正在消除中";
    if (prog.hwnd && IsWindow(prog.hwnd))
        SetWindowTextW(GetDlgItem(prog.hwnd, IDC_PROG_TEXT), prog.status.c_str());

    std::wstring imgFileId, imgUpload, upErr;
    if (!VolcUpload(key, cropBytes, imgFileId, imgUpload, upErr, prog.cancel)) {
        if (prog.cancel.IsCancelled()) return;
        MessageBoxW(owner, upErr.empty() ? L"上传图像失败，请检查火山 API Key" : upErr.c_str(),
                    L"魔法消除", MB_ICONERROR);
        return;
    }

    std::wstring maskUrl;
    bool useArea = false;
    double nx1 = 0, ny1 = 0, nx2 = 0, ny2 = 0;

    if (hasRegion) {
        useArea = true;
        nx1 = static_cast<double>(rx - ox) * scale / static_cast<double>(sendW);
        ny1 = static_cast<double>(ry - oy) * scale / static_cast<double>(sendH);
        nx2 = static_cast<double>(rx + rw - ox) * scale / static_cast<double>(sendW);
        ny2 = static_cast<double>(ry + rh - oy) * scale / static_cast<double>(sendH);
        nx1 = (std::max)(0.0, (std::min)(1.0, nx1));
        ny1 = (std::max)(0.0, (std::min)(1.0, ny1));
        nx2 = (std::max)(0.0, (std::min)(1.0, nx2));
        ny2 = (std::max)(0.0, (std::min)(1.0, ny2));
    } else {
        // RGB mask: white = erase（遮罩尺寸与发送图一致，坐标按统一缩放比映射）
        Bitmap mask(sendW, sendH, PixelFormat24bppRGB);
        {
            Graphics g(&mask);
            g.Clear(Color(255, 0, 0, 0));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            for (int idx : brushIdx) {
                auto* f = dynamic_cast<FreehandAnn*>(doc->annotations[idx].get());
                if (!f) continue;
                float tw = (std::max)(1.0f, static_cast<float>(f->style.thickness) *
                                               static_cast<float>(scale));
                Pen p(Color(255, 255, 255), tw);
                p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
                p.SetLineJoin(LineJoinRound);
                if (f->points.size() == 1) {
                    float r = tw * 0.5f;
                    SolidBrush br(Color(255, 255, 255));
                    g.FillEllipse(&br,
                                  (f->points[0].X - ox) * static_cast<float>(scale) - r,
                                  (f->points[0].Y - oy) * static_cast<float>(scale) - r,
                                  r * 2, r * 2);
                } else if (f->points.size() >= 2) {
                    std::vector<PointF> pts;
                    pts.reserve(f->points.size());
                    for (const auto& pt : f->points) {
                        pts.push_back(PointF((pt.X - ox) * static_cast<float>(scale),
                                             (pt.Y - oy) * static_cast<float>(scale)));
                    }
                    g.DrawLines(&p, pts.data(), static_cast<INT>(pts.size()));
                }
            }
        }
        std::string maskPng;
        if (!ziputil::BitmapToPngBytes(&mask, maskPng)) {
            MessageBoxW(owner, L"遮罩编码失败", L"魔法消除", MB_ICONERROR);
            return;
        }
        std::vector<BYTE> maskBytes(maskPng.begin(), maskPng.end());
        std::wstring maskFileId, maskUpload, maskErr;
        if (!VolcUpload(key, maskBytes, maskFileId, maskUpload, maskErr, prog.cancel)) {
            if (prog.cancel.IsCancelled()) return;
            MessageBoxW(owner, maskErr.empty() ? L"上传遮罩失败" : maskErr.c_str(), L"魔法消除", MB_ICONERROR);
            return;
        }
        maskUrl = EnsureMediakitUrl(maskFileId);
    }

    prog.status = L"正在消除中";
    if (prog.hwnd && IsWindow(prog.hwnd))
        SetWindowTextW(GetDlgItem(prog.hwnd, IDC_PROG_TEXT), prog.status.c_str());

    std::wstring imageUrl = EnsureMediakitUrl(imgFileId);
    std::vector<BYTE> resultBytes;
    std::wstring err;
    if (!VolcEraseImage(key, imageUrl, useArea, nx1, ny1, nx2, ny2, maskUrl, resultBytes, err, prog.cancel)) {
        if (prog.cancel.IsCancelled()) return;
        MessageBoxW(owner, err.empty() ? L"魔法消除失败" : err.c_str(), L"魔法消除", MB_ICONERROR);
        return;
    }

    auto resultBmp = BitmapFromBytes(resultBytes);
    if (!resultBmp) {
        MessageBoxW(owner, L"结果图解析失败", L"魔法消除", MB_ICONERROR);
        return;
    }

    doc->PushUndo();
    // 直接烙进底图：消除结果覆盖写入 base 像素，不生成可选中/可拖动的标注图层。
    // 发送图可能做过归一化（缩放/补边），这里只取结果的内容区域并还原到选区原尺寸。
    {
        Gdiplus::Graphics g(doc->base.get());
        const UINT resW = resultBmp->GetWidth();
        const UINT resH = resultBmp->GetHeight();
        // 结果的内容区域（发送图左上角 contentW×contentH），防止越界
        INT srcW = static_cast<INT>(std::min<UINT>(resW, static_cast<UINT>(contentW)));
        INT srcH = static_cast<INT>(std::min<UINT>(resH, static_cast<UINT>(contentH)));
        if (srcW == cw && srcH == ch) {
            // 未归一化：逐像素回写，保证无损
            g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            g.DrawImage(resultBmp.get(), Gdiplus::Rect(ox, oy, cw, ch),
                        0, 0, srcW, srcH, Gdiplus::UnitPixel);
        } else {
            // 有缩放：高质量重采样还原到选区原尺寸
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            g.DrawImage(resultBmp.get(), Gdiplus::Rect(ox, oy, cw, ch),
                        0, 0, srcW, srcH, Gdiplus::UnitPixel);
        }
    }
    resultBmp.reset();

    // remove consumed brush strokes
    if (!brushIdx.empty()) {
        for (int i = static_cast<int>(brushIdx.size()) - 1; i >= 0; --i) {
            int idx = brushIdx[i];
            if (idx >= 0 && idx < static_cast<int>(doc->annotations.size())) {
                if (doc->annotations[idx] && doc->annotations[idx]->type == AnnType::Brush) {
                    doc->annotations.erase(doc->annotations.begin() + idx);
                }
            }
        }
        doc->selectedIdx = -1;
    }
    if (hasRegion) doc->ClearRegion();
    doc->selectedIdx = -1;

    Canvas::Instance().Refresh();
    App::Instance().ShowStatusMessage(L"魔法消除完成");
}

} // namespace

void RunExtractFlow(HWND owner, Document* doc) {
    if (!doc || !doc->base) {
        MessageBoxW(owner, L"请先截图或打开页签", L"提取内容", MB_ICONWARNING);
        return;
    }
    std::wstring token = util::TrimToken(Settings().mineruToken);
    if (token.empty()) {
        MessageBoxW(owner, L"请先在「设置」中填写 MinerU token", L"提取内容", MB_ICONINFORMATION);
        return;
    }

    auto composite = doc->RenderComposite();
    if (!composite) {
        MessageBoxW(owner, L"图像合成失败", L"提取内容", MB_ICONWARNING);
        return;
    }

    ExtractResult result;
    ProgressState prog;
    prog.status = L"正在识别";
    prog.cancelText = L"取消识别";
    std::unique_ptr<Bitmap> clone(
        composite->Clone(0, 0, composite->GetWidth(), composite->GetHeight(), PixelFormat32bppARGB));
    Bitmap* raw = clone.get();

    prog.work = [&]() {
        MineruExtractOnBitmap(raw, token, prog, result);
    };
    ShowProgressAndRun(owner, L"提取内容", prog);

    // 中间产物：成功时在内存就绪后立刻物理删除；失败/取消也删除
    auto wipeWorkDir = [&]() {
        if (!result.workDir.empty()) {
            netutil::DeletePathRecursive(result.workDir);
            result.workDir.clear();
        }
    };

    if (!result.success) {
        wipeWorkDir();
        if (!result.cancelled && !result.errorMsg.empty()) {
            MessageBoxW(owner, result.errorMsg.c_str(), L"提取内容", MB_ICONERROR);
        } else if (result.cancelled) {
            App::Instance().ShowStatusMessage(L"已取消识别");
        }
        return;
    }

    // 结果已在内存（markdown + 图片字节），立刻删掉磁盘临时目录
    wipeWorkDir();

    if (result.markdownWide.empty()) result.markdownWide = netutil::Utf8ToWide(result.markdownUtf8);
    // 剪贴板也不含图片标签
    netutil::TextToClipboard(StripMarkdownImagesWide(result.markdownWide));
    App::Instance().ShowStatusMessage(L"识别完成，文本已写入剪贴板");

    ShowResultDialog(owner, result);
    wipeWorkDir(); // double-check
}

void RunMagicErase(HWND owner, Document* doc) {
    ExtractResult dummy;
    ProgressState prog;
    prog.status = L"正在消除中";
    prog.cancelText = L"取消消除";
    prog.work = [&]() {
        RunMagicEraseImpl(owner, doc, prog, dummy);
    };
    ShowProgressAndRun(owner, L"魔法消除", prog);
    Canvas::Instance().Refresh();
    App::Instance().UpdateStatus();
}

} // namespace extract
