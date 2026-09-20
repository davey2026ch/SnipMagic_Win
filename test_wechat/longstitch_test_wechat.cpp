// 微信聊天式低纹理内容的长截图拼接回归（无 GUI）
// 内容特征：大片纯色背景、重复头像、重复短消息("好的")、稀疏文字行。
// 校验手段：每行文档坐标用"最后一列 B/G 通道"不可见编码（luma 影响 < 容差，
// 且所有对齐采样的列步长都不会采到最后一列），拼完后逐行解码，
// 统计重复行（硬失败）与缺失行（软指标）。
#include "longstitch.h"
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>

using namespace longstitch;

namespace {

struct Doc {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // BGRA
};

void FillRect(Doc& d, int x0, int y0, int x1, int y1, int r, int g, int b) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > d.w) x1 = d.w;
    if (y1 > d.h) y1 = d.h;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint8_t* p = d.px.data() + (static_cast<size_t>(y) * d.w + x) * 4;
            p[0] = static_cast<uint8_t>(b);
            p[1] = static_cast<uint8_t>(g);
            p[2] = static_cast<uint8_t>(r);
            p[3] = 255;
        }
    }
}

// 文字行：若干短黑条，模拟一行聊天气泡里的文字
void TextLine(Doc& d, int x, int y, int len, std::mt19937& rng) {
    int cx = x;
    while (cx < x + len) {
        int seg = 6 + static_cast<int>(rng() % 14);
        if (cx + seg > x + len) seg = x + len - cx;
        FillRect(d, cx, y, cx + seg, y + 7, 70, 70, 70);
        cx += seg + 4;
    }
}

// 固定内容的"好的"短消息：每次生成像素完全一致（重复消息歧义源）
int BuildOkMsg(Doc& d, int y) {
    FillRect(d, 12, y + 2, 52, y + 42, 66, 133, 244);      // 左头像（蓝）
    FillRect(d, 20, y + 10, 44, y + 34, 200, 220, 255);    // 头像内部高光
    FillRect(d, 60, y, 124, y + 30, 255, 255, 255);        // 白色气泡
    FillRect(d, 68, y + 11, 108, y + 18, 60, 60, 60);      // "好的"文字
    return y + 30;
}

// 普通消息：左/右侧气泡 + 头像，行数随机
int BuildMsg(Doc& d, int y, bool right, int lines, std::mt19937& rng) {
    const int bh = lines * 16 + 14;
    const int bw = 110 + static_cast<int>(rng() % 150);
    if (right) {
        FillRect(d, d.w - 52, y + 2, d.w - 12, y + 42, 240, 170, 60); // 右头像（橙）
        FillRect(d, d.w - 44, y + 10, d.w - 20, y + 34, 255, 230, 190);
        const int bx = d.w - 64 - bw;
        FillRect(d, bx, y, d.w - 64, y + bh, 149, 236, 105);          // 绿色气泡
        for (int i = 0; i < lines; ++i) {
            int len = (i == lines - 1) ? (bw - 60) / 2 : bw - 24;
            TextLine(d, bx + 12, y + 8 + i * 16, len, rng);
        }
    } else {
        FillRect(d, 12, y + 2, 52, y + 42, 66, 133, 244);             // 左头像（蓝）
        FillRect(d, 20, y + 10, 44, y + 34, 200, 220, 255);
        FillRect(d, 60, y, 60 + bw, y + bh, 255, 255, 255);           // 白色气泡
        for (int i = 0; i < lines; ++i) {
            int len = (i == lines - 1) ? (bw - 60) / 2 : bw - 24;
            TextLine(d, 72, y + 8 + i * 16, len, rng);
        }
    }
    return y + bh;
}

int BuildTimestamp(Doc& d, int y) {
    FillRect(d, d.w / 2 - 30, y + 4, d.w / 2 + 30, y + 14, 175, 175, 175);
    return y + 18;
}

struct DocOpts {
    bool heavyBlank = false;  // 消息间距拉大
    int repeatEvery = 5;      // 每 N 条消息插一条固定"好的"
};

Doc BuildDoc(int w, int h, const DocOpts& opts, uint32_t seed) {
    Doc d;
    d.w = w;
    d.h = h;
    d.px.assign(static_cast<size_t>(w) * h * 4, 0);
    // 背景：微信灰
    FillRect(d, 0, 0, w, h, 245, 245, 245);
    std::mt19937 rng(seed);
    int y = 6;
    int msgIdx = 0;
    while (y < h - 80) {
        const int gap = opts.heavyBlank
            ? 40 + static_cast<int>(rng() % 50)
            : 14 + static_cast<int>(rng() % 30);
        y += gap;
        if (rng() % 10 == 0) {
            y = BuildTimestamp(d, y);
            y += 6 + static_cast<int>(rng() % 10);
        }
        ++msgIdx;
        if (opts.repeatEvery > 0 && msgIdx % opts.repeatEvery == 0) {
            y = BuildOkMsg(d, y); // 重复短消息
        } else {
            const bool right = (rng() % 2) == 0;
            const int lines = 1 + static_cast<int>(rng() % 5);
            y = BuildMsg(d, y, right, lines, rng);
        }
    }
    // 文档坐标不可见编码：最后一列 B = y & 0xFF, G = (y >> 8) & 0xFF
    for (int ry = 0; ry < h; ++ry) {
        uint8_t* p = d.px.data() + (static_cast<size_t>(ry) * w + (w - 1)) * 4;
        p[0] = static_cast<uint8_t>(ry & 0xFF);
        p[1] = static_cast<uint8_t>((ry >> 8) & 0xFF);
    }
    return d;
}

// 稠密纹理文档（静态网页式样，回归对照组）
Doc BuildDenseDoc(int w, int h, uint32_t seed) {
    Doc d;
    d.w = w;
    d.h = h;
    d.px.assign(static_cast<size_t>(w) * h * 4, 0);
    std::mt19937 rng(seed);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = d.px.data() + (static_cast<size_t>(y) * w + x) * 4;
            uint32_t n = static_cast<uint32_t>(y) * 2654435761u + static_cast<uint32_t>(x) * 40503u;
            n ^= n >> 13;
            n *= 1274126177u;
            uint8_t v = static_cast<uint8_t>(n & 0xFF);
            p[0] = v;
            p[1] = static_cast<uint8_t>((v * 3) / 4 + (x & 7));
            p[2] = static_cast<uint8_t>(255 - v);
            p[3] = 255;
        }
    }
    for (int ry = 0; ry < h; ++ry) {
        uint8_t* p = d.px.data() + (static_cast<size_t>(ry) * w + (w - 1)) * 4;
        p[0] = static_cast<uint8_t>(ry & 0xFF);
        p[1] = static_cast<uint8_t>((ry >> 8) & 0xFF);
    }
    return d;
}

// 抓帧：mode 0 正常；1 撕裂（下半帧额外位移）；2 插入干扰（y0 以下整体下移）
FrameData Grab(const Doc& d, int off, int H, int mode, int y0, int extra) {
    std::vector<uint8_t> buf(static_cast<size_t>(d.w) * H * 4);
    for (int i = 0; i < H; ++i) {
        int src = off + i;
        if (mode == 1 && i >= H / 2) src += extra;
        if (mode == 2 && i >= y0) src += extra;
        if (src < 0) src = 0;
        if (src >= d.h) src = d.h - 1;
        std::memcpy(buf.data() + static_cast<size_t>(i) * d.w * 4,
                    d.px.data() + static_cast<size_t>(src) * d.w * 4,
                    static_cast<size_t>(d.w) * 4);
    }
    return MakeFrame(buf.data(), d.w, H);
}

struct Result {
    int dupRows = 0;
    int dupBlocks = 0;
    int missing = 0;
    int maxDoc = -1;
    int rows = 0;
    int firstDupAt = -1;
};

Result Analyze(const std::vector<uint8_t>& out, int w, int oh) {
    Result r;
    r.rows = oh;
    // 高水位判定：v 不超过历史最大值即重复行；超过才推进并累计缺失
    int highWater = -1;
    bool inDup = false;
    int dupStart = 0, dupStartDoc = 0, dupPeakBack = 0;
    int invalidRows = 0;
    for (int y = 0; y < oh; ++y) {
        const uint8_t* p = out.data() + (static_cast<size_t>(y) * w + (w - 1)) * 4;
        const int v = p[0] | (p[1] << 8);
        if (v >= 60000) { // 分隔条（白/灰225）
            ++invalidRows;
            inDup = false;
            continue;
        }
        if (v <= highWater) {
            if (!inDup) {
                ++r.dupBlocks;
                dupStart = y;
                dupStartDoc = v;
                dupPeakBack = highWater - v;
                if (r.firstDupAt < 0) r.firstDupAt = y;
            } else if (highWater - v > dupPeakBack) {
                dupPeakBack = highWater - v;
            }
            ++r.dupRows;
            inDup = true;
        } else {
            if (inDup) {
                std::printf("  [dup] out[%d..%d] 回退至 docY=%d（高水位 %d，块内 %d 行）\n",
                            dupStart, y - 1, dupStartDoc, highWater, y - dupStart);
            }
            r.missing += v - highWater - 1;
            highWater = v;
            inDup = false;
        }
        if (v > r.maxDoc) r.maxDoc = v;
    }
    if (invalidRows > 0) std::printf("  invalidRows=%d\n", invalidRows);
    return r;
}

int g_fail = 0;
void Check(bool cond, const char* name) {
    std::printf("%s: %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) ++g_fail;
}

// 通用滚动仿真：dy 随机，按模式注入干扰帧
Result RunScenario(const Doc& d, int H, uint32_t seed,
                   bool injectTear, bool injectShift, int dyMin, int dyMax) {
    Stitcher st(d.w);
    std::mt19937 rng(seed);
    double t = 0.0;
    int off = 0;
    auto f0 = Grab(d, 0, H, 0, 0, 0);
    st.Process(f0, t);
    int i = 0;
    while (off < d.h - H) {
        ++i;
        t += 0.16;
        const int dy = dyMin + static_cast<int>(rng() % (dyMax - dyMin + 1));
        off += dy;
        if (off > d.h - H) off = d.h - H;
        int mode = 0, y0 = 0, extra = 0;
        if (injectTear && i % 7 == 3) {
            mode = 1;
            extra = 18 + static_cast<int>(rng() % 20);
        } else if (injectShift && i % 11 == 5) {
            mode = 2;
            y0 = H / 3 + static_cast<int>(rng() % (H / 3));
            extra = 10 + static_cast<int>(rng() % 30);
            off += extra; // 插入导致视口内容整体上移
            if (off > d.h - H) off = d.h - H;
        }
        auto f = Grab(d, off, H, mode, y0, extra);
        st.Process(f, t);
    }
    auto fl = Grab(d, d.h - H, H, 0, 0, 0);
    std::vector<uint8_t> out;
    int ow = 0, oh = 0;
    if (!st.Finish(&fl, out, ow, oh)) {
        std::printf("  Finish failed!\n");
        ++g_fail;
        return Result{};
    }
    Result r = Analyze(out, ow, oh);
    std::printf("  rows=%d dupBlocks=%d dupRows=%d missing=%d maxDoc=%d (docH=%d) gaps=%d suspects=%d\n",
                r.rows, r.dupBlocks, r.dupRows, r.missing, r.maxDoc, d.h,
                st.GapCount(), st.SuspectSeamCount());
    return r;
}

} // namespace

int main(int argc, char** argv) {
    const int W = 420, H = 480, DOCH = 4200;
    const char* only = argc > 1 ? argv[1] : nullptr;
    auto Skip = [&](const char* name) { return only && std::strcmp(only, name) != 0; };

    if (!Skip("S1")) {
        std::printf("== S1: 重复短消息 + 正常空白（核心复现场景）==\n");
    {
        Doc d = BuildDoc(W, DOCH, DocOpts{ false, 5 }, 7u);
        Result r = RunScenario(d, H, 1000u, false, false, 40, 110);
        Check(r.dupBlocks == 0, "S1 no duplicated blocks");
        Check(r.maxDoc >= DOCH - H - 100, "S1 coverage reaches doc tail");
    }

    }
    if (!Skip("S2")) {
        std::printf("== S2: 大空白 + 重复短消息 ==\n");
    {
        DocOpts o;
        o.heavyBlank = true;
        o.repeatEvery = 4;
        Doc d = BuildDoc(W, DOCH, o, 11u);
        Result r = RunScenario(d, H, 2000u, false, false, 40, 110);
        Check(r.dupBlocks == 0, "S2 no duplicated blocks");
        Check(r.maxDoc >= DOCH - H - 100, "S2 coverage reaches doc tail");
    }

    }
    if (!Skip("S3")) {
        std::printf("== S3: 重复消息 + 撕裂帧（D3D 截屏半帧）==\n");
    {
        Doc d = BuildDoc(W, DOCH, DocOpts{ false, 5 }, 13u);
        Result r = RunScenario(d, H, 3000u, true, false, 40, 110);
        Check(r.dupBlocks == 0, "S3 no duplicated blocks");
    }

    }
    if (!Skip("S4")) {
        std::printf("== S4: 重复消息 + 新消息插入干扰 ==\n");
    {
        Doc d = BuildDoc(W, DOCH, DocOpts{ false, 5 }, 17u);
        Result r = RunScenario(d, H, 4000u, false, true, 40, 110);
        Check(r.dupBlocks == 0, "S4 no duplicated blocks");
    }

    }
    if (!Skip("S5")) {
        std::printf("== S5: 快速滚动（每帧 90~160 行）==\n");
    {
        Doc d = BuildDoc(W, DOCH, DocOpts{ false, 5 }, 19u);
        Result r = RunScenario(d, H, 5000u, false, false, 90, 160);
        Check(r.dupBlocks == 0, "S5 no duplicated blocks");
    }

    }
    if (!Skip("S7")) {
        std::printf("== S7: 慢速滚动 + 高频重复消息（歧义最大化）==\n");
    {
        DocOpts o;
        o.heavyBlank = false;
        o.repeatEvery = 3;
        Doc d = BuildDoc(W, DOCH, o, 29u);
        Result r = RunScenario(d, H, 7000u, false, false, 25, 60);
        Check(r.dupBlocks == 0, "S7 no duplicated blocks");
        Check(r.maxDoc >= DOCH - H - 100, "S7 coverage reaches doc tail");
    }

    }
    if (!Skip("S6")) {
        std::printf("== S6: 稠密纹理回归（静态网页式样，不容许缺失）==\n");
    {
        Doc d = BuildDenseDoc(W, DOCH, 23u);
        Result r = RunScenario(d, H, 6000u, false, false, 40, 110);
        Check(r.dupBlocks == 0, "S6 no duplicated blocks");
        Check(r.missing <= DOCH / 100, "S6 dense content almost no missing");
    }

    }
    std::printf("\n%s (%d failed)\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
