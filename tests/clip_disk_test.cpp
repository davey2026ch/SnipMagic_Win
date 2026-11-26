// 剪贴板「落盘」行为验证：确认默认模式下磁盘上不留任何截图临时文件，
// 兼容模式下最多只有一张、且被下一次复制/清理动作删掉。
// 构建：zig c++ -target x86_64-windows-gnu -std=c++17 -DUNICODE -D_UNICODE -DNOMINMAX \
//        -DWIN32_LEAN_AND_MEAN -D__USE_MINGW_ANSI_STDIO=0 -o _clip_test.exe tests/clip_disk_test.cpp \
//        -lgdiplus -lgdi32 -luser32 -lshell32 -lshlwapi -lole32 -luuid -lcomctl32 -lcomdlg32 -static
#include "util.h"
#include <cstdio>
#include <string>

using namespace Gdiplus;

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) ++g_fail;
}

// 临时目录里 png 的个数；-1 表示目录不存在
static int CountTempPngs() {
    std::wstring dir = util::ClipboardTempDir();
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

static bool DirExists() {
    return GetFileAttributesW(util::ClipboardTempDir().c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::string ReadClipboardHtml() {
    std::string out;
    UINT fmt = RegisterClipboardFormatW(L"HTML Format");
    if (!fmt) return out;
    bool opened = false;
    for (int i = 0; i < 6 && !opened; ++i) {
        opened = OpenClipboard(nullptr) != FALSE;
        if (!opened) Sleep(20);
    }
    if (!opened) return out;
    HANDLE h = GetClipboardData(fmt);
    if (h) {
        if (const char* p = static_cast<const char*>(GlobalLock(h))) {
            SIZE_T sz = GlobalSize(h);
            out.assign(p, strnlen(p, sz));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

int main() {
    GdiplusStartupInput si;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &si, nullptr) != Ok) return 2;

    auto* bmp = new Bitmap(320, 200, PixelFormat32bppARGB);
    {
        Graphics g(bmp);
        g.Clear(Color(255, 240, 240, 240));
        SolidBrush br(Color(255, 30, 120, 220));
        g.FillRectangle(&br, 20, 20, 160, 90);
    }

    util::PurgeClipboardTempFiles();
    Check(!DirExists(), "起始状态：临时目录不存在");

    // ---- 默认模式（设置里「邮件客户端兼容」关闭）：完全不应落盘 ----
    bool ok = util::BitmapToClipboard(bmp, false);
    Check(ok, "默认模式复制返回成功");
    Check(CountTempPngs() == -1 && !DirExists(), "默认模式：磁盘上没有临时目录/临时文件");
    Check(IsClipboardFormatAvailable(CF_DIB) != 0, "剪贴板含 CF_DIB");
    UINT pngFmt = RegisterClipboardFormatW(L"PNG");
    Check(pngFmt && IsClipboardFormatAvailable(pngFmt), "剪贴板含 PNG 格式（带透明）");
    UINT htmlFmt = RegisterClipboardFormatW(L"HTML Format");
    Check(htmlFmt && IsClipboardFormatAvailable(htmlFmt), "剪贴板含 HTML Format");
    std::string html = ReadClipboardHtml();
    Check(html.find("data:image/png;base64,") != std::string::npos, "HTML 用内嵌 data URI 带图");
    Check(html.find("file:///") == std::string::npos, "HTML 里不含任何磁盘路径");
    Check(html.find("Version:0.9") == 0, "CF_HTML 头部完好");

    // ---- 兼容模式：写一张，且永远只留最新一张 ----
    bool ok1 = util::BitmapToClipboard(bmp, true);
    Check(ok1, "兼容模式复制返回成功");
    { int n = CountTempPngs(); char b[80]; snprintf(b, sizeof b, "兼容模式：临时目录里正好 1 张（实际 %d）", n); Check(n == 1, b); }
    html = ReadClipboardHtml();
    Check(html.find("file:///") != std::string::npos, "兼容模式：HTML 引用磁盘文件（邮件客户端用得上）");

    util::BitmapToClipboard(bmp, true);
    Check(CountTempPngs() == 1, "连续复制第二次：仍然只有 1 张（旧的被删）");
    util::BitmapToClipboard(bmp, true);
    Check(CountTempPngs() == 1, "连续复制第三次：仍然只有 1 张");

    // ---- 切回默认模式：残留文件应被清掉 ----
    util::BitmapToClipboard(bmp, false);
    std::printf("      [debug] dirExists=%d pngCount=%d\n", DirExists() ? 1 : 0, CountTempPngs());
    if (DirExists()) {
        std::wstring dir = util::ClipboardTempDir();
        WIN32_FIND_DATAW fd = {};
        HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::printf("      [debug] leftover: %ls attr=0x%lx\n", fd.cFileName,
                            fd.dwFileAttributes);
                std::wstring f = dir + L"\\" + fd.cFileName;
                if (DeleteFileW(f.c_str())) {
                    std::printf("      [debug] delete now OK (刚才是别的原因)\n");
                } else {
                    std::printf("      [debug] delete err=%lu\n", GetLastError());
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    Check(CountTempPngs() <= 0, "切回默认模式后，旧临时图片被清空（目录空壳留着无害）");

    // ---- 退出/启动清理 ----
    util::BitmapToClipboard(bmp, true);
    Check(CountTempPngs() == 1, "兼容模式再复制一张（准备测清理）");
    util::PurgeClipboardTempFiles();
    Check(!DirExists() && CountTempPngs() == -1, "PurgeClipboardTempFiles 清空并删除目录");

    // ---- 连复制压力测试：反复覆盖同一份，数量必须始终 ≤1 ----
    bool alwaysOne = true;
    for (int i = 0; i < 30; ++i) {
        util::BitmapToClipboard(bmp, true);
        int n = CountTempPngs();
        if (n != 1) {
            alwaysOne = false;
            std::printf("      [debug] 第 %d 次复制后 png 数 = %d\n", i + 1, n);
        }
    }
    Check(alwaysOne, "连复制 30 次：磁盘上始终正好 1 张（固定文件名覆盖）");

    // ---- 开/关交替压力测试（历史上这里偶发删不掉） ----
    bool toggleOk = true;
    for (int i = 0; i < 10; ++i) {
        util::BitmapToClipboard(bmp, true);
        util::BitmapToClipboard(bmp, false);
        if (CountTempPngs() > 0) {
            toggleOk = false;
            std::printf("      [debug] 第 %d 轮开关交替后仍有 %d 张\n", i + 1, CountTempPngs());
            util::PurgeClipboardTempFiles(false);
        }
    }
    Check(toggleOk, "开/关交替 10 轮：关掉后都不留图片");

    delete bmp;
    GdiplusShutdown(token);
    std::printf("\n%s（失败 %d 项）\n", g_fail == 0 ? "全部通过" : "有失败项", g_fail);
    return g_fail == 0 ? 0 : 1;
}
