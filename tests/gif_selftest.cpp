// GIF 编码自检：合成动画 → 写 GIF → 用 Windows 自带的 GIF 解码器（GDI+/WIC）读回比对
// 编译（在项目根目录）：
//   zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -o _selftest.exe tests\gif_selftest.cpp src\gifcodec.cpp
// 运行：_selftest.exe  → 打印结果并生成 _selftest.gif
#include "../src/gifcodec.h"

#include <objidl.h>
#include <cstdio>
#include <vector>

using namespace Gdiplus;

static int g_fails = 0;

static void Check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "[ OK ]" : "[FAIL]", what);
    if (!ok) ++g_fails;
}

// 读回画面并取像素（GDI+ 会把 GIF 解成 32bpp）
static bool ReadPixel(Bitmap* frame, int x, int y, BYTE& r, BYTE& g, BYTE& b) {
    if (!frame) return false;
    Rect rc(0, 0, static_cast<INT>(frame->GetWidth()), static_cast<INT>(frame->GetHeight()));
    BitmapData bd;
    if (frame->LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) return false;
    auto* row = static_cast<BYTE*>(bd.Scan0) + y * bd.Stride + x * 4;
    b = row[0];
    g = row[1];
    r = row[2];
    frame->UnlockBits(&bd);
    return true;
}

int main() {
    GdiplusStartupInput si;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &si, nullptr) != Ok) {
        printf("GDI+ init failed\n");
        return 2;
    }

    const int W = 96, H = 64;
    const int N = 10;

    // 1) 合成 N 帧：背景渐变 + 移动的纯色方块（块的位置可预期，便于像素校验）
    gifcodec::Anim anim;
    anim.width = W;
    anim.height = H;
    anim.loop = true;

    std::vector<std::vector<BYTE>> src(N);
    for (int f = 0; f < N; ++f) {
        std::vector<BYTE> buf(static_cast<size_t>(W) * H * 4, 0);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                BYTE* p = buf.data() + (static_cast<size_t>(y) * W + x) * 4;
                p[0] = static_cast<BYTE>(30 + y * 3);          // B
                p[1] = static_cast<BYTE>(40 + x * 2);          // G
                p[2] = static_cast<BYTE>(60);                  // R
                p[3] = 255;
            }
        }
        // 20x20 纯红方块，横向移动
        int bx = 4 + f * 6;
        for (int y = 10; y < 30; ++y) {
            for (int x = bx; x < bx + 20 && x < W; ++x) {
                BYTE* p = buf.data() + (static_cast<size_t>(y) * W + x) * 4;
                p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255;
            }
        }
        src[f] = std::move(buf);

        gifcodec::Frame fr;
        if (!gifcodec::CompressFrame(src[f].data(), W, H, 40 + f, fr)) {
            Check(false, "CompressFrame");
            GdiplusShutdown(token);
            return 1;
        }
        anim.frames.push_back(std::move(fr));
    }
    Check(anim.frames.size() == N, "帧数正确");
    Check(anim.MemoryBytes() < static_cast<size_t>(W) * H * 4 * N / 2, "压缩后体积小于原始 1/2");

    // 2) 自解码一致性
    {
        auto bmp = anim.Decode(3);
        Check(bmp != nullptr, "自解码成功");
        BYTE r = 0, g = 0, b = 0;
        Check(ReadPixel(bmp.get(), 5, 5, r, g, b) && b > 20 && r < 120, "自解码背景像素合理");
        const int bx = 4 + 3 * 6 + 5;
        Check(ReadPixel(bmp.get(), bx, 20, r, g, b) && r > 200 && g < 60 && b < 60,
              "自解码方块像素为红");
    }

    // 3) 写文件
    const std::wstring path = L"_selftest.gif";
    Check(gifcodec::WriteGif(path, anim), "写 GIF 文件成功");
    {
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, path.c_str(), L"rb") == 0 && fp) {
            char hdr[6] = {};
            fread(hdr, 1, 6, fp);
            fseek(fp, 0, SEEK_END);
            long sz = ftell(fp);
            fclose(fp);
            Check(memcmp(hdr, "GIF89a", 6) == 0, "文件头是 GIF89a");
            printf("       文件大小 %ld 字节\n", sz);
            Check(sz > 200 && sz < 4 * 1024 * 1024, "文件大小合理");
        } else {
            Check(false, "打不开生成的 gif");
        }
    }

    // 4) 用系统解码器（WIC/GDI+）读回：帧数、尺寸、像素
    {
        Bitmap loaded(path.c_str());
        Check(loaded.GetLastStatus() == Ok, "系统解码器能打开该 GIF");
        UINT dimCount = loaded.GetFrameDimensionsCount();
        Check(dimCount >= 1, "存在帧维度");
        GUID dim = {};
        if (dimCount >= 1) loaded.GetFrameDimensionsList(&dim, 1);
        const UINT frames = loaded.GetFrameCount(&dim);
        printf("       系统解码器读到 %u 帧\n", frames);
        Check(frames == static_cast<UINT>(N), "系统解码器帧数与写入一致");

        Bitmap canvas(W, H, PixelFormat32bppARGB);
        {
            Graphics g(&canvas);
            g.Clear(Color(255, 0, 0, 0));
            g.DrawImage(&loaded, 0, 0, W, H);
        }
        BYTE r = 0, g2 = 0, b = 0;
        Check(ReadPixel(&canvas, 5, 5, r, g2, b), "解码第 0 帧像素可读");
        printf("       第0帧 (5,5) = R%d G%d B%d\n", r, g2, b);
        Check(r >= 52 && r <= 68 && g2 >= 40 && g2 <= 58 && b >= 37 && b <= 53,
              "第 0 帧背景色接近源图");

        // 切到第 5 帧
        if (loaded.SelectActiveFrame(&dim, 5) == Ok) {
            Bitmap c2(W, H, PixelFormat32bppARGB);
            Graphics g(&c2);
            g.DrawImage(&loaded, 0, 0, W, H);
            const int bx = 4 + 5 * 6 + 5;
            Check(ReadPixel(&c2, bx, 20, r, g2, b) && r > 200 && g2 < 60 && b < 60,
                  "系统解码器第 5 帧方块位置正确");
            Check(ReadPixel(&c2, 2, 2, r, g2, b) && !(r > 200), "第 5 帧左上角仍是背景");
        } else {
            Check(false, "切帧失败");
        }
    }

    // 5) 单色画面（边界情况：只有一种颜色）
    {
        std::vector<BYTE> flat(static_cast<size_t>(W) * H * 4, 255);
        gifcodec::Frame fr;
        Check(gifcodec::CompressFrame(flat.data(), W, H, 40, fr), "纯色帧压缩成功");
        Check(fr.palette.size() >= 4 && (fr.palette.size() & (fr.palette.size() - 1)) == 0,
              "调色板长度为 2 的幂且≥4");
        Check(fr.minCodeSize >= 2 && fr.minCodeSize <= 8, "minCodeSize 在 2..8");
        gifcodec::Anim a2;
        a2.width = W; a2.height = H; a2.loop = false;
        a2.frames.push_back(fr);
        Check(gifcodec::WriteGif(L"_selftest_flat.gif", a2), "纯色 GIF 写出成功");
    }

    GdiplusShutdown(token);
    printf("\n%s (%d 项失败)\n", g_fails == 0 ? "全部通过" : "存在失败", g_fails);
    return g_fails == 0 ? 0 : 1;
}
