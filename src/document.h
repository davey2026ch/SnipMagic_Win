#pragma once
#include "util.h"
#include "annotation.h"
#include "gifcodec.h"

// One sheet / tab = one screenshot + annotations
class Document {
public:
    int id = 0;
    std::wstring name; // P1, P2...
    std::unique_ptr<Gdiplus::Bitmap> base; // original capture (physical pixels)
    std::vector<std::unique_ptr<Annotation>> annotations;

    // 动图页签（GIF 录制结果）：非空时本页签是 GIF，base 只用于缩略显示（当前帧）
    std::shared_ptr<gifcodec::Anim> gif;

    // GIF 播放视图状态（每页签独立；由画布维护）
    int gifFrame = 0;              // 当前帧
    bool gifPlaying = false;       // 播放中（默认不自动播放，只有点播放按钮才播）
    bool gifLoop = false;          // 循环播放（默认关：点播放只播一次）
    float gifZoom = 1.0f;          // 预览缩放倍数（1 = 适应可视区）
    float gifPanX = 0.0f;          // 预览平移（相对居中位置，屏幕像素）
    float gifPanY = 0.0f;
    int gifStripScroll = 0;        // 缩略图条横向滚动量
    unsigned long long gifNextTick = 0; // 下一帧的时间点（GetTickCount64）
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> gifThumbs; // 缩略图缓存（懒加载）

    // Undo / redo snapshots
    struct Snapshot {
        std::vector<std::unique_ptr<Annotation>> annotations;
        std::unique_ptr<Gdiplus::Bitmap> base;
    };
    std::vector<Snapshot> undoStack;
    std::vector<Snapshot> redoStack;
    static const size_t kMaxUndo = 100;

    // Runtime
    float zoom = 1.0f; // 1.0 = 100%
    int scrollX = 0, scrollY = 0;
    int selectedIdx = -1;

    // Rubber-band region selection (image coords) from Select tool
    bool hasRegion = false;
    float regionL = 0, regionT = 0, regionR = 0, regionB = 0;

    void SetRegion(float x1, float y1, float x2, float y2) {
        hasRegion = true;
        regionL = (std::min)(x1, x2);
        regionT = (std::min)(y1, y2);
        regionR = (std::max)(x1, x2);
        regionB = (std::max)(y1, y2);
    }
    void ClearRegion() { hasRegion = false; }
    bool GetRegion(int& x, int& y, int& w, int& h) const {
        if (!hasRegion) return false;
        x = static_cast<int>(std::floor(regionL));
        y = static_cast<int>(std::floor(regionT));
        w = static_cast<int>(std::ceil(regionR - regionL));
        h = static_cast<int>(std::ceil(regionB - regionT));
        return w > 0 && h > 0;
    }

    explicit Document(std::unique_ptr<Gdiplus::Bitmap> bmp, int documentId, std::wstring docName)
        : id(documentId), name(std::move(docName)), base(std::move(bmp)) {}

    // GIF 动图页签：base 取第一帧，便于画布/页签按普通文档处理尺寸
    Document(std::shared_ptr<gifcodec::Anim> anim, int documentId, std::wstring docName);

    bool IsGif() const { return gif && !gif->Empty(); }
    int GifFrameCount() const { return gif ? static_cast<int>(gif->frames.size()) : 0; }
    // 把当前帧写回 base（切帧时调用，界面与保存用的都是 base 的合成结果）
    void SyncGifBaseToCurrentFrame();
    // 取当前帧位图（独立副本，调用方拥有）
    std::unique_ptr<Gdiplus::Bitmap> CurrentGifFrame() const;

    int Width() const  { return base ? static_cast<int>(base->GetWidth())  : 0; }
    int Height() const { return base ? static_cast<int>(base->GetHeight()) : 0; }

    void PushUndo();
    void Undo();
    void Redo();
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }

    void ClearSelection();
    Annotation* GetSelected();
    int HitTest(float x, float y) const; // image coords, topmost first

    // Render composite at original resolution
    std::unique_ptr<Gdiplus::Bitmap> RenderComposite() const;

    // Draw onto graphics in image coordinate space (caller sets transform)
    void DrawAnnotations(Gdiplus::Graphics& g, bool forExport) const;

    bool SaveAs(const std::wstring& path, bool jpg) const;

    void DeleteSelected();
    bool CopySelectionToClipboard(Gdiplus::Bitmap** outInternal);
    std::unique_ptr<ImageAnn> CreatePasteFrom(std::unique_ptr<Gdiplus::Bitmap> bmp, float atX, float atY);
    bool ApplyMosaic(int mosaicSize);

    // 把浮动图片图层（ImageAnn：移花接木抠图 / 粘贴图片）烙进底图，
    // 并从标注列表移除，返回烙入的图层数量。
    // pushUndo 为 true 时先压撤销栈（Ctrl+Z 可整体回退到图层态）；
    // 为 false 时复用调用方已压好的快照（如魔法消除的「烙图+回写结果」合并为一步撤销）。
    // 无图层时什么都不做、不压栈。
    int FlattenImageLayers(bool pushUndo = true);

    // 查看模式「定版」：把当前所有标注（图形/文字/序号/图片图层）一次性烙进底图，
    // 并清空标注列表，返回烙入的标注数量。烙完后任何工具都无法再编辑旧标注。
    // pushUndo 为 true 时先压撤销栈（Ctrl+Z 可整体回退到标注态）；无标注时不压栈。
    int FlattenAll(bool pushUndo = true);
};

// Global paste buffer for 移花接木 across sheets
struct PasteBuffer {
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    bool Has() const { return bitmap != nullptr; }
    void Set(std::unique_ptr<Gdiplus::Bitmap> bmp) { bitmap = std::move(bmp); }
    std::unique_ptr<Gdiplus::Bitmap> Clone() const {
        if (!bitmap) return nullptr;
        return std::unique_ptr<Gdiplus::Bitmap>(
            bitmap->Clone(0, 0, bitmap->GetWidth(), bitmap->GetHeight(), PixelFormat32bppARGB));
    }
};

PasteBuffer& GlobalPasteBuffer();
