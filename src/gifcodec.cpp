#include "gifcodec.h"
#include <cstring>
#include <cstddef>
#include <cstdio>

using namespace Gdiplus;

namespace gifcodec {
namespace {

// ------------------------------------------------------------------
// 直方图：RGB 各取高 5 位 → 32768 个格子
// ------------------------------------------------------------------
constexpr int kHistSize = 32768;

struct HistCell {
    uint32_t n;      // 像素数
    uint32_t rs, gs, bs; // 分量和
};

inline int BucketOf(int r, int g, int b) {
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}
inline uint8_t Expand5(int v) {
    return static_cast<uint8_t>((v * 255 + 15) / 31);
}

// ------------------------------------------------------------------
// 位写入（GIF 的 LZW 是低位先出）
// ------------------------------------------------------------------
struct BitWriter {
    std::vector<uint8_t>& out;
    uint32_t acc = 0;
    int nbits = 0;
    explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}
    void Put(int code, int n) {
        acc |= static_cast<uint32_t>(code) << nbits;
        nbits += n;
        while (nbits >= 8) {
            out.push_back(static_cast<uint8_t>(acc & 0xFF));
            acc >>= 8;
            nbits -= 8;
        }
    }
    void Flush() {
        if (nbits > 0) {
            out.push_back(static_cast<uint8_t>(acc & 0xFF));
            acc = 0;
            nbits = 0;
        }
    }
};

struct BitReader {
    const std::vector<uint8_t>& in;
    size_t pos = 0;      // 以 bit 计
    explicit BitReader(const std::vector<uint8_t>& v) : in(v) {}
    bool Get(int n, int& code) {
        if (pos + static_cast<size_t>(n) > in.size() * 8) return false;
        uint32_t v = 0;
        for (int k = 0; k < n; ++k) {
            size_t bit = pos + k;
            if (in[bit >> 3] & (1u << (bit & 7))) v |= (1u << k);
        }
        pos += static_cast<size_t>(n);
        code = static_cast<int>(v);
        return true;
    }
};

// ------------------------------------------------------------------
// LZW 字典（开放寻址，容量 8192 足够放 4096 个码）
// ------------------------------------------------------------------
struct CodeTable {
    static constexpr int N = 8192;
    int32_t key[N];
    int16_t val[N];
    void Reset() {
        for (int i = 0; i < N; ++i) key[i] = -1;
    }
    static int Hash(int32_t k) {
        return static_cast<int>((static_cast<uint32_t>(k) * 2654435761u) >> 19) & (N - 1);
    }
    int Find(int32_t k) const {
        int i = Hash(k);
        for (;;) {
            if (key[i] == k) return val[i];
            if (key[i] == -1) return -1;
            i = (i + 1) & (N - 1);
        }
    }
    void Insert(int32_t k, int v) {
        int i = Hash(k);
        while (key[i] != -1 && key[i] != k) i = (i + 1) & (N - 1);
        key[i] = k;
        val[i] = static_cast<int16_t>(v);
    }
};

void LzwEncode(const uint8_t* idx, size_t n, int minCodeSize, std::vector<uint8_t>& out) {
    out.clear();
    out.push_back(static_cast<uint8_t>(minCodeSize));

    std::vector<uint8_t> bits;
    bits.reserve(n / 3 + 64);
    BitWriter bw(bits);

    const int clearCode = 1 << minCodeSize;
    const int eoiCode = clearCode + 1;
    int nextCode = eoiCode + 1;
    int codeSize = minCodeSize + 1;

    CodeTable table;
    table.Reset();
    bw.Put(clearCode, codeSize);

    if (n > 0) {
        int prefix = idx[0];
        for (size_t i = 1; i < n; ++i) {
            const int c = idx[i];
            const int32_t key = (static_cast<int32_t>(prefix) << 8) | c;
            const int found = table.Find(key);
            if (found >= 0) {
                prefix = found;
                continue;
            }
            bw.Put(prefix, codeSize);
            if (nextCode < 4096) {
                table.Insert(key, nextCode);
                ++nextCode;
                // 码长升位：GIF 标准（compress.c 的 free_ent > maxcode）是在「下一个待分配码
                // 超过当前位宽能表示的最大值」时升位。nextCode 此时指向下一个待分配码，
                // 因此要等到 nextCode == 2^codeSize + 1 才升，早一步就会和所有标准解码器错位。
                if (nextCode == (1 << codeSize) + 1 && codeSize < 12) ++codeSize;
            } else {
                bw.Put(clearCode, codeSize);
                table.Reset();
                nextCode = eoiCode + 1;
                codeSize = minCodeSize + 1;
            }
            prefix = c;
        }
        bw.Put(prefix, codeSize);
    }
    bw.Put(eoiCode, codeSize);
    bw.Flush();

    // GIF 数据按 ≤255 字节的子块存放，末尾用 0 长度块结束
    size_t pos = 0;
    while (pos < bits.size()) {
        const size_t chunk = (std::min<size_t>)(255, bits.size() - pos);
        out.push_back(static_cast<uint8_t>(chunk));
        out.insert(out.end(), bits.begin() + static_cast<std::ptrdiff_t>(pos),
                   bits.begin() + static_cast<std::ptrdiff_t>(pos + chunk));
        pos += chunk;
    }
    out.push_back(0);
}

bool LzwDecode(const std::vector<uint8_t>& data, size_t expected, std::vector<uint8_t>& out);

// ------------------------------------------------------------------
// 中位切分量化：把画面压到 ≤256 色
// ------------------------------------------------------------------
struct ColorBox {
    std::vector<int> buckets;   // 指向 used 列表下标（实际存的是 15bit 格子号）
    uint64_t count = 0;
    int lo[3] = {31, 31, 31};   // 5bit 空间最小/最大
    int hi[3] = {0, 0, 0};
};

void BoxBounds(ColorBox& b, const HistCell* hist) {
    b.count = 0;
    b.lo[0] = b.lo[1] = b.lo[2] = 31;
    b.hi[0] = b.hi[1] = b.hi[2] = 0;
    for (int bucket : b.buckets) {
        const int r = (bucket >> 10) & 31, g = (bucket >> 5) & 31, bl = bucket & 31;
        b.count += hist[bucket].n;
        if (r < b.lo[0]) b.lo[0] = r;
        if (r > b.hi[0]) b.hi[0] = r;
        if (g < b.lo[1]) b.lo[1] = g;
        if (g > b.hi[1]) b.hi[1] = g;
        if (bl < b.lo[2]) b.lo[2] = bl;
        if (bl > b.hi[2]) b.hi[2] = bl;
    }
}

} // namespace

int Anim::TotalMs() const {
    int t = 0;
    for (const auto& f : frames) t += (f.delayMs > 0 ? f.delayMs : 40);
    return t;
}

size_t Anim::MemoryBytes() const {
    size_t n = 0;
    for (const auto& f : frames) n += f.data.size() + f.palette.size() * 4 + 32;
    return n;
}

int Anim::AvgDelayMs() const {
    if (frames.empty()) return 40;
    long long t = 0;
    for (const auto& f : frames) t += (f.delayMs > 0 ? f.delayMs : 40);
    return static_cast<int>(t / static_cast<long long>(frames.size()));
}

bool DecodeIndices(const Frame& f, int w, int h, std::vector<uint8_t>& indices) {
    if (w <= 0 || h <= 0 || f.data.empty()) return false;
    return LzwDecode(f.data, static_cast<size_t>(w) * h, indices);
}

std::unique_ptr<Bitmap> Anim::Decode(int idx) const {
    if (Empty()) return nullptr;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(frames.size())) idx = static_cast<int>(frames.size()) - 1;
    const Frame& f = frames[static_cast<size_t>(idx)];

    std::vector<uint8_t> indices;
    if (!DecodeIndices(f, width, height, indices)) return nullptr;
    if (indices.size() < static_cast<size_t>(width) * height) return nullptr;

    auto bmp = std::make_unique<Bitmap>(width, height, PixelFormat32bppARGB);
    if (!bmp || bmp->GetLastStatus() != Ok) return nullptr;

    Rect rc(0, 0, width, height);
    BitmapData bd;
    if (bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &bd) != Ok) return nullptr;

    // 调色板先转成 32 位 ARGB 常量，避免内层循环里做位运算
    uint32_t pal[256] = {};
    const int palN = static_cast<int>(f.palette.size());
    for (int i = 0; i < 256; ++i) {
        uint32_t c = (i < palN) ? f.palette[static_cast<size_t>(i)] : 0u;
        pal[i] = 0xFF000000u | (c & 0x00FFFFFFu);
    }

    for (int y = 0; y < height; ++y) {
        auto* dst = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(bd.Scan0) + y * bd.Stride);
        const uint8_t* src = indices.data() + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) dst[x] = pal[src[x]];
    }
    bmp->UnlockBits(&bd);
    return bmp;
}

bool CompressFrame(const uint8_t* bgra, int w, int h, int delayMs, Frame& out) {
    if (!bgra || w <= 0 || h <= 0) return false;

    std::vector<HistCell> hist(kHistSize);
    memset(hist.data(), 0, sizeof(HistCell) * kHistSize);

    const size_t total = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < total; ++i) {
        const uint8_t* p = bgra + i * 4;
        const int r = p[2], g = p[1], b = p[0];
        HistCell& c = hist[BucketOf(r, g, b)];
        ++c.n;
        c.rs += static_cast<uint32_t>(r);
        c.gs += static_cast<uint32_t>(g);
        c.bs += static_cast<uint32_t>(b);
    }

    std::vector<int> used;
    used.reserve(1024);
    for (int i = 0; i < kHistSize; ++i) {
        if (hist[i].n) used.push_back(i);
    }
    if (used.empty()) return false;

    // 中位切分
    std::vector<ColorBox> boxes;
    boxes.reserve(300);
    ColorBox first;
    first.buckets = used;
    BoxBounds(first, hist.data());
    boxes.push_back(std::move(first));

    const int target = 256;
    while (static_cast<int>(boxes.size()) < target) {
        // 选样本最多且还能再切的盒子
        int pick = -1;
        uint64_t best = 0;
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].buckets.size() < 2) continue;
            if (boxes[i].count > best) {
                best = boxes[i].count;
                pick = static_cast<int>(i);
            }
        }
        if (pick < 0) break;

        ColorBox& b = boxes[static_cast<size_t>(pick)];
        // 沿跨度最大的通道切
        int axis = 0, span = -1;
        for (int a = 0; a < 3; ++a) {
            const int s = b.hi[a] - b.lo[a];
            if (s > span) { span = s; axis = a; }
        }
        if (span <= 0) break;
        const int shift = (axis == 0) ? 10 : (axis == 1) ? 5 : 0;
        std::sort(b.buckets.begin(), b.buckets.end(), [shift](int x, int y) {
            return ((x >> shift) & 31) < ((y >> shift) & 31);
        });
        // 按样本数中位切
        uint64_t half = b.count / 2, acc = 0;
        size_t cut = 1;
        for (size_t i = 0; i < b.buckets.size(); ++i) {
            acc += hist[b.buckets[i]].n;
            if (acc >= half && i + 1 < b.buckets.size()) { cut = i + 1; break; }
            cut = i + 1;
        }
        if (cut == 0) cut = 1;
        if (cut >= b.buckets.size()) cut = b.buckets.size() - 1;

        ColorBox b2;
        b2.buckets.assign(b.buckets.begin() + static_cast<std::ptrdiff_t>(cut), b.buckets.end());
        b.buckets.erase(b.buckets.begin() + static_cast<std::ptrdiff_t>(cut), b.buckets.end());
        BoxBounds(b, hist.data());
        BoxBounds(b2, hist.data());
        boxes.push_back(std::move(b2));
    }

    // 每个盒子出一个调色板色（加权平均），同时把盒子内的格子映射到该色
    const int nColors = static_cast<int>(boxes.size());
    int tableSize = 4;
    int minCodeSize = 2;
    while (tableSize < nColors) { tableSize <<= 1; ++minCodeSize; }
    if (tableSize > 256) { tableSize = 256; minCodeSize = 8; }

    std::vector<uint32_t> palette(static_cast<size_t>(tableSize), 0u);
    std::vector<uint8_t> lut(kHistSize, 0);

    for (int i = 0; i < nColors && i < tableSize; ++i) {
        const ColorBox& b = boxes[static_cast<size_t>(i)];
        uint64_t n = 0, rs = 0, gs = 0, bs = 0;
        for (int bucket : b.buckets) {
            const HistCell& c = hist[bucket];
            n += c.n; rs += c.rs; gs += c.gs; bs += c.bs;
            lut[bucket] = static_cast<uint8_t>(i);
        }
        if (n == 0) continue;
        const int r = static_cast<int>(rs / n);
        const int g = static_cast<int>(gs / n);
        const int bl = static_cast<int>(bs / n);
        palette[static_cast<size_t>(i)] =
            (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
            static_cast<uint32_t>(bl);
    }

    // 像素 → 索引
    std::vector<uint8_t> idx(total);
    const uint8_t* src = bgra;
    for (size_t i = 0; i < total; ++i) {
        const uint8_t* p = src + i * 4;
        idx[i] = lut[BucketOf(p[2], p[1], p[0])];
    }

    out.palette = std::move(palette);
    out.minCodeSize = minCodeSize;
    out.delayMs = (delayMs > 0 ? delayMs : 40);
    LzwEncode(idx.data(), idx.size(), minCodeSize, out.data);
    return true;
}

namespace {

bool LzwDecode(const std::vector<uint8_t>& data, size_t expected, std::vector<uint8_t>& out) {
    if (data.size() < 2) return false;
    const int minCodeSize = data[0];
    if (minCodeSize < 2 || minCodeSize > 8) return false;

    // 拼接子块
    std::vector<uint8_t> bits;
    size_t p = 1;
    while (p < data.size()) {
        const int n = data[p++];
        if (n == 0) break;
        if (p + static_cast<size_t>(n) > data.size()) return false;
        bits.insert(bits.end(), data.begin() + static_cast<std::ptrdiff_t>(p),
                    data.begin() + static_cast<std::ptrdiff_t>(p + n));
        p += static_cast<size_t>(n);
    }
    if (bits.empty()) return false;

    const int clearCode = 1 << minCodeSize;
    const int eoiCode = clearCode + 1;
    int codeSize = minCodeSize + 1;
    int next = eoiCode + 1;

    std::vector<int> prefix(4096, 0);
    std::vector<uint8_t> suffix(4096, 0);
    for (int i = 0; i < clearCode; ++i) {
        prefix[static_cast<size_t>(i)] = 0;
        suffix[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    }

    BitReader br(bits);
    out.clear();
    out.reserve(expected);

    std::vector<uint8_t> stack;
    stack.reserve(4096);
    int old = -1;
    int first = 0;

    for (;;) {
        int code = 0;
        if (!br.Get(codeSize, code)) break;
        if (code == clearCode) {
            codeSize = minCodeSize + 1;
            next = eoiCode + 1;
            old = -1;
            continue;
        }
        if (code == eoiCode) break;

        if (old < 0) {
            if (code >= clearCode) return false;
            out.push_back(suffix[static_cast<size_t>(code)]);
            first = code;
            old = code;
            continue;
        }

        int cur = code;
        stack.clear();
        if (code >= next) {
            if (code != next) return false; // 非法码
            stack.push_back(static_cast<uint8_t>(first));
            cur = old;
        }
        while (cur >= clearCode) {
            stack.push_back(suffix[static_cast<size_t>(cur)]);
            cur = prefix[static_cast<size_t>(cur)];
            if (stack.size() > 4096) return false;
        }
        stack.push_back(suffix[static_cast<size_t>(cur)]);
        first = stack.back();

        for (size_t i = stack.size(); i > 0; --i) out.push_back(stack[i - 1]);

        if (next < 4096) {
            prefix[static_cast<size_t>(next)] = old;
            suffix[static_cast<size_t>(next)] = static_cast<uint8_t>(first);
            ++next;
            // 解码端字典比编码端晚一项，故 next 到达 2^codeSize 时升位，与编码端对上
            if (next == (1 << codeSize) && codeSize < 12) ++codeSize;
        }
        old = code;

        if (expected && out.size() >= expected) break;
    }
    return !out.empty();
}

} // namespace

bool WriteGifToMemory(const Anim& anim, std::vector<uint8_t>& out) {
    out.clear();
    if (anim.Empty()) return false;
    const int w = anim.width, h = anim.height;

    out.reserve(1024 + anim.MemoryBytes());

    auto put8 = [&](int v) { out.push_back(static_cast<uint8_t>(v & 0xFF)); };
    auto put16 = [&](int v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    };
    auto puts = [&](const char* s) {
        for (const char* p = s; *p; ++p) out.push_back(static_cast<uint8_t>(*p));
    };

    puts("GIF89a");
    put16(w);
    put16(h);
    // 全局调色板：固定 256 色（内容取第一帧，纯属兼容性兜底），每帧再用局部调色板
    put8(0xF7);   // 0b1111_0111: GCT 存在 + 8 位色深 + GCT 大小 256
    put8(0);      // 背景色索引
    put8(0);      // 像素宽高比
    {
        const std::vector<uint32_t>& pal = anim.frames.front().palette;
        for (int i = 0; i < 256; ++i) {
            const uint32_t c = (i < static_cast<int>(pal.size())) ? pal[static_cast<size_t>(i)] : 0u;
            put8(static_cast<int>((c >> 16) & 0xFF));
            put8(static_cast<int>((c >> 8) & 0xFF));
            put8(static_cast<int>(c & 0xFF));
        }
    }
    if (anim.loop) {
        put8(0x21); put8(0xFF); put8(11);
        puts("NETSCAPE2.0");
        put8(3); put8(1); put16(0);  // 循环次数 0 = 无限
        put8(0);
    }

    for (const Frame& f : anim.frames) {
        int delayCs = (f.delayMs > 0 ? f.delayMs : 40) / 10;
        if (delayCs < 2) delayCs = 2;      // <2 会被部分播放器当成 100ms
        if (delayCs > 1000) delayCs = 1000;
        // 图形控制块：处置方式 1（保留画面）、无透明色
        put8(0x21); put8(0xF9); put8(4);
        put8(0x04);
        put16(delayCs);
        put8(0);
        put8(0);
        // 图像描述符
        put8(0x2C);
        put16(0); put16(0); put16(w); put16(h);
        int bits = f.minCodeSize;
        if (bits < 2) bits = 2;
        if (bits > 8) bits = 8;
        put8(0x80 | (bits - 1));         // 局部调色板存在 + 大小 2^bits
        const int palN = 1 << bits;
        for (int i = 0; i < palN; ++i) {
            const uint32_t c = (i < static_cast<int>(f.palette.size()))
                                   ? f.palette[static_cast<size_t>(i)] : 0u;
            put8(static_cast<int>((c >> 16) & 0xFF));
            put8(static_cast<int>((c >> 8) & 0xFF));
            put8(static_cast<int>(c & 0xFF));
        }
        out.insert(out.end(), f.data.begin(), f.data.end());
    }
    put8(0x3B);
    return true;
}

bool WriteGif(const std::wstring& path, const Anim& anim) {
    std::vector<uint8_t> bytes;
    if (!WriteGifToMemory(anim, bytes)) return false;
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || !fp) return false;
    const size_t n = fwrite(bytes.data(), 1, bytes.size(), fp);
    fclose(fp);
    return n == bytes.size();
}

} // namespace gifcodec
