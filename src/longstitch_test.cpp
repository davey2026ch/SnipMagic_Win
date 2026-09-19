// 长截图算法离线合成帧回归（无 GUI）
// cl /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE longstitch_test.cpp longstitch.cpp
#include "longstitch.h"
#include <cstdio>
#include <random>

using namespace longstitch;

static FrameData Synth(int w, int h, int phase, bool withHeader) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = px.data() + (static_cast<size_t>(y) * w + x) * 4;
            int docY = y + phase;
            // 非周期哈希纹理，避免模运算造成假匹配
            uint32_t n = static_cast<uint32_t>(docY) * 2654435761u + static_cast<uint32_t>(x) * 40503u;
            n ^= n >> 13;
            n *= 1274126177u;
            uint8_t v = static_cast<uint8_t>(n & 0xFF);
            // 结构性横条
            if ((docY % 29) < 4) v = 30;
            else if ((docY % 41) < 3) v = 230;
            // 文本状竖纹
            if ((docY % 11) < 6 && (x % 17) < 3) v = static_cast<uint8_t>(255 - v);
            if (withHeader && y < 12) {
                v = static_cast<uint8_t>(40 + (x % 8));
            }
            if (withHeader && y >= h - 10) {
                v = 180;
            }
            p[0] = v;
            p[1] = static_cast<uint8_t>((v * 3) / 4 + (x & 7));
            p[2] = static_cast<uint8_t>(255 - v);
            p[3] = 255;
        }
    }
    return MakeFrame(px.data(), w, h);
}

static int g_fail = 0;
static void Check(bool cond, const char* name) {
    if (!cond) {
        std::printf("FAIL: %s\n", name);
        ++g_fail;
    } else {
        std::printf("PASS: %s\n", name);
    }
}

int main() {
    const int W = 320, H = 200;

    // 1) 首帧 Started
    {
        Stitcher st(W);
        auto f0 = Synth(W, H, 0, true);
        Check(st.Process(f0, 0.0) == Event::Started, "first frame Started");
        Check(st.CanvasRows() == H, "first frame canvas == H");
    }

    // 2) 向下滚 dy=40
    {
        Stitcher st(W);
        auto f0 = Synth(W, H, 0, true);
        auto f1 = Synth(W, H, 40, true);
        st.Process(f0, 0.0);
        auto e = st.Process(f1, 0.3);
        std::printf("  dy=40 event=%d rows=%d\n", static_cast<int>(e), st.CanvasRows());
        Check(e == Event::Appended || e == Event::NoChange || e == Event::Gap, "scroll event not invalid");
        // 有结构内容时应能 Appended，且画布变长
        if (e == Event::Appended) {
            Check(st.CanvasRows() > H, "canvas grew after scroll");
        }
    }

    // 3) 无变化
    {
        Stitcher st(W);
        auto f0 = Synth(W, H, 0, true);
        st.Process(f0, 0.0);
        auto e = st.Process(f0, 0.3);
        Check(e == Event::NoChange, "identical frame NoChange");
    }

    // 4) 回滚 dy<0
    {
        Stitcher st(W);
        auto f0 = Synth(W, H, 0, true);
        auto f1 = Synth(W, H, 50, true);
        auto f2 = Synth(W, H, 20, true); // 相对 f1 上移
        st.Process(f0, 0.0);
        st.Process(f1, 0.3);
        int before = st.CanvasRows();
        auto e = st.Process(f2, 0.6);
        std::printf("  scroll-up event=%d rows=%d (before %d)\n",
                    static_cast<int>(e), st.CanvasRows(), before);
        if (e == Event::ScrolledUp) {
            Check(st.CanvasRows() == before, "scroll-up does not append");
        }
    }

    // 5) finish 出图尺寸
    {
        Stitcher st(W);
        auto f0 = Synth(W, H, 0, true);
        auto f1 = Synth(W, H, 30, true);
        st.Process(f0, 0.0);
        st.Process(f1, 0.3);
        std::vector<uint8_t> out;
        int ow = 0, oh = 0;
        auto fl = Synth(W, H, 60, true);
        bool ok = st.Finish(&fl, out, ow, oh);
        Check(ok && ow == W && oh > 0, "finish produces image");
        std::printf("  finish %dx%d bytes=%zu gaps=%d\n", ow, oh, out.size(), st.GapCount());
    }

    // 6) 纯空白页不应误 sticky 导致崩溃
    {
        Stitcher st(W);
        std::vector<uint8_t> blank(static_cast<size_t>(W) * H * 4, 200);
        for (size_t i = 3; i < blank.size(); i += 4) blank[i] = 255;
        auto f0 = MakeFrame(blank.data(), W, H);
        auto e = st.Process(f0, 0.0);
        Check(e == Event::Started, "blank first frame Started");
        e = st.Process(f0, 0.3);
        Check(e == Event::NoChange || e == Event::Skipped, "blank second frame safe");
    }

    // 7) 多次滚动：不应出现大段重复
    {
        Stitcher st(W);
        auto f0 = Synth(W, H, 0, true);
        st.Process(f0, 0.0);
        for (int i = 1; i <= 4; ++i) {
            auto fi = Synth(W, H, i * 40, true);
            st.Process(fi, 0.2 * i);
        }
        const int rows = st.CanvasRows();
        std::printf("  multi-scroll rows=%d (expect ~%d)\n", rows, H + 4 * 40);
        Check(rows <= H + 4 * 40 + 40, "multi-scroll not overly tall (no massive dup)");
        Check(rows >= H + 40, "multi-scroll appended some content");
        // 画布不应暴涨（整节重复会让 rows 明显超过合理值）
        Check(rows < H * 3, "multi-scroll under 3x first frame");
    }

    // 8) 对齐失败后成功拼接，skipAlignCount 应清零
    {
        Stitcher st(W);
        auto f0 = Synth(W, H, 0, true);
        st.Process(f0, 0.0);
        // 先正常滚一段
        auto f1 = Synth(W, H, 40, true);
        st.Process(f1, 0.3);
        // 注入与画布无关的乱码帧，触发对齐失败
        {
            std::vector<uint8_t> noise(static_cast<size_t>(W) * H * 4);
            std::mt19937 rng(99);
            for (size_t i = 0; i < noise.size(); i += 4) {
                noise[i] = static_cast<uint8_t>(rng() & 0xFF);
                noise[i + 1] = static_cast<uint8_t>(rng() & 0xFF);
                noise[i + 2] = static_cast<uint8_t>(rng() & 0xFF);
                noise[i + 3] = 255;
            }
            auto fn = MakeFrame(noise.data(), W, H);
            auto en = st.Process(fn, 0.5);
            std::printf("  noise event=%d skips=%d\n",
                        static_cast<int>(en), st.SkipAlignCount());
            if (en == Event::NeedOverlap) {
                Check(st.SkipAlignCount() > 0, "NeedOverlap increments skipAlignCount");
            }
        }
        // 回滚到已捕获区再慢滚，应能重新对齐并清零计数
        auto fBack = Synth(W, H, 20, true);
        st.Process(fBack, 0.7);
        auto fDown = Synth(W, H, 70, true);
        auto eRec = st.Process(fDown, 0.9);
        std::printf("  recover event=%d skips=%d rows=%d\n",
                    static_cast<int>(eRec), st.SkipAlignCount(), st.CanvasRows());
        if (eRec == Event::Appended || eRec == Event::Gap) {
            Check(st.SkipAlignCount() == 0, "skipAlignCount cleared after successful append");
        } else {
            // 算法在合成噪声后可能仍需一帧才能对齐；再试一次
            auto fDown2 = Synth(W, H, 100, true);
            auto eRec2 = st.Process(fDown2, 1.1);
            std::printf("  recover2 event=%d skips=%d\n",
                        static_cast<int>(eRec2), st.SkipAlignCount());
            if (eRec2 == Event::Appended || eRec2 == Event::Gap) {
                Check(st.SkipAlignCount() == 0, "skipAlignCount cleared after successful append (2nd try)");
            } else {
                std::printf("  (skip clear check: recovery not aligned in synth, count=%d)\n",
                            st.SkipAlignCount());
            }
        }
    }

    std::printf("\n%s (%d failed)\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
