#include "longstitch.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#ifdef LC_DEBUG
#include <cstdio>
#endif

namespace longstitch {
namespace {

// 与参考方案对齐的常量；Windows 上若效果不佳优先调 segmentTolerance 相关项
constexpr int kSegments = 16;
constexpr int kMaxSamplesPerRow = 256;
constexpr int kSegmentToleranceFactor = 14;
constexpr int kDistinctiveMinSpan = 300;
constexpr int kDistinctiveFactor = 18;
constexpr int kMinOverlap = 40;
constexpr int kRowSampleCount = 48;
constexpr int kMinSticky = 8;
constexpr int kMaxStickyDiv = 3;
constexpr int kEdgePadDiv = 10;
constexpr double kConfidenceThreshold = 0.80;
constexpr int kPixelTolerance = 14;
constexpr double kVerifyThreshold = 0.88;
constexpr int kMinEvidenceRows = 6;
constexpr int kMaxVerifyCandidates = 32;
constexpr int kSeparatorRows = 8;
constexpr int kSeamSearch = 28;      // 加宽：抗亚像素/半次像素滚动
constexpr int kSeamContext = 24;
constexpr double kSeamMaxAvg = 48.0;
constexpr int kMinNewRows = 2;       // 新增行过少时跳过（调小，减少丢行）
constexpr double kAnchorAgeLimit = 1.5;
constexpr double kGapCooldown = 1.5;
constexpr size_t kMemBudget = 320ull * 1024ull * 1024ull;

// BGRA：(3R + 6G + B) / 10
inline int LumaBGRA(const uint8_t* p) {
    return (3 * static_cast<int>(p[2]) +
            6 * static_cast<int>(p[1]) +
            1 * static_cast<int>(p[0])) / 10;
}

int MaxRowsForWidth(int w) {
    if (w <= 0) return 600;
    const size_t perRow = static_cast<size_t>(w) * 4ull;
    const int byMem = static_cast<int>(kMemBudget / perRow);
    int r = byMem;
    if (r > 30000) r = 30000;
    if (r < 600) r = 600;
    return r;
}

void ComputeSignature(FrameData& f) {
    if (f.w <= 0 || f.h <= 0 || f.pixels.empty()) return;
    const int samples = (std::min)(f.w, kMaxSamplesPerRow);
    f.samplesPerRow = samples;
    const int segSpan = (std::max)(1, samples / kSegments);
    f.sig.assign(static_cast<size_t>(f.h) * kSegments, 0);
    f.distinctive.assign(static_cast<size_t>(f.h), 0);

    for (int y = 0; y < f.h; ++y) {
        const uint8_t* row = f.pixels.data() + static_cast<size_t>(y) * f.w * 4;
        int32_t* sig = f.sig.data() + static_cast<size_t>(y) * kSegments;
        for (int s = 0; s < kSegments; ++s) {
            int32_t acc = 0;
            const int begin = s * segSpan;
            int end = begin + segSpan;
            if (s == kSegments - 1) end = samples;
            for (int x = begin; x < end && x < samples; ++x) {
                const int srcX = (samples >= f.w)
                    ? x
                    : static_cast<int>((static_cast<int64_t>(x) * f.w) / samples);
                acc += LumaBGRA(row + static_cast<size_t>(srcX) * 4);
            }
            sig[s] = acc;
        }
    }
}

// 修正 distinctive：参考实现用的是 16 段签名最大最小差
// 阈值 max(segSpan*18, 300)。签名是段内 luma 累加，这里用：
// max-min >= max(segSpan * 18, 300)，与 Mac 标定一致。
void FixDistinctive(FrameData& f) {
    if (f.w <= 0 || f.h <= 0 || f.sig.empty()) return;
    const int samples = f.samplesPerRow > 0 ? f.samplesPerRow : (std::min)(f.w, kMaxSamplesPerRow);
    const int segSpan = (std::max)(1, samples / kSegments);
    const int thr = (std::max)(segSpan * kDistinctiveFactor, kDistinctiveMinSpan);
    f.distinctive.assign(static_cast<size_t>(f.h), 0);
    for (int y = 0; y < f.h; ++y) {
        const int32_t* sig = f.sig.data() + static_cast<size_t>(y) * kSegments;
        int mn = sig[0], mx = sig[0];
        for (int s = 1; s < kSegments; ++s) {
            if (sig[s] < mn) mn = sig[s];
            if (sig[s] > mx) mx = sig[s];
        }
        f.distinctive[static_cast<size_t>(y)] = ((mx - mn) >= thr) ? 1 : 0;
    }
}

} // namespace

FrameData MakeFrame(const uint8_t* bgra, int w, int h) {
    FrameData f;
    if (!bgra || w <= 0 || h <= 0) return f;
    f.w = w;
    f.h = h;
    f.pixels.resize(static_cast<size_t>(w) * h * 4);
    std::memcpy(f.pixels.data(), bgra, f.pixels.size());
    ComputeSignature(f);
    FixDistinctive(f);
    return f;
}

Stitcher::Stitcher(int width) : width_(width) {
    if (width_ < 4) width_ = 4;
    maxRows_ = MaxRowsForWidth(width_);
    canvas_.clear();
    canvasRows_ = 0;
}

int Stitcher::stickyBand(const FrameData& a, const FrameData& b, bool top) const {
    if (a.w != b.w || a.h != b.h || a.h < 8) return 0;
    const int H = a.h;
    const int limit = H / kMaxStickyDiv;
    int count = 0;
    int distinctiveRows = 0;
    const int stride = width_ * 4;
    for (int i = 0; i < limit; ++i) {
        const int y = top ? i : (H - 1 - i);
        const uint8_t* ra = a.pixels.data() + static_cast<size_t>(y) * stride;
        const uint8_t* rb = b.pixels.data() + static_cast<size_t>(y) * stride;
        bool same = true;
        for (int x = 0; x < stride; x += 4) {
            if (ra[x] != rb[x] || ra[x + 1] != rb[x + 1] || ra[x + 2] != rb[x + 2]) {
                same = false;
                break;
            }
        }
        if (!same) break;
        ++count;
        if (y >= 0 && y < H && !a.distinctive.empty() && a.distinctive[static_cast<size_t>(y)]) {
            ++distinctiveRows;
        }
    }
    // §7.5：大片纯空白不作为固定头尾，避免无谓压缩对齐窗口
    if (count < kMinSticky) return 0;
    if (count > H / 4 && distinctiveRows <= 0) return 0;
    return count;
}

Stitcher::RatioOut Stitcher::ratioAtEx(const FrameData& a, const FrameData& b, int dy,
                                       int top, int bottom) const {
    RatioOut out;
    if (a.w != b.w || a.h != b.h) return out;
    const int H = a.h;
    if (bottom <= top) return out;

    // 约定：dy > 0 表示内容相对锚帧向上位移 dy。
    // 当前帧行 y 对应锚帧行 (y + dy)。
    std::vector<int> rows;
    rows.reserve(kRowSampleCount);
    const int span = bottom - top;
    if (span <= 0) return out;
    const int step = (std::max)(1, span / kRowSampleCount);
    for (int y = top; y < bottom && static_cast<int>(rows.size()) < kRowSampleCount; y += step) {
        const int ya = y + dy;
        if (ya < 0 || ya >= H) continue;
        if (!a.distinctive.empty() && !a.distinctive[static_cast<size_t>(ya)]) continue;
        if (!b.distinctive.empty() && !b.distinctive[static_cast<size_t>(y)]) continue;
        rows.push_back(y);
    }
    out.used = static_cast<int>(rows.size());
    if (out.used < kMinEvidenceRows) return out;

    const int samples = a.samplesPerRow > 0 ? a.samplesPerRow : (std::min)(a.w, kMaxSamplesPerRow);
    const int segSpan = (std::max)(1, samples / kSegments);
    const int tol = segSpan * kSegmentToleranceFactor;

    int matched = 0;
    for (int y : rows) {
        const int ya = y + dy;
        const int32_t* sa = a.sig.data() + static_cast<size_t>(ya) * kSegments;
        const int32_t* sb = b.sig.data() + static_cast<size_t>(y) * kSegments;
        bool ok = true;
        for (int s = 0; s < kSegments; ++s) {
            if (std::abs(sa[s] - sb[s]) > tol) {
                ok = false;
                break;
            }
        }
        if (ok) ++matched;
    }
    out.matched = matched;
    out.ratio = static_cast<double>(matched) / static_cast<double>(out.used);
    return out;
}

bool Stitcher::verifyPixels(const FrameData& a, const FrameData& b, int dy,
                            int top, int bottom) const {
    if (a.w != b.w || a.h != b.h) return false;
    const int H = a.h;
    const int span = bottom - top;
    if (span < 8) return false;

    const int rowsN = 24;
    const int colsN = 16;
    int total = 0, passed = 0, rowChecked = 0;
    const int rowStep = (std::max)(1, span / rowsN);
    const int colStep = (std::max)(1, a.w / colsN);
    for (int y = top; y < bottom; y += rowStep) {
        const int ya = y + dy;
        if (ya < 0 || ya >= H) continue;
        // 空白行不提供证据：微信式页面里背景行在任意 dy 下都"匹配"，
        // 若计入分母会把错误的位移候选抬过 88% 阈值。
        const bool evA = a.distinctive.empty() || a.distinctive[static_cast<size_t>(ya)];
        const bool evB = b.distinctive.empty() || b.distinctive[static_cast<size_t>(y)];
        if (!evA && !evB) continue;
        const uint8_t* ra = a.pixels.data() + static_cast<size_t>(ya) * a.w * 4;
        const uint8_t* rb = b.pixels.data() + static_cast<size_t>(y) * b.w * 4;
        ++rowChecked;
        for (int x = 0; x < a.w; x += colStep) {
            const uint8_t* pa = ra + static_cast<size_t>(x) * 4;
            const uint8_t* pb = rb + static_cast<size_t>(x) * 4;
            ++total;
            if (std::abs(pa[0] - pb[0]) <= kPixelTolerance &&
                std::abs(pa[1] - pb[1]) <= kPixelTolerance &&
                std::abs(pa[2] - pb[2]) <= kPixelTolerance) {
                ++passed;
            }
        }
    }
    if (rowChecked < kMinEvidenceRows || total < 48) return false;
    return static_cast<double>(passed) / static_cast<double>(total) >= kVerifyThreshold;
}

bool Stitcher::framesSimilar(const FrameData& a, const FrameData& b) const {
    if (a.w != b.w || a.h != b.h || a.w <= 0 || a.h <= 0) return false;
    const int stepX = (std::max)(1, a.w / 24);
    const int stepY = (std::max)(1, a.h / 24);
    int total = 0, ok = 0;
    for (int y = 0; y < a.h; y += stepY) {
        const uint8_t* ra = a.pixels.data() + static_cast<size_t>(y) * a.w * 4;
        const uint8_t* rb = b.pixels.data() + static_cast<size_t>(y) * b.w * 4;
        for (int x = 0; x < a.w; x += stepX) {
            const uint8_t* pa = ra + static_cast<size_t>(x) * 4;
            const uint8_t* pb = rb + static_cast<size_t>(x) * 4;
            ++total;
            const int d = std::abs(pa[0] - pb[0]) + std::abs(pa[1] - pb[1]) + std::abs(pa[2] - pb[2]);
            if (d <= kPixelTolerance) ++ok;
        }
    }
    if (total <= 0) return false;
    return static_cast<double>(ok) / static_cast<double>(total) >= 0.90;
}

Stitcher::MatchResult Stitcher::detect(const FrameData& anchor, const FrameData& frame) const {
    MatchResult r;
    if (anchor.w != frame.w || anchor.h != frame.h || frame.h < kMinOverlap + 10) {
        r.kind = MatchResult::Kind::Unknown;
        return r;
    }
    const int H = frame.h;
    const int topBand = stickyBand(anchor, frame, true);
    const int botBand = stickyBand(anchor, frame, false);
    int top = (std::max)(H / kEdgePadDiv, topBand);
    int bottom = (std::min)(H - H / kEdgePadDiv, H - botBand);
    if (bottom - top < kMinOverlap) {
        top = H / kEdgePadDiv;
        bottom = H - H / kEdgePadDiv;
        if (bottom - top < kMinOverlap) {
            top = 0;
            bottom = H;
        }
    }

    const RatioOut r0 = ratioAtEx(anchor, frame, 0, top, bottom);
    if (r0.used >= kMinEvidenceRows && r0.ratio >= kConfidenceThreshold) {
        r.kind = MatchResult::Kind::NoChange;
        r.stickyTop = topBand;
        r.stickyBottom = botBand;
        return r;
    }

    struct Cand { int dy; double ratio; int used; int matched; };
    std::vector<Cand> cands;
    const int maxDy = H - kMinOverlap;
    auto consider = [&](int dy) {
        const RatioOut rr = ratioAtEx(anchor, frame, dy, top, bottom);
        if (rr.used >= kMinEvidenceRows && rr.ratio >= kConfidenceThreshold) {
            cands.push_back({ dy, rr.ratio, rr.used, rr.matched });
        }
    };
    for (int dy = 1; dy <= maxDy; ++dy) consider(dy);
    for (int dy = -1; dy >= -maxDy; --dy) consider(dy);

    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.matched != b.matched) return a.matched > b.matched;
        if (a.used != b.used) return a.used > b.used;
        if (a.ratio != b.ratio) return a.ratio > b.ratio;
        return std::abs(a.dy) < std::abs(b.dy);
    });
    if (cands.size() > static_cast<size_t>(kMaxVerifyCandidates)) {
        cands.resize(static_cast<size_t>(kMaxVerifyCandidates));
    }

    // 收集所有通过像素校验的候选，再保守选择：
    // 误差接近时优先 |dy| 更小者 —— 高估位移是「重影/重复内容」的主因
    int chosen = 0;
    bool got = false;
    double bestSad = 1e9;
    for (const Cand& c : cands) {
        if (!verifyPixels(anchor, frame, c.dy, top, bottom)) continue;
        const int dy2 = refineDy(anchor, frame, c.dy, top, bottom);
        if (!verifyPixels(anchor, frame, dy2, top, bottom)) continue;
        const double sad = overlapSad(anchor, frame, dy2, top, bottom);
        if (sad > 40.0) continue; // 误差过大不采信
        const int ady = std::abs(dy2);
        if (!got) {
            bestSad = sad;
            chosen = dy2;
            got = true;
            continue;
        }
        const int abest = std::abs(chosen);
        // 误差差不多（或更好一点）时，选更小的滚动量，避免把旧内容再拼一遍
        if (ady < abest && sad <= bestSad * 1.25 + 2.0) {
            bestSad = sad;
            chosen = dy2;
        } else if (sad < bestSad * 0.75 && sad < bestSad - 4.0) {
            // 明显更准才允许更大的 |dy|
            if (ady > abest && sad + 8.0 < bestSad) {
                bestSad = sad;
                chosen = dy2;
            } else if (ady <= abest) {
                bestSad = sad;
                chosen = dy2;
            }
        }
        if (sad < 10.0 && std::abs(chosen) <= std::abs(dy2)) break;
    }
    if (got) {
        // 帧内一致性：上/下半区在同一 dy 下都必须吻合。
        // 微信滚动时抓到的"撕裂帧"（上半旧下半新）、懒加载重排、
        // 新消息插入都会造成半区位移不一致；这种帧若拿来拼接，
        // 撕裂线附近的内容会被错位贴进画布（块内重复/缺失）。宁跳过不错拼。
        // 注意比对区间要按 dy 裁掉锚帧的固定头尾：dy>0 时锚帧页脚会被
        // 映射进比对区（帧的内容行 vs 锚的固定页脚），不误裁会误报不一致。
        int effTop = top;
        int effBot = bottom;
        if (chosen > 0) {
            effBot = (std::min)(bottom, H - botBand - chosen);
        } else if (chosen < 0) {
            effTop = (std::max)(top, topBand - chosen);
        }
        const int mid = effTop + (effBot - effTop) / 2;
        if (mid - effTop >= 24 && effBot - mid >= 24) {
            const double sadTop = overlapSad(anchor, frame, chosen, effTop, mid);
            const double sadBot = overlapSad(anchor, frame, chosen, mid, effBot);
            if (sadTop > 30.0 || sadBot > 30.0) {
#ifdef LC_DEBUG
                std::fprintf(stdout, "  [detect] inconsistent halves dy=%d top=%.1f bot=%.1f -> Unknown\n",
                             chosen, sadTop, sadBot);
#endif
                r.kind = MatchResult::Kind::Unknown;
                r.unstable = true;
                r.stickyTop = topBand;
                r.stickyBottom = botBand;
                return r;
            }
        }
        r.kind = MatchResult::Kind::Scrolled;
        r.dy = chosen;
        r.stickyTop = topBand;
        r.stickyBottom = botBand;
#ifdef LC_DEBUG
        std::fprintf(stdout, "  [detect] Scrolled dy=%d stickyTop=%d stickyBottom=%d\n", chosen, topBand, botBand);
#endif
        return r;
    }

    r.kind = MatchResult::Kind::Unknown;
    r.stickyTop = topBand;
    r.stickyBottom = botBand;
    return r;
}

static double SeamScore(const uint8_t* canvas, int canvasRows, int width,
                        const FrameData& anchor, int s, int N) {
    if (N <= 0 || canvasRows < N) return 1e9;
    const int H = anchor.h;
    if (s < 0 || s + N > H) return 1e9;
    const int stride = width * 4;
    double sum = 0.0;
    int cnt = 0;
    for (int i = 0; i < N; ++i) {
        const uint8_t* rc = canvas + static_cast<size_t>(canvasRows - N + i) * stride;
        const uint8_t* ra = anchor.pixels.data() + static_cast<size_t>(s + i) * stride;
        for (int x = 0; x < stride; x += 8) {
            sum += (std::abs(rc[x] - ra[x]) +
                    std::abs(rc[x + 1] - ra[x + 1]) +
                    std::abs(rc[x + 2] - ra[x + 2])) / 3.0;
            ++cnt;
        }
    }
    return cnt > 0 ? sum / cnt : 1e9;
}

double Stitcher::overlapSad(const FrameData& a, const FrameData& b, int dy,
                            int top, int bottom) const {
    if (a.w != b.w || a.h != b.h) return 1e9;
    const int H = a.h;
    if (bottom <= top) return 1e9;
    double sum = 0.0;
    int cnt = 0;
    const int rowStep = (std::max)(1, (bottom - top) / 32);
    const int colStep = (std::max)(1, a.w / 24);
    for (int y = top; y < bottom; y += rowStep) {
        const int ya = y + dy;
        if (ya < 0 || ya >= H) continue;
        const uint8_t* ra = a.pixels.data() + static_cast<size_t>(ya) * a.w * 4;
        const uint8_t* rb = b.pixels.data() + static_cast<size_t>(y) * b.w * 4;
        for (int x = 0; x < a.w; x += colStep) {
            const uint8_t* pa = ra + static_cast<size_t>(x) * 4;
            const uint8_t* pb = rb + static_cast<size_t>(x) * 4;
            sum += std::abs(pa[0] - pb[0]) + std::abs(pa[1] - pb[1]) + std::abs(pa[2] - pb[2]);
            ++cnt;
        }
    }
    return cnt > 0 ? sum / cnt : 1e9;
}

int Stitcher::refineDy(const FrameData& anchor, const FrameData& frame, int dy,
                       int top, int bottom) const {
    if (anchor.w != frame.w || anchor.h != frame.h) return dy;
    const int H = frame.h;
    const int maxDy = H - kMinOverlap;
    int best = dy;
    double bestSad = overlapSad(anchor, frame, dy, top, bottom);
    // 半径过大容易跳到「过大 dy」→ 重影；限制在较小邻域
    const int radius = (std::min)(20, (std::max)(6, std::abs(dy) / 8));
    for (int d = dy - radius; d <= dy + radius; ++d) {
        if (d == 0 || d == dy) continue;
        if (d > maxDy || d < -maxDy) continue;
        if (dy > 0 && d < 1) continue;
        if (dy < 0 && d > -1) continue;
        // 优先更小的正向滚动（抗高估）
        if (dy > 0 && d > dy && bestSad < 20.0) continue;
        const double sad = overlapSad(anchor, frame, d, top, bottom);
        if (sad + 0.5 < bestSad) {
            bestSad = sad;
            best = d;
        }
    }
    return best;
}

// 用画布尾部在新帧中定位：真正要追加的起点 = 尾部对齐位置 + 尾部高度
// 画布尾部 K 行与新帧对齐：找到 off 使 frame[off..] ≈ canvas tail
// 返回真正应追加的起点 off+K；失败返回 -1
int Stitcher::alignCanvasTailToFrame(const FrameData& frame, int stickyTop, int e,
                                     double* outScore) const {
    if (outScore) *outScore = 1e9;
    if (canvasRows_ < 16 || frame.pixels.empty() || e <= stickyTop) return -1;
    const int K = (std::min)(28, canvasRows_ / 2);
    if (K < 8) return -1;
    const int stride = width_ * 4;
    // 搜索范围限定在内容区 [stickyTop, e-K]：避免误匹配页眉/页脚
    const int lo = stickyTop;
    const int hi = e - K;
    if (hi < lo) return -1;

    auto scoreAt = [&](int off) -> double {
        double sum = 0.0;
        int cnt = 0;
        for (int i = 0; i < K; ++i) {
            const uint8_t* rc = canvas_.data() +
                static_cast<size_t>(canvasRows_ - K + i) * stride;
            const uint8_t* rf = frame.pixels.data() +
                static_cast<size_t>(off + i) * stride;
            for (int x = 0; x < stride; x += 8) {
                sum += std::abs(rc[x] - rf[x]) +
                       std::abs(rc[x + 1] - rf[x + 1]) +
                       std::abs(rc[x + 2] - rf[x + 2]);
                ++cnt;
            }
        }
        return cnt ? sum / cnt : 1e9;
    };

    // 第一遍：全局最优分
    double best = 1e9;
    for (int off = lo; off <= hi; ++off) {
        const double avg = scoreAt(off);
        if (avg < best) best = avg;
    }
    if (best > 30.0) {
        if (outScore) *outScore = best;
        return -1;
    }

    // 第二遍：收集所有与最优分接近的局部极小候选。
    // 微信聊天里"好的""收到"这类短消息会原样重复，画布尾在帧内可能有
    // 多个几乎一样好的匹配位置；旧逻辑取最靠上的，会把两段相同消息之间
    // 已拼过的内容整段再贴一遍（用户看到的大块重复正源于此）。
    const double margin = 3.0 + best * 0.10;
    struct Cand { int off; double avg; };
    std::vector<Cand> cands;
    for (int off = lo; off <= hi; ++off) {
        const double avg = scoreAt(off);
        if (avg > best + margin) continue;
        if (!cands.empty() && off - cands.back().off <= 2) {
            if (avg < cands.back().avg) cands.back() = { off, avg };
            continue;
        }
        cands.push_back({ off, avg });
    }

    int bestOff = cands.empty() ? -1 : cands[0].off;

    // 第三遍：多候选时用「加长上下文」消歧——相同短消息的上方邻居通常不同。
    // 核验通过的候选里取加长分最优；近似并列时偏向更大 off（更靠近最新内容，
    // 抗重复）；无法加长核验时退回最优分候选。
    if (cands.size() > 1) {
        const int K2 = (std::min)(96, canvasRows_ / 2);
        if (K2 >= K + 16) {
            double bestExt = 1e9;
            int bestExtOff = -1;
            for (const Cand& c : cands) {
                const int extStart = c.off - (K2 - K);
                if (extStart < stickyTop) {
#ifdef LC_DEBUG
                    std::fprintf(stdout, "  [align]   cand off=%d k=%.2f ext=NA(页眉)\n",
                                 c.off, c.avg);
#endif
                    continue; // 加长部分伸进页眉区，无法核验
                }
                double sum = 0.0;
                int cnt = 0;
                for (int i = 0; i < K2; ++i) {
                    const uint8_t* rc = canvas_.data() +
                        static_cast<size_t>(canvasRows_ - K2 + i) * stride;
                    const uint8_t* rf = frame.pixels.data() +
                        static_cast<size_t>(extStart + i) * stride;
                    for (int x = 0; x < stride; x += 8) {
                        sum += std::abs(rc[x] - rf[x]) +
                               std::abs(rc[x + 1] - rf[x + 1]) +
                               std::abs(rc[x + 2] - rf[x + 2]);
                        ++cnt;
                    }
                }
                const double ext = cnt ? sum / cnt : 1e9;
#ifdef LC_DEBUG
                std::fprintf(stdout, "  [align]   cand off=%d k=%.2f ext=%.2f\n",
                             c.off, c.avg, ext);
#endif
                if (ext < bestExt - 1.0 ||
                    (ext <= bestExt + 1.0 && c.off > bestExtOff)) {
                    bestExt = ext;
                    bestExtOff = c.off;
                }
            }
            if (bestExtOff >= 0) {
#ifdef LC_DEBUG
                std::fprintf(stdout, "  [align] disambiguate %zu cands -> off=%d ext=%.2f\n",
                             cands.size(), bestExtOff, bestExt);
#endif
                bestOff = bestExtOff;
            }
        }
    }

    if (outScore) *outScore = best;
    if (bestOff < 0) return -1;
    // 微信类应用：消息之间是大片纯色背景。画布尾若恰好落在纯背景上，
    // 背景行在帧内处处 avg≈0，严格最小值会锁定最靠上的背景位置，
    // 导致整段已拼内容被重复追加。对策：匹配区必须含足够多「有特征行」，
    // 纯背景对齐一律不采信（走保守路径，宁跳帧不重复）。
    if (!frame.distinctive.empty() && bestOff + K <= frame.h) {
        int distCount = 0;
        for (int i = 0; i < K; ++i) {
            if (frame.distinctive[static_cast<size_t>(bestOff + i)]) ++distCount;
        }
        if (distCount < (std::max)(4, K / 4)) {
#ifdef LC_DEBUG
            std::fprintf(stdout, "  [align] reject flat tail off=%d dist=%d K=%d\n",
                         bestOff, distCount, K);
#endif
            return -1;
        }
    }
    int s = bestOff + K;
    if (s < stickyTop) s = stickyTop;
#ifdef LC_DEBUG
    std::fprintf(stdout, "  [align] canvasRows=%d K=%d lo=%d hi=%d bestOff=%d best=%.2f -> s=%d (e=%d)\n",
                 canvasRows_, K, lo, hi, bestOff, best, s, e);
#endif
    return s;
}

// 帧里有多少内容已经在画布中（按行采样，在画布近段搜索）
double Stitcher::frameAlreadyOnCanvas(const FrameData& frame) const {
    if (canvasRows_ < 32 || frame.pixels.empty()) return 0.0;
    const int stride = width_ * 4;
    const int H = frame.h;
    const int lookback = (std::min)(canvasRows_, H * 3 + 200);
    int total = 0, hit = 0;
    const int stepY = (std::max)(4, H / 40);
    for (int y = 8; y < H - 8; y += stepY) {
        const uint8_t* rf = frame.pixels.data() + static_cast<size_t>(y) * stride;
        ++total;
        bool found = false;
        // 在画布近段粗搜
        for (int cy = canvasRows_ - lookback; cy < canvasRows_ && !found; cy += 4) {
            if (cy < 0) continue;
            const uint8_t* rc = canvas_.data() + static_cast<size_t>(cy) * stride;
            int diff = 0, cnt = 0;
            for (int x = 0; x < stride; x += 16) {
                diff += std::abs(rc[x] - rf[x]) +
                        std::abs(rc[x + 1] - rf[x + 1]) +
                        std::abs(rc[x + 2] - rf[x + 2]);
                ++cnt;
            }
            if (cnt > 0 && static_cast<double>(diff) / cnt < 22.0) {
                found = true;
            }
        }
        if (found) ++hit;
    }
    return total ? static_cast<double>(hit) / total : 0.0;
}

int Stitcher::findLastRowAlreadyOnCanvas(const FrameData& frame, int stickyTop, int e) const {
    if (canvasRows_ < 32 || frame.pixels.empty()) return -1;
    const int stride = width_ * 4;
    const int lookback = (std::min)(canvasRows_, frame.h * 3 + 200);
    for (int y = e - 1; y >= stickyTop; y -= 3) {
        const uint8_t* rf = frame.pixels.data() + static_cast<size_t>(y) * stride;
        for (int cy = canvasRows_ - lookback; cy < canvasRows_; cy += 3) {
            if (cy < 0) continue;
            const uint8_t* rc = canvas_.data() + static_cast<size_t>(cy) * stride;
            int diff = 0, cnt = 0;
            for (int x = 0; x < stride; x += 12) {
                diff += std::abs(rc[x] - rf[x]) +
                        std::abs(rc[x + 1] - rf[x + 1]) +
                        std::abs(rc[x + 2] - rf[x + 2]);
                ++cnt;
            }
            if (cnt > 0 && static_cast<double>(diff) / cnt < 20.0) {
                return y;
            }
        }
    }
    return -1;
}

bool Stitcher::rowOnCanvasStrict(const FrameData& frame, int y, int* outCanvasY) const {
    if (outCanvasY) *outCanvasY = -1;
    if (canvasRows_ < 16 || frame.pixels.empty()) return false;
    if (y < 1 || y + 1 >= frame.h) return false;
    const int stride = width_ * 4;
    const int lookback = (std::min)(canvasRows_, frame.h * 2 + 400);

    auto rowDiff = [&](const uint8_t* ra, const uint8_t* rb) -> double {
        double sum = 0;
        int cnt = 0;
        for (int x = 0; x < stride; x += 6) {
            sum += std::abs(ra[x] - rb[x]) +
                   std::abs(ra[x + 1] - rb[x + 1]) +
                   std::abs(ra[x + 2] - rb[x + 2]);
            ++cnt;
        }
        return cnt ? sum / cnt : 1e9;
    };

    const uint8_t* r1 = frame.pixels.data() + static_cast<size_t>(y) * stride;
    const uint8_t* r0 = frame.pixels.data() + static_cast<size_t>(y - 1) * stride;
    const uint8_t* r2 = frame.pixels.data() + static_cast<size_t>(y + 1) * stride;

    int bestCy = -1;
    double best = 1e9;
    // 从画布尾向头扫：相同内容在画布上出现多次时，优先命中「最新」位置——
    // 追加起点校正依赖它判断接缝是否正好接在画布末尾。
    for (int cy = canvasRows_ - 2; cy >= canvasRows_ - lookback; cy -= 2) {
        if (cy < 1 || cy + 1 >= canvasRows_) continue;
        const uint8_t* c1 = canvas_.data() + static_cast<size_t>(cy) * stride;
        const double d1 = rowDiff(r1, c1);
        if (d1 >= best) continue;
        // 邻行也要像，避免相似表格行误匹配
        const uint8_t* c0 = canvas_.data() + static_cast<size_t>(cy - 1) * stride;
        const uint8_t* c2 = canvas_.data() + static_cast<size_t>(cy + 1) * stride;
        const double d0 = rowDiff(r0, c0);
        const double d2 = rowDiff(r2, c2);
        if (d0 < 14.0 && d2 < 14.0 && d1 < 12.0) {
            best = d1;
            bestCy = cy;
        }
    }
    if (bestCy >= 0) {
        if (outCanvasY) *outCanvasY = bestCy;
        return true;
    }
    return false;
}

// 追加起点统一校正：对三条路径给出的 s 做最后把关。
// 抗重复推进：若 frame[s] 起的一段能在画布上「逐行连续」匹配、且一路延伸到
// 画布末尾，说明这段早已拼过（对齐被重复消息/空白骗到了），把 s 推进到重复区
// 之后。注意必须确认匹配区延伸到画布尾——若匹配中断在画布中段，说明帧里这段
// 是「新的重复内容」（用户又发了一遍同样的话），不能当重复吃掉。
// 抗跳行回退：若 s 上一行是有特征且不在画布上的新内容，说明 s 偏大、
// 接缝上方漏了新行，向后回退直到接上轨。
int Stitcher::correctAppendStart(const FrameData& frame, int s, int stickyTop, int e) const {
    if (s < 0 || frame.pixels.empty() || canvasRows_ < 24) return s;
    if (s < stickyTop) s = stickyTop;
    if (s > e) s = e;

    const int stride = width_ * 4;
    auto rowDiffAt = [&](int fy, int cy) -> double {
        const uint8_t* rf = frame.pixels.data() + static_cast<size_t>(fy) * stride;
        const uint8_t* rc = canvas_.data() + static_cast<size_t>(cy) * stride;
        double sum = 0.0;
        int cnt = 0;
        for (int x = 0; x < stride; x += 6) {
            sum += std::abs(rf[x] - rc[x]) +
                   std::abs(rf[x + 1] - rc[x + 1]) +
                   std::abs(rf[x + 2] - rc[x + 2]);
            ++cnt;
        }
        return cnt ? sum / cnt : 1e9;
    };
    // 直接比对画布最末几行（rowOnCanvasStrict 要三行上下文，够不到末行）
    auto onCanvasTail = [&](int fy) -> bool {
        for (int probe = canvasRows_ - 1; probe >= canvasRows_ - 3 && probe >= 0; --probe) {
            if (rowDiffAt(fy, probe) < 12.0) return true;
        }
        return false;
    };

    // 抗重复推进：假设「frame[s..] 是画布 [cy0..] 的复制品」，直接按假设位置
    // 逐行比对（不能用 rowOnCanvasStrict 逐行搜——重复区里的空白行会在画布上
    // 到处乱匹配，打断连续性判断；且它够不到画布末行，接缝级重复会漏网）。
    // 有特征行全部吻合、且匹配区一路延伸到画布末尾，才确认这段已拼过，
    // 把 s 推进到重复区之后。若匹配中途在有特征行上断裂，说明帧里这段是
    // 「新的重复内容」（用户又发了一遍同样的话），不能当重复吃掉。
    {
        int cy0 = -1;
        if (s < e) {
            for (int probe = canvasRows_ - 1; probe >= canvasRows_ - 6 && probe >= 0; --probe) {
                if (rowDiffAt(s, probe) < 12.0) {
                    cy0 = probe;
                    break;
                }
            }
            if (cy0 < 0) {
                rowOnCanvasStrict(frame, s, &cy0);
            }
        }
        if (cy0 >= 0) {
            int runLen = 0;
            bool brokeOnDistinctive = false;
            const int maxRun = (std::min)(e - s, canvasRows_ - cy0);
            for (int i = 0; i < maxRun; ++i) {
                const int fy = s + i;
                const double d = rowDiffAt(fy, cy0 + i);
                if (d < 12.0) {
                    runLen = i + 1;
                    continue;
                }
                const bool dist = !frame.distinctive.empty() &&
                                  frame.distinctive[static_cast<size_t>(fy)];
                if (dist) {
                    brokeOnDistinctive = true;
                    break;
                }
                // 空白行不吻合（重绘微差）：容忍，不延伸也不中断
            }
            if (!brokeOnDistinctive && runLen > 0 && cy0 + runLen >= canvasRows_ - 3) {
#ifdef LC_DEBUG
                if (runLen > 1) {
                    std::fprintf(stdout, "  [correct] anti-dup s %d -> %d (dup %d rows to tail)\n",
                                 s, s + runLen, runLen);
                }
#endif
                s += runLen;
            }
        }
    }

    // 抗跳行回退：s 上一行是有特征且不在画布上的新内容 → s 偏大，回退接上轨
    int guard = 0;
    while (s - 1 > stickyTop && guard < 60) {
        if (!frame.distinctive.empty() &&
            !frame.distinctive[static_cast<size_t>(s - 1)]) {
            break; // 空白行无法判读，停（宁可少退也不错退）
        }
        int cy = -1;
        if (rowOnCanvasStrict(frame, s - 1, &cy)) break; // 已接上轨
        if (onCanvasTail(s - 1)) break;                  // 已接上轨（末行直比）
        --s;
        ++guard;
    }
    if (s < stickyTop) s = stickyTop;
    return s;
}

int Stitcher::resolveAppendStart(const FrameData& frame, int stickyTop, int e,
                                 double* outScore, int anchorDy) const {
    if (outScore) *outScore = 1e9;
    if (e <= stickyTop || frame.pixels.empty() || canvasRows_ < 24) return -1;

    int s = -1;

    // 1) 画布尾部 ↔ 新帧 对齐：严格最优 + 多候选加长上下文消歧
    double sc = 1e9;
    const int sTail = alignCanvasTailToFrame(frame, stickyTop, e, &sc);
    if (sTail >= 0 && sc < 24.0) {
        s = (std::max)(stickyTop, (std::min)(e, sTail));
        if (outScore) *outScore = sc;
    }

    // 2) 纯背景尾（微信式页面）：内容定位不可用，但若画布尾恰为锚帧底部
    //    且本次位移已通过像素校验，可直接用位移推算追加起点
    if (s < 0 && anchorDy > 0 && tailAtAnchor_) {
        const int sd = e - anchorDy;
        if (sd >= stickyTop && sd < e) {
            s = sd;
            if (outScore) *outScore = 20.0;
        }
    }

    // 3) 底部必须是「新」的；接缝处必须能证明上方有「已在画布」的内容
    if (s < 0) {
        int cy = -1;
        if (rowOnCanvasStrict(frame, e - 2, &cy)) {
            // 底部已在画布 → 没有可靠新增
            return -1;
        }
        // 从下往上找最后一个「已在画布」的行
        int lastOn = -1;
        for (int y = e - 3; y > stickyTop + 2; y -= 2) {
            int c2 = -1;
            if (rowOnCanvasStrict(frame, y, &c2)) {
                // 连续两行都命中才可信
                int c3 = -1;
                if (y - 2 > stickyTop && rowOnCanvasStrict(frame, y - 2, &c3)) {
                    lastOn = y;
                    break;
                }
            }
        }
        if (lastOn > stickyTop + 2) {
            // 仅当新增区较小且紧贴已对齐尾部时才采信回溯结果；
            // 增量过大会把已上画布的内容整段重复贴一遍，宁可提示重对齐。
            if (e - lastOn >= 2 && e - lastOn <= 50) {
                s = lastOn + 1;
                if (outScore) *outScore = 22.0;
            }
        }
    }

    if (s < 0) return -1;
    return correctAppendStart(frame, s, stickyTop, e);
}

Event Stitcher::appendNewFrom(const FrameData& frame, int s, int e, bool gap) {
    if (e <= s) return Event::NoChange;
    // 注：历史上这里有个"若 frame[s] 与画布末行几乎相同就向后跳"的保险丝（最多 8 行）。
    // 它会在画布内容里制造文档坐标空洞，导致加长上下文核验对真正确的候选误判；
    // 接缝级重复现由 correctAppendStart 的抗重复推进统一处理，故移除。
    if (e - s < kMinNewRows) return Event::NoChange;
#ifdef LC_DEBUG
    std::fprintf(stdout, "  [appendNewFrom] s=%d e=%d gap=%d canvasRows_before=%d\n", s, e, (int)gap, canvasRows_);
#endif
    if (gap) appendSeparator();
    if (!appendChecked(frame, s, e)) return Event::Capped;
    trimDuplicateTailMut();
    accEnd_ = canvasRows_;
    docOffset_ = (std::max)(0, canvasRows_ - (e - s));
    return gap ? Event::Gap : Event::Appended;
}

int Stitcher::trimDuplicateTailMut() {
    if (canvasRows_ < 32 || width_ <= 0) return 0;
    const int stride = width_ * 4;
    int removed = 0;
    // 只裁「高度几乎相同」的大块重复；阈值收紧，避免误删相似表格行
    // 128 档：微信等大片空白页面可能一次贴出 >64 行的重复段
    const int sizes[] = { 128, 64, 40 };
    for (int bi = 0; bi < 3; ++bi) {
        const int B = sizes[bi];
        if (canvasRows_ < B * 2 + 16) continue;
        for (int pass = 0; pass < 2; ++pass) {
            if (canvasRows_ < B * 2 + 16) break;
            double best = 1e9;
            int bestGap = 0;
            for (int gap = B; gap <= (std::min)(B * 3, canvasRows_ - B - 8); gap += 4) {
                double sum = 0.0;
                int cnt = 0;
                for (int i = 0; i < B; i += 2) {
                    const uint8_t* ra = canvas_.data() +
                        static_cast<size_t>(canvasRows_ - B + i) * stride;
                    const uint8_t* rb = canvas_.data() +
                        static_cast<size_t>(canvasRows_ - gap - B + i) * stride;
                    for (int x = 0; x < stride; x += 8) {
                        sum += std::abs(ra[x] - rb[x]) +
                               std::abs(ra[x + 1] - rb[x + 1]) +
                               std::abs(ra[x + 2] - rb[x + 2]);
                        ++cnt;
                    }
                }
                const double avg = cnt ? sum / cnt : 1e9;
                if (avg < best) {
                    best = avg;
                    bestGap = gap;
                }
            }
            // 只裁「逐像素相同」的大块重复；阈值收到 1.0：
            // 真实重复(同内容再贴一遍)是像素级相同 avg≈0；周期性条纹/相似表格行 avg 通常 1~5，
            // 旧阈值 8.0 会误裁周期性内容并级联成错位。
            if (bestGap > 0 && best < 1.0) {
                // 边界判别：周期性内容（斑马条纹/表格行）的"上方"会同样匹配，
                // 真正重复接缝的"上方"则不一致。周期性 → 不裁，避免吞掉真实内容。
                const int aboveAvail = (std::min)(
                    (std::min)(canvasRows_ - B - 1, canvasRows_ - bestGap - B - 1), 8);
                bool periodic = false;
                if (aboveAvail > 0) {
                    double aboveSum = 0.0;
                    int aboveCnt = 0;
                    for (int i = 1; i <= aboveAvail; ++i) {
                        const uint8_t* ra = canvas_.data() +
                            static_cast<size_t>(canvasRows_ - B - i) * stride;
                        const uint8_t* rb = canvas_.data() +
                            static_cast<size_t>(canvasRows_ - bestGap - B - i) * stride;
                        for (int x = 0; x < stride; x += 8) {
                            aboveSum += std::abs(ra[x] - rb[x]) +
                                        std::abs(ra[x + 1] - rb[x + 1]) +
                                        std::abs(ra[x + 2] - rb[x + 2]);
                            ++aboveCnt;
                        }
                    }
                    const double aboveAvg = aboveCnt ? aboveSum / aboveCnt : 1e9;
                    // 上方也匹配(周期性) → 不裁；上方明显断裂(真正接缝) → 裁
                    if (aboveAvg < 15.0) periodic = true;
                }
                if (periodic) {
#ifdef LC_DEBUG
                    std::fprintf(stdout, "  [trim] periodic skip B=%d gap=%d\n", B, bestGap);
#endif
                    break;
                }
                canvasRows_ -= B;
                const size_t nb = static_cast<size_t>(canvasRows_) * stride;
                if (canvas_.size() > nb) canvas_.resize(nb);
#ifdef LC_DEBUG
                std::fprintf(stdout, "  [trim] removed B=%d gap=%d canvasRows=%d\n", B, bestGap, canvasRows_);
#endif
                removed += B;
            } else {
                break;
            }
        }
    }
    return removed;
}

// 在 [lo, hi) 内找与画布尾部最吻合的 s（用于新帧坐标）
static int FindBestSeamS(const uint8_t* canvas, int canvasRows, int width,
                         const FrameData& frame, int lo, int hi, int N,
                         double* outScore) {
    if (outScore) *outScore = 1e9;
    if (N < 8 || canvasRows < N || frame.pixels.empty()) return lo;
    int best = lo;
    double bestScore = 1e9;
    for (int s = lo; s < hi; ++s) {
        const double sc = SeamScore(canvas, canvasRows, width, frame, s, N);
        if (sc < bestScore) {
            bestScore = sc;
            best = s;
        }
    }
    if (outScore) *outScore = bestScore;
    return best;
}

bool Stitcher::appendChecked(const FrameData& src, int startRow, int endRow) {
    if (src.pixels.empty() || src.w != width_) return false;
    if (endRow <= startRow) return true;
    const int H = src.h;
    if (startRow < 0) startRow = 0;
    if (endRow > H) endRow = H;
    if (endRow <= startRow) return true;

    const int add = endRow - startRow;
    if (canvasRows_ + add > maxRows_) {
        const int room = maxRows_ - canvasRows_;
        if (room <= 0) {
            capped_ = true;
            return false;
        }
        endRow = startRow + room;
        capped_ = true;
    }

    const int stride = width_ * 4;
    const size_t oldBytes = static_cast<size_t>(canvasRows_) * stride;
    const size_t addBytes = static_cast<size_t>(endRow - startRow) * stride;
    if (canvas_.size() < oldBytes + addBytes) {
        canvas_.resize(oldBytes + addBytes);
    }
    std::memcpy(canvas_.data() + oldBytes,
                src.pixels.data() + static_cast<size_t>(startRow) * stride,
                addBytes);
    canvasRows_ += (endRow - startRow);
    return !capped_ || (endRow > startRow);
}

void Stitcher::appendSeparator() {
    if (width_ <= 0) return;
    const int stride = width_ * 4;
    if (canvasRows_ + kSeparatorRows > maxRows_) {
        capped_ = true;
        return;
    }
    const size_t oldBytes = static_cast<size_t>(canvasRows_) * stride;
    canvas_.resize(oldBytes + static_cast<size_t>(kSeparatorRows) * stride);
    uint8_t* base = canvas_.data() + oldBytes;
    // 前 4 行白，后 4 行浅灰 225
    for (int y = 0; y < 4; ++y) {
        std::memset(base + static_cast<size_t>(y) * stride, 255, static_cast<size_t>(stride));
        // alpha
        for (int x = 0; x < width_; ++x) {
            base[static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4 + 3] = 255;
        }
    }
    for (int y = 4; y < kSeparatorRows; ++y) {
        uint8_t* row = base + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width_; ++x) {
            row[x * 4 + 0] = 225;
            row[x * 4 + 1] = 225;
            row[x * 4 + 2] = 225;
            row[x * 4 + 3] = 255;
        }
    }
    canvasRows_ += kSeparatorRows;
}

bool Stitcher::endsWithSeparator() const {
    if (canvasRows_ < kSeparatorRows || width_ <= 0) return false;
    const int stride = width_ * 4;
    const uint8_t* row = canvas_.data() + static_cast<size_t>(canvasRows_ - 1) * stride;
    return row[0] == 225 && row[1] == 225 && row[2] == 225;
}

void Stitcher::trimStickyFooter(int stickyBottom, const FrameData& frame) {
    if (stickyBottom <= 0 || canvasRows_ <= stickyBottom || !hasAnchor_) return;
    // 确认画布尾部几行等于该页脚
    const int stride = width_ * 4;
    int matchCount = 0;
    int check = (std::min)(stickyBottom, 24);
    for (int i = 0; i < check; ++i) {
        const int canvasY = canvasRows_ - check + i;
        // 页脚对应当前帧底部
        const int fy = frame.h - check + i;
        if (fy < 0 || fy >= frame.h) continue;
        const uint8_t* rc = canvas_.data() + static_cast<size_t>(canvasY) * stride;
        const uint8_t* rf = frame.pixels.data() + static_cast<size_t>(fy) * stride;
        int same = 0, total = 0;
        for (int x = 0; x < stride; x += 8) {
            ++total;
            if (std::abs(rc[x] - rf[x]) <= 3 &&
                std::abs(rc[x + 1] - rf[x + 1]) <= 3 &&
                std::abs(rc[x + 2] - rf[x + 2]) <= 3) {
                ++same;
            }
        }
        if (total > 0 && static_cast<double>(same) / total >= 0.90) ++matchCount;
    }
    if (check > 0 && matchCount >= static_cast<int>(check * 0.75)) {
        // 从画布尾部删掉 stickyBottom 行（不超过现有行）
        const int cut = (std::min)(stickyBottom, canvasRows_);
        canvasRows_ -= cut;
        const size_t newBytes = static_cast<size_t>(canvasRows_) * stride;
        if (canvas_.size() > newBytes) canvas_.resize(newBytes);
        if (accEnd_ > docOffset_ + canvasRows_) {
            // 保守：不改 docOffset，下次追加会重算
        }
    }
}

Event Stitcher::Process(const FrameData& frame, double nowSec) {
    if (frame.w != width_ || frame.h <= 0 || frame.pixels.empty()) {
        return Event::Invalid;
    }
    if (capped_) return Event::Capped;

    if (!started_) {
        hasAnchor_ = true;
        anchor_ = frame;
        anchorTime_ = nowSec;
        stickyTop_ = 0;
        stickyBottom_ = 0;
        docOffset_ = 0;
        accEnd_ = frame.h;
        tailAtAnchor_ = true;
        canvasRows_ = 0;
        // 首帧整段入画布（不含未知 sticky）
        if (!appendChecked(frame, 0, frame.h)) {
            started_ = true;
            return Event::Capped;
        }
        started_ = true;
        return Event::Started;
    }

    // 锚点老化
    if (hasAnchor_ && (nowSec - anchorTime_) > kAnchorAgeLimit) {
        if (framesSimilar(anchor_, frame)) {
            anchor_ = frame;
            anchorTime_ = nowSec;
            return Event::NoChange;
        }
        anchor_ = frame;
        anchorTime_ = nowSec;
        tailAtAnchor_ = false; // 画布尾与新锚帧的对应关系已不可信
        return Event::Skipped;
    }

    const MatchResult m = detect(anchor_, frame);

    if (m.kind == MatchResult::Kind::NoChange) {
        anchorTime_ = nowSec;
        return Event::NoChange;
    }

    // 不稳定帧（撕裂/懒加载重排/新消息插入）：不拼（撕裂线会被烙进画布），
    // 默认也不污染锚点——撕裂是瞬时的，下一帧仍应与旧锚点比较，恢复零成本。
    // 连续 3 帧不稳定说明是持续性变化（插入后内容重排），接受它当新锚点。
    if (m.unstable) {
        ++skipAlignCount_;
        ++suspectSeams_;
        ++unstableStreak_;
        if (unstableStreak_ >= 3) {
            anchor_ = frame;
            anchorTime_ = nowSec;
            tailAtAnchor_ = false;
            unstableStreak_ = 0;
        }
        return Event::NeedOverlap;
    }
    unstableStreak_ = 0;

    if (m.kind == MatchResult::Kind::Unknown ||
        m.kind == MatchResult::Kind::Scrolled) {
        if (m.kind == MatchResult::Kind::Scrolled && m.dy < 0) {
            if (m.stickyTop > 0) stickyTop_ = m.stickyTop;
            if (m.stickyBottom > 0) stickyBottom_ = m.stickyBottom;
            docOffset_ += m.dy;
            if (docOffset_ < 0) docOffset_ = 0;
            anchor_ = frame;
            anchorTime_ = nowSec;
            tailAtAnchor_ = false; // 内容下移，画布尾不再对应锚帧底部
            return Event::ScrolledUp;
        }
        if (m.kind == MatchResult::Kind::Scrolled && m.dy == 0) {
            anchorTime_ = nowSec;
            return Event::NoChange;
        }
        // Unknown（detect 失败）时用相似度兜底；Scrolled 已确认滚动，不再短路，
        // 否则慢滚/低对比页会被误判 NoChange 漏帧。
        if (m.kind == MatchResult::Kind::Unknown && framesSimilar(anchor_, frame)) {
            anchorTime_ = nowSec;
            return Event::NoChange;
        }
        // sticky 状态：Scrolled 时无条件刷新（清除陈旧的页脚判定，避免内容滚到底仍被裁）；
        // Unknown 时只采纳正向检出，不因检测失败而清零已有判定。
        if (m.kind == MatchResult::Kind::Scrolled) {
            stickyTop_ = m.stickyTop;
            stickyBottom_ = m.stickyBottom;
        } else {
            if (m.stickyTop > 0) stickyTop_ = m.stickyTop;
            if (m.stickyBottom > 0) stickyBottom_ = m.stickyBottom;
        }
        if (m.stickyBottom > 0) {
            // 只要检出固定页脚就裁掉画布尾部的页脚行（不限于 Scrolled）：
            // 首帧整段入画布会带入页脚，未裁则后续 tail-align 在内容区找不到含页脚的尾部。
            trimStickyFooter(m.stickyBottom, frame);
        }
        const int st = stickyTop_;
        const int e = frame.h - stickyBottom_;
        if (e <= st) {
            anchor_ = frame;
            anchorTime_ = nowSec;
            return Event::Skipped;
        }

        // 核心：必须能证明「画布末尾」落在新帧哪里，否则不追加
        double sc = 1e9;
        const int s = resolveAppendStart(frame, st, e, &sc, m.dy);
        if (s < 0) {
            ++skipAlignCount_;
            ++suspectSeams_;
            tailAtAnchor_ = false;
            anchor_ = frame;
            anchorTime_ = nowSec;
            return Event::NeedOverlap;
        }

        Event ev = appendNewFrom(frame, s, e, false);
        if (ev == Event::NoChange) {
            anchor_ = frame;
            anchorTime_ = nowSec;
            return Event::NoChange;
        }
        // 对齐成功后清零失败计数，避免状态栏一直显示「未对齐」
        if (ev == Event::Appended || ev == Event::Gap) {
            skipAlignCount_ = 0;
        }
        // 只有正常追加后画布尾才恰好对应锚帧底部（Gap 会插入分隔条，不算）
        tailAtAnchor_ = (ev == Event::Appended);
        hasAnchor_ = true;
        anchor_ = frame;
        anchorTime_ = nowSec;
        return ev;
    }

    // 不可达
    anchor_ = frame;
    anchorTime_ = nowSec;
    return Event::Skipped;
}

bool Stitcher::Finish(const FrameData* last,
                      std::vector<uint8_t>& outBgra,
                      int& outW, int& outH) {
    outW = width_;
    outH = 0;
    outBgra.clear();

    if (last && last->w == width_ && last->h > 0 && !last->pixels.empty()) {
        if (!started_) {
            Process(*last, 0.0);
        } else {
            // 最后一帧：仅在能证明对齐时补全，避免收尾再贴一遍
            Process(*last, 1e9);
            const int st = stickyTop_;
            const int e = last->h - stickyBottom_;
            if (e > st && canvasRows_ > 0) {
                double sc = 1e9;
                const int s = resolveAppendStart(*last, st, e, &sc, 0);
                if (s >= 0 && e - s >= kMinNewRows) {
                    appendChecked(*last, s, e);
                    trimDuplicateTailMut();
                    skipAlignCount_ = 0;
                }
                // 页脚：仅当尾部不像页脚时补
                if (stickyBottom_ > 0 && last->h >= stickyBottom_) {
                    const int footStart = last->h - stickyBottom_;
                    const int stride2 = width_ * 4;
                    bool tailIsFoot = false;
                    if (canvasRows_ >= stickyBottom_) {
                        const uint8_t* tail = canvas_.data() +
                            static_cast<size_t>(canvasRows_ - stickyBottom_) * stride2;
                        const uint8_t* foot = last->pixels.data() +
                            static_cast<size_t>(footStart) * stride2;
                        int same = 0, tot = 0;
                        for (int x = 0; x < stride2; x += 8) {
                            ++tot;
                            if (std::abs(tail[x] - foot[x]) <= 3 &&
                                std::abs(tail[x + 1] - foot[x + 1]) <= 3 &&
                                std::abs(tail[x + 2] - foot[x + 2]) <= 3) {
                                ++same;
                            }
                        }
                        tailIsFoot = tot > 0 && same * 10 >= tot * 9;
                    }
                    if (!tailIsFoot && canvasRows_ > 0) {
                        // 页脚也可能已存在，用严格行匹配再确认
                        int cy = -1;
                        if (!rowOnCanvasStrict(*last, footStart + 1, &cy)) {
                            appendChecked(*last, footStart, last->h);
                        }
                    }
                }
                trimDuplicateTailMut();
            }
        }
    }

    if (canvasRows_ <= 0 || width_ <= 0) return false;
    const int stride = width_ * 4;
    const size_t bytes = static_cast<size_t>(canvasRows_) * stride;
    if (canvas_.size() < bytes) return false;
    // 不可变快照
    outBgra.resize(bytes);
    std::memcpy(outBgra.data(), canvas_.data(), bytes);
    outW = width_;
    outH = canvasRows_;
    return true;
}

} // namespace longstitch
