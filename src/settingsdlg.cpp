#include "settingsdlg.h"
#include "settings.h"
#include "version.h"
#include "app.h"
#include "util.h"
#include "updater.h"
#include <thread>
#include <memory>

namespace {

const int kW = 760;
const int kH = 520;

// 更新检测的后台结果槽（单实例单对话框）
updater::UpdateInfo s_updInfo;
std::wstring s_updDest;
std::wstring s_updErr;

enum {
    WM_APP_UPD_CHECKED = WM_APP + 21,  // 检测完成
    WM_APP_UPD_READY   = WM_APP + 22,  // 下载完成
    WM_APP_UPD_FAILED  = WM_APP + 23   // 下载失败
};

HICON g_setBlankIcon = nullptr;

HICON MakeBlankIconSet(int size) {
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

enum {
    IDC_HOTKEY = 3001,
    IDC_THEME = 3002,
    IDC_MOSAIC = 3003,
    IDC_LINE = 3004,
    IDC_BRUSH = 3005,
    IDC_THEMECOLOR = 3006,
    IDC_OK = 3007,
    IDC_CANCEL = 3008,
    IDC_VERSION = 3009,
    IDC_MINERU_TOKEN = 3010,
    IDC_MINERU_EYE = 3011,
    IDC_VOLC_KEY = 3012,
    IDC_VOLC_EYE = 3013,
    IDC_LONG_HOTKEY = 3014,
    IDC_CHECK_UPDATE = 3015
};

struct SetDlgState {
    HWND hwnd = nullptr;
    bool ok = false;
    bool done = false;
    COLORREF themeColor = RGB(0, 0x78, 0xD4);
    HWND colorBtn = nullptr;
    AppSettings draft;
    bool showMineru = false;
    bool showVolc = false;
    // 检测更新
    HWND verLabel = nullptr;
    bool updateBusy = false;
};

const wchar_t* kClass = L"ScreenshotToolSettingsDlg";

void PaintColorBtn(HWND hwnd, SetDlgState* st) {
    if (!st || !st->colorBtn) return;
    HDC hdc = GetDC(st->colorBtn);
    if (!hdc) return;
    RECT rc; GetClientRect(st->colorBtn, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bm);
    {
        Graphics g(mem);
        SolidBrush br(ToGpColor(st->themeColor));
        g.FillRectangle(&br, 0, 0, rc.right, rc.bottom);
        Pen pen(Color(255, 80, 80, 80), 1);
        g.DrawRectangle(&pen, 0, 0, rc.right - 1, rc.bottom - 1);
    }
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(st->colorBtn, hdc);
}

void DrawEyeButton(DRAWITEMSTRUCT* dis, bool shown) {
    if (!dis) return;
    Graphics g(dis->hDC);
    RECT rc = dis->rcItem;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    SolidBrush bg(Color(255, 245, 245, 245));
    g.FillRectangle(&bg, 0, 0, w, h);
    Pen border(Color(255, 180, 180, 180), 1);
    g.DrawRectangle(&border, 0, 0, w - 1, h - 1);

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    REAL cx = w * 0.5f;
    REAL cy = h * 0.5f;
    REAL ew = (std::min)(w, h) * 0.38f;
    REAL eh = (std::min)(w, h) * 0.22f;
    Pen eye(Color(255, 60, 60, 60), 1.4f);
    // almond eye outline
    GraphicsPath path;
    path.AddBezier(cx - ew, cy, cx - ew * 0.4f, cy - eh * 1.6f,
                   cx + ew * 0.4f, cy - eh * 1.6f, cx + ew, cy);
    path.AddBezier(cx + ew, cy, cx + ew * 0.4f, cy + eh * 1.6f,
                   cx - ew * 0.4f, cy + eh * 1.6f, cx - ew, cy);
    g.DrawPath(&eye, &path);
    if (shown) {
        SolidBrush pupil(Color(255, 40, 40, 40));
        g.FillEllipse(&pupil, cx - eh * 0.55f, cy - eh * 0.55f, eh * 1.1f, eh * 1.1f);
    } else {
        Pen slash(Color(255, 60, 60, 60), 1.4f);
        g.DrawLine(&slash, cx - ew * 0.85f, cy + eh * 0.9f, cx + ew * 0.85f, cy - eh * 0.9f);
        SolidBrush pupil(Color(255, 40, 40, 40));
        g.FillEllipse(&pupil, cx - eh * 0.45f, cy - eh * 0.45f, eh * 0.9f, eh * 0.9f);
    }
}

void TogglePassword(HWND edit, bool show) {
    if (!edit) return;
    SendMessageW(edit, EM_SETPASSWORDCHAR, show ? 0 : static_cast<WPARAM>(L'•'), 0);
    InvalidateRect(edit, nullptr, TRUE);
}

LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SetDlgState* st = reinterpret_cast<SetDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = static_cast<SetDlgState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->hwnd = hwnd;
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(40, 40, 40));
        static HBRUSH br = CreateSolidBrush(RGB(250, 250, 250));
        return reinterpret_cast<LRESULT>(br);
    }
    case WM_GETICON:
        // 标题栏不显示图标
        return 0;
    case WM_COMMAND: {
        if (!st) return 0;
        int id = LOWORD(wParam);
        if (id == IDC_MINERU_EYE) {
            st->showMineru = !st->showMineru;
            TogglePassword(GetDlgItem(hwnd, IDC_MINERU_TOKEN), st->showMineru);
            return 0;
        }
        if (id == IDC_VOLC_EYE) {
            st->showVolc = !st->showVolc;
            TogglePassword(GetDlgItem(hwnd, IDC_VOLC_KEY), st->showVolc);
            return 0;
        }
        if (id == IDC_CHECK_UPDATE) {
            if (st->updateBusy) return 0;
            st->updateBusy = true;
            EnableWindow(GetDlgItem(hwnd, IDC_CHECK_UPDATE), FALSE);
            SetWindowTextW(st->verLabel, L"正在检测更新，请稍候…");
            auto info = std::make_shared<updater::UpdateInfo>();
            HWND h = hwnd;
            std::thread([h, info]() {
                updater::CheckForUpdate(*info);
                s_updInfo = *info;
                PostMessageW(h, WM_APP_UPD_CHECKED, 0, 0);
            }).detach();
            return 0;
        }
        if (id == IDC_OK) {
            wchar_t buf[256] = {};
            GetWindowTextW(GetDlgItem(hwnd, IDC_HOTKEY), buf, 256);
            UINT m = 0, v = 0;
            if (ParseHotkeyText(buf, m, v) && v != 0) {
                st->draft.hotkeyModifiers = m;
                st->draft.hotkeyVk = v;
                st->draft.hotkeyText = HotkeyToText(m, v);
            } else {
                MessageBoxW(hwnd, L"区域截图快捷键格式无效，示例：Ctrl+Shift+R", L"提示", MB_ICONWARNING);
                return 0;
            }

            wchar_t lbuf[256] = {};
            GetWindowTextW(GetDlgItem(hwnd, IDC_LONG_HOTKEY), lbuf, 256);
            UINT lm = 0, lv = 0;
            if (ParseHotkeyText(lbuf, lm, lv) && lv != 0) {
                st->draft.longHotkeyModifiers = lm;
                st->draft.longHotkeyVk = lv;
                st->draft.longHotkeyText = HotkeyToText(lm, lv);
            } else {
                MessageBoxW(hwnd, L"长截图快捷键格式无效，示例：Ctrl+Shift+E", L"提示", MB_ICONWARNING);
                return 0;
            }
            if (st->draft.hotkeyModifiers == st->draft.longHotkeyModifiers &&
                st->draft.hotkeyVk == st->draft.longHotkeyVk) {
                MessageBoxW(hwnd, L"区域截图与长截图的快捷键不能相同，请修改其中一个。",
                            L"提示", MB_ICONWARNING);
                return 0;
            }
            int theme = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_THEME), CB_GETCURSEL, 0, 0));
            if (theme < 0) theme = 0;
            st->draft.theme = static_cast<ThemeMode>(theme);
            st->draft.mosaicSize = GetDlgItemInt(hwnd, IDC_MOSAIC, nullptr, FALSE);
            st->draft.lineThickness = GetDlgItemInt(hwnd, IDC_LINE, nullptr, FALSE);
            st->draft.brushThickness = GetDlgItemInt(hwnd, IDC_BRUSH, nullptr, FALSE);
            if (st->draft.mosaicSize < 1) st->draft.mosaicSize = 1;
            if (st->draft.lineThickness < 1) st->draft.lineThickness = 1;
            if (st->draft.brushThickness < 1) st->draft.brushThickness = 1;

            wchar_t tokenBuf[512] = {};
            GetWindowTextW(GetDlgItem(hwnd, IDC_MINERU_TOKEN), tokenBuf, 512);
            st->draft.mineruToken = util::TrimToken(tokenBuf);
            wchar_t volcBuf[512] = {};
            GetWindowTextW(GetDlgItem(hwnd, IDC_VOLC_KEY), volcBuf, 512);
            st->draft.volcApiKey = util::TrimToken(volcBuf);

            // 保留原主题色（界面已移除该项）
            st->ok = true;
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDC_CANCEL) {
            st->ok = false;
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (st) PaintColorBtn(hwnd, st);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!dis) return 0;
        if (st && dis->CtlID == IDC_THEMECOLOR) {
            Graphics g(dis->hDC);
            SolidBrush br(ToGpColor(st->themeColor));
            g.FillRectangle(&br, 0, 0, dis->rcItem.right - dis->rcItem.left,
                            dis->rcItem.bottom - dis->rcItem.top);
            return TRUE;
        }
        if (dis->CtlID == IDC_MINERU_EYE) {
            DrawEyeButton(dis, st && st->showMineru);
            return TRUE;
        }
        if (dis->CtlID == IDC_VOLC_EYE) {
            DrawEyeButton(dis, st && st->showVolc);
            return TRUE;
        }
        return 0;
    }
    case WM_APP_UPD_CHECKED: {
        if (!st) return 0;
        EnableWindow(GetDlgItem(hwnd, IDC_CHECK_UPDATE), TRUE);
        st->updateBusy = false;
        const updater::UpdateInfo& i = s_updInfo;
        if (i.available) {
            std::wstring msg = L"发现新版本 v" + i.latestVersion +
                               L"（当前 v" + APP_VERSION + L"）。\n"
                               L"将自动下载并重启程序（未保存的设置修改会丢失），是否继续？";
            if (MessageBoxW(hwnd, msg.c_str(), L"检测更新",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                s_updDest = updater::NewExeStagingPath();
                SetWindowTextW(st->verLabel, L"正在下载更新，请稍候…");
                EnableWindow(GetDlgItem(hwnd, IDC_CHECK_UPDATE), FALSE);
                st->updateBusy = true;
                auto url = std::make_shared<std::wstring>(i.assetUrl);
                auto dest = std::make_shared<std::wstring>(s_updDest);
                HWND h = hwnd;
                std::thread([h, url, dest]() {
                    std::wstring err;
                    bool ok = updater::DownloadUpdate(*url, *dest, err);
                    s_updErr = err;
                    PostMessageW(h, ok ? WM_APP_UPD_READY : WM_APP_UPD_FAILED, 0, 0);
                }).detach();
            }
        } else if (!i.fetched) {
            SetWindowTextW(st->verLabel, (L"检测更新失败：" + i.error).c_str());
        } else {
            SetWindowTextW(st->verLabel,
                           (std::wstring(L"当前已是最新版本（v") + APP_VERSION + L"）").c_str());
        }
        return 0;
    }
    case WM_APP_UPD_READY: {
        if (!st || s_updDest.empty()) return 0;
        if (updater::ApplyUpdateAndRestart(s_updDest)) {
            ExitProcess(0); // 新版本进程已启动，当前进程立即退出
        }
        EnableWindow(GetDlgItem(hwnd, IDC_CHECK_UPDATE), TRUE);
        st->updateBusy = false;
        SetWindowTextW(st->verLabel, L"更新失败：文件替换未成功，可稍后重试");
        return 0;
    }
    case WM_APP_UPD_FAILED: {
        if (!st) return 0;
        EnableWindow(GetDlgItem(hwnd, IDC_CHECK_UPDATE), TRUE);
        st->updateBusy = false;
        SetWindowTextW(st->verLabel, (L"下载更新失败：" + s_updErr).c_str());
        return 0;
    }
    case WM_CLOSE:
        if (st) { st->ok = false; st->done = true; DestroyWindow(hwnd); }
        return 0;
    case WM_DESTROY:
        if (st) st->done = true;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void EnsureClass(HINSTANCE hi) {
    static bool done = false;
    if (done) return;
    // 不设置窗口类图标：标题栏「设置」左侧不要图标
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
    wc.hIcon = nullptr;
    wc.hIconSm = nullptr;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    done = true;
}

} // namespace

bool SettingsDialog::Show(HWND owner) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsureClass(hi);

    SetDlgState st;
    st.draft = Settings();
    st.themeColor = Settings().themeColor;

    RECT wr = { 0, 0, kW, kH };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exStyle = WS_EX_TOPMOST;
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
    int outerW = wr.right - wr.left;
    int outerH = wr.bottom - wr.top;
    // 弹窗显示在主窗口所在的显示器（多屏时不再固定弹到主屏）
    POINT pos = util::CenterOnMonitorOf(owner, outerW, outerH);

    HWND hwnd = CreateWindowExW(exStyle,
                                kClass, L"设置",
                                style,
                                pos.x, pos.y, outerW, outerH,
                                owner, nullptr, hi, &st);
    if (!hwnd) return false;
    SetWindowTextW(hwnd, L"设置");
    // 去掉标题栏图标（大/小 + 窗口类）
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, 0);
    SetClassLongPtrW(hwnd, GCLP_HICON, 0);
    SetClassLongPtrW(hwnd, GCLP_HICONSM, 0);

    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    // 紧凑布局：减小行距与底部留白
    int y = 16;
    auto label = [&](const wchar_t* text, int x, int yy) {
        HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                               x, yy, 110, 24, hwnd, nullptr, hi, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    };
    auto edit = [&](int id, const wchar_t* val, int x, int yy, int w) {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", val,
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 x, yy, w, 26, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hi, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    };

    label(L"区域截图", 20, y);
    edit(IDC_HOTKEY, st.draft.hotkeyText.c_str(), 140, y - 3, 300);
    y += 42;

    label(L"长截图", 20, y);
    edit(IDC_LONG_HOTKEY, st.draft.longHotkeyText.c_str(), 140, y - 3, 300);
    y += 42;

    label(L"主题", 20, y);
    HWND theme = CreateWindowW(L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                               140, y - 3, 300, 160, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_THEME)), hi, nullptr);
    SendMessageW(theme, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"跟随系统"));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"明亮"));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"暗色"));
    SendMessageW(theme, CB_SETCURSEL, static_cast<int>(st.draft.theme), 0);
    y += 42;

    label(L"马赛克密度", 20, y);
    edit(IDC_MOSAIC, std::to_wstring(st.draft.mosaicSize).c_str(), 140, y - 3, 100);
    y += 42;

    label(L"线条粗细", 20, y);
    edit(IDC_LINE, std::to_wstring(st.draft.lineThickness).c_str(), 140, y - 3, 100);
    y += 42;

    label(L"笔刷粗细", 20, y);
    edit(IDC_BRUSH, std::to_wstring(st.draft.brushThickness).c_str(), 140, y - 3, 100);
    y += 42;

    // 密码字号略小，保证长 token 单行可完整显示
    HFONT fontPw = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    if (!fontPw) fontPw = font;

    // MinerU token：密码框 + 小眼睛 + 右侧说明
    label(L"MinerU token", 20, y);
    HWND mineruEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st.draft.mineruToken.c_str(),
                                      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
                                      140, y - 3, 470, 26, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MINERU_TOKEN)), hi, nullptr);
    SendMessageW(mineruEdit, EM_SETPASSWORDCHAR, static_cast<WPARAM>(L'•'), 0);
    SendMessageW(mineruEdit, WM_SETFONT, reinterpret_cast<WPARAM>(fontPw), TRUE);
    HWND mineruEye = CreateWindowW(L"BUTTON", L"",
                                   WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   614, y - 3, 28, 26, hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MINERU_EYE)), hi, nullptr);
    (void)mineruEye;
    HWND mineruHint = CreateWindowW(L"STATIC", L"提取内容用",
                                    WS_CHILD | WS_VISIBLE,
                                    646, y + 2, 90, 22, hwnd, nullptr, hi, nullptr);
    SendMessageW(mineruHint, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    y += 42;

    // 火山 API Key
    label(L"火山 API Key", 20, y);
    HWND volcEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st.draft.volcApiKey.c_str(),
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD,
                                    140, y - 3, 470, 26, hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_KEY)), hi, nullptr);
    SendMessageW(volcEdit, EM_SETPASSWORDCHAR, static_cast<WPARAM>(L'•'), 0);
    SendMessageW(volcEdit, WM_SETFONT, reinterpret_cast<WPARAM>(fontPw), TRUE);
    HWND volcEye = CreateWindowW(L"BUTTON", L"",
                                 WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 614, y - 3, 28, 26, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VOLC_EYE)), hi, nullptr);
    (void)volcEye;
    HWND volcHint = CreateWindowW(L"STATIC", L"魔法消除用",
                                  WS_CHILD | WS_VISIBLE,
                                  646, y + 2, 90, 22, hwnd, nullptr, hi, nullptr);
    SendMessageW(volcHint, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    y += 42;

    // 申请地址提示（版本信息与火山 API Key 之间）
    const wchar_t* tipText =
        L"MinerU token申请地址（免费，每3个月一换）：https://mineru.net/apiManage/token\n"
        L"火山APIkey申请地址（费用超低）：https://console.volcengine.com/imp/ai-mediakit/settings?";
    HFONT fontTip = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
    HWND tip = CreateWindowW(L"STATIC", tipText,
                             WS_CHILD | WS_VISIBLE,
                             20, y, kW - 40, 48, hwnd, nullptr, hi, nullptr);
    SendMessageW(tip, WM_SETFONT, reinterpret_cast<WPARAM>(fontTip ? fontTip : font), TRUE);
    y += 56;

    // 打包时间：yyyy-MM-dd HH:mm:ss（右侧带「检测更新」按钮）
    std::wstring ver = std::wstring(L"版本 ") + APP_VERSION +
                       L"  ·  打包时间 " + AppBuildTimeFormatted();
    HWND verH = CreateWindowW(L"STATIC", ver.c_str(), WS_CHILD | WS_VISIBLE,
                              20, y, kW - 160, 22, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_VERSION)), hi, nullptr);
    SendMessageW(verH, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    HWND chkUpd = CreateWindowW(L"BUTTON", L"检测更新",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                kW - 130, y - 4, 110, 28, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHECK_UPDATE)), hi, nullptr);
    SendMessageW(chkUpd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    st.verLabel = verH;
    y += 30;

    // 确定/取消固定在窗口底部（内容底部与按钮之间留白，视觉更稳）
    int btnY = kH - 52;
    HWND ok = CreateWindowW(L"BUTTON", L"确定",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            kW - 220, btnY, 90, 32, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OK)), hi, nullptr);
    HWND cancel = CreateWindowW(L"BUTTON", L"取消",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                kW - 120, btnY, 90, 32, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANCEL)), hi, nullptr);
    SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

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
    if (font) DeleteObject(font);
    if (fontPw && fontPw != font) DeleteObject(fontPw);
    if (fontTip) DeleteObject(fontTip);

    if (!st.ok) return false;

    AppSettings& s = Settings();
    s.hotkeyModifiers = st.draft.hotkeyModifiers;
    s.hotkeyVk = st.draft.hotkeyVk;
    s.hotkeyText = st.draft.hotkeyText;
    s.longHotkeyModifiers = st.draft.longHotkeyModifiers;
    s.longHotkeyVk = st.draft.longHotkeyVk;
    s.longHotkeyText = st.draft.longHotkeyText;
    s.theme = st.draft.theme;
    s.mosaicSize = st.draft.mosaicSize;
    s.lineThickness = st.draft.lineThickness;
    s.brushThickness = st.draft.brushThickness;
    s.mineruToken = util::TrimToken(st.draft.mineruToken);
    s.volcApiKey = util::TrimToken(st.draft.volcApiKey);
    // themeColor 保持原值（设置界面已不再提供）
    s.Save();
    App::Instance().OnSettingsChanged();
    return true;
}
