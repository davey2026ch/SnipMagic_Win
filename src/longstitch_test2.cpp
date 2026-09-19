// 长截图算法 · 地面真值回归测试（无 GUI）
// 模型：一个确定性的“文档”（Doc），帧 = 文档在 phase 处的可视窗口（可含固定页眉/页脚）。
// 完美拼接 = 画布行应与文档行一一对应（单调、不缺失、不重复）。
// 编译：cl /EHsc /std:c++17 /utf-8 /O2 longstitch_test2.cpp longstitch.cpp
#include "longstitch.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <unordered_map>

using namespace longstitch;

// ---------------- 文档模型 ----------------
struct Doc {
    int w = 0;
    std::vector<uint8_t> px;   // rows * w * 4
    int rows() const { return w ? static_cast<int>(px.size() / (static_cast<size_t>(w) * 4)) : 0; }
    const uint8_t* row(int y) const { return px.data() + static_cast<size_t>(y) * w * 4; }
};

enum class Style { Text, Paragraphs, Stripes, Mixed };

static uint32_t HashRow(const uint8_t* p, size_t bytes) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < bytes; i += 3) {
        h ^= p[i]; h *= 16777619u;
        h ^= p[(i + 1) % bytes]; h *= 16777619u;
    }
    return h;
}

static bool RowUniform(const uint8_t* p, size_t bytes) {
    for (size_t i = 4; i < bytes; i += 4) {
        if (p[i] != p[0] || p[i + 1] != p[1] || p[i + 2] != p[2]) return false;
    }
    return true;
}

Doc MakeDoc(int w, int rows, Style style, unsigned seed) {
    Doc d;
    d.w = w;
    d.px.resize(static_cast<size_t>(w) * rows * 4);
    std::mt19937 rng(seed);
    // 段落块布局（Paragraphs/Mixed 用）
    std::vector<std::pair<int, int>> blocks; // {start, len}
    {
        int y = 0;
        while (y < rows) {
            int len = 24 + static_cast<int>(rng() % 40);
            blocks.push_back({ y, len });
            y += len + 14 + static_cast<int>(rng() % 26); // 段间空白
        }
    }
    for (int y = 0; y < rows; ++y) {
        uint8_t* p = d.px.data() + static_cast<size_t>(y) * w * 4;
        bool inBlock = false;
        for (auto& b : blocks) if (y >= b.first && y < b.first + b.second) { inBlock = true; break; }
        switch (style) {
        case Style::Text: {
            for (int x = 0; x < w; ++x) {
                uint32_t n = static_cast<uint32_t>(y) * 2654435761u + static_cast<uint32_t>(x) * 40503u;
                n ^= n >> 13; n *= 1274126177u;
                uint8_t v = static_cast<uint8_t>(n & 0xFF);
                if ((y % 29) < 4) v = static_cast<uint8_t>(30 + (y & 3));   // 横条也带 docY 噪声，避免跨行像素一致
                else if ((y % 41) < 3) v = static_cast<uint8_t>(230 - (y & 1));
                if ((y % 11) < 6 && (x % 17) < 3) v = static_cast<uint8_t>(255 - v);
                p[x * 4 + 0] = v;
                p[x * 4 + 1] = static_cast<uint8_t>((v * 3) / 4 + (x & 7) + (y & 3));
                p[x * 4 + 2] = static_cast<uint8_t>(255 - v);
                p[x * 4 + 3] = 255;
            }
            break;
        }
        case Style::Paragraphs: {
            if (!inBlock) {
                for (int x = 0; x < w; ++x) {
                    p[x * 4 + 0] = 250; p[x * 4 + 1] = 250; p[x * 4 + 2] = 250; p[x * 4 + 3] = 255;
                }
            } else {
                int ly = y - blocks[0].first; // unused
                (void)ly;
                int localStart = 0;
                for (auto& b : blocks) if (y >= b.first && y < b.first + b.second) { localStart = b.first; break; }
                int ly2 = y - localStart;
                for (int x = 0; x < w; ++x) {
                    uint32_t n = static_cast<uint32_t>(y) * 2654435761u + static_cast<uint32_t>(x) * 40503u;
                    n ^= n >> 13; n *= 1274126177u;
                    uint8_t v = static_cast<uint8_t>(n & 0xFF);
                    if ((ly2 % 13) < 2) v = 235; // 行间距
                    if (v < 120 && (x % 19) < 2) v = static_cast<uint8_t>(255 - v);
                    p[x * 4 + 0] = v;
                    p[x * 4 + 1] = static_cast<uint8_t>((v * 3) / 4 + (x & 7) + (y & 3));
                    p[x * 4 + 2] = static_cast<uint8_t>(255 - v);
                    p[x * 4 + 3] = 255;
                }
            }
            break;
        }
        case Style::Stripes: {
            // 周期 32 的斑马条纹（表格/列表常见），但每行叠加 docY 噪声使其唯一，
            // 真实页面里条纹背景上总有文字/图标打破周期性。
            for (int x = 0; x < w; ++x) {
                uint8_t v;
                if ((y % 97) == 50) v = static_cast<uint8_t>(20 + (y & 7));
                else if (((y / 32) % 2) == 0) v = static_cast<uint8_t>(248 - (y & 3));
                else v = static_cast<uint8_t>(228 - (y & 3));
                if ((x % 64) < 2) v = static_cast<uint8_t>(v - 30);
                // 叠加 docY 相关细噪声，让同周期行也彼此不同
                uint32_t n = static_cast<uint32_t>(y) * 2246822519u + static_cast<uint32_t>(x) * 374761393u;
                n ^= n >> 13;
                v = static_cast<uint8_t>(v + (n & 3) - 1);
                p[x * 4 + 0] = v;
                p[x * 4 + 1] = v;
                p[x * 4 + 2] = v;
                p[x * 4 + 3] = 255;
            }
            break;
        }
        case Style::Mixed: {
            // 前半文本、中段条纹+文本混排、后段段落
            Style s = (y < rows / 3) ? Style::Text
                    : (y < (2 * rows) / 3) ? Style::Stripes
                    : Style::Paragraphs;
            if (s == Style::Text) {
                for (int x = 0; x < w; ++x) {
                    uint32_t n = static_cast<uint32_t>(y) * 2654435761u + static_cast<uint32_t>(x) * 40503u;
                    n ^= n >> 13; n *= 1274126177u;
                    uint8_t v = static_cast<uint8_t>(n & 0xFF);
                    if ((y % 29) < 4) v = static_cast<uint8_t>(30 + (y & 3));
                    if ((y % 11) < 6 && (x % 17) < 3) v = static_cast<uint8_t>(255 - v);
                    p[x * 4 + 0] = v;
                    p[x * 4 + 1] = static_cast<uint8_t>((v * 3) / 4 + (x & 7) + (y & 3));
                    p[x * 4 + 2] = static_cast<uint8_t>(255 - v);
                    p[x * 4 + 3] = 255;
                }
            } else if (s == Style::Stripes) {
                for (int x = 0; x < w; ++x) {
                    uint8_t v = (((y / 32) % 2) == 0) ? static_cast<uint8_t>(248 - (y & 3)) : static_cast<uint8_t>(228 - (y & 3));
                    if ((x % 64) < 2) v = static_cast<uint8_t>(v - 30);
                    uint32_t n = static_cast<uint32_t>(y) * 2246822519u + static_cast<uint32_t>(x) * 374761393u;
                    n ^= n >> 13;
                    v = static_cast<uint8_t>(v + (n & 3) - 1);
                    p[x * 4 + 0] = v; p[x * 4 + 1] = v; p[x * 4 + 2] = v; p[x * 4 + 3] = 255;
                }
            } else {
                if (!inBlock) {
                    for (int x = 0; x < w; ++x) { p[x * 4 + 0] = 250; p[x * 4 + 1] = 250; p[x * 4 + 2] = 250; p[x * 4 + 3] = 255; }
                } else {
                    int localStart = 0;
                    for (auto& b : blocks) if (y >= b.first && y < b.first + b.second) { localStart = b.first; break; }
                    int ly2 = y - localStart;
                    for (int x = 0; x < w; ++x) {
                        uint32_t n = static_cast<uint32_t>(y) * 2654435761u + static_cast<uint32_t>(x) * 40503u;
                        n ^= n >> 13; n *= 1274126177u;
                        uint8_t v = static_cast<uint8_t>(n & 0xFF);
                        if ((ly2 % 13) < 2) v = 235;
                        if (v < 120 && (x % 19) < 2) v = static_cast<uint8_t>(255 - v);
                        p[x * 4 + 0] = v;
                        p[x * 4 + 1] = static_cast<uint8_t>((v * 3) / 4 + (x & 7));
                        p[x * 4 + 2] = static_cast<uint8_t>(255 - v);
                        p[x * 4 + 3] = 255;
                    }
                }
            }
            break;
        }
        }
    }
    return d;
}

// ---------------- 帧构造 ----------------
struct FrameView {
    const Doc* doc = nullptr;
    int Hh = 0;   // 页眉高度（固定）
    int Ff = 0;   // 页脚高度（固定）
    int h = 0;    // 帧高
    int hdrSeed = 7, ftrSeed = 13;

    FrameData Make(int phase, unsigned noiseSeed, int noiseAmp) const {
        std::vector<uint8_t> buf(static_cast<size_t>(doc->w) * h * 4);
        std::mt19937 rng(noiseSeed);
        const int contentTop = Hh;
        const int contentBottom = h - Ff;
        for (int y = 0; y < h; ++y) {
            uint8_t* dst = buf.data() + static_cast<size_t>(y) * doc->w * 4;
            if (y < contentTop) {
                // 固定页眉：独特纹理
                for (int x = 0; x < doc->w; ++x) {
                    uint32_t n = hdrSeed * 2654435761u + static_cast<uint32_t>(y) * 97u + static_cast<uint32_t>(x) * 40503u;
                    n ^= n >> 13; n *= 1274126177u;
                    uint8_t v = static_cast<uint8_t>(n & 0xFF);
                    if (v > 200) v = 200;
                    dst[x * 4 + 0] = v; dst[x * 4 + 1] = v; dst[x * 4 + 2] = v; dst[x * 4 + 3] = 255;
                }
            } else if (y >= contentBottom) {
                for (int x = 0; x < doc->w; ++x) {
                    uint32_t n = ftrSeed * 2246822519u + static_cast<uint32_t>(y) * 131u + static_cast<uint32_t>(x) * 40503u;
                    n ^= n >> 13; n *= 1274126177u;
                    uint8_t v = static_cast<uint8_t>(n & 0xFF);
                    if (v < 60) v = 60;
                    dst[x * 4 + 0] = v; dst[x * 4 + 1] = v; dst[x * 4 + 2] = v; dst[x * 4 + 3] = 255;
                }
            } else {
                int dy = phase + (y - contentTop);
                if (dy < doc->rows()) {
                    std::memcpy(dst, doc->row(dy), static_cast<size_t>(doc->w) * 4);
                } else {
                    for (int x = 0; x < doc->w; ++x) { dst[x * 4 + 0] = 252; dst[x * 4 + 1] = 252; dst[x * 4 + 2] = 252; dst[x * 4 + 3] = 255; }
                }
            }
            if (noiseAmp > 0) {
                for (int x = 0; x < doc->w * 4; ++x) {
                    int nv = (static_cast<int>(dst[x]) + static_cast<int>(rng() % (2 * noiseAmp + 1)) - noiseAmp);
                    if (nv < 0) nv = 0; if (nv > 255) nv = 255;
                    dst[x] = static_cast<uint8_t>(nv);
                }
            }
        }
        return MakeFrame(buf.data(), doc->w, h);
    }
};

// ---------------- 评估 ----------------
struct Metrics {
    int missingText = 0;    // 应见但缺失的非空白文档行
    int duplicatedText = 0; // 重复出现的非空白文档行
    int unknownRows = 0;    // 无法映射的画布行（接缝损坏）
    int canvasRows = 0;
    int expectedContent = 0;
    int needOverlap = 0;
    int monotonicBreaks = 0;
    int maxRunMissing = 0;
};

Metrics Evaluate(const Doc& doc, const uint8_t* canvas, int canvasRows, const FrameView& fv,
                 const std::vector<int>& phases, Style style) {
    Metrics m;
    m.canvasRows = canvasRows;
    const int w = doc.w;
    const int stride = w * 4;
    const int contentH = fv.h - fv.Hh - fv.Ff;
    int maxSeen = 0;
    for (int p : phases) maxSeen = (std::max)(maxSeen, p + contentH);
    if (maxSeen > doc.rows()) maxSeen = doc.rows();
    m.expectedContent = maxSeen;

    // doc 行哈希（只对“独特”行建立索引；纯色/周期行排除在严格检查外）
    std::unordered_map<uint32_t, std::vector<int>> docIndex;
    std::vector<uint8_t> isUniform(doc.rows(), 0);
    for (int y = 0; y < doc.rows(); ++y) {
        const uint8_t* r = doc.row(y);
        bool uni = RowUniform(r, stride);
        isUniform[y] = uni ? 1 : 0;
        if (!uni) {
            uint32_t hsh = HashRow(r, stride);
            docIndex[hsh].push_back(y);
        }
    }
    std::vector<uint8_t> covered(doc.rows(), 0);
    int lastDoc = -1;
    int runMissing = 0;
    // 按位置严格核对：画布第 r 行（内容区）应等于 doc[r - Hh]。
    // 这是比哈希映射更直接的地面真值，能避开周期性内容的哈希碰撞误判。
    int posMismatch = 0;
    int posMaxRun = 0;
    int posRun = 0;
    const int contentStart = fv.Hh;
    const int contentEnd = (std::min)(canvasRows, canvasRows - fv.Ff);
    for (int r = contentStart; r < contentEnd; ++r) {
        const int expected = r - fv.Hh;
        if (expected < 0 || expected >= doc.rows()) { posRun = 0; continue; }
        const uint8_t* cr = canvas + static_cast<size_t>(r) * stride;
        const uint8_t* dr = doc.row(expected);
        if (RowUniform(cr, stride) && RowUniform(dr, stride)) { posRun = 0; continue; }
        double s = 0; int c = 0;
        for (int x = 0; x < stride; x += 8) {
            s += std::abs(cr[x] - dr[x]) + std::abs(cr[x+1] - dr[x+1]) + std::abs(cr[x+2] - dr[x+2]);
            ++c;
        }
        const double avg = c ? s / c : 1e9;
        if (avg > 6.0) {
            ++posMismatch;
            ++posRun;
            posMaxRun = (std::max)(posMaxRun, posRun);
        } else {
            posRun = 0;
        }
    }
    m.missingText = posMismatch;
    m.maxRunMissing = posMaxRun;
    // 画布行 r（内容区从 Hh 起）→ 文档行哈希映射（用于检测重复/乱序）
    for (int r = fv.Hh; r < canvasRows - fv.Ff && r < canvasRows; ++r) {
        const uint8_t* cr = canvas + static_cast<size_t>(r) * stride;
        if (RowUniform(cr, stride)) continue; // 空白/纯色行不计
        uint32_t hsh = HashRow(cr, stride);
        auto it = docIndex.find(hsh);
        if (it == docIndex.end()) {
            // 模糊匹配（抗噪）：在期望位置附近找均差最小的文档行
            double bestD = 1e9; int bestY = -1;
            int center = r - fv.Hh;
            int lo = (std::max)(0, center - 400), hi = (std::min)(doc.rows() - 1, center + 400);
            for (int y = lo; y <= hi; ++y) {
                if (isUniform[y]) continue;
                const uint8_t* dr = doc.row(y);
                double s = 0; int c = 0;
                for (int x = 0; x < stride; x += 12) { s += std::abs(dr[x] - cr[x]); ++c; }
                if (c && s / c < bestD) { bestD = s / c; bestY = y; }
            }
            if (bestY >= 0 && bestD < 8.0) {
                covered[bestY]++;
                if (lastDoc >= 0 && bestY < lastDoc) ++m.monotonicBreaks;
                if (style == Style::Text) lastDoc = bestY;
            } else {
                ++m.unknownRows;
            }
        } else {
            // 选最接近 lastDoc+1 的候选（解决哈希碰撞歧义）
            int bestY = it->second[0];
            if (it->second.size() > 1 && lastDoc >= 0) {
                int bestDist = 1 << 30;
                for (int y : it->second) {
                    int dist = std::abs(y - (lastDoc + 1));
                    if (dist < bestDist) { bestDist = dist; bestY = y; }
                }
            }
            covered[bestY]++;
            if (lastDoc >= 0 && bestY < lastDoc) ++m.monotonicBreaks;
            if (style == Style::Text) lastDoc = bestY;
        }
    }
    for (int y = 0; y < maxSeen; ++y) {
        if (isUniform[y]) continue;
        if (covered[y] == 0) {
            ++m.missingText;
            ++runMissing;
            m.maxRunMissing = (std::max)(m.maxRunMissing, runMissing);
        } else {
            runMissing = 0;
            if (covered[y] >= 2) ++m.duplicatedText;
        }
    }
    return m;
}

struct Scenario {
    std::string name;
    Style style;
    int Hh, Ff;               // 页眉/页脚高度
    std::vector<int> phases;   // 滚动轨迹
    int noiseAmp = 0;
    int expectMaxMissing = 0;
    int expectMaxDup = 0;
};

int g_fail = 0;

void RunScenario(const Scenario& sc, int w, int h, int docRows, unsigned seed, bool verbose) {
    Doc doc = MakeDoc(w, docRows, sc.style, seed);
    FrameView fv;
    fv.doc = &doc; fv.Hh = sc.Hh; fv.Ff = sc.Ff; fv.h = h;
    Stitcher st(w);
    double t = 0.0;
    int needOverlap = 0, evStarted = 0, evAppended = 0, evNoChange = 0,
        evSkipped = 0, evScrolledUp = 0, evGap = 0, evCapped = 0;
    unsigned ns = seed * 7 + 3;
    int prevRows = 0;
    bool trace = (sc.name.find("A3") != std::string::npos) ||
                 (sc.name.find("D2") != std::string::npos);
    for (size_t i = 0; i < sc.phases.size(); ++i) {
        auto fr = fv.Make(sc.phases[i], ns + static_cast<unsigned>(i), sc.noiseAmp);
        t += 0.15;
        Event e = st.Process(fr, t);
        int nowRows = st.CanvasRows();
        if (trace && i < 12) {
            std::printf("    frame %zu phase=%d ev=%d rows %d->%d (+%d)\n",
                        i, sc.phases[i], static_cast<int>(e), prevRows, nowRows, nowRows - prevRows);
        }
        prevRows = nowRows;
        switch (e) {
        case Event::NeedOverlap: ++needOverlap; break;
        case Event::Started: ++evStarted; break;
        case Event::Appended: ++evAppended; break;
        case Event::NoChange: ++evNoChange; break;
        case Event::Skipped: ++evSkipped; break;
        case Event::ScrolledUp: ++evScrolledUp; break;
        case Event::Gap: ++evGap; break;
        case Event::Capped: ++evCapped; break;
        default: break;
        }
    }
    // Finish：末帧再补一次
    auto lastFr = fv.Make(sc.phases.back(), ns + 9999, sc.noiseAmp);
    std::vector<uint8_t> out;
    int ow = 0, oh = 0;
    bool ok = st.Finish(&lastFr, out, ow, oh);
    Metrics m;
    m.needOverlap = needOverlap;
    if (ok && ow == w) {
        m = Evaluate(doc, out.data(), oh, fv, sc.phases, sc.style);
        m.needOverlap = needOverlap;
    } else {
        m.canvasRows = -1;
    }
    std::printf("  [%s] rows=%d expect=%d missing=%d maxRun=%d dup=%d unknown=%d monoBreak=%d needOv=%d%s\n",
                sc.name.c_str(), m.canvasRows, m.expectedContent + sc.Hh + sc.Ff,
                m.missingText, m.maxRunMissing, m.duplicatedText, m.unknownRows,
                m.monotonicBreaks, m.needOverlap,
                (m.missingText <= sc.expectMaxMissing && m.duplicatedText <= sc.expectMaxDup && m.unknownRows == 0)
                    ? "" : "   <<<< FAIL");
    std::printf("      events: Started=%d Appended=%d NoChange=%d Skipped=%d ScrolledUp=%d Gap=%d needOv=%d\n",
                evStarted, evAppended, evNoChange, evSkipped, evScrolledUp, evGap, needOverlap);
    if (m.missingText > sc.expectMaxMissing || m.duplicatedText > sc.expectMaxDup || m.unknownRows > 0) ++g_fail;
}

int main() {
    const int W = 320, H = 220, DocRows = 4000;

    std::printf("== A. 基础滚动 ==\n");
    {
        Scenario sc = { "A1 匀速偶数偏移", Style::Text, 0, 0, {}, 0 };
        for (int p = 0; p + (H) <= 2600; p += 60) sc.phases.push_back(p);
        RunScenario(sc, W, H, DocRows, 11, true);
    }
    {
        Scenario sc = { "A2 匀速奇数偏移", Style::Text, 0, 0, {}, 0 };
        sc.phases.push_back(0);
        int p = 0;
        for (int i = 0; i < 43; ++i) { p += 37 + (i % 3); sc.phases.push_back(p); }
        RunScenario(sc, W, H, DocRows, 12, true);
    }
    {
        Scenario sc = { "A3 极慢滚动(3px)", Style::Text, 0, 0, {}, 0 };
        int p = 0;
        for (int i = 0; i < 60; ++i) { sc.phases.push_back(p); p += 3; }
        RunScenario(sc, W, H, DocRows, 13, true);
    }

    std::printf("== B. 页眉页脚 ==\n");
    {
        Scenario sc = { "B1 固定页眉+页脚", Style::Text, 26, 22, {}, 0 };
        for (int p = 0; p + (H - 48) <= 2600; p += 55) sc.phases.push_back(p);
        RunScenario(sc, W, H, DocRows, 21, true);
    }
    {
        Scenario sc = { "B2 页眉页脚+奇数偏移", Style::Text, 26, 22, {}, 0 };
        sc.phases.push_back(0);
        int p = 0;
        for (int i = 0; i < 40; ++i) { p += 41 + (i % 4); sc.phases.push_back(p); }
        RunScenario(sc, W, H, DocRows, 22, true);
    }

    std::printf("== C. 复杂轨迹 ==\n");
    {
        Scenario sc = { "C1 快速滚动(150px/帧)", Style::Text, 0, 0, {}, 0 };
        int p = 0;
        for (int i = 0; i < 16; ++i) { sc.phases.push_back(p); p += 150; }
        RunScenario(sc, W, H, DocRows, 31, true);
    }
    {
        Scenario sc = { "C2 来回滚动", Style::Text, 0, 0, {}, 0 };
        int p = 0;
        for (int i = 0; i < 30; ++i) {
            p += 50;
            sc.phases.push_back(p);
        }
        // 回滚
        for (int i = 0; i < 8; ++i) { p -= 40; sc.phases.push_back(p); }
        // 再前进超过
        for (int i = 0; i < 20; ++i) { p += 45; sc.phases.push_back(p); }
        RunScenario(sc, W, H, DocRows, 32, true);
    }
    {
        Scenario sc = { "C3 停顿+继续", Style::Text, 0, 0, {}, 0 };
        int p = 0;
        for (int i = 0; i < 10; ++i) { sc.phases.push_back(p); p += 50; }
        for (int i = 0; i < 6; ++i) sc.phases.push_back(p); // 停住
        for (int i = 0; i < 10; ++i) { p += 33; sc.phases.push_back(p); }
        for (int i = 0; i < 4; ++i) sc.phases.push_back(p); // 停住
        RunScenario(sc, W, H, DocRows, 33, true);
    }

    std::printf("== D. 内容形态 ==\n");
    {
        Scenario sc = { "D1 段落+大片空白", Style::Paragraphs, 0, 0, {}, 0 };
        int p = 0;
        for (int i = 0; i < 45; ++i) { sc.phases.push_back(p); p += 47; }
        RunScenario(sc, W, H, DocRows, 41, true);
    }
    {
        Scenario sc = { "D2 斑马条纹(周期32)", Style::Stripes, 0, 0, {}, 0 };
        int p = 0;
        for (int i = 0; i < 40; ++i) { sc.phases.push_back(p); p += 55; }
        RunScenario(sc, W, H, DocRows, 42, true);
    }
    {
        Scenario sc = { "D3 混合内容", Style::Mixed, 0, 0, {}, 0 };
        int p = 0;
        for (int i = 0; i < 50; ++i) { sc.phases.push_back(p); p += 51; }
        RunScenario(sc, W, H, DocRows, 43, true);
    }

    std::printf("== E. 抗噪 ==\n");
    {
        Scenario sc = { "E1 轻噪声(±2)", Style::Text, 0, 0, {}, 2, 2, 0 };
        int p = 0;
        for (int i = 0; i < 35; ++i) { sc.phases.push_back(p); p += 43; }
        RunScenario(sc, W, H, DocRows, 51, true);
    }

    std::printf("\n%s (%d scenario failures)\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
