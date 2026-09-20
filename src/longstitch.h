#pragma once
#include "util.h"
#include <cstdint>
#include <vector>

// 长截图拼接算法层：纯字节运算，不依赖窗口/抓帧 API。
// 像素格式：紧凑 BGRA，top-down，stride = w * 4。
namespace longstitch {

struct FrameData {
    std::vector<uint8_t> pixels;     // w * h * 4 BGRA
    int w = 0;
    int h = 0;
    std::vector<int32_t> sig;        // h * 16
    std::vector<uint8_t> distinctive; // h
    int samplesPerRow = 0;
};

enum class Event {
    Started,
    Appended,
    NoChange,
    ScrolledUp,
    Gap,
    Skipped,
    Capped,
    Invalid,
    NeedOverlap  // 对齐失败：不追加，提示用户回滚重建重叠
};

// 从原始 BGRA 缓冲构建帧（计算行签名）
FrameData MakeFrame(const uint8_t* bgra, int w, int h);

class Stitcher {
public:
    explicit Stitcher(int width);

    Event Process(const FrameData& frame, double nowSec);

    bool Finish(const FrameData* last,
                std::vector<uint8_t>& outBgra,
                int& outW, int& outH);

    int GapCount() const { return gapCount_; }
    int SuspectSeamCount() const { return suspectSeams_; }
    int SkipAlignCount() const { return skipAlignCount_; }
    int CanvasRows() const { return canvasRows_; }
    bool Capped() const { return capped_; }
    int Width() const { return width_; }

private:
    struct MatchResult {
        enum class Kind { NoChange, Scrolled, Unknown } kind = Kind::Unknown;
        int dy = 0;
        int stickyTop = 0;
        int stickyBottom = 0;
        // 帧内位移不一致（撕裂/重排/新消息插入）：不可拼，且不应污染锚点
        bool unstable = false;
    };

    struct RatioOut {
        double ratio = 0.0;
        int used = 0;
        int matched = 0;
    };

    int stickyBand(const FrameData& a, const FrameData& b, bool top) const;
    MatchResult detect(const FrameData& anchor, const FrameData& frame) const;
    RatioOut ratioAtEx(const FrameData& a, const FrameData& b, int dy,
                       int top, int bottom) const;
    bool verifyPixels(const FrameData& a, const FrameData& b, int dy,
                      int top, int bottom) const;
    double overlapSad(const FrameData& a, const FrameData& b, int dy,
                      int top, int bottom) const;
    int refineDy(const FrameData& anchor, const FrameData& frame, int dy,
                 int top, int bottom) const;
    // 严格判定：从哪一行开始是「画布里还没有」的新内容；失败返回 -1
    // anchorDy：detect() 得到的锚帧位移（>0 有效），用于纯背景尾的航位推算
    int resolveAppendStart(const FrameData& frame, int stickyTop, int e,
                           double* outScore, int anchorDy) const;
    // 追加起点统一校正：抗重复推进 + 抗跳行回退（微信式重复消息/大片空白场景）
    int correctAppendStart(const FrameData& frame, int s, int stickyTop, int e) const;
    bool rowOnCanvasStrict(const FrameData& frame, int y, int* outCanvasY) const;
    int alignCanvasTailToFrame(const FrameData& frame, int stickyTop, int e,
                               double* outScore) const;
    double frameAlreadyOnCanvas(const FrameData& frame) const;
    int findLastRowAlreadyOnCanvas(const FrameData& frame, int stickyTop, int e) const;
    int trimDuplicateTailMut();
    bool framesSimilar(const FrameData& a, const FrameData& b) const;
    Event appendNewFrom(const FrameData& frame, int s, int e, bool gap);
    bool appendChecked(const FrameData& src, int startRow, int endRow);
    void appendSeparator();
    bool endsWithSeparator() const;
    void trimStickyFooter(int stickyBottom, const FrameData& frame);

    int width_ = 0;
    int maxRows_ = 0;
    std::vector<uint8_t> canvas_; // width * canvasRows * 4
    int canvasRows_ = 0;
    int accEnd_ = 0;              // 画布底部对应的文档坐标
    int docOffset_ = 0;           // 当前帧第 0 行对应文档坐标
    FrameData anchor_;
    bool hasAnchor_ = false;
    double anchorTime_ = 0.0;
    int stickyTop_ = 0;
    int stickyBottom_ = 0;
    int gapCount_ = 0;
    int suspectSeams_ = 0;
    int skipAlignCount_ = 0;
    int unstableStreak_ = 0;   // 连续不稳定帧计数：瞬时撕裂不污染锚点，持续变化则接受新现实
    double gapCooldownUntil_ = 0.0;
    bool capped_ = false;
    bool started_ = false;
    // 画布末尾是否恰为锚帧 [e-K, e) 内容（追加成功后为真；上滚/跳帧后失效）
    bool tailAtAnchor_ = false;
};

} // namespace longstitch
