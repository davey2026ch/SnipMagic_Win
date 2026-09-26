// -*- coding: utf-8 -*-
// 剪贴板 CF_HTML + 设置持久化 测试
// 覆盖：
//  A. AppSettings::Save/Load 对 BorderCopyToExternal 的持久化（默认开、可写 0/1）
//  B. BitmapToClipboard 产出 CF_DIB + "PNG" + "HTML Format" 三种格式
//  C. CF_HTML 头部 101 字节、四个偏移正确、fragment 含 <img src="file:///...">
//  D. CF_HTML 引用的临时 PNG 文件存在，且字节与 "PNG" 格式完全一致
//  E. BitmapHasAlpha 对不透明图返回 false、含透明像素返回 true
#include "util.h"
#include "settings.h"
#include "document.h"
#include <cstdio>
#include <fstream>

using namespace Gdiplus;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS: %s\n", msg); } \
    else { printf("  FAIL: %s\n", msg); ++g_fail; } } while (0)

static std::string Slurp(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    return f ? std::string((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>()) : std::string();
}

// base64 解码（测试用）：失败返回 false
static bool B64Decode(const std::string& in, std::string& out) {
    auto hx = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    out.clear();
    int val = 0, bits = 0;
    for (char c : in) {
        if (c == '=') break;
        int d = hx(c);
        if (d < 0) return false;
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((val >> bits) & 0xFF);
        }
    }
    return true;
}

// 从 CF_HTML 数据取 fragment 的 src="..." 里的 URI 并百分号解码为本地路径
static bool ExtractTempPngPath(const std::string& html, std::wstring& out) {
    size_t p = html.find("this.src='file:///");
    if (p == std::string::npos) return false;
    p += 10; // 跳过 this.src=
    size_t e = html.find('\'', p);
    if (e == std::string::npos) return false;
    std::string uri = html.substr(p, e - p);
    if (uri.rfind("file:///", 0) != 0) return false;
    std::string path;
    for (size_t i = 8; i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            auto hx = [&](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            int hi = hx(uri[i + 1]), lo = hx(uri[i + 2]);
            if (hi < 0 || lo < 0) return false;
            path += static_cast<char>((hi << 4) | lo);
            i += 2;
        } else {
            path += uri[i];
        }
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.size(), nullptr, 0);
    out.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.size(), &out[0], wlen);
    return true;
}

static void TestSettings() {
    printf("[A] 设置持久化\n");
    AppSettings& s = Settings();
    s.borderCopyToExternal = true;
    s.Save();
    CHECK(Slurp(util::GetIniPath()).find("BorderCopyToExternal=1") != std::string::npos,
          "Save 后 ini 含 BorderCopyToExternal=1");
    s.borderCopyToExternal = false;
    s.Save();
    s.Load();
    CHECK(s.borderCopyToExternal == false, "写 0 后 Load 读回 false");
    CHECK(Slurp(util::GetIniPath()).find("BorderCopyToExternal=0") != std::string::npos,
          "ini 中值为 0");
    s.borderCopyToExternal = true;
    s.Save();
    s.Load();
    CHECK(s.borderCopyToExternal == true, "写 1 后 Load 读回 true");
    // 删掉该键再 Load：应回落默认值 true（IniInt 的 def 参数）
    WritePrivateProfileStringW(L"Settings", L"BorderCopyToExternal", nullptr,
                               util::GetIniPath().c_str());
    s.Load();
    CHECK(s.borderCopyToExternal == true, "键缺失时 Load 回落默认 true");
}

static std::unique_ptr<Bitmap> MakeOpaqueBitmap() {
    auto bmp = std::make_unique<Bitmap>(320, 200, PixelFormat32bppARGB);
    BitmapData d;
    Rect rc(0, 0, 320, 200);
    bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &d);
    for (UINT y = 0; y < 200; ++y) {
        BYTE* row = (BYTE*)d.Scan0 + y * d.Stride;
        for (UINT x = 0; x < 320; ++x) {
            row[x * 4 + 0] = (BYTE)(x & 0xFF);
            row[x * 4 + 1] = (BYTE)(y & 0xFF);
            row[x * 4 + 2] = 0x80;
            row[x * 4 + 3] = 255;
        }
    }
    bmp->UnlockBits(&d);
    return bmp;
}

static std::unique_ptr<Bitmap> MakeAlphaBitmap() {
    auto bmp = MakeOpaqueBitmap();
    BitmapData d;
    Rect rc(0, 0, 320, 200);
    bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &d);
    BYTE* row = (BYTE*)d.Scan0;
    row[3] = 128; // (0,0) 半透明
    bmp->UnlockBits(&d);
    return bmp;
}

static std::string GetClipboardDataBytes(UINT fmt) {
    if (!IsClipboardFormatAvailable(fmt)) return {};
    HANDLE h = GetClipboardData(fmt);
    if (!h) return {};
    SIZE_T n = GlobalSize(h);
    const char* p = (const char*)GlobalLock(h);
    if (!p) return {};
    std::string out(p, n);
    GlobalUnlock(h);
    return out;
}

static std::wstring FormatName(UINT fmt) {
    wchar_t name[128] = {};
    if (GetClipboardFormatNameW(fmt, name, 128)) return name;
    static const wchar_t* stds[] = {L"", L"CF_TEXT", L"CF_BITMAP", L"CF_METAFILEPICT",
        L"CF_SYLK", L"CF_DIF", L"CF_TIFF", L"CF_OEMTEXT", L"CF_DIB", L"CF_PALETTE",
        L"CF_PENDATA", L"CF_RIFF", L"CF_WAVE", L"CF_UNICODETEXT", L"CF_ENHMETAFILE",
        L"CF_HDROP", L"CF_LOCALE", L"CF_DIBV5"};
    if (fmt < 18) return stds[fmt];
    return L"";
}

static void TestClipboard() {
    printf("[B] 剪贴板格式与 CF_HTML 结构\n");
    auto bmp = MakeOpaqueBitmap();
    CHECK(util::BitmapToClipboard(bmp.get()) == true, "BitmapToClipboard 返回 true");

    CHECK(OpenClipboard(nullptr) != 0, "OpenClipboard");
    std::vector<std::wstring> names;
    UINT f = 0;
    while ((f = EnumClipboardFormats(f)) != 0) names.push_back(FormatName(f));
    bool hasDib = false, hasPng = false, hasHtml = false;
    UINT pngFmt = RegisterClipboardFormatW(L"PNG");
    UINT htmlFmt = RegisterClipboardFormatW(L"HTML Format");
    for (auto& n : names) {
        if (n == L"CF_DIB") hasDib = true;
        if (n == L"PNG") hasPng = true;
        if (n == L"HTML Format") hasHtml = true;
    }
    printf("  formats: ");
    for (auto& n : names) { printf("%ls ", n.c_str()); }
    printf("\n");
    CHECK(hasDib, "含 CF_DIB");
    CHECK(hasPng, "含 PNG 格式");
    CHECK(hasHtml, "含 HTML Format 格式");

    if (hasHtml) {
        std::string html = GetClipboardDataBytes(htmlFmt);
        // GlobalSize 可能大于实际长度，按 \0 截断
        size_t z = html.find('\0');
        if (z != std::string::npos) html.resize(z);
        CHECK(html.rfind("Version:0.9\r\n", 0) == 0, "CF_HTML 头部 Version:0.9");
        unsigned startHtml = 0, endHtml = 0, startFrag = 0, endFrag = 0;
        sscanf(html.c_str(),
               "Version:0.9\r\nStartHTML:%10u\r\nEndHTML:%10u\r\n"
               "StartFragment:%10u\r\nEndFragment:%10u\r\n",
               &startHtml, &endHtml, &startFrag, &endFrag);
        // 头部长度 = 版本行 13 + (字段名长度+10数字+2)×4，字段名长度不同不能写死
        const unsigned kHeaderLen = 13 + 22 + 20 + 26 + 24;
        CHECK(startHtml == kHeaderLen, "StartHTML == 实际头部长度（105）");
        CHECK(html.size() >= startHtml, "数据至少包含完整头部");
        CHECK(endHtml == html.size(), "EndHTML == 数据总长");
        CHECK(startFrag > startHtml && endFrag <= endHtml, "fragment 偏移在范围内");
        std::string frag = html.substr(startFrag, endFrag - startFrag);
        CHECK(frag.find("<img src=\"data:image/png;base64,") == 0,
              "fragment 以 <img src=\"data:image/png;base64, 开头（Word 只认内嵌）");
        CHECK(frag.find("onerror=\"this.onerror=null;this.src='file:///") != std::string::npos,
              "带 file:/// 的 onerror 兜底");
        CHECK(frag.find("width=\"320\" height=\"200\"") != std::string::npos,
              "img 带正确 width/height");
        CHECK(html.compare(endFrag, 20, "\r\n<!--EndFragment-->") == 0,
              "EndFragment 紧跟结束标记");
        // base64 载荷解码后应与剪贴板 PNG 格式字节一致
        size_t b64start = frag.find("base64,") + 7;
        size_t b64end = frag.find('"', b64start);
        std::string decoded;
        bool decOk = B64Decode(frag.substr(b64start, b64end - b64start), decoded);
        CHECK(decOk, "base64 解码成功");
        if (hasPng && decOk) {
            std::string clipPng = GetClipboardDataBytes(pngFmt);
            CHECK(decoded == clipPng.substr(0, decoded.size()),
                  "base64 载荷与剪贴板 PNG 格式字节一致");
        }
        std::wstring pngPath;
        if (ExtractTempPngPath(html, pngPath)) {
            std::string fileBytes = Slurp(pngPath);
            CHECK(!fileBytes.empty(), "兜底引用的临时 PNG 文件存在且非空");
            if (hasPng && decOk) {
                CHECK(fileBytes.size() == decoded.size(),
                      "临时文件与 base64 载荷大小一致");
            }
        } else {
            CHECK(false, "解析 onerror 里的 file:/// 得到本地路径");
        }
    }
    CloseClipboard();
}

// 诊断：逐步复刻 SetClipboardHtmlFormat 的关键步骤，打印失败点
static void DebugHtmlSteps() {
    printf("[D] CF_HTML 步骤诊断\n");
    wchar_t temp[MAX_PATH] = {};
    DWORD n = GetTempPathW(MAX_PATH, temp);
    printf("  GetTempPathW ret=%u path=%ls (err=%lu)\n", n, temp, GetLastError());
    std::wstring dir = std::wstring(temp) + L"SnipMagicClip";
    BOOL cd = CreateDirectoryW(dir.c_str(), nullptr);
    printf("  CreateDirectoryW ret=%d (err=%lu, 存在时 err=183 属正常)\n", cd, GetLastError());

    UINT htmlFmt = RegisterClipboardFormatW(L"HTML Format");
    printf("  RegisterClipboardFormatW(HTML Format)=%u\n", htmlFmt);

    std::vector<BYTE> png = {0x89, 0x50, 0x4E, 0x47}; // 仅用于探针
    static unsigned s_seq = 0;
    std::wstring file = dir + util::Format(L"\\clip_probe_%u_%u.png", GetTickCount(), ++s_seq);
    HANDLE hf = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    printf("  CreateFileW %ls -> %p (err=%lu)\n", file.c_str(), (void*)hf, GetLastError());
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hf, png.data(), (DWORD)png.size(), &written, nullptr);
        printf("  WriteFile written=%lu\n", written);
        CloseHandle(hf);
        DeleteFileW(file.c_str());
    }

    size_t kHeaderLen = 105;
    char header[128] = {};
    _snprintf_s(header, _TRUNCATE,
                "Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\n"
                "StartFragment:%010zu\r\nEndFragment:%010zu\r\n",
                kHeaderLen, kHeaderLen + 50, kHeaderLen + 20, kHeaderLen + 40);
    printf("  header len=%zu (期望 105) 内容前 40 字节: %.40s\n", strlen(header), header);
}

static void TestAlpha() {
    printf("[E] BitmapHasAlpha\n");
    auto opaque = MakeOpaqueBitmap();
    CHECK(util::BitmapHasAlpha(opaque.get()) == false, "不透明图返回 false");
    auto alpha = MakeAlphaBitmap();
    CHECK(util::BitmapHasAlpha(alpha.get()) == true, "含透明像素图返回 true");
}

// 读 CF_DIB 的 (x,y) 像素，返回 0xAARRGGBB；失败返回 0xFFFFFFFF 标记
static unsigned GetDibPixel(UINT x, UINT y) {
    if (!OpenClipboard(nullptr)) return 0xFFFFFFFFu;
    std::string dib = GetClipboardDataBytes(CF_DIB);
    CloseClipboard();
    if (dib.size() < 40) return 0xFFFFFFFFu;
    BITMAPINFOHEADER bi;
    memcpy(&bi, dib.data(), sizeof(bi));
    if (bi.biBitCount != 32 || bi.biCompression != BI_RGB) return 0xFFFFFFFFu;
    UINT w = static_cast<UINT>(bi.biWidth);
    UINT h = static_cast<UINT>(bi.biHeight < 0 ? -bi.biHeight : bi.biHeight);
    if (x >= w || y >= h) return 0xFFFFFFFFu;
    bool topDown = bi.biHeight < 0;
    UINT row = topDown ? y : (h - 1 - y);
    size_t off = 40 + (static_cast<size_t>(row) * w + x) * 4;
    if (off + 4 > dib.size()) return 0xFFFFFFFFu;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(dib.data()) + off;
    return (255u << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
}

// [C] 边框开关回归测试：走真实的 Document::CopySelectionToClipboard
static void TestBorderToggle() {
    printf("[C] 边框开关回归（Document 复制路径）\n");
    const unsigned kBase = 0xFF141414u;             // 底图色 RGB(20,20,20)
    const unsigned kBorder = 0xFFA0A0A0u;           // 边框色 RGB(160,160,160)

    auto makeBase = []() {
        auto bmp = std::make_unique<Bitmap>(100, 100, PixelFormat32bppARGB);
        BitmapData d;
        Rect rc(0, 0, 100, 100);
        bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &d);
        for (UINT yy = 0; yy < 100; ++yy) {
            BYTE* row = (BYTE*)d.Scan0 + yy * d.Stride;
            for (UINT xx = 0; xx < 100; ++xx) {
                row[xx * 4 + 0] = 20; row[xx * 4 + 1] = 20;
                row[xx * 4 + 2] = 20; row[xx * 4 + 3] = 255;
            }
        }
        bmp->UnlockBits(&d);
        return bmp;
    };

    AppSettings& s = Settings();
    s.borderCopyToExternal = false;
    {
        Document doc(makeBase(), 1, L"t");
        doc.SetRegion(0, 0, 100, 100);
        CHECK(doc.CopySelectionToClipboard(nullptr) == true, "关：复制成功");
        CHECK(GetDibPixel(0, 0) == kBase, "关：外部剪贴板左上角无边框");
        CHECK(GetDibPixel(50, 50) == kBase, "关：外部剪贴板中心为原图色");
        auto inner = GlobalPasteBuffer().Clone();
        CHECK(inner && util::BitmapHasAlpha(inner.get()) == false, "关：应用内缓冲存在");
        BitmapData d;
        Rect rc(0, 0, 1, 1);
        inner->LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &d);
        BYTE* p = (BYTE*)d.Scan0;
        unsigned px = (255u << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
        inner->UnlockBits(&d);
        CHECK(px == kBase, "关：应用内缓冲左上角无边框");
    }
    s.borderCopyToExternal = true;
    {
        Document doc(makeBase(), 2, L"t");
        doc.SetRegion(0, 0, 100, 100);
        CHECK(doc.CopySelectionToClipboard(nullptr) == true, "开：复制成功");
        CHECK(GetDibPixel(0, 0) == kBorder, "开：外部剪贴板左上角为边框色");
        CHECK(GetDibPixel(1, 1) == kBase, "开：边框内侧为原图色");
        CHECK(GetDibPixel(50, 50) == kBase, "开：中心为原图色");
        auto inner = GlobalPasteBuffer().Clone();
        BitmapData d;
        Rect rc(0, 0, 1, 1);
        inner->LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &d);
        BYTE* p = (BYTE*)d.Scan0;
        unsigned px = (255u << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
        inner->UnlockBits(&d);
        CHECK(px == kBase, "开：应用内缓冲仍无边框");
    }
    s.borderCopyToExternal = true; // 恢复默认
    s.Save();
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0); // 崩溃时输出不丢
    ULONG_PTR tok = 0;
    GdiplusStartupInput in;
    GdiplusStartup(&tok, &in, nullptr);
    {
        TestSettings();
        TestBorderToggle();
        TestClipboard();
        DebugHtmlSteps();
        TestAlpha();
    }
    // 关键：清空应用内粘贴缓冲，避免其 Bitmap 在 GdiplusShutdown 之后析构（0xC0000005）
    GlobalPasteBuffer().Set(nullptr);
    GdiplusShutdown(tok);
    printf(g_fail == 0 ? "RESULT: PASS\n" : "RESULT: FAIL (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
