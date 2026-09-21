#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <functional>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "imm32.lib")

namespace util {

inline std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(0, pos);
}

inline std::wstring GetIniPath() {
    // 注意：util.h 不引入 version.h（避免循环），新 ini 名在此保持字面量，
    // 与 version.h 的 APP_INI_NAME 保持一致；改名时两处需同步
    std::wstring newPath = GetExeDir() + L"\\SnipMagic.ini";
    // 旧版配置迁移：截图工具.ini → SnipMagic.ini（保留 token 等用户配置）
    if (GetFileAttributesW(newPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring legacy = GetExeDir() + L"\\截图工具.ini";
        if (GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES) {
            MoveFileW(legacy.c_str(), newPath.c_str());
        }
    }
    return newPath;
}

inline std::wstring Format(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    return buf;
}

// 去掉首尾空白（空格/Tab/换行/全角空格/零宽）
inline std::wstring TrimToken(const std::wstring& s) {
    if (s.empty()) return {};
    auto isBlank = [](wchar_t c) -> bool {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' ||
               c == L'\u00A0' || c == L'\u3000' || c == L'\u200B' || c == L'\uFEFF';
    };
    size_t b = 0, e = s.size();
    while (b < e && isBlank(s[b])) ++b;
    while (e > b && isBlank(s[e - 1])) --e;
    return s.substr(b, e - b);
}

inline std::wstring ToHex(COLORREF c) {
    wchar_t buf[16];
    swprintf_s(buf, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

inline COLORREF ParseHex(const std::wstring& s) {
    std::wstring t;
    for (wchar_t ch : s) {
        if (iswxdigit(static_cast<wint_t>(ch))) t.push_back(ch);
    }
    if (t.size() >= 6) {
        unsigned r = 0, g = 0, b = 0;
        swscanf_s(t.c_str(), L"%02x%02x%02x", &r, &g, &b);
        return RGB(r & 0xFF, g & 0xFF, b & 0xFF);
    }
    return RGB(0, 0, 0);
}

inline Gdiplus::Color ToGpColor(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

inline COLORREF FromGpColor(const Gdiplus::Color& c) {
    return RGB(c.GetR(), c.GetG(), c.GetB());
}

inline void InflateRectF(Gdiplus::RectF& r, float dx, float dy) {
    r.X -= dx; r.Y -= dy; r.Width += dx * 2; r.Height += dy * 2;
}

inline bool PtInRectF(const Gdiplus::RectF& r, float x, float y) {
    return x >= r.X && y >= r.Y && x <= r.X + r.Width && y <= r.Y + r.Height;
}

inline Gdiplus::RectF NormalizeRectF(float x1, float y1, float x2, float y2) {
    float l = (std::min)(x1, x2), t = (std::min)(y1, y2);
    float r = (std::max)(x1, x2), b = (std::max)(y1, y2);
    return Gdiplus::RectF(l, t, r - l, b - t);
}

// Distance from point to segment
inline float DistToSegment(float px, float py, float x1, float y1, float x2, float y2) {
    float dx = x2 - x1, dy = y2 - y1;
    float len2 = dx * dx + dy * dy;
    if (len2 < 1e-6f) {
        float ax = px - x1, ay = py - y1;
        return std::sqrt(ax * ax + ay * ay);
    }
    float t = ((px - x1) * dx + (py - y1) * dy) / len2;
    t = (std::max)(0.0f, (std::min)(1.0f, t));
    float cx = x1 + t * dx, cy = y1 + t * dy;
    float ax = px - cx, ay = py - cy;
    return std::sqrt(ax * ax + ay * ay);
}

// HSL <-> RGB for color wheel
inline void RGBtoHSL(COLORREF c, float& h, float& s, float& l) {
    float r = GetRValue(c) / 255.0f;
    float g = GetGValue(c) / 255.0f;
    float b = GetBValue(c) / 255.0f;
    float mx = (std::max)({r, g, b});
    float mn = (std::min)({r, g, b});
    l = (mx + mn) * 0.5f;
    if (mx == mn) { h = s = 0; return; }
    float d = mx - mn;
    s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h *= 60.0f;
}

inline float Hue2RGB(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

inline COLORREF HSLtoRGB(float h, float s, float l) {
    if (s <= 0.0001f) {
        BYTE v = static_cast<BYTE>(l * 255);
        return RGB(v, v, v);
    }
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    float p = 2 * l - q;
    float hn = h / 360.0f;
    float r = Hue2RGB(p, q, hn + 1.0f / 3);
    float g = Hue2RGB(p, q, hn);
    float b = Hue2RGB(p, q, hn - 1.0f / 3);
    return RGB(static_cast<BYTE>(r * 255), static_cast<BYTE>(g * 255), static_cast<BYTE>(b * 255));
}

// 弹窗定位：以 owner 所在显示器的工作区居中（多屏时弹窗跟主窗口走，不固定在主屏）
// 返回窗口左上角坐标（屏幕坐标）
inline POINT CenterOnMonitorOf(HWND owner, int outerW, int outerH) {
    RECT wa = {};
    HMONITOR mon = owner ? MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST) : nullptr;
    MONITORINFO mi = { sizeof(mi) };
    if (mon && GetMonitorInfoW(mon, &mi)) {
        wa = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    }
    POINT pt;
    pt.x = wa.left + ((wa.right - wa.left) - outerW) / 2;
    pt.y = wa.top + ((wa.bottom - wa.top) - outerH) / 2;
    if (pt.x < wa.left) pt.x = wa.left;
    if (pt.y < wa.top) pt.y = wa.top;
    return pt;
}

inline int GetDpiForWindowSafe(HWND hwnd) {    UINT dpi = 96;
    // GetDpiForWindow is Win10 1607+
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT(WINAPI* Fn)(HWND);
        auto fn = reinterpret_cast<Fn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (fn) {
            dpi = fn(hwnd);
            if (dpi > 0) return static_cast<int>(dpi);
        }
    }
    HDC hdc = GetDC(hwnd);
    if (hdc) {
        dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(hwnd, hdc);
    }
    return dpi > 0 ? static_cast<int>(dpi) : 96;
}

inline int Scale(int v, int dpi) {
    return MulDiv(v, dpi, 96);
}

// Create HBITMAP from GDI+ Bitmap
inline HBITMAP BitmapToHBITMAP(Gdiplus::Bitmap* bmp) {
    if (!bmp) return nullptr;
    HBITMAP hbm = nullptr;
    bmp->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbm);
    return hbm;
}

inline std::unique_ptr<Gdiplus::Bitmap> HBITMAPToBitmap(HBITMAP hbm) {
    if (!hbm) return nullptr;
    return std::make_unique<Gdiplus::Bitmap>(hbm, nullptr);
}

// Pixelate a GDI+ bitmap region into a new bitmap
inline std::unique_ptr<Gdiplus::Bitmap> PixelateBitmap(Gdiplus::Bitmap* src, int x, int y, int w, int h, int block) {
    if (!src || w <= 0 || h <= 0 || block < 1) return nullptr;
    x = (std::max)(0, x);
    y = (std::max)(0, y);
    w = (std::min)(w, static_cast<int>(src->GetWidth()) - x);
    h = (std::min)(h, static_cast<int>(src->GetHeight()) - y);
    if (w <= 0 || h <= 0) return nullptr;

    auto out = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppARGB);
    Gdiplus::Graphics g(out.get());
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);

    int sw = (std::max)(1, w / block);
    int sh = (std::max)(1, h / block);
    // Downscale then upscale for blocky mosaic
    Gdiplus::Bitmap tiny(sw, sh, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics gt(&tiny);
        gt.SetInterpolationMode(Gdiplus::InterpolationModeLowQuality);
        gt.DrawImage(src, Gdiplus::Rect(0, 0, sw, sh), x, y, w, h, Gdiplus::UnitPixel);
    }
    g.DrawImage(&tiny, Gdiplus::Rect(0, 0, w, h), 0, 0, sw, sh, Gdiplus::UnitPixel);
    return out;
}

// Crop region from bitmap
inline std::unique_ptr<Gdiplus::Bitmap> CropBitmap(Gdiplus::Bitmap* src, int x, int y, int w, int h) {
    if (!src || w <= 0 || h <= 0) return nullptr;
    x = (std::max)(0, x);
    y = (std::max)(0, y);
    int maxW = static_cast<int>(src->GetWidth()) - x;
    int maxH = static_cast<int>(src->GetHeight()) - y;
    if (maxW <= 0 || maxH <= 0) return nullptr;
    w = (std::min)(w, maxW);
    h = (std::min)(h, maxH);
    auto out = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppARGB);
    Gdiplus::Graphics g(out.get());
    g.DrawImage(src, Gdiplus::Rect(0, 0, w, h), x, y, w, h, Gdiplus::UnitPixel);
    return out;
}

// Add a 1px border along the image edge (inside, size unchanged).
// Used for screenshots copied to external apps so they stay visible on white pages.
inline std::unique_ptr<Gdiplus::Bitmap> AddInnerBorder(Gdiplus::Bitmap* src,
                                                       Gdiplus::Color color) {
    if (!src) return nullptr;
    int w = static_cast<int>(src->GetWidth());
    int h = static_cast<int>(src->GetHeight());
    if (w <= 2 || h <= 2) return nullptr;
    auto out = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppARGB);
    Gdiplus::Graphics g(out.get());
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    g.DrawImage(src, Gdiplus::Rect(0, 0, w, h), 0, 0, w, h, Gdiplus::UnitPixel);
    // Four explicit 1px edge fills — deterministic, unlike a 1px pen stroke
    // which can fall outside the bitmap due to pixel alignment.
    Gdiplus::SolidBrush b(color);
    g.FillRectangle(&b, 0, 0, w, 1);          // top
    g.FillRectangle(&b, 0, h - 1, w, 1);      // bottom
    g.FillRectangle(&b, 0, 0, 1, h);          // left
    g.FillRectangle(&b, w - 1, 0, 1, h);      // right
    return out;
}

// PNG 编码到内存（供剪贴板 PNG 格式等使用）
inline bool BitmapToPngMem(Gdiplus::Bitmap* bmp, std::vector<BYTE>& out) {
    out.clear();
    if (!bmp) return false;
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, info);
    CLSID clsid{};
    bool found = false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            clsid = info[i].Clsid;
            found = true;
            break;
        }
    }
    if (!found) return false;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) return false;
    bool ok = false;
    LARGE_INTEGER zero = {};
    if (bmp->Save(stream, &clsid, nullptr) == Gdiplus::Ok) {
        STATSTG stg{};
        if (stream->Seek(zero, STREAM_SEEK_SET, nullptr) == S_OK &&
            stream->Stat(&stg, STATFLAG_NONAME) == S_OK && stg.cbSize.QuadPart > 0) {
            out.resize(static_cast<size_t>(stg.cbSize.QuadPart));
            ULONG read = 0;
            if (stream->Read(out.data(), static_cast<ULONG>(out.size()), &read) == S_OK &&
                read == out.size()) {
                ok = true;
            }
        }
    }
    stream->Release();
    if (!ok) out.clear();
    return ok;
}

// Put GDI+ bitmap onto Windows clipboard as CF_DIB
inline bool BitmapToClipboard(Gdiplus::Bitmap* bmp) {
    if (!bmp) return false;
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();

    UINT w = bmp->GetWidth();
    UINT h = bmp->GetHeight();
    Gdiplus::BitmapData data;
    Gdiplus::Rect rc(0, 0, static_cast<INT>(w), static_cast<INT>(h));
    if (bmp->LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        CloseClipboard();
        return false;
    }

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = static_cast<LONG>(w);
    bi.biHeight = -static_cast<LONG>(h); // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = w * h * 4;

    SIZE_T total = sizeof(BITMAPINFOHEADER) + static_cast<SIZE_T>(w) * h * 4;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, total);
    if (!hMem) {
        bmp->UnlockBits(&data);
        CloseClipboard();
        return false;
    }
    unsigned char* dst = static_cast<unsigned char*>(GlobalLock(hMem));
    memcpy(dst, &bi, sizeof(BITMAPINFOHEADER));
    unsigned char* pixels = dst + sizeof(BITMAPINFOHEADER);
    const unsigned char* src = static_cast<const unsigned char*>(data.Scan0);
    int stride = data.Stride;
    for (UINT row = 0; row < h; ++row) {
        memcpy(pixels + row * w * 4, src + row * stride, w * 4);
        // Convert ARGB (premultiplied GDI+) to BGRX for clipboard
        unsigned char* line = pixels + row * w * 4;
        for (UINT col = 0; col < w; ++col) {
            unsigned char a = line[col * 4 + 3];
            if (a == 0) {
                line[col * 4 + 0] = 255; // B
                line[col * 4 + 1] = 255; // G
                line[col * 4 + 2] = 255; // R
                line[col * 4 + 3] = 0;
            } else if (a < 255) {
                // un-premultiply roughly then drop alpha
                line[col * 4 + 0] = static_cast<unsigned char>((std::min)(255, line[col * 4 + 0] * 255 / a));
                line[col * 4 + 1] = static_cast<unsigned char>((std::min)(255, line[col * 4 + 1] * 255 / a));
                line[col * 4 + 2] = static_cast<unsigned char>((std::min)(255, line[col * 4 + 2] * 255 / a));
                line[col * 4 + 3] = 0;
            } else {
                line[col * 4 + 3] = 0;
            }
            // GDI+ is BGRA already in memory on little-endian for 32bpp
        }
    }
    bmp->UnlockBits(&data);
    GlobalUnlock(hMem);
    SetClipboardData(CF_DIB, hMem);

    // 额外放一份 PNG（注册格式 "PNG"）：CF_DIB 不带透明度（透明处已填白），
    // 支持透明底的应用（微信 / Word / 支持贴透明图的新版应用）会优先读 PNG 格式，
    // 拿到的就是带 Alpha 通道的原图。失败不影响 CF_DIB。
    if (UINT pngFmt = RegisterClipboardFormatW(L"PNG")) {
        std::vector<BYTE> png;
        if (BitmapToPngMem(bmp, png) && !png.empty()) {
            if (HGLOBAL hPng = GlobalAlloc(GMEM_MOVEABLE, png.size())) {
                if (void* p = GlobalLock(hPng)) {
                    memcpy(p, png.data(), png.size());
                    GlobalUnlock(hPng);
                    if (SetClipboardData(pngFmt, hPng) == nullptr) {
                        GlobalFree(hPng); // 系统未接管，自己释放
                    }
                } else {
                    GlobalFree(hPng);
                }
            }
        }
    }
    CloseClipboard();
    return true;
}

// Load bitmap from clipboard if available
inline std::unique_ptr<Gdiplus::Bitmap> BitmapFromClipboard() {
    if (!IsClipboardFormatAvailable(CF_DIB) && !IsClipboardFormatAvailable(CF_DIBV5)) {
        return nullptr;
    }
    if (!OpenClipboard(nullptr)) return nullptr;
    HANDLE h = GetClipboardData(CF_DIB);
    if (!h) {
        h = GetClipboardData(CF_DIBV5);
    }
    if (!h) {
        CloseClipboard();
        return nullptr;
    }
    const BITMAPINFOHEADER* bih = static_cast<const BITMAPINFOHEADER*>(GlobalLock(h));
    if (!bih) {
        CloseClipboard();
        return nullptr;
    }
    int w = bih->biWidth;
    int hgt = bih->biHeight;
    bool topDown = hgt < 0;
    if (topDown) hgt = -hgt;
    int bpp = bih->biBitCount;
    auto out = std::make_unique<Gdiplus::Bitmap>(w, hgt, PixelFormat32bppARGB);
    Gdiplus::BitmapData data;
    Gdiplus::Rect rc(0, 0, w, hgt);
    if (out->LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
        const unsigned char* bits = reinterpret_cast<const unsigned char*>(bih + 1);
        // skip color table for paletted
        if (bpp <= 8) {
            int colors = bih->biClrUsed ? bih->biClrUsed : (1 << bpp);
            bits += colors * sizeof(RGBQUAD);
        }
        int srcStride = ((w * bpp + 31) / 32) * 4;
        unsigned char* dst = static_cast<unsigned char*>(data.Scan0);
        for (int row = 0; row < hgt; ++row) {
            const unsigned char* srcRow = bits + (topDown ? row : (hgt - 1 - row)) * srcStride;
            unsigned char* dstRow = dst + row * data.Stride;
            for (int col = 0; col < w; ++col) {
                if (bpp == 32) {
                    dstRow[col * 4 + 0] = srcRow[col * 4 + 0];
                    dstRow[col * 4 + 1] = srcRow[col * 4 + 1];
                    dstRow[col * 4 + 2] = srcRow[col * 4 + 2];
                    dstRow[col * 4 + 3] = 255;
                } else if (bpp == 24) {
                    dstRow[col * 4 + 0] = srcRow[col * 3 + 0];
                    dstRow[col * 4 + 1] = srcRow[col * 3 + 1];
                    dstRow[col * 4 + 2] = srcRow[col * 3 + 2];
                    dstRow[col * 4 + 3] = 255;
                } else if (bpp == 16) {
                    WORD v = *reinterpret_cast<const WORD*>(srcRow + col * 2);
                    dstRow[col * 4 + 0] = static_cast<unsigned char>(((v) & 0x1F) * 255 / 31);
                    dstRow[col * 4 + 1] = static_cast<unsigned char>(((v >> 5) & 0x1F) * 255 / 31);
                    dstRow[col * 4 + 2] = static_cast<unsigned char>(((v >> 10) & 0x1F) * 255 / 31);
                    dstRow[col * 4 + 3] = 255;
                } else {
                    dstRow[col * 4 + 0] = 255;
                    dstRow[col * 4 + 1] = 255;
                    dstRow[col * 4 + 2] = 255;
                    dstRow[col * 4 + 3] = 255;
                }
            }
        }
        out->UnlockBits(&data);
    }
    GlobalUnlock(h);
    CloseClipboard();
    return out;
}

// 用户「下载」文件夹（导出/保存对话框的默认起始位置）
inline std::wstring DownloadsDir() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) return L"";
    std::wstring dir = p;
    CoTaskMemFree(p);
    return dir;
}

inline std::wstring OpenSaveDialog(HWND owner, bool save, const wchar_t* filter, const wchar_t* defExt,
                                   const wchar_t* defName, const wchar_t* title,
                                   const wchar_t* initialDir = nullptr) {
    wchar_t file[MAX_PATH] = {};
    if (defName) wcsncpy_s(file, defName, _TRUNCATE);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defExt;
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = initialDir;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save) {
        if (!GetSaveFileNameW(&ofn)) return L"";
    } else {
        if (!GetOpenFileNameW(&ofn)) return L"";
    }
    return file;
}

inline std::wstring BrowseFolder(HWND owner, const wchar_t* title,
                                 const wchar_t* initialDir = nullptr) {
    // 优先用现代文件夹对话框（支持默认起始目录）
    IFileDialog* fd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&fd)))) {
        std::wstring out;
        DWORD opts = 0;
        fd->GetOptions(&opts);
        fd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (title) fd->SetTitle(title);
        if (initialDir && *initialDir) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initialDir, nullptr, IID_PPV_ARGS(&item)))) {
                fd->SetFolder(item);
                item->Release();
            }
        }
        if (SUCCEEDED(fd->Show(owner))) {
            IShellItem* res = nullptr;
            if (SUCCEEDED(fd->GetResult(&res))) {
                PWSTR p = nullptr;
                if (SUCCEEDED(res->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                    out = p;
                    CoTaskMemFree(p);
                }
                res->Release();
            }
        }
        fd->Release();
        return out;
    }
    // 兜底：旧式对话框
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return ok ? path : L"";
}

inline bool SaveBitmapToFile(Gdiplus::Bitmap* bmp, const std::wstring& path, bool jpg = false) {
    if (!bmp || path.empty()) return false;
    CLSID clsid;
    if (jpg) {
        // image/jpeg
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (!size) return false;
        std::vector<BYTE> buf(size);
        auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
        Gdiplus::GetImageEncoders(num, size, info);
        for (UINT i = 0; i < num; ++i) {
            if (wcscmp(info[i].MimeType, L"image/jpeg") == 0) {
                clsid = info[i].Clsid;
                Gdiplus::EncoderParameters params;
                params.Count = 1;
                params.Parameter[0].Guid = Gdiplus::EncoderQuality;
                params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
                params.Parameter[0].NumberOfValues = 1;
                ULONG quality = 92;
                params.Parameter[0].Value = &quality;
                return bmp->Save(path.c_str(), &clsid, &params) == Gdiplus::Ok;
            }
        }
        return false;
    }
    // PNG default
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) return false;
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            clsid = info[i].Clsid;
            return bmp->Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
        }
    }
    return false;
}

// Apply Shift constraint for shape drawing
inline void ConstrainSquare(float x0, float y0, float& x1, float& y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float adx = std::fabs(dx), ady = std::fabs(dy);
    float m = (std::max)(adx, ady);
    x1 = x0 + (dx < 0 ? -m : m);
    y1 = y0 + (dy < 0 ? -m : m);
}

// Constrain to 0/45/90 degrees for line/arrow
inline void ConstrainAngle(float x0, float y0, float& x1, float& y1) {
    float dx = x1 - x0, dy = y1 - y0;
    float ang = std::atan2(dy, dx);
    const float step = 3.14159265358979f / 4.0f; // 45°
    float snapped = std::round(ang / step) * step;
    float len = std::sqrt(dx * dx + dy * dy);
    x1 = x0 + std::cos(snapped) * len;
    y1 = y0 + std::sin(snapped) * len;
}

inline bool IsShiftDown() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }
inline bool IsCtrlDown()  { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }

inline COLORREF SampleScreenColor(POINT pt) {
    HDC hdc = GetDC(nullptr);
    COLORREF c = GetPixel(hdc, pt.x, pt.y);
    ReleaseDC(nullptr, hdc);
    return c == CLR_INVALID ? RGB(0, 0, 0) : c;
}

} // namespace util

// Bring commonly used helpers into global namespace for call sites
using util::ToGpColor;
using util::FromGpColor;
using util::ToHex;
using util::ParseHex;
using util::NormalizeRectF;
using util::PtInRectF;
using util::DistToSegment;
using util::IsShiftDown;
using util::IsCtrlDown;
using util::ConstrainSquare;
using util::ConstrainAngle;
using util::Format;
