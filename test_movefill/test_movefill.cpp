// 选区抠起+烙白 移动逻辑的独立单元测试（无 GUI）
// 编译：链接 document.cpp annotation.cpp settings.cpp
#include "document.h"
#include "util.h"
#include <cstdio>
#include <cmath>

using namespace Gdiplus;
#pragma comment(lib, "gdiplus.lib")

static int g_fail = 0;
static void Check(bool cond, const char* name) {
    std::printf("%s: %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++g_fail;
}

// 造一张测试底图：左半蓝、右半绿、中线黑
static std::unique_ptr<Bitmap> MakeBase(int w, int h) {
    auto bmp = std::make_unique<Bitmap>(w, h, PixelFormat32bppARGB);
    BitmapData d;
    Rect rc(0, 0, w, h);
    if (bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &d) != Ok) return nullptr;
    for (int y = 0; y < h; ++y) {
        BYTE* row = static_cast<BYTE*>(d.Scan0) + static_cast<size_t>(y) * d.Stride;
        for (int x = 0; x < w; ++x) {
            BYTE* p = row + static_cast<size_t>(x) * 4;
            if (x == w / 2) { p[0] = 0; p[1] = 0; p[2] = 0; }
            else if (x < w / 2) { p[0] = 250; p[1] = 120; p[2] = 30; }   // BGRA 蓝
            else { p[0] = 40; p[1] = 200; p[2] = 60; }                    // 绿
            p[3] = 255;
        }
    }
    bmp->UnlockBits(&d);
    return bmp;
}

// 读某点像素
static bool Px(Bitmap* b, int x, int y, BYTE& B, BYTE& G, BYTE& R) {
    Color c;
    if (b->GetPixel(x, y, &c) != Ok) return false;
    B = c.GetB(); G = c.GetG(); R = c.GetR();
    return true;
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    GdiplusStartupInput si;
    ULONG_PTR tok;
    GdiplusStartup(&tok, &si, nullptr);
    {
        const int W = 200, H = 100;
        auto base = MakeBase(W, H);
        Document doc(std::move(base), 1, L"T");
        doc.SetRegion(40, 20, 90, 60); // 选区 40,20 - 90,60

        // === 模拟 BeginRegionContentMove 核心 ===
        int rx, ry, rw, rh;
        Check(doc.GetRegion(rx, ry, rw, rh), "region exists");
        doc.PushUndo();
        auto piece = util::CropBitmap(doc.base.get(), rx, ry, rw, rh);
        Check(piece != nullptr, "piece cropped");
        // piece 马上要 move 走，留一份参照用于比对像素
        auto pieceRef = std::unique_ptr<Bitmap>(
            piece->Clone(0, 0, piece->GetWidth(), piece->GetHeight(), PixelFormat32bppARGB));
        {
            Graphics g(doc.base.get());
            SolidBrush white(Color(255, 255, 255, 255));
            g.FillRectangle(&white, rx, ry, rw, rh);
        }
        auto img = doc.CreatePasteFrom(std::move(piece), static_cast<float>(rx),
                                       static_cast<float>(ry));
        img->selected = true;
        doc.ClearSelection();
        doc.ClearRegion();
        int idx = static_cast<int>(doc.annotations.size());
        doc.annotations.push_back(std::move(img));
        doc.selectedIdx = idx;

        // 1) 原位置已烙白（含四角与中心）
        bool holeWhite = true;
        int pts[][2] = {{rx, ry}, {rx + rw - 1, ry}, {rx, ry + rh - 1},
                        {rx + rw - 1, ry + rh - 1}, {rx + rw / 2, ry + rh / 2}};
        for (auto& pt : pts) {
            BYTE B, G, R;
            if (!Px(doc.base.get(), pt[0], pt[1], B, G, R) || B != 255 || G != 255 || R != 255) {
                holeWhite = false;
            }
        }
        Check(holeWhite, "hole baked pure white in base");
        // 选区外的像素不受影响
        BYTE B, G, R;
        Px(doc.base.get(), 5, 5, B, G, R);
        Check(B == 250 && G == 120 && R == 30, "outside-left pixel untouched");
        Px(doc.base.get(), W - 5, 5, B, G, R);
        Check(B == 40 && G == 200 && R == 60, "outside-right pixel untouched");

        // 2) 抠出的图层像素与原区域一致（抽查）
        auto comp0 = doc.RenderComposite();
        bool pieceOk = true;
        for (auto& pt : pts) {
            BYTE b1, g1, r1, b2, g2, r2;
            Px(pieceRef.get(), pt[0] - rx, pt[1] - ry, b1, g1, r1);
            Px(comp0.get(), pt[0], pt[1], b2, g2, r2); // 图层放回原位 → 合成应等于原图内容
            if (b1 != b2 || g1 != g2 || r1 != r2) pieceOk = false;
        }
        Check(pieceOk, "floating layer carries original pixels");

        // 3) 移动图层 (+30, +10) 后：新位置出现原图内容，旧位置保持白
        doc.annotations[idx]->Move(30, 10);
        auto comp1 = doc.RenderComposite();
        bool movedOk = true, oldStillWhite = true;
        BYTE b1, g1, r1, b2, g2, r2;
        Px(pieceRef.get(), 5, 5, b1, g1, r1);
        Px(comp1.get(), rx + 5 + 30, ry + 5 + 10, b2, g2, r2);
        if (b1 != b2 || g1 != g2 || r1 != r2) movedOk = false;
        Px(comp1.get(), rx + 5, ry + 5, b2, g2, r2);
        if (b2 != 255 || g2 != 255 || r2 != 255) oldStillWhite = false;
        Check(movedOk, "content appears at new position after move");
        Check(oldStillWhite, "old position stays white after move");

        // 4) 保存语义：合成图里烙白是永久内容（非浮层再算一遍也一样）
        auto comp2 = doc.RenderComposite();
        Px(comp2.get(), rx + 5, ry + 5, b2, g2, r2);
        Check(b2 == 255 && g2 == 255 && r2 == 255, "white hole stable across renders");

        // 5) Undo 撤销：底图与图层都回到操作前
        doc.Undo();
        bool undoOk = doc.annotations.empty();
        Px(doc.base.get(), rx + 5, ry + 5, B, G, R);
        if (!(B == 250 && G == 120 && R == 30)) undoOk = false;
        Check(undoOk, "undo restores base and removes layer");

        // 6) 选区在图外/越界的夹取不崩
        doc.SetRegion(W - 10, H - 10, W + 50, H + 50);
        int ox, oy, ow, oh;
        Check(doc.GetRegion(ox, oy, ow, oh), "out-of-range region readable");
        doc.ClearRegion();
    }
    GdiplusShutdown(tok);
    std::printf("\n%s (%d failed)\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
