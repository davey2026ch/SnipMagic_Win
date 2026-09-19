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
    return GetExeDir() + L"\\截图工具.ini";
}

inline std::wstring Format(const wchar_t* fmt, ...) {
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);
    return buf;
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

inline int GetDpiForWindowSafe(HWND hwnd) {
    UINT dpi = 96;
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

inline std::wstring OpenSaveDialog(HWND owner, bool save, const wchar_t* filter, const wchar_t* defExt,
                                   const wchar_t* defName, const wchar_t* title) {
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
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save) {
        if (!GetSaveFileNameW(&ofn)) return L"";
    } else {
        if (!GetOpenFileNameW(&ofn)) return L"";
    }
    return file;
}

inline std::wstring BrowseFolder(HWND owner, const wchar_t* title) {
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
