#include "app.h"
#include "settings.h"
#include "settingsdlg.h"
#include "overlay.h"
#include "colorpicker.h"
#include "compareview.h"
#include "extract.h"
#include "longcapture.h"
#include "version.h"
#include "updater.h"
#include <winuser.h>
#include <thread>

using namespace Gdiplus;

namespace {
const wchar_t* kMainClass = L"ScreenshotToolMainWindow";
const int kHotkeyId = 1;
const int kLongHotkeyId = 2;

// ---- 自动更新检测的后台结果槽（单实例程序，静态槽即可） ----
updater::UpdateInfo g_updateInfo;
std::wstring g_updateDest;   // 下载好的新 exe 路径
std::wstring g_updateErr;    // 下载失败原因

bool IsDark() { return Settings().IsDarkTheme(); }

const wchar_t* kTipClass = L"ScreenshotToolTooltip";

HICON g_appIcon = nullptr;
HICON g_appIconSm = nullptr;
HICON g_blankIcon = nullptr;

// 快捷键被其他程序占用时：用低级键盘钩子强制接管
// 用 void* 存句柄，避免个别 SDK/宏环境下 HHOOK 不可见
void* g_kbHook = nullptr;
bool g_useHotkeyHook = false;

bool HotkeyModifiersDown(UINT mods) {
    if ((mods & MOD_CONTROL) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return false;
    if ((mods & MOD_SHIFT) && !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) return false;
    if ((mods & MOD_ALT) && !(GetAsyncKeyState(VK_MENU) & 0x8000)) return false;
    if ((mods & MOD_WIN) &&
        !(GetAsyncKeyState(VK_LWIN) & 0x8000) &&
        !(GetAsyncKeyState(VK_RWIN) & 0x8000)) {
        return false;
    }
    return true;
}

void RemoveHotkeyHook() {
    g_useHotkeyHook = false;
    if (g_kbHook) {
        UnhookWindowsHookEx(reinterpret_cast<HHOOK>(g_kbHook));
        g_kbHook = nullptr;
    }
}

LRESULT CALLBACK HotkeyKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_useHotkeyHook &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && lParam) {
        const auto* ks = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const auto& s = Settings();
        HWND hwnd = App::Instance().Hwnd();
        if (!hwnd) return CallNextHookEx(reinterpret_cast<HHOOK>(g_kbHook), nCode, wParam, lParam);

        if (ks->vkCode == s.hotkeyVk && HotkeyModifiersDown(s.hotkeyModifiers)) {
            PostMessageW(hwnd, WM_HOTKEY, static_cast<WPARAM>(kHotkeyId), 0);
            return 1;
        }
        if (ks->vkCode == s.longHotkeyVk && HotkeyModifiersDown(s.longHotkeyModifiers)) {
            PostMessageW(hwnd, WM_HOTKEY, static_cast<WPARAM>(kLongHotkeyId), 0);
            return 1;
        }
    }
    return CallNextHookEx(reinterpret_cast<HHOOK>(g_kbHook), nCode, wParam, lParam);
}

void InstallHotkeyHook() {
    RemoveHotkeyHook();
    g_useHotkeyHook = true;
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, HotkeyKeyboardProc,
                                 GetModuleHandleW(nullptr), 0);
}

HICON MakeScreenshotIcon(int size) {
    Bitmap bmp(size, size, PixelFormat32bppPARGB);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.Clear(Color(0, 0, 0, 0));
        const REAL sz = static_cast<REAL>(size);
        SolidBrush bg(Color(255, 24, 90, 156));
        GraphicsPath path;
        REAL r = sz * 0.18f;
        path.AddArc(0.0f, 0.0f, r * 2, r * 2, 180.0f, 90.0f);
        path.AddArc(sz - r * 2, 0.0f, r * 2, r * 2, 270.0f, 90.0f);
        path.AddArc(sz - r * 2, sz - r * 2, r * 2, r * 2, 0.0f, 90.0f);
        path.AddArc(0.0f, sz - r * 2, r * 2, r * 2, 90.0f, 90.0f);
        path.CloseFigure();
        g.FillPath(&bg, &path);
        REAL m = sz * 0.20f;
        Pen white(Color(255, 255, 255), (std::max)(1.2f, sz * 0.07f));
        g.DrawRectangle(&white, m, m * 1.05f, sz - m * 2, sz - m * 2.1f);
        REAL c = sz * 0.5f;
        REAL t = sz * 0.14f;
        Pen cross(Color(255, 255, 210), (std::max)(1.2f, sz * 0.06f));
        g.DrawLine(&cross, c - t, c, c + t, c);
        g.DrawLine(&cross, c, c - t, c, c + t);
        SolidBrush dot(Color(255, 255, 255));
        REAL d = (std::max)(2.0f, sz * 0.08f);
        g.FillEllipse(&dot, m - d / 2, m * 1.05f - d / 2, d, d);
        g.FillEllipse(&dot, sz - m - d / 2, m * 1.05f - d / 2, d, d);
        g.FillEllipse(&dot, m - d / 2, sz - m * 1.05f - d / 2, d, d);
        g.FillEllipse(&dot, sz - m - d / 2, sz - m * 1.05f - d / 2, d, d);
    }
    HBITMAP hbm = nullptr;
    bmp.GetHBITMAP(Color(0, 0, 0, 0), &hbm);
    if (!hbm) return nullptr;
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = hbm;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(hbm);
    DeleteObject(mask);
    return icon;
}

HICON MakeBlankIcon(int size) {
    Bitmap bmp(size, size, PixelFormat32bppPARGB);
    HBITMAP hbm = nullptr;
    bmp.GetHBITMAP(Color(0, 0, 0, 0), &hbm);
    if (!hbm) return nullptr;
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = hbm;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(hbm);
    DeleteObject(mask);
    return icon;
}

void EnsureTipClass(HINSTANCE hi) {
    static bool reg = false;
    if (reg) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hi;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_INFOBK + 1));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kTipClass;
    RegisterClassExW(&wc);
    reg = true;
}
} // namespace

App& App::Instance() {
    static App app;
    return app;
}

bool App::Init(HINSTANCE hi, int nCmdShow) {
    hi_ = hi;
    dpi_ = 96;
    Settings().Load();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = App::WndProc;
    wc.hInstance = hi_;
    // 截图风格图标（任务栏/标题栏/Alt-Tab）
    if (!g_appIcon) g_appIcon = MakeScreenshotIcon(32);
    if (!g_appIconSm) g_appIconSm = MakeScreenshotIcon(16);
    if (!g_blankIcon) g_blankIcon = MakeBlankIcon(16);
    wc.hIcon = g_appIcon ? g_appIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = g_appIconSm ? g_appIconSm : wc.hIcon;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc)) return false;

    dpi_ = 96;
    RECT desk;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desk, 0);
    int deskW = desk.right - desk.left;
    int deskH = desk.bottom - desk.top;
    // 初次打开：在默认尺寸上 ×2，并限制不超过工作区
    int w = (std::min)(util::Scale(2000, dpi_), deskW - 40);
    int h = (std::min)(util::Scale(1400, dpi_), deskH - 40);
    if (w < util::Scale(900, dpi_)) w = (std::max)(util::Scale(900, dpi_), deskW - 80);
    if (h < util::Scale(600, dpi_)) h = (std::max)(util::Scale(600, dpi_), deskH - 80);
    int x = desk.left + ((deskW - w) / 2);
    int y = desk.top + ((deskH - h) / 2);

    std::wstring title = std::wstring(APP_NAME) + L" " + APP_VERSION;
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW,
                            kMainClass, title.c_str(),
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            x, y, w, h,
                            nullptr, nullptr, hi_, this);
    if (!hwnd_) return false;

    if (g_appIcon) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_appIcon));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_appIconSm ? g_appIconSm : g_appIcon));
        SetClassLongPtrW(hwnd_, GCLP_HICON, reinterpret_cast<LONG_PTR>(g_appIcon));
        SetClassLongPtrW(hwnd_, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(g_appIconSm ? g_appIconSm : g_appIcon));
    }

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    UpdateHotkey();
    return true;
}

int App::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Global shortcuts when canvas doesn't have focus
        if (msg.message == WM_KEYDOWN) {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl && msg.wParam == 'Z') {
                if (GetKeyState(VK_SHIFT) & 0x8000) Canvas::Instance().Redo();
                else Canvas::Instance().Undo();
                continue;
            }
            if (ctrl && msg.wParam == 'Y') {
                Canvas::Instance().Redo();
                continue;
            }
            if (ctrl && msg.wParam == 'C') {
                Canvas::Instance().CopySelection();
                continue;
            }
            if (ctrl && msg.wParam == 'V') {
                Canvas::Instance().PasteFromBuffer();
                continue;
            }
            if (ctrl && msg.wParam == 'S') {
                if (activeIdx_ >= 0) SaveDoc(activeIdx_);
                continue;
            }
            if (ctrl && msg.wParam == 'B') {
                // bold hint for text tool - handled in dialog
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->Handle(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_SIZE:
        OnSize();
        return 0;
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;
    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;
    case WM_HOTKEY:
        OnHotkey(wParam);
        return 0;
    case WM_APP_CAPTURE_DONE:
        OnCaptureFinished();
        return 0;
    case WM_APP_BEGIN_CAPTURE:
        StartCaptureNow();
        return 0;
    case WM_APP_LONG_REGION:
        OnLongRegionSelected();
        return 0;
    case WM_APP_LONG_DONE:
        OnLongCaptureFinished(wParam != 0);
        return 0;
    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_RBUTTONDOWN:
        OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_TIMER:
        if (wParam == kTimerTooltip) {
            KillTimer(hwnd, kTimerTooltip);
            if (hoverLeft_ >= 0 && hoverLeft_ < static_cast<int>(leftBtns_.size())) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                // 仍在同一按钮上才显示
                if (HitLeftButton(pt.x, pt.y) == hoverLeft_) {
                    ShowTooltip(pt.x, pt.y, leftBtns_[hoverLeft_].tip);
                }
            }
        } else if (wParam == kTimerBeginCapture) {
            KillTimer(hwnd, kTimerBeginCapture);
            StartCaptureNow();
        } else if (wParam == kTimerUpdateCheck) {
            KillTimer(hwnd, kTimerUpdateCheck);
            std::thread([hwnd]() {
                updater::UpdateInfo info;
                updater::CheckForUpdate(info);
                if (info.available) {
                    g_updateInfo = info;
                    PostMessageW(hwnd, WM_APP_UPDATE_FOUND, 0, 0);
                }
                // 无新版本 / 网络异常：静默，不打扰
            }).detach();
        }
        return 0;
    case WM_APP_UPDATE_FOUND: {
        // 发现新版本：弹窗询问，确认后后台下载
        std::wstring msg = std::wstring(L"发现新版本 v") + g_updateInfo.latestVersion +
                           L"（当前 v" + APP_VERSION + L"）\n\n是否自动下载并更新？";
        if (!g_updateInfo.notes.empty()) {
            std::wstring notes = g_updateInfo.notes;
            if (notes.size() > 300) notes = notes.substr(0, 300) + L"…";
            msg += L"\n\n---- 更新说明 ----\n" + notes;
        }
        if (MessageBoxW(hwnd, msg.c_str(), L"软件更新",
                        MB_OKCANCEL | MB_ICONINFORMATION) == IDOK) {
            wchar_t tmp[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tmp);
            std::wstring dest = std::wstring(tmp) + L"ScreenshotTool_update.exe";
            std::thread([hwnd, dest]() {
                std::wstring err;
                if (updater::DownloadUpdate(g_updateInfo.assetUrl, dest, err)) {
                    g_updateDest = dest;
                    PostMessageW(hwnd, WM_APP_UPDATE_READY, 0, 0);
                } else {
                    g_updateErr = err;
                    PostMessageW(hwnd, WM_APP_UPDATE_FAILED, 0, 0);
                }
            }).detach();
        }
        return 0;
    }
    case WM_APP_UPDATE_READY:
        // 新 exe 已下载完成：替换自身并重启新程序，成功后本进程退出
        if (updater::ApplyUpdateAndRestart(g_updateDest)) {
            DestroyWindow(hwnd);
        } else {
            MessageBoxW(hwnd, L"更新替换失败，文件未被改动，可稍后在设置中重试。",
                        L"软件更新", MB_ICONWARNING);
        }
        return 0;
    case WM_APP_UPDATE_FAILED:
        MessageBoxW(hwnd, (L"下载新版本失败：\n" + g_updateErr +
                           L"\n\n可稍后在设置中手动检测重试。").c_str(),
                    L"软件更新", MB_ICONWARNING);
        return 0;
    case WM_MOUSELEAVE:
        hoverLeft_ = -1;
        hoverTop_ = -1;
        KillTimer(hwnd, kTimerTooltip);
        HideTooltip();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int idx = HitLeftButton(x, y);
        if (idx >= 0 && leftBtns_[idx].id == ID_TOOL_NUMBER) {
            ShowNumberMenu(leftBtns_[idx].rc.left, leftBtns_[idx].rc.bottom);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        // 关闭主窗口：有截图页签时先问是否保存，避免直接丢掉
        if (!docs_.empty()) {
            for (;;) {
                int r = MessageBoxW(hwnd,
                                    L"还有打开的截图页签，关闭前是否保存？\n\n"
                                    L"是 = 选择路径保存全部后退出\n"
                                    L"否 = 不保存直接退出\n"
                                    L"取消 = 不退出，留在当前界面",
                                    APP_NAME, MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDCANCEL) {
                    // 明确取消：不退出
                    return 0;
                }
                if (r == IDNO) {
                    DestroyWindow(hwnd);
                    return 0;
                }
                if (r == IDYES) {
                    // 仅当保存流程真正完成才退出；取消路径/格式选择则不关
                    if (SaveAllDocs()) {
                        DestroyWindow(hwnd);
                        return 0;
                    }
                    ShowWindow(hwnd_, SW_SHOW);
                    SetForegroundWindow(hwnd_);
                    ShowStatusMessage(L"已取消保存路径选择，程序未退出，请重新选择");
                    continue;
                }
                return 0;
            }
        }
        if (MessageBoxW(hwnd,
                        L"确定退出截图工具吗？\n（最小化可继续在后台待命）",
                        APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, kHotkeyId);
        UnregisterHotKey(hwnd, kLongHotkeyId);
        RemoveHotkeyHook();
        if (LongCapture::Instance().IsActive()) {
            LongCapture::Instance().Cancel();
        }
        PostQuitMessage(0);
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        RECT* r = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        BuildToolbars();
        LayoutChildren();
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void App::OnCreate() {
    dpi_ = util::GetDpiForWindowSafe(hwnd_);
    BuildToolbars();
    Canvas::Instance().Create(hwnd_, hi_);
    CompareView::Instance().Create(hwnd_, hi_);
    status_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                              WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                              0, 0, 0, 0, hwnd_, nullptr, hi_, nullptr);
    SelectTool(Tool::Select);
    ApplyTheme();
    UpdateStatus();
    // 启动 3 秒后自动检测更新（WM_TIMER 里触发，结果经 WM_APP_UPDATE_FOUND 回主线程）
    SetTimer(hwnd_, kTimerUpdateCheck, 3000, nullptr);
}

void App::BuildToolbars() {
    topBtns_.clear();
    leftBtns_.clear();

    auto addTop = [&](int id, const wchar_t* text) {
        ToolButton b;
        b.id = id;
        b.text = text;
        b.tip = text;
        b.isLeft = false;
        topBtns_.push_back(b);
    };
    addTop(ID_CMD_CAPTURE, L"截图");
    addTop(ID_CMD_LONG_CAPTURE, L"长截图");
    addTop(ID_CMD_MOSAIC, L"马赛克");
    addTop(ID_CMD_EXTRACT, L"提取内容");
    addTop(ID_CMD_MAGIC_ERASE, L"魔法消除");
    addTop(ID_CMD_SETTINGS, L"设置");
    addTop(ID_CMD_SAVE_ALL, L"全部保存");
    addTop(ID_CMD_UNDO, L"撤销");
    addTop(ID_CMD_REDO, L"重做");

    struct L { int id; const wchar_t* tip; Tool tool; bool toggle; bool num; };
    const L left[] = {
        { ID_TOOL_SELECT,   L"选择（框选区域）", Tool::Select, true, false },
        { ID_TOOL_BRUSH,    L"笔刷（半透明高亮）", Tool::Brush, true, false },
        { ID_TOOL_VIEW,     L"查看模式",         Tool::View, true, false },
        { ID_TOOL_TEXT,     L"插入文字",         Tool::Text, true, false },
        { ID_TOOL_ARROW,    L"箭头",            Tool::Arrow, true, false },
        { ID_TOOL_LINE,     L"直线",            Tool::Line, true, false },
        { ID_TOOL_PEN,      L"画笔",            Tool::Freehand, true, false },
        { ID_TOOL_RECT,     L"矩形框",          Tool::Rect, true, false },
        { ID_TOOL_ROUND,    L"圆角矩形框",       Tool::RoundRect, true, false },
        { ID_TOOL_ELLIPSE,  L"椭圆",            Tool::Ellipse, true, false },
        { ID_TOOL_FRECT,    L"实心矩形",         Tool::FilledRect, true, false },
        { ID_TOOL_FROUND,   L"实心圆角矩形",     Tool::FilledRoundRect, true, false },
        { ID_TOOL_FELLIPSE, L"实心椭圆",         Tool::FilledEllipse, true, false },
        { ID_TOOL_NUMBER,   L"序号（点击选择 1-20）", Tool::Number, true, true },
        { ID_CMD_COLOR,     L"颜色",            Tool::Select, false, false },
    };
    for (auto& item : left) {
        ToolButton b;
        b.id = item.id;
        b.tip = item.tip;
        b.text = item.num ? std::to_wstring(numberIndex_ + 1) : item.tip;
        b.tool = item.tool;
        b.toggle = item.toggle;
        b.isLeft = true;
        b.showNumber = item.num;
        leftBtns_.push_back(b);
    }
}

void App::DrawToolIcon(Graphics& g, const ToolButton& b, const RECT& rc,
                       COLORREF iconColor, COLORREF accent) const {
    const REAL cx = (rc.left + rc.right) * 0.5f;
    const REAL cy = (rc.top + rc.bottom) * 0.5f;
    Color ink = ToGpColor(iconColor);
    Color acc = ToGpColor(accent);
    Pen pen(ink, 1.8f);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    Pen penThin(ink, 1.4f);
    SolidBrush br(ink);
    SolidBrush brAcc(acc);

    switch (b.id) {
    case ID_TOOL_SELECT: {
        Pen dash(ink, 1.6f);
        dash.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&dash, cx - 10.0f, cy - 10.0f, 20.0f, 20.0f);
        g.DrawLine(&pen, cx + 2.0f, cy + 2.0f, cx + 9.0f, cy + 12.0f);
        g.DrawLine(&pen, cx + 2.0f, cy + 2.0f, cx + 12.0f, cy + 4.0f);
        g.DrawLine(&pen, cx + 4.0f, cy + 8.0f, cx + 9.0f, cy + 12.0f);
        break;
    }
    case ID_TOOL_BRUSH: {
        // 格式刷：原竖直造型顺时针约 45° 再水平镜像，握柄朝右上、刷毛朝左下
        const REAL deg = 3.14159265f / 4.0f; // 45°
        const REAL cs = std::cos(deg), sn = std::sin(deg);
        // 局部坐标：柄向上(-y)、毛向下(+y)；顺时针旋转后水平翻转（x 取反）
        auto rot = [&](REAL lx, REAL ly) -> PointF {
            // 顺时针 45°: x' = x*cos + y*sin, y' = -x*sin + y*cos
            REAL xr = lx * cs + ly * sn;
            REAL yr = -lx * sn + ly * cs;
            return PointF(cx - xr, cy + yr);
        };
        // 握柄（局部 y -11 → -3）
        PointF h0 = rot(0.0f, -11.0f);
        PointF h1 = rot(0.0f, -3.0f);
        Pen handle(ink, 3.0f);
        handle.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&handle, h0.X, h0.Y, h1.X, h1.Y);
        // 金属箍
        PointF f0 = rot(-4.5f, -3.0f);
        PointF f1 = rot(4.5f, -3.0f);
        PointF f2 = rot(4.5f, 0.5f);
        PointF f3 = rot(-4.5f, 0.5f);
        PointF ferrule[4] = { f0, f1, f2, f3 };
        g.FillPolygon(&br, ferrule, 4);
        // 刷毛
        PointF b0 = rot(-4.5f, 0.5f);
        PointF b1 = rot(4.5f, 0.5f);
        PointF b2 = rot(6.5f, 10.0f);
        PointF b3 = rot(-6.5f, 10.0f);
        PointF bristles[4] = { b0, b1, b2, b3 };
        g.FillPolygon(&br, bristles, 4);
        // 刷毛纹理
        PointF t0 = rot(-2.0f, 2.0f), t1 = rot(-3.5f, 8.5f);
        PointF t2 = rot(2.0f, 2.0f), t3 = rot(3.5f, 8.5f);
        g.DrawLine(&penThin, t0.X, t0.Y, t1.X, t1.Y);
        g.DrawLine(&penThin, t2.X, t2.Y, t3.X, t3.Y);
        break;
    }
    case ID_TOOL_VIEW: {
        g.DrawEllipse(&penThin, cx - 11.0f, cy - 7.0f, 22.0f, 14.0f);
        g.FillEllipse(&br, cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        break;
    }
    case ID_TOOL_TEXT: {
        FontFamily family(L"Segoe UI");
        Font font(&family, 19.0f, FontStyleBold, UnitPixel);
        SolidBrush tb(ink);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"T", 1, &font, RectF(cx - 14.0f, cy - 14.0f, 28.0f, 28.0f), &fmt, &tb);
        break;
    }
    case ID_TOOL_ARROW: {
        g.DrawLine(&pen, cx - 9.0f, cy + 7.0f, cx + 8.0f, cy - 7.0f);
        PointF head[3] = {
            PointF(cx + 9.0f, cy - 8.0f),
            PointF(cx + 2.0f, cy - 6.0f),
            PointF(cx + 6.0f, cy - 1.0f)
        };
        g.FillPolygon(&br, head, 3);
        break;
    }
    case ID_TOOL_LINE: {
        g.DrawLine(&pen, cx - 9.0f, cy + 8.0f, cx + 9.0f, cy - 8.0f);
        break;
    }
    case ID_TOOL_PEN: {
        // 自由画笔：连绵打圈的涂鸦轨迹
        GraphicsPath scribble;
        REAL x = cx - 10.0f;
        REAL y = cy + 4.0f;
        scribble.AddBezier(x, y,
                           x + 2.0f, y - 8.0f,
                           x + 6.0f, y - 8.0f,
                           x + 5.0f, y - 1.0f);
        scribble.AddBezier(x + 5.0f, y - 1.0f,
                           x + 4.0f, y + 5.0f,
                           x + 9.0f, y + 5.0f,
                           x + 9.5f, y - 2.0f);
        scribble.AddBezier(x + 9.5f, y - 2.0f,
                           x + 10.0f, y - 8.0f,
                           x + 14.0f, y - 7.0f,
                           x + 13.0f, y + 1.0f);
        Pen scribPen(ink, 2.0f);
        scribPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        scribPen.SetLineJoin(LineJoinRound);
        g.DrawPath(&scribPen, &scribble);
        // 末端小笔尖
        PointF tip[3] = {
            PointF(cx + 12.0f, cy - 10.0f),
            PointF(cx + 9.0f, cy - 4.0f),
            PointF(cx + 13.5f, cy - 3.0f)
        };
        g.FillPolygon(&br, tip, 3);
        break;
    }
    case ID_TOOL_RECT: {
        g.DrawRectangle(&pen, cx - 10.0f, cy - 8.0f, 20.0f, 16.0f);
        break;
    }
    case ID_TOOL_ROUND: {
        GraphicsPath path;
        path.AddArc(cx - 10.0f, cy - 8.0f, 12.0f, 12.0f, 180.0f, 90.0f);
        path.AddArc(cx + 2.0f, cy - 8.0f, 12.0f, 12.0f, 270.0f, 90.0f);
        path.AddArc(cx + 2.0f, cy + 2.0f, 12.0f, 12.0f, 0.0f, 90.0f);
        path.AddArc(cx - 10.0f, cy + 2.0f, 12.0f, 12.0f, 90.0f, 90.0f);
        path.CloseFigure();
        g.DrawPath(&pen, &path);
        break;
    }
    case ID_TOOL_ELLIPSE: {
        g.DrawEllipse(&pen, cx - 10.0f, cy - 8.0f, 20.0f, 16.0f);
        break;
    }
    case ID_TOOL_FRECT: {
        g.FillRectangle(&br, cx - 10.0f, cy - 8.0f, 20.0f, 16.0f);
        break;
    }
    case ID_TOOL_FROUND: {
        GraphicsPath path;
        path.AddArc(cx - 10.0f, cy - 8.0f, 12.0f, 12.0f, 180.0f, 90.0f);
        path.AddArc(cx + 2.0f, cy - 8.0f, 12.0f, 12.0f, 270.0f, 90.0f);
        path.AddArc(cx + 2.0f, cy + 2.0f, 12.0f, 12.0f, 0.0f, 90.0f);
        path.AddArc(cx - 10.0f, cy + 2.0f, 12.0f, 12.0f, 90.0f, 90.0f);
        path.CloseFigure();
        g.FillPath(&br, &path);
        break;
    }
    case ID_TOOL_FELLIPSE: {
        g.FillEllipse(&br, cx - 10.0f, cy - 8.0f, 20.0f, 16.0f);
        break;
    }
    case ID_TOOL_NUMBER: {
        g.DrawEllipse(&penThin, cx - 10.0f, cy - 10.0f, 20.0f, 20.0f);
        FontFamily family(L"Segoe UI");
        Font font(&family, 12.0f, FontStyleBold, UnitPixel);
        SolidBrush tb(ink);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        std::wstring n = std::to_wstring(numberIndex_ + 1);
        g.DrawString(n.c_str(), -1, &font, RectF(cx - 12.0f, cy - 12.0f, 24.0f, 24.0f), &fmt, &tb);
        break;
    }
    case ID_CMD_COLOR: {
        // 纯色色块，无边框
        COLORREF c = Canvas::Instance().GetDrawColor();
        SolidBrush sw(ToGpColor(c));
        g.FillRectangle(&sw, cx - 10.0f, cy - 10.0f, 20.0f, 20.0f);
        break;
    }
    default: {
        FontFamily family(L"Microsoft YaHei");
        Font font(&family, 12.0f, FontStyleRegular, UnitPixel);
        SolidBrush tb(ink);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(b.text.c_str(), -1, &font,
                     RectF(static_cast<REAL>(rc.left), static_cast<REAL>(rc.top),
                           static_cast<REAL>(rc.right - rc.left),
                           static_cast<REAL>(rc.bottom - rc.top)),
                     &fmt, &tb);
        break;
    }
    }
    (void)brAcc;
}

void App::ShowTooltip(int x, int y, const std::wstring& text) {
    if (text.empty()) return;
    HINSTANCE hi = hi_ ? hi_ : GetModuleHandleW(nullptr);
    EnsureTipClass(hi);
    if (!tipHwnd_) {
        tipHwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            kTipClass, L"",
            WS_POPUP | WS_BORDER,
            0, 0, 10, 10,
            hwnd_, nullptr, hi, nullptr);
    }
    if (!tipHwnd_) return;

    // 已显示且文字未变 → 不重绘，避免闪烁
    if (tipVisible_ && tipText_ == text && IsWindowVisible(tipHwnd_)) {
        return;
    }
    tipText_ = text;

    HDC hdc = GetDC(tipHwnd_);
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
    HGDIOBJ old = SelectObject(hdc, font);
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(tipHwnd_, hdc);

    int w = sz.cx + 16;
    int h = sz.cy + 12;
    POINT pt = { x + 18, y + 10 };
    ClientToScreen(hwnd_, &pt);

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    if (pt.x + w > work.right) pt.x = x - w - 8;
    if (pt.y + h > work.bottom) pt.y = y - h - 8;

    SetWindowPos(tipHwnd_, HWND_TOPMOST, pt.x, pt.y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    HDC tdc = GetDC(tipHwnd_);
    RECT rc; GetClientRect(tipHwnd_, &rc);
    FillRect(tdc, &rc, GetSysColorBrush(COLOR_INFOBK));
    SetBkMode(tdc, TRANSPARENT);
    SetTextColor(tdc, GetSysColor(COLOR_INFOTEXT));
    HFONT f2 = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
    HGDIOBJ o2 = SelectObject(tdc, f2);
    DrawTextW(tdc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(tdc, o2);
    DeleteObject(f2);
    ReleaseDC(tipHwnd_, tdc);
    tipVisible_ = true;
}

void App::HideTooltip() {
    if (tipHwnd_ && IsWindowVisible(tipHwnd_)) ShowWindow(tipHwnd_, SW_HIDE);
    tipVisible_ = false;
    tipText_.clear();
}

void App::ShowNumberMenu(int x, int y) {
    HMENU menu = CreatePopupMenu();
    for (int i = 1; i <= 20; ++i) {
        wchar_t buf[16];
        swprintf_s(buf, L"%d", i);
        UINT flags = MF_STRING | (i == numberIndex_ + 1 ? MF_CHECKED : 0);
        AppendMenuW(menu, flags, ID_NUM_BASE + i - 1, buf);
    }
    POINT pt = { x, y };
    ClientToScreen(hwnd_, &pt);
    SelectTool(Tool::Number);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::LayoutChildren() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    topH_ = util::Scale(48, dpi_);
    leftW_ = util::Scale(56, dpi_);
    tabH_ = util::Scale(32, dpi_);
    if (status_) {
        SendMessageW(status_, WM_SIZE, 0, 0);
        RECT sr;
        GetWindowRect(status_, &sr);
        statusH_ = sr.bottom - sr.top;
    }

    // top buttons
    int pad = util::Scale(6, dpi_);
    int bx = pad;
    int by = (topH_ - util::Scale(30, dpi_)) / 2;
    int bw = util::Scale(72, dpi_);
    int bh = util::Scale(30, dpi_);
    int gap = util::Scale(6, dpi_);
    for (auto& b : topBtns_) {
        b.rc = { bx, by, bx + bw, by + bh };
        bx += bw + gap;
    }

    // left buttons
    int lx = pad;
    int ly = topH_ + pad;
    int lw = leftW_ - pad * 2;
    int lh = util::Scale(36, dpi_);
    int lgap = util::Scale(3, dpi_);
    for (auto& b : leftBtns_) {
        b.rc = { lx, ly, lx + lw, ly + lh };
        ly += lh + lgap;
    }

    // compare-mode controls: right-aligned on top bar
    LayoutCompareButtons();

    int canvasTop = topH_;
    int canvasLeft = leftW_;
    int canvasRight = rc.right;
    int canvasBottom = rc.bottom - tabH_ - statusH_;
    if (canvasBottom < canvasTop + 40) canvasBottom = canvasTop + 40;

    HWND canvas = Canvas::Instance().Hwnd();
    HWND compareHwnd = CompareView::Instance().Hwnd();
    int splitGap = util::Scale(4, dpi_);

    if (compareMode_ && compareHwnd) {
        int totalW = canvasRight - canvasLeft;
        int leftW = totalW / 2 - splitGap / 2;
        int rightW = totalW - leftW - splitGap;
        if (leftW < 80) leftW = 80;
        if (rightW < 80) rightW = 80;
        if (canvas) {
            MoveWindow(canvas, canvasLeft, canvasTop, leftW,
                       canvasBottom - canvasTop, TRUE);
            Canvas::Instance().UpdateScrollBars();
            Canvas::Instance().Refresh();
        }
        MoveWindow(compareHwnd, canvasLeft + leftW + splitGap, canvasTop, rightW,
                   canvasBottom - canvasTop, TRUE);
        CompareView::Instance().ShowPane(true);
        CompareView::Instance().UpdateScrollBars();
        CompareView::Instance().Refresh();
    } else {
        if (canvas) {
            MoveWindow(canvas, canvasLeft, canvasTop,
                       canvasRight - canvasLeft,
                       canvasBottom - canvasTop, TRUE);
            Canvas::Instance().UpdateScrollBars();
            Canvas::Instance().Refresh();
        }
        if (compareHwnd) CompareView::Instance().ShowPane(false);
    }
}

void App::LayoutCompareButtons() {
    compareBtnsBuilt_ = false;
    syncScrollRc_ = {};
    exitCompareRc_ = {};
    if (!compareMode_) return;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    int bh = util::Scale(30, dpi_);
    int by = (topH_ - bh) / 2;
    int pad = util::Scale(8, dpi_);
    int gap = util::Scale(12, dpi_);
    int exitW = util::Scale(88, dpi_);
    // label「同步滚动」+ switch on the right
    int syncW = util::Scale(120, dpi_);

    int right = rc.right - pad;
    exitCompareRc_ = { right - exitW, by, right, by + bh };
    syncScrollRc_ = { exitCompareRc_.left - gap - syncW, by, exitCompareRc_.left - gap, by + bh };
    compareBtnsBuilt_ = true;
}

void App::HitTestCompareControls(int x, int y, int& outId) const {
    outId = 0;
    if (!compareMode_ || !compareBtnsBuilt_) return;
    POINT pt = { x, y };
    if (PtInRect(&syncScrollRc_, pt)) outId = ID_CMD_SYNC_SCROLL;
    else if (PtInRect(&exitCompareRc_, pt)) outId = ID_CMD_EXIT_COMPARE;
}

void App::OnSize() {
    LayoutChildren();
    if (status_) {
        int parts[] = { 220, 400, 520, -1 };
        SendMessageW(status_, SB_SETPARTS, 4, reinterpret_cast<LPARAM>(parts));
        UpdateStatus();
    }
}

void App::ApplyTheme() {
    if (!hwnd_) return;
    InvalidateRect(hwnd_, nullptr, TRUE);
    Canvas::Instance().Refresh();
}

Document* App::ActiveDoc() {
    if (activeIdx_ < 0 || activeIdx_ >= static_cast<int>(docs_.size())) return nullptr;
    return docs_[activeIdx_].get();
}

void App::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) { EndPaint(hwnd_, &ps); return; }

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bm);

    Graphics g(mem);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    auto& s = Settings();
    COLORREF bg = s.BgColor();
    COLORREF panel = s.PanelColor();
    COLORREF text = s.TextColor();
    COLORREF border = s.BorderColor();

    // top bar
    SolidBrush panelBr(ToGpColor(panel));
    SolidBrush bgBr(ToGpColor(bg));
    g.FillRectangle(&bgBr, 0, 0, w, h);
    g.FillRectangle(&panelBr, 0, 0, w, topH_);
    // left bar
    g.FillRectangle(&panelBr, 0, topH_, leftW_, h - topH_);

    FontFamily family(L"Microsoft YaHei");
    Font font(&family, 13, FontStyleRegular, UnitPixel);
    Font fontSmall(&family, 12, FontStyleRegular, UnitPixel);
    SolidBrush textBr(ToGpColor(text));
    Pen borderPen(ToGpColor(border), 1);

    auto drawBtn = [&](const ToolButton& b, bool active, bool hover, bool leftRail) {
        int bw = b.rc.right - b.rc.left;
        int bh = b.rc.bottom - b.rc.top;

        if (leftRail) {
            // Default: no filled background — only icon on panel
            COLORREF iconColor = s.TextColor();
            if (active) {
                SolidBrush hi(ToGpColor(s.themeColor, 40));
                g.FillRectangle(&hi, b.rc.left, b.rc.top, bw, bh);
                Pen edge(ToGpColor(s.themeColor), 2);
                g.DrawLine(&edge, b.rc.left + 1, b.rc.top + 4, b.rc.left + 1, b.rc.bottom - 4);
                iconColor = s.themeColor;
            } else if (hover) {
                SolidBrush hi(ToGpColor(s.IsDarkTheme() ? RGB(70, 70, 70) : RGB(230, 230, 235)));
                g.FillRectangle(&hi, b.rc.left, b.rc.top, bw, bh);
            }
            DrawToolIcon(g, b, b.rc, iconColor, s.themeColor);
            return;
        }

        // top bar buttons: 与底部页签同一套配色（未选 250 底 / 悬停 230 / 按下主题色+白字）
        // 「截图 / 长截图」为主操作按钮：常显主题蓝（与 P1 激活时一致），悬停稍深以示反馈
        bool primary = (b.id == ID_CMD_CAPTURE || b.id == ID_CMD_LONG_CAPTURE);
        Color fill;
        if (primary) {
            COLORREF base = s.themeColor;
            COLORREF c = hover ? RGB(GetRValue(base) * 85 / 100,
                                     GetGValue(base) * 85 / 100,
                                     GetBValue(base) * 85 / 100)
                               : base;
            fill = ToGpColor(c);
        } else {
            fill = hover
                ? ToGpColor(s.IsDarkTheme() ? RGB(70, 70, 70) : RGB(230, 230, 230))
                : ToGpColor(s.IsDarkTheme() ? RGB(55, 55, 55) : RGB(250, 250, 250));
            if (active) fill = ToGpColor(s.themeColor);
        }
        SolidBrush br(fill);
        g.FillRectangle(&br, b.rc.left, b.rc.top, bw, bh);
        Pen p(ToGpColor(s.BorderColor()), 1);
        g.DrawRectangle(&p, b.rc.left, b.rc.top, bw - 1, bh - 1);
        Color tc = (active || primary) ? Color(255, 255, 255, 255) : ToGpColor(s.TextColor());
        SolidBrush tbr(tc);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF layout(static_cast<REAL>(b.rc.left), static_cast<REAL>(b.rc.top),
                     static_cast<REAL>(bw), static_cast<REAL>(bh));
        g.DrawString(b.text.c_str(), -1, &font, layout, &fmt, &tbr);
    };

    Tool cur = Canvas::Instance().GetTool();
    for (size_t i = 0; i < topBtns_.size(); ++i)
        drawBtn(topBtns_[i], static_cast<int>(i) == pressedTop_,
                static_cast<int>(i) == hoverTop_, false);
    for (size_t i = 0; i < leftBtns_.size(); ++i) {
        bool active = leftBtns_[i].toggle && leftBtns_[i].tool == cur;
        drawBtn(leftBtns_[i], active, static_cast<int>(i) == hoverLeft_, true);
    }

    // compare-mode right-side controls
    if (compareMode_ && compareBtnsBuilt_) {
        // 「同步滚动」标签 + 开关（开=启用，关=不启用；默认开）
        {
            const RECT& r = syncScrollRc_;
            int trackW = util::Scale(40, dpi_);
            int trackH = util::Scale(20, dpi_);
            int trackX = r.right - util::Scale(8, dpi_) - trackW;
            int trackY = r.top + (r.bottom - r.top - trackH) / 2;
            int labelRight = trackX - util::Scale(10, dpi_);

            // label
            SolidBrush labelBr(ToGpColor(s.TextColor()));
            StringFormat lfmt;
            lfmt.SetAlignment(StringAlignmentNear);
            lfmt.SetLineAlignment(StringAlignmentCenter);
            RectF lrect(static_cast<REAL>(r.left), static_cast<REAL>(r.top),
                        static_cast<REAL>(labelRight - r.left), static_cast<REAL>(r.bottom - r.top));
            g.DrawString(L"同步滚动", -1, &font, lrect, &lfmt, &labelBr);

            // switch track
            REAL rx = static_cast<REAL>(trackX);
            REAL ry = static_cast<REAL>(trackY);
            REAL rw = static_cast<REAL>(trackW);
            REAL rh = static_cast<REAL>(trackH);
            Color trackFill = compareSyncScroll_
                ? ToGpColor(s.themeColor)
                : ToGpColor(s.IsDarkTheme() ? RGB(90, 90, 90) : RGB(180, 180, 180));
            SolidBrush trackBr(trackFill);
            GraphicsPath track;
            REAL rr = rh * 0.5f;
            track.AddArc(rx, ry, rh, rh, 90.0f, 180.0f);
            track.AddArc(rx + rw - rh, ry, rh, rh, 270.0f, 180.0f);
            track.CloseFigure();
            g.FillPath(&trackBr, &track);

            // switch knob
            REAL knob = rh - 4.0f;
            REAL kx = compareSyncScroll_ ? (rx + rw - knob - 2.0f) : (rx + 2.0f);
            REAL ky = ry + 2.0f;
            SolidBrush knobBr(Color(255, 255, 255, 255));
            g.FillEllipse(&knobBr, kx, ky, knob, knob);
        }

        // 「退出对比」按钮
        {
            const RECT& r = exitCompareRc_;
            int bw = r.right - r.left;
            int bh = r.bottom - r.top;
            Color fill = ToGpColor(s.IsDarkTheme() ? RGB(50, 50, 50) : RGB(245, 245, 245));
            SolidBrush br(fill);
            g.FillRectangle(&br, r.left, r.top, bw, bh);
            Pen p(ToGpColor(s.BorderColor()), 1);
            g.DrawRectangle(&p, r.left, r.top, bw - 1, bh - 1);
            SolidBrush tbr(ToGpColor(s.TextColor()));
            StringFormat fmt;
            fmt.SetAlignment(StringAlignmentCenter);
            fmt.SetLineAlignment(StringAlignmentCenter);
            RectF layout(static_cast<REAL>(r.left), static_cast<REAL>(r.top),
                         static_cast<REAL>(bw), static_cast<REAL>(bh));
            g.DrawString(L"退出对比", -1, &font, layout, &fmt, &tbr);
        }
    }

    // tab bar background
    int tabTop = h - statusH_ - tabH_;
    SolidBrush tabBg(ToGpColor(s.IsDarkTheme() ? RGB(40, 40, 40) : RGB(230, 230, 230)));
    g.FillRectangle(&tabBg, 0, tabTop, w, tabH_);
    g.DrawLine(&borderPen, 0, tabTop, w, tabTop);

    // tabs — width is 50% of the previous 72px
    int tx = leftW_ + 8;
    int ty = tabTop + 4;
    int th = tabH_ - 8;
    int tabW = util::Scale(36, dpi_);
    for (size_t i = 0; i < docs_.size(); ++i) {
        int tw = tabW;
        RECT trc = { tx, ty, tx + tw, ty + th };
        bool active = static_cast<int>(i) == activeIdx_;
        bool inCompare = compareMode_ &&
                         (static_cast<int>(i) == activeIdx_ || static_cast<int>(i) == compareIdx_);
        Color tc = active ? ToGpColor(s.themeColor)
                          : (inCompare ? ToGpColor(s.IsDarkTheme() ? RGB(70, 90, 70) : RGB(210, 230, 210))
                                       : ToGpColor(s.IsDarkTheme() ? RGB(55, 55, 55) : RGB(250, 250, 250)));
        SolidBrush tb(tc);
        g.FillRectangle(&tb, trc.left, trc.top, tw, th);
        Pen tp(ToGpColor(s.BorderColor()), 1);
        g.DrawRectangle(&tp, trc.left, trc.top, tw - 1, th - 1);
        Color fc = active ? Color(255, 255, 255) : ToGpColor(text);
        SolidBrush fb(fc);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF lr(static_cast<REAL>(trc.left), static_cast<REAL>(trc.top),
                 static_cast<REAL>(tw), static_cast<REAL>(th));
        g.DrawString(docs_[i]->name.c_str(), -1, &fontSmall, lr, &fmt, &fb);
        tx += tw + 4;
    }

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

void App::OnCommand(int id) {
    // number selection menu
    if (id >= ID_NUM_BASE && id < ID_NUM_BASE + 20) {
        numberIndex_ = id - ID_NUM_BASE;
        Canvas::Instance().SetNumber(numberIndex_ + 1);
        for (auto& b : leftBtns_) {
            if (b.id == ID_TOOL_NUMBER) b.text = std::to_wstring(numberIndex_ + 1);
        }
        SelectTool(Tool::Number);
        InvalidateRect(hwnd_, nullptr, FALSE);
        ShowStatusMessage(L"序号：" + std::to_wstring(numberIndex_ + 1));
        return;
    }

    if (id >= ID_TOOL_SELECT && id <= ID_TOOL_NUMBER) {
        for (auto& b : leftBtns_) {
            if (b.id == id) {
                if (id == ID_TOOL_NUMBER) {
                    // open 1-20 dropdown menu near the button
                    ShowNumberMenu(b.rc.left, b.rc.bottom);
                    return;
                }
                SelectTool(b.tool);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
    }

    switch (id) {
    case ID_CMD_CAPTURE: StartCapture(); break;
    case ID_CMD_LONG_CAPTURE: StartLongCapture(); break;
    case ID_CMD_MOSAIC: Canvas::Instance().ApplyMosaicToSelection(); break;
    case ID_CMD_EXTRACT: extract::RunExtractFlow(hwnd_, ActiveDoc()); break;
    case ID_CMD_MAGIC_ERASE: extract::RunMagicErase(hwnd_, ActiveDoc()); break;
    case ID_CMD_SETTINGS: OpenSettings(); break;
    case ID_CMD_SAVE_ALL: SaveAllDocs(); break;
    case ID_CMD_UNDO: Canvas::Instance().Undo(); break;
    case ID_CMD_REDO: Canvas::Instance().Redo(); break;
    case ID_CMD_SAVE: if (activeIdx_ >= 0) SaveDoc(activeIdx_); break;
    case ID_CMD_COLOR: OpenColorPicker(); break;
    case ID_CMD_COPY: Canvas::Instance().CopySelection(); break;
    case ID_CMD_PASTE: Canvas::Instance().PasteFromBuffer(); break;
    case ID_CMD_SYNC_SCROLL: ToggleCompareSyncScroll(); break;
    case ID_CMD_EXIT_COMPARE: ExitCompare(); break;
    case ID_MENU_COMPARE:
        if (contextTabIdx_ >= 0) StartCompare(contextTabIdx_);
        break;
    case ID_MENU_CLOSE: {
        int idx = contextTabIdx_ >= 0 ? contextTabIdx_ : activeIdx_;
        if (idx >= 0) CloseDoc(idx);
        contextTabIdx_ = -1;
        break;
    }
    case ID_MENU_SAVE: {
        int idx = contextTabIdx_ >= 0 ? contextTabIdx_ : activeIdx_;
        if (idx >= 0) SaveDoc(idx);
        contextTabIdx_ = -1;
        break;
    }
    case ID_MENU_SAVE_ALL: SaveAllDocs(); break;
    case ID_MENU_CLOSE_OTHERS: {
        int keep = contextTabIdx_ >= 0 ? contextTabIdx_ : activeIdx_;
        for (int i = static_cast<int>(docs_.size()) - 1; i >= 0; --i) {
            if (i != keep) CloseDoc(i);
        }
        contextTabIdx_ = -1;
        break;
    }
    default:
        if (id >= ID_TAB_BASE && id < ID_TAB_BASE + static_cast<int>(docs_.size()) + 8) {
            int idx = id - ID_TAB_BASE;
            if (idx >= 0 && idx < static_cast<int>(docs_.size())) ActivateDoc(idx);
        }
        break;
    }
}

void App::OnKeyDown(WPARAM vk) {
    if (vk == VK_ESCAPE) {
        if (CaptureOverlay::Instance().IsOpen()) {
            CaptureOverlay::Instance().Cancel();
            return;
        }
    }
    if (vk == VK_DELETE) {
        Canvas::Instance().DeleteSelection();
    }
}

void App::OnHotkey(WPARAM id) {
    if (static_cast<int>(id) == kLongHotkeyId) {
        StartLongCapture();
    } else {
        StartCapture();
    }
}

void App::StartCapture() {
    if (CaptureOverlay::Instance().IsOpen()) return;
    if (LongCapture::Instance().IsActive()) return;
    if (capturePending_) return;

    longModePending_ = false;
    BeginCaptureHide();
    capturePending_ = true;
    if (hwnd_) {
        PostMessageW(hwnd_, WM_APP_BEGIN_CAPTURE, 0, 0);
        SetTimer(hwnd_, kTimerBeginCapture, 150, nullptr);
    } else {
        StartCaptureNow();
    }
}

void App::StartLongCapture() {
    if (CaptureOverlay::Instance().IsOpen()) return;
    if (LongCapture::Instance().IsActive()) return;
    if (capturePending_) return;

    longModePending_ = true;
    BeginCaptureHide();
    capturePending_ = true;
    if (hwnd_) {
        PostMessageW(hwnd_, WM_APP_BEGIN_CAPTURE, 0, 0);
        SetTimer(hwnd_, kTimerBeginCapture, 150, nullptr);
    } else {
        StartCaptureNow();
    }
}

void App::BeginCaptureHide() {
    HideTooltip();
    if (tipHwnd_ && IsWindow(tipHwnd_)) ShowWindow(tipHwnd_, SW_HIDE);
    // 快速隐藏：立刻从屏幕消失，完整等待放在 StartCaptureNow/Start 里
    if (hwnd_ && IsWindow(hwnd_)) {
        if (GetForegroundWindow() == hwnd_) {
            INPUT inp[2] = {};
            inp[0].type = INPUT_KEYBOARD;
            inp[0].ki.wVk = VK_MENU;
            inp[1].type = INPUT_KEYBOARD;
            inp[1].ki.wVk = VK_MENU;
            inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inp, sizeof(INPUT));
            HWND shell = GetShellWindow();
            if (shell && shell != hwnd_) SetForegroundWindow(shell);
        }
        ShowWindow(hwnd_, SW_HIDE);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }
    EnumWindows([](HWND h, LPARAM) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(h)) {
            ShowWindow(h, SW_HIDE);
        }
        return TRUE;
    }, 0);
}

void App::StartCaptureNow() {
    if (hwnd_) KillTimer(hwnd_, kTimerBeginCapture);
    capturePending_ = false;
    if (CaptureOverlay::Instance().IsOpen()) return;
    if (LongCapture::Instance().IsActive()) return;
    const CaptureMode mode = longModePending_ ? CaptureMode::Long : CaptureMode::Region;
    longModePending_ = false;
    CaptureOverlay::Instance().Start(hwnd_, mode);
}

void App::OnCaptureFinished() {
    auto bmp = CaptureOverlay::Instance().TakeResult();
    if (!bmp) return;
    AddDocument(std::move(bmp));
    ShowStatusMessage(L"截图完成，已新建页签");
}

void App::OnLongRegionSelected() {
    // 覆盖层已在框选结束时直接启动会话；这里只兜底
    if (LongCapture::Instance().IsActive()) return;
    RECT rc = CaptureOverlay::Instance().LastRegionScreen();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w < 20 || h < 20) {
        CaptureOverlay::UncloakAndShow(hwnd_);
        if (hwnd_) SetForegroundWindow(hwnd_);
        ShowStatusMessage(L"长截图区域过小，已取消");
        return;
    }
    LongCapture::Instance().Start(hwnd_, rc);
    ShowStatusMessage(L"长截图进行中：滚动页面，完成后点「完成」");
}

void App::OnLongCaptureFinished(bool hasResult) {
    auto bmp = LongCapture::Instance().TakeResult();
    const int gaps = LongCapture::Instance().LastGapCount();
    const int suspects = LongCapture::Instance().LastSuspectCount();
    if (hwnd_ && IsWindow(hwnd_)) {
        CaptureOverlay::UncloakAndShow(hwnd_);
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
    }
    if (hasResult && bmp) {
        AddDocument(std::move(bmp));
        if (gaps > 0 || suspects > 0) {
            std::wstring warn = L"长截图已生成";
            if (gaps > 0) warn += util::Format(L"，但有 %d 处内容可能缺失（滚动过快）", gaps);
            if (suspects > 0) warn += util::Format(L"，%d 处接缝可能异常", suspects);
            warn += L"。\n可在图中查找浅色分隔带；必要时放慢滚动重截。";
            MessageBoxW(hwnd_, warn.c_str(), APP_NAME, MB_ICONWARNING | MB_OK);
            ShowStatusMessage(util::Format(L"长截图完成 · 缺失 %d · 接缝异常 %d", gaps, suspects));
        } else {
            ShowStatusMessage(L"长截图完成，已新建页签");
        }
    } else if (!hasResult) {
        if (gaps > 0 || suspects > 0) {
            ShowStatusMessage(util::Format(L"长截图已取消（曾检测到缺失 %d · 接缝异常 %d）", gaps, suspects));
        } else {
            ShowStatusMessage(L"长截图已取消");
        }
    }
}

void App::AddDocument(std::unique_ptr<Bitmap> bmp) {
    if (!bmp) return;
    wchar_t name[16];
    swprintf_s(name, L"P%d", nextDocId_++);
    auto doc = std::make_unique<Document>(std::move(bmp),
                                          nextDocId_ - 1,
                                          std::wstring(name));
    docs_.push_back(std::move(doc));
    activeIdx_ = static_cast<int>(docs_.size()) - 1;
    Canvas::Instance().SetDocument(docs_[activeIdx_].get());
    Canvas::Instance().ResetZoom();
    UpdateTabBar();
    UpdateTitle();
    UpdateStatus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ActivateDoc(int idx) {
    if (idx < 0 || idx >= static_cast<int>(docs_.size())) return;
    activeIdx_ = idx;
    Canvas::Instance().SetDocument(docs_[idx].get());
    if (compareMode_) {
        // left pane is always the active tab; if it collides with compare target, leave compare
        if (compareIdx_ == activeIdx_) {
            ExitCompare();
        } else if (compareIdx_ >= 0 && compareIdx_ < static_cast<int>(docs_.size())) {
            CompareView::Instance().SetDocument(docs_[compareIdx_].get());
            if (compareSyncScroll_) OnMainCanvasScrolled();
        } else {
            ExitCompare();
        }
    }
    UpdateTabBar();
    UpdateTitle();
    UpdateStatus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::CloseDoc(int idx) {
    if (idx < 0 || idx >= static_cast<int>(docs_.size())) return;

    // 关闭单个截图页签前提示是否保存
    const std::wstring name = docs_[idx]->name;
    int choice = MessageBoxW(hwnd_,
                             (L"关闭「" + name + L"」前是否保存图片？\n\n"
                              L"是 = 保存并关闭\n否 = 不保存直接关闭\n取消 = 不关闭").c_str(),
                             APP_NAME, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return;
    if (choice == IDYES) {
        if (!SaveDoc(idx)) {
            // 用户取消了保存对话框，或保存失败 → 不关闭
            return;
        }
    }

    if (compareMode_) {
        if (idx == compareIdx_ || idx == activeIdx_) {
            ExitCompare();
        } else if (idx < compareIdx_) {
            --compareIdx_;
        }
    }

    docs_.erase(docs_.begin() + idx);
    if (activeIdx_ >= static_cast<int>(docs_.size()))
        activeIdx_ = static_cast<int>(docs_.size()) - 1;
    else if (activeIdx_ > idx)
        --activeIdx_;

    if (activeIdx_ >= 0)
        Canvas::Instance().SetDocument(docs_[activeIdx_].get());
    else
        Canvas::Instance().SetDocument(nullptr);

    if (compareMode_) {
        if (compareIdx_ >= 0 && compareIdx_ < static_cast<int>(docs_.size()))
            CompareView::Instance().SetDocument(docs_[compareIdx_].get());
        else
            ExitCompare();
    }

    UpdateTabBar();
    UpdateTitle();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool App::SaveDoc(int idx) {
    if (idx < 0 || idx >= static_cast<int>(docs_.size())) return false;
    Document* d = docs_[idx].get();
    std::wstring def = d->name + L".png";
    std::wstring path = util::OpenSaveDialog(
        hwnd_, true,
        L"PNG 图片\0*.png\0JPG 图片\0*.jpg\0所有文件\0*.*\0",
        L"png", def.c_str(), L"保存截图", util::DownloadsDir().c_str());
    if (path.empty()) return false;
    bool jpg = path.size() > 4 && _wcsicmp(path.c_str() + path.size() - 4, L".jpg") == 0;
    if (d->SaveAs(path, jpg)) {
        ShowStatusMessage(L"已保存：" + path);
        return true;
    }
    MessageBoxW(hwnd_, L"保存失败", APP_NAME, MB_ICONERROR);
    return false;
}

bool App::SaveAllDocs() {
    if (docs_.empty()) {
        ShowStatusMessage(L"没有可保存的页签");
        return true;
    }
    std::wstring dir = util::BrowseFolder(hwnd_, L"选择保存目录", util::DownloadsDir().c_str());
    if (dir.empty()) {
        // 用户取消路径选择：调用方据此决定不退出
        return false;
    }

    int choice = MessageBoxW(hwnd_, L"是否使用 JPG 格式？\n（否 = PNG）", APP_NAME,
                             MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) {
        // 用户取消格式选择：同样视为未完成保存
        return false;
    }
    bool jpg = (choice == IDYES);

    int n = 0;
    for (auto& d : docs_) {
        std::wstring path = dir + L"\\" + d->name + (jpg ? L".jpg" : L".png");
        if (d->SaveAs(path, jpg)) ++n;
    }
    ShowStatusMessage(util::Format(L"已保存 %d 张到 %s", n, dir.c_str()));
    return true;
}

void App::SelectTool(Tool t) {
    Canvas::Instance().SetTool(t);
    if (t == Tool::Number) Canvas::Instance().SetNumber(numberIndex_ + 1);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateStatus();
}

void App::OpenColorPicker() {
    COLORREF c = Canvas::Instance().GetDrawColor();
    BYTE a = Settings().drawAlpha;
    auto r = ColorPicker::Show(hwnd_, c, a);
    if (r.ok) {
        Canvas::Instance().SetDrawColor(r.color);
        Canvas::Instance().SetAlpha(r.alpha);
        Settings().drawColor = r.color;
        Settings().drawAlpha = r.alpha;
        Settings().Save();
        // update color button text
        for (auto& b : leftBtns_) {
            if (b.id == ID_CMD_COLOR) {
                b.text = L"色";
            }
        }
        ShowStatusMessage(L"当前颜色：" + util::ToHex(r.color));
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::OpenSettings() {
    SettingsDialog::Show(hwnd_);
}

void App::OnSettingsChanged() {
    UpdateHotkey();
    ApplyTheme();
    BuildToolbars();
    LayoutChildren();
    InvalidateRect(hwnd_, nullptr, TRUE);
    UpdateTitle();
}

void App::UpdateHotkey() {
    UnregisterHotKey(hwnd_, kHotkeyId);
    UnregisterHotKey(hwnd_, kLongHotkeyId);
    RemoveHotkeyHook();

    bool needHook = false;
    std::wstring failMsg;

    // 区域截图
    {
        UINT mods = Settings().hotkeyModifiers | 0x4000; // MOD_NOREPEAT
        if (RegisterHotKey(hwnd_, kHotkeyId, mods, Settings().hotkeyVk) ||
            RegisterHotKey(hwnd_, kHotkeyId, Settings().hotkeyModifiers, Settings().hotkeyVk)) {
            ShowStatusMessage(L"区域截图快捷键已注册：" + Settings().hotkeyText);
        } else {
            needHook = true;
            failMsg += L"区域截图「" + Settings().hotkeyText + L"」\n";
        }
    }

    // 长截图
    {
        UINT mods = Settings().longHotkeyModifiers | 0x4000;
        if (RegisterHotKey(hwnd_, kLongHotkeyId, mods, Settings().longHotkeyVk) ||
            RegisterHotKey(hwnd_, kLongHotkeyId, Settings().longHotkeyModifiers, Settings().longHotkeyVk)) {
            ShowStatusMessage(L"长截图快捷键已注册：" + Settings().longHotkeyText);
        } else {
            needHook = true;
            failMsg += L"长截图「" + Settings().longHotkeyText + L"」\n";
        }
    }

    if (!needHook) return;

    // 有一方被占用时：全部改走钩子，避免 RegisterHotKey 与钩子对同一组合键双触发
    UnregisterHotKey(hwnd_, kHotkeyId);
    UnregisterHotKey(hwnd_, kLongHotkeyId);

    std::wstring msg =
        L"以下快捷键已被其他程序占用：\n" + failMsg + L"\n"
        L"本工具将强制接管（忽略原程序占用）。\n"
        L"按下对应组合键时仍会触发截图；原程序一般不会再响应。\n\n"
        L"若不想冲突，可在「设置」里改成其他快捷键。";
    if (hwnd_) {
        MessageBoxW(hwnd_, msg.c_str(), APP_NAME, MB_ICONWARNING | MB_OK);
    } else {
        MessageBoxW(nullptr, msg.c_str(), APP_NAME, MB_ICONWARNING | MB_OK);
    }

    InstallHotkeyHook();
    if (g_kbHook) {
        ShowStatusMessage(L"快捷键被占用，已强制接管：" + failMsg);
    } else {
        ShowStatusMessage(L"快捷键被占用且强制接管失败，请在设置中更换快捷键");
    }
}

void App::UpdateTitle() {
    std::wstring title = std::wstring(APP_NAME) + L" " + APP_VERSION;
    if (Document* d = ActiveDoc()) {
        title += L" — " + d->name + L" (" + std::to_wstring(d->Width()) + L"×" + std::to_wstring(d->Height()) + L")";
    }
    SetWindowTextW(hwnd_, title.c_str());
}

void App::UpdateTabBar() {
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::UpdateStatus() {
    if (!status_) return;
    Document* d = ActiveDoc();
    std::wstring p1 = statusMsg_.empty() ? L"就绪" : statusMsg_;
    std::wstring p2;
    std::wstring p3;
    std::wstring p4;
    if (d) {
        float z = d->zoom * 100.0f;
        p2 = util::Format(L"页签 %s  ·  %d×%d  ·  缩放 %.0f%%",
                          d->name.c_str(), d->Width(), d->Height(), z);
        int rx, ry, rw, rh;
        if (d->GetRegion(rx, ry, rw, rh)) {
            p3 = util::Format(L"框选 %d×%d  ·  可马赛克/复制", rw, rh);
        } else if (d->GetSelected()) {
            RectF b;
            d->GetSelected()->GetBounds(b);
            p3 = util::Format(L"选中 %d×%d  ·  %s",
                              static_cast<int>(b.Width), static_cast<int>(b.Height),
                              AnnTypeLabel(d->GetSelected()->type));
        } else if (hasHover_) {
            p3 = util::Format(L"坐标 (%.0f, %.0f)", hoverIx_, hoverIy_);
        }
        p4 = L"颜色 " + util::ToHex(Canvas::Instance().GetDrawColor());
    } else {
        p2 = L"无文档";
        p4 = L"颜色 " + util::ToHex(Canvas::Instance().GetDrawColor());
    }
    SendMessageW(status_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(p1.c_str()));
    SendMessageW(status_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(p2.c_str()));
    SendMessageW(status_, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(p3.c_str()));
    SendMessageW(status_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(p4.c_str()));
}

void App::ShowStatusMessage(const std::wstring& msg) {
    statusMsg_ = msg;
    UpdateStatus();
}

void App::SetCanvasHover(float ix, float iy) {
    hoverIx_ = ix;
    hoverIy_ = iy;
    hasHover_ = true;
    // throttle status updates lightly
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (now - last > 80) {
        last = now;
        UpdateStatus();
    }
}

int App::HitTopButton(int x, int y) const {
    POINT pt = { x, y };
    for (size_t i = 0; i < topBtns_.size(); ++i) {
        if (PtInRect(&topBtns_[i].rc, pt)) return static_cast<int>(i);
    }
    return -1;
}

int App::HitLeftButton(int x, int y) const {
    POINT pt = { x, y };
    for (size_t i = 0; i < leftBtns_.size(); ++i) {
        if (PtInRect(&leftBtns_[i].rc, pt)) return static_cast<int>(i);
    }
    return -1;
}

int App::HitTab(int x, int y) const {
    RECT rc;
    GetClientRect(hwnd_, const_cast<RECT*>(&rc));
    int h = rc.bottom;
    int tabTop = h - statusH_ - tabH_;
    if (y < tabTop || y >= tabTop + tabH_) return -1;
    int tx = leftW_ + 8;
    int tw = util::Scale(36, dpi_); // 50% of previous 72px
    for (size_t i = 0; i < docs_.size(); ++i) {
        RECT trc = { tx, tabTop + 4, tx + tw, tabTop + 4 + tabH_ - 8 };
        if (PtInRect(&trc, { x, y })) return static_cast<int>(i);
        tx += tw + 4;
    }
    return -1;
}

void App::OnLButtonDown(int x, int y) {
    int cmpId = 0;
    HitTestCompareControls(x, y, cmpId);
    if (cmpId != 0) {
        OnCommand(cmpId);
        return;
    }
    int top = HitTopButton(x, y);
    if (top >= 0) {
        // 与页签一致：按下先点亮（主题色），松开在同一按钮上才执行
        pressedTop_ = top;
        SetCapture(hwnd_); // 按住拖出窗口也能收到 WM_LBUTTONUP
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    int left = HitLeftButton(x, y);
    if (left >= 0) {
        OnCommand(leftBtns_[left].id);
        return;
    }
    int tab = HitTab(x, y);
    if (tab >= 0) {
        ActivateDoc(tab);
        return;
    }
}

void App::OnLButtonUp(int x, int y) {
    if (pressedTop_ < 0) return;
    int top = HitTopButton(x, y);
    bool fire = (top == pressedTop_);
    pressedTop_ = -1;
    ReleaseCapture();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (fire) OnCommand(topBtns_[top].id);
}

void App::OnRButtonDown(int x, int y) {
    int tab = HitTab(x, y);
    if (tab >= 0) {
        // Do not switch active tab — compare target is the right-clicked tab
        contextTabIdx_ = tab;
        CreateTabMenu(tab, x, y);
        return;
    }
}

void App::OnMouseMove(int x, int y) {
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd_;
    TrackMouseEvent(&tme);

    int left = HitLeftButton(x, y);
    int top = HitTopButton(x, y); // 顶部按钮本身有中文，不显示悬浮提示

    // 仅在进入/离开左侧图标按钮时更新，避免每帧重绘导致闪烁
    if (left != hoverLeft_) {
        hoverLeft_ = left;
        KillTimer(hwnd_, kTimerTooltip);
        if (left >= 0) {
            HideTooltip();
            // 延迟约 0.35s 再显示，更稳定
            SetTimer(hwnd_, 1, 350, nullptr);
        } else {
            HideTooltip();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (top != hoverTop_) {
        hoverTop_ = top;
        InvalidateRect(hwnd_, nullptr, FALSE); // 悬停/按下态变化需要重绘
    }
    if (pressedTop_ >= 0) InvalidateRect(hwnd_, nullptr, FALSE);
}

// WM_TIMER handled in Handle()

void App::CreateTabMenu(int tabIdx, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_MENU_SAVE, L"保存");
    AppendMenuW(menu, MF_STRING, ID_MENU_SAVE_ALL, L"全部保存");
    // 「与当前页签对比」：右键的页签 vs 当前激活页签
    if (tabIdx != activeIdx_ && activeIdx_ >= 0 &&
        activeIdx_ < static_cast<int>(docs_.size())) {
        AppendMenuW(menu, MF_STRING, ID_MENU_COMPARE, L"与当前页签对比");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_MENU_CLOSE, L"关闭");
    AppendMenuW(menu, MF_STRING, ID_MENU_CLOSE_OTHERS, L"关闭其他");
    POINT pt = { x, y };
    ClientToScreen(hwnd_, &pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void App::StartCompare(int targetIdx) {
    if (targetIdx < 0 || targetIdx >= static_cast<int>(docs_.size())) return;
    if (activeIdx_ < 0 || activeIdx_ >= static_cast<int>(docs_.size())) return;
    if (targetIdx == activeIdx_) {
        ShowStatusMessage(L"不能与当前页签自身对比");
        return;
    }

    compareIdx_ = targetIdx;
    compareMode_ = true;
    compareSyncScroll_ = true; // default on
    CompareView::Instance().SetDocument(docs_[compareIdx_].get());
    LayoutChildren();
    if (compareSyncScroll_) OnMainCanvasScrolled();
    UpdateTabBar();
    ShowStatusMessage(util::Format(L"对比模式：%s ↔ %s（同步滚动默认开）",
                                   docs_[activeIdx_]->name.c_str(),
                                   docs_[compareIdx_]->name.c_str()));
}

void App::ExitCompare() {
    if (!compareMode_) return;
    compareMode_ = false;
    compareIdx_ = -1;
    compareSyncScroll_ = true;
    CompareView::Instance().SetDocument(nullptr);
    CompareView::Instance().ShowPane(false);
    LayoutChildren();
    UpdateTabBar();
    ShowStatusMessage(L"已退出对比");
}

void App::ToggleCompareSyncScroll() {
    if (!compareMode_) return;
    compareSyncScroll_ = !compareSyncScroll_;
    if (compareSyncScroll_) OnMainCanvasScrolled();
    ShowStatusMessage(compareSyncScroll_ ? L"同步滚动：开" : L"同步滚动：关（左右独立滚动）");
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnMainCanvasScrolled() {
    if (!compareMode_ || !compareSyncScroll_) return;
    Document* mainDoc = ActiveDoc();
    Document* cmpDoc = CompareView::Instance().GetDocument();
    if (!mainDoc || !cmpDoc || mainDoc == cmpDoc) return;
    static bool syncing = false;
    if (syncing) return;
    syncing = true;
    CompareView::Instance().ApplyScroll(mainDoc->scrollX, mainDoc->scrollY);
    syncing = false;
}

void App::OnComparePaneScrolled() {
    if (!compareMode_ || !compareSyncScroll_) return;
    Document* mainDoc = ActiveDoc();
    Document* cmpDoc = CompareView::Instance().GetDocument();
    if (!mainDoc || !cmpDoc || mainDoc == cmpDoc) return;
    static bool syncing = false;
    if (syncing) return;
    syncing = true;
    mainDoc->scrollX = cmpDoc->scrollX;
    mainDoc->scrollY = cmpDoc->scrollY;
    Canvas::Instance().UpdateScrollBars();
    Canvas::Instance().Refresh();
    syncing = false;
}
