#include "colorpicker.h"
#include "settings.h"

using namespace Gdiplus;

namespace {

const int kDlgW = 520;
const int kDlgH = 600;
const int kWheelSize = 200;

HICON g_cpBlankIcon = nullptr;

HICON MakeWheelIcon(int size) {
    Bitmap bmp(size, size, PixelFormat32bppPARGB);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.Clear(Color(255, 245, 245, 245));
        REAL cx = size * 0.5f, cy = size * 0.5f;
        REAL R = size * 0.42f;
        // 迷你色相环（六色扇形）
        for (int a = 0; a < 360; a += 30) {
            COLORREF c = util::HSLtoRGB(static_cast<float>(a), 1.0f, 0.5f);
            SolidBrush br(ToGpColor(c));
            g.FillPie(&br, cx - R, cy - R, R * 2, R * 2, static_cast<REAL>(a), 30.0f);
        }
        // 中心白孔
        SolidBrush hole(Color(255, 245, 245, 245));
        g.FillEllipse(&hole, cx - R * 0.28f, cy - R * 0.28f, R * 0.56f, R * 0.56f);
        Pen edge(Color(255, 90, 90, 90), 1.0f);
        g.DrawEllipse(&edge, cx - R, cy - R, R * 2, R * 2);
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

HICON MakeBlankIconCP(int size) {
    return MakeWheelIcon(size);
}

struct PickState {
    HWND hwnd = nullptr;
    float h = 0, s = 1.0f, l = 0.5f;
    int r = 255, g = 0, b = 0;
    int alpha = 255;
    std::wstring hex = L"#FF0000";
    bool draggingWheel = false;
    bool draggingSlider = false;
    int sliderId = 0;
    bool updating = false;
    bool done = false;
    ColorResult result;
    // cached wheel bitmap keyed by lightness
    std::unique_ptr<Bitmap> wheelCache;
    float wheelCacheL = -1.0f;

    void SyncFromHSL() {
        COLORREF c = util::HSLtoRGB(h, s, l);
        r = GetRValue(c); g = GetGValue(c); b = GetBValue(c);
        hex = util::ToHex(RGB(r, g, b));
    }
    void SyncFromRGB() {
        COLORREF c = RGB(r, g, b);
        util::RGBtoHSL(c, h, s, l);
        hex = util::ToHex(c);
    }
    void SyncFromHex() {
        COLORREF c = util::ParseHex(hex);
        r = GetRValue(c); g = GetGValue(c); b = GetBValue(c);
        util::RGBtoHSL(c, h, s, l);
    }
    COLORREF Current() const { return RGB(r, g, b); }
};

// layout helpers
enum {
    IDC_WHEEL = 1001,
    IDC_HEX = 1002,
    IDC_EYEDROP = 1003,
    IDC_OK = 1004,
    IDC_CANCEL = 1005,
    IDC_WHITE = 1006,
    IDC_SLIDER_BASE = 1100,
    IDC_EDIT_BASE = 1200,
};

RECT WheelRect(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int x = (rc.right - kWheelSize) / 2;
    return { x, 24, x + kWheelSize, 24 + kWheelSize };
}

RECT SliderRect(HWND hwnd, int idx) {
    RECT rc; GetClientRect(hwnd, &rc);
    int left = 90;
    int right = rc.right - 100;
    int top = 240 + idx * 44;
    return { left, top + 6, right, top + 28 };
}

const wchar_t* kSliderNames[] = { L"明度", L"R", L"G", L"B", L"透明度" };

void DrawWheel(HDC hdc, RECT rc, PickState& st) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeHighQuality);
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;
    int R = (std::min)(rc.right - rc.left, rc.bottom - rc.top) / 2;
    if (R < 4) return;

    // Rebuild cache only when lightness changes
    if (!st.wheelCache || std::fabs(st.wheelCacheL - st.l) > 0.002f) {
        st.wheelCache = std::make_unique<Bitmap>(R * 2, R * 2, PixelFormat32bppARGB);
        BitmapData data;
        Rect lockRc(0, 0, R * 2, R * 2);
        if (st.wheelCache->LockBits(&lockRc, ImageLockModeWrite, PixelFormat32bppARGB, &data) == Ok) {
            auto* pixels = static_cast<BYTE*>(data.Scan0);
            for (int y = 0; y < R * 2; ++y) {
                auto* row = pixels + y * data.Stride;
                for (int x = 0; x < R * 2; ++x) {
                    int dx = x - R, dy = y - R;
                    int d2 = dx * dx + dy * dy;
                    BYTE* p = row + x * 4;
                    if (d2 > R * R) {
                        p[0] = p[1] = p[2] = p[3] = 0;
                        continue;
                    }
                    float dist = std::sqrt(static_cast<float>(d2)) / R;
                    float ang = std::atan2(static_cast<float>(dy), static_cast<float>(dx)) * 180.0f / 3.14159265f;
                    if (ang < 0) ang += 360;
                    COLORREF c = util::HSLtoRGB(ang, dist, st.l);
                    p[0] = GetBValue(c);
                    p[1] = GetGValue(c);
                    p[2] = GetRValue(c);
                    p[3] = 255;
                }
            }
            st.wheelCache->UnlockBits(&data);
        }
        st.wheelCacheL = st.l;
    }

    g.DrawImage(st.wheelCache.get(), cx - R, cy - R);

    float angRad = st.h * 3.14159265f / 180.0f;
    float mx = cx + std::cos(angRad) * st.s * R;
    float my = cy + std::sin(angRad) * st.s * R;
    Pen w(Color(255, 255, 255, 255), 2);
    Pen b(Color(255, 0, 0, 0), 1);
    g.DrawEllipse(&w, static_cast<INT>(mx) - 6, static_cast<INT>(my) - 6, 12, 12);
    g.DrawEllipse(&b, static_cast<INT>(mx) - 6, static_cast<INT>(my) - 6, 12, 12);
}

void DrawSlider(HDC hdc, RECT rc, int idx, const PickState& st) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeNone);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 4 || h < 4) return;

    for (int x = 0; x < w; ++x) {
        float t = static_cast<float>(x) / (std::max)(1, w - 1);
        COLORREF c;
        if (idx == 0) c = util::HSLtoRGB(st.h, st.s, t);
        else if (idx == 1) c = RGB(static_cast<BYTE>(t * 255), st.g, st.b);
        else if (idx == 2) c = RGB(st.r, static_cast<BYTE>(t * 255), st.b);
        else if (idx == 3) c = RGB(st.r, st.g, static_cast<BYTE>(t * 255));
        else c = RGB(st.r, st.g, st.b);
        SolidBrush br(ToGpColor(c, idx == 4 ? static_cast<BYTE>(t * 255) : 255));
        g.FillRectangle(&br, rc.left + x, rc.top, 1, h);
    }
    // 不绘制黑色边框，仅保留滑块

    int val = 0;
    if (idx == 0) val = static_cast<int>(st.l * 255);
    else if (idx == 1) val = st.r;
    else if (idx == 2) val = st.g;
    else if (idx == 3) val = st.b;
    else val = st.alpha;
    int hx = rc.left + static_cast<int>(val / 255.0f * (w - 8));
    SolidBrush hb(Color(255, 255, 255, 255));
    g.FillRectangle(&hb, hx, rc.top - 2, 8, h + 4);
    Pen hp(Color(255, 90, 90, 90), 1);
    g.DrawRectangle(&hp, hx, rc.top - 2, 8, h + 4);
}

int SliderFromX(HWND hwnd, int idx, int x) {
    RECT rc = SliderRect(hwnd, idx);
    int w = rc.right - rc.left;
    float t = static_cast<float>(x - rc.left) / (std::max)(1, w - 1);
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    return static_cast<int>(t * 255 + 0.5f);
}

void UpdateHexEdit(PickState* st) {
    wchar_t buf[32];
    swprintf_s(buf, L"%s", st->hex.c_str());
    if (HWND h = GetDlgItem(st->hwnd, IDC_HEX))
        SetWindowTextW(h, buf);
}

void UpdateValueEdits(PickState* st) {
    int vals[5] = {
        static_cast<int>(st->l * 255), st->r, st->g, st->b, st->alpha
    };
    for (int i = 0; i < 5; ++i) {
        HWND h = GetDlgItem(st->hwnd, IDC_EDIT_BASE + i);
        if (h) SetWindowTextW(h, std::to_wstring(vals[i]).c_str());
    }
}

void RefreshAll(PickState* st) {
    UpdateHexEdit(st);
    UpdateValueEdits(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void OnWheelClick(PickState* st, int x, int y) {
    RECT rc = WheelRect(st->hwnd);
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;
    int R = (std::min)(rc.right - rc.left, rc.bottom - rc.top) / 2;
    float dx = static_cast<float>(x - cx);
    float dy = static_cast<float>(y - cy);
    float dist = std::sqrt(dx * dx + dy * dy) / (std::max)(1.0f, static_cast<float>(R));
    float ang = std::atan2(dy, dx) * 180.0f / 3.14159265f;
    if (ang < 0) ang += 360;
    st->h = ang;
    st->s = (std::max)(0.0f, (std::min)(1.0f, dist));
    st->SyncFromHSL();
    RefreshAll(st);
}

// ---- Eyedropper ----
const wchar_t* kEyeClass = L"ScreenshotToolEyedropper";

struct EyeState {
    HWND hwnd = nullptr;
    std::unique_ptr<Bitmap> frozen;
    COLORREF picked = RGB(0, 0, 0);
    bool done = false;
    bool ok = false;
    int mag = 10;
};

LRESULT CALLBACK EyeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<EyeState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = static_cast<EyeState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->hwnd = hwnd;
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (st && st->frozen) {
            RECT rc; GetClientRect(hwnd, &rc);
            Graphics g(hdc);
            g.SetInterpolationMode(InterpolationModeNearestNeighbor);
            g.DrawImage(st->frozen.get(), 0, 0, rc.right, rc.bottom);

            POINT pt; GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int magSize = 120;
            int half = magSize / (2 * st->mag);
            int sx = pt.x - half, sy = pt.y - half;
            // draw magnifier
            Rect dst(pt.x + 20, pt.y - magSize - 20, magSize, magSize);
            if (dst.X + magSize > rc.right) dst.X = pt.x - magSize - 20;
            if (dst.Y < 4) dst.Y = pt.y + 20;
            g.DrawImage(st->frozen.get(), dst, sx, sy, half * 2, half * 2, UnitPixel);
            Pen pen(Color(255, 0, 0, 0), 2);
            g.DrawRectangle(&pen, dst);
            // center cross
            int ccx = dst.X + magSize / 2, ccy = dst.Y + magSize / 2;
            int cell = magSize / (half * 2);
            Pen cpen(Color(255, 255, 255, 255), 1);
            g.DrawLine(&cpen, ccx - cell / 2 - 4, ccy, ccx + cell / 2 + 4, ccy);
            g.DrawLine(&cpen, ccx, ccy - cell / 2 - 4, ccx, ccy + cell / 2 + 4);

            // HEX tip
            COLORREF c = st->picked;
            wchar_t tip[64];
            swprintf_s(tip, L"%s  RGB(%d,%d,%d)", util::ToHex(c).c_str(),
                       GetRValue(c), GetGValue(c), GetBValue(c));
            FontFamily family(L"Segoe UI");
            Font font(&family, 12, FontStyleBold, UnitPixel);
            RectF layout;
            g.MeasureString(tip, -1, &font, PointF(0, 0), &layout);
            float tx = static_cast<float>(dst.X);
            float ty = static_cast<float>(dst.Y + magSize + 6);
            SolidBrush bg(Color(220, 20, 20, 20));
            g.FillRectangle(&bg, tx, ty, layout.Width + 12, layout.Height + 8);
            SolidBrush fg(Color(255, 255, 255, 255));
            g.DrawString(tip, -1, &font, PointF(tx + 6, ty + 4), &fg);

            // preview swatch
            SolidBrush sw(ToGpColor(c));
            g.FillRectangle(&sw, dst.X + magSize - 24, dst.Y + magSize - 24, 20, 20);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (st) {
            POINT pt; GetCursorPos(&pt);
            st->picked = util::SampleScreenColor(pt);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (st) {
            POINT pt; GetCursorPos(&pt);
            st->picked = util::SampleScreenColor(pt);
            st->ok = true;
            st->done = true;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && st) {
            st->ok = false;
            st->done = true;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        if (st) st->done = true;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ---- Color dialog ----
const wchar_t* kPickClass = L"ScreenshotToolColorPicker";

PickState* g_pick = nullptr;

LRESULT CALLBACK PickProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PickState* st = reinterpret_cast<PickState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = static_cast<PickState*>(cs->lpCreateParams);
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
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bm);

        Graphics g(mem);
        SolidBrush bg(ToGpColor(RGB(250, 250, 250)));
        g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);

        if (st) {
            RECT wr = WheelRect(hwnd);
            DrawWheel(mem, wr, *st);

            FontFamily family(L"Microsoft YaHei");
            Font font(&family, 13, FontStyleRegular, UnitPixel);
            SolidBrush fg(Color(255, 30, 30, 30)); // 纯文字，无背景

            // 右上角小色块预览（不遮挡左侧标签）
            SolidBrush pv(ToGpColor(st->Current(), static_cast<BYTE>(st->alpha)));
            g.FillRectangle(&pv, rc.right - 48, 24, 28, 28);
            Pen pen(Color(255, 100, 100, 100), 1);
            g.DrawRectangle(&pen, rc.right - 48, 24, 28, 28);

            // 左侧文字标签：无背景色，与滑杆同行
            for (int i = 0; i < 5; ++i) {
                RECT sr = SliderRect(hwnd, i);
                DrawSlider(mem, sr, i, *st);
                g.DrawString(kSliderNames[i], -1, &font,
                              PointF(16.0f, static_cast<REAL>(sr.top + 2)),
                              &fg);
            }

            g.DrawString(L"HEX", -1, &font, PointF(16.0f, 468.0f), &fg);
            SolidBrush pc(ToGpColor(st->Current(), static_cast<BYTE>(st->alpha)));
            g.FillEllipse(&pc, static_cast<REAL>(rc.right - 48), 468.0f, 28.0f, 28.0f);
            Pen ep(Color(255, 100, 100, 100), 1);
            g.DrawEllipse(&ep, static_cast<REAL>(rc.right - 48), 468.0f, 28.0f, 28.0f);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bm);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_MOUSEMOVE: {
        if (!st) return 0;
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (msg == WM_LBUTTONDOWN) {
            RECT wr = WheelRect(hwnd);
            if (x >= wr.left && x <= wr.right && y >= wr.top && y <= wr.bottom) {
                st->draggingWheel = true;
                SetCapture(hwnd);
                OnWheelClick(st, x, y);
                return 0;
            }
            for (int i = 0; i < 5; ++i) {
                RECT sr = SliderRect(hwnd, i);
                if (x >= sr.left && x <= sr.right && y >= sr.top - 4 && y <= sr.bottom + 4) {
                    st->draggingSlider = true;
                    st->sliderId = i;
                    SetCapture(hwnd);
                    int v = SliderFromX(hwnd, i, x);
                    if (i == 0) { st->l = v / 255.0f; st->SyncFromHSL(); }
                    else if (i == 1) { st->r = v; st->SyncFromRGB(); }
                    else if (i == 2) { st->g = v; st->SyncFromRGB(); }
                    else if (i == 3) { st->b = v; st->SyncFromRGB(); }
                    else st->alpha = v;
                    RefreshAll(st);
                    return 0;
                }
            }
            return 0;
        }
        if (msg == WM_MOUSEMOVE) {
            if (st->draggingWheel) { OnWheelClick(st, x, y); return 0; }
            if (st->draggingSlider) {
                int v = SliderFromX(hwnd, st->sliderId, x);
                int i = st->sliderId;
                if (i == 0) { st->l = v / 255.0f; st->SyncFromHSL(); }
                else if (i == 1) { st->r = v; st->SyncFromRGB(); }
                else if (i == 2) { st->g = v; st->SyncFromRGB(); }
                else if (i == 3) { st->b = v; st->SyncFromRGB(); }
                else st->alpha = v;
                RefreshAll(st);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (st) {
            st->draggingWheel = false;
            st->draggingSlider = false;
            ReleaseCapture();
        }
        return 0;
    case WM_COMMAND: {
        if (!st) return 0;
        int id = LOWORD(wParam);
        if (id == IDC_OK) {
            wchar_t buf[64] = {};
            GetWindowTextW(GetDlgItem(hwnd, IDC_HEX), buf, 64);
            st->hex = buf;
            st->SyncFromHex();
            st->result.ok = true;
            st->result.color = st->Current();
            st->result.alpha = static_cast<BYTE>(st->alpha);
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDC_CANCEL) {
            st->result.ok = false;
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDC_WHITE) {
            st->r = 255; st->g = 255; st->b = 255;
            st->SyncFromRGB();
            st->alpha = 255;
            RefreshAll(st);
            return 0;
        }
        if (id == IDC_EYEDROP) {
            COLORREF c = st->Current();
            if (ColorPicker::Eyedropper(hwnd, c)) {
                st->r = GetRValue(c); st->g = GetGValue(c); st->b = GetBValue(c);
                st->SyncFromRGB();
                RefreshAll(st);
            }
            return 0;
        }
        // value edits on EN_CHANGE
        if (HIWORD(wParam) == EN_CHANGE) {
            for (int i = 0; i < 5; ++i) {
                if (LOWORD(wParam) == IDC_EDIT_BASE + i && !st->updating) {
                    wchar_t buf[32] = {};
                    GetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_BASE + i), buf, 32);
                    int v = _wtoi(buf);
                    v = (std::max)(0, (std::min)(255, v));
                    if (i == 0) { st->l = v / 255.0f; st->SyncFromHSL(); }
                    else if (i == 1) { st->r = v; st->SyncFromRGB(); }
                    else if (i == 2) { st->g = v; st->SyncFromRGB(); }
                    else if (i == 3) { st->b = v; st->SyncFromRGB(); }
                    else st->alpha = v;
                    st->updating = true;
                    UpdateHexEdit(st);
                    // update other edits lightly
                    st->updating = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        return 0;
    }
    case WM_CLOSE:
        if (st) { st->result.ok = false; st->done = true; DestroyWindow(hwnd); }
        return 0;
    case WM_DESTROY:
        if (st) st->done = true;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void EnsurePickClasses(HINSTANCE hi) {
    static bool done = false;
    if (done) return;
    if (!g_cpBlankIcon) g_cpBlankIcon = MakeBlankIconCP(16);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.hIcon = g_cpBlankIcon;
    wc.hIconSm = g_cpBlankIcon;

    wc.lpfnWndProc = PickProc;
    wc.lpszClassName = kPickClass;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = EyeProc;
    wc.lpszClassName = kEyeClass;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    RegisterClassExW(&wc);
    done = true;
}

} // namespace

bool ColorPicker::Eyedropper(HWND owner, COLORREF& outColor) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsurePickClasses(hi);

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    EyeState st;
    {
        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
        HGDIOBJ old = SelectObject(hdcMem, hbm);
        BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);
        SelectObject(hdcMem, old);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        st.frozen = std::make_unique<Bitmap>(hbm, nullptr);
        DeleteObject(hbm);
    }

    if (owner) ShowWindow(owner, SW_HIDE);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                kEyeClass, L"",
                                WS_POPUP | WS_VISIBLE,
                                x, y, w, h,
                                nullptr, nullptr, hi, &st);
    if (hwnd) {
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        SetCapture(hwnd);
        MSG msg;
        while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        ReleaseCapture();
    }
    if (owner) {
        ShowWindow(owner, SW_SHOW);
        SetForegroundWindow(owner);
    }
    if (st.ok) outColor = st.picked;
    return st.ok;
}

ColorResult ColorPicker::Show(HWND owner, COLORREF initial, BYTE initialAlpha, bool showQuickWhite) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsurePickClasses(hi);

    PickState st;
    util::RGBtoHSL(initial, st.h, st.s, st.l);
    st.r = GetRValue(initial);
    st.g = GetGValue(initial);
    st.b = GetBValue(initial);
    st.alpha = initialAlpha;
    st.hex = util::ToHex(initial);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    RECT wr = { 0, 0, kDlgW, kDlgH };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&wr, style, FALSE, WS_EX_TOPMOST);
    int outerW = wr.right - wr.left;
    int outerH = wr.bottom - wr.top;
    int x = (sw - outerW) / 2;
    int y = (sh - outerH) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST,
                                kPickClass, L"选择颜色",
                                style,
                                x, y, outerW, outerH,
                                owner, nullptr, hi, &st);
    if (!hwnd) return st.result;
    SetWindowTextW(hwnd, L"选择颜色");
    if (g_cpBlankIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_cpBlankIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_cpBlankIcon));
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(g_cpBlankIcon));
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(g_cpBlankIcon));
    }

    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    RECT alphaSr = SliderRect(hwnd, 4);
    int hexW = alphaSr.right - alphaSr.left;
    HWND hex = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st.hex.c_str(),
                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                               alphaSr.left, 462, hexW, 30, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HEX)), hi, nullptr);
    HWND eye = CreateWindowW(L"BUTTON", L"吸管（全屏取色）",
                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             alphaSr.left, 502, hexW, 34, hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EYEDROP)), hi, nullptr);

    int btnY = kDlgH - 48;
    int btnW = 90, btnH = 32;
    int okX = kDlgW - 230;
    HWND whiteBtn = nullptr;
    if (showQuickWhite) {
        whiteBtn = CreateWindowW(L"BUTTON", L"白色",
                                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                 okX - 100, btnY, btnW, btnH, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_WHITE)), hi, nullptr);
    }
    HWND ok = CreateWindowW(L"BUTTON", L"确定",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            okX, btnY, btnW, btnH, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OK)), hi, nullptr);
    HWND cancel = CreateWindowW(L"BUTTON", L"取消",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                kDlgW - 120, btnY, btnW, btnH, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANCEL)), hi, nullptr);

    for (int i = 0; i < 5; ++i) {
        RECT sr = SliderRect(hwnd, i);
        int vals[5] = { static_cast<int>(st.l * 255), st.r, st.g, st.b, st.alpha };
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                        std::to_wstring(vals[i]).c_str(),
                        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                        sr.right + 8, sr.top - 2, 56, 28,
                        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDIT_BASE + i)), hi, nullptr);
    }

    if (font) {
        SendMessageW(hex, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(eye, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (whiteBtn) SendMessageW(whiteBtn, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (int i = 0; i < 5; ++i)
            SendMessageW(GetDlgItem(hwnd, IDC_EDIT_BASE + i), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
    if (font) DeleteObject(font);
    return st.result;
}
