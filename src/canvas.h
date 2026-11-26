#pragma once
#include "util.h"
#include "document.h"
#include "scrollui.h"

enum class Tool {
    Select,
    Brush,
    View,
    Text,
    Arrow,
    Line,
    Freehand,
    Rect,
    RoundRect,
    Ellipse,
    FilledRect,
    FilledRoundRect,
    FilledEllipse,
    Number
};

// Canvas child window: draws active document, handles annotation input
class Canvas {
public:
    static Canvas& Instance();

    bool Create(HWND parent, HINSTANCE hi);
    HWND Hwnd() const { return hwnd_; }

    void SetDocument(Document* doc);
    Document* GetDocument() const { return doc_; }

    // 退出时在 GdiplusShutdown 之前调用：释放画布自己持有的 GDI+ 对象，
    // 否则静态对象在 GDI+ 关闭之后再析构会崩（0xC0000005）
    void ReleaseGdiplusCache();

    void SetTool(Tool t);
    Tool GetTool() const { return tool_; }
    // 双击工具栏按钮锁定：连续添加，添加后不切换回选择工具、不显示选择框
    void SetToolLocked(bool locked) { toolLocked_ = locked; }
    bool IsToolLocked() const { return toolLocked_; }

    void SetDrawColor(COLORREF c) { color_ = c; }
    COLORREF GetDrawColor() const { return color_; }
    void SetAlpha(BYTE a) { alpha_ = a; }

    void SetNumber(int n) { number_ = n; }
    int GetNumber() const { return number_; }

    void ZoomBy(float factor);
    void ZoomAt(float factor, int screenX, int screenY);
    void SetZoom(float z);
    float GetZoom() const { return doc_ ? doc_->zoom : 1.0f; }
    void ResetZoom() { SetZoom(1.0f); }

    void Refresh();
    void UpdateScrollBars();
    // 自绘滚动条（scrollui）：把新滚动位置写回 ScrollInfo/文档
    void OnCustomScroll(int bar, int pos);

    // image <-> client
    void ClientToImage(int cx, int cy, float& ix, float& iy) const;
    void ImageToClient(float ix, float iy, int& cx, int& cy) const;

    void CopySelection();
    void PasteFromBuffer();
    void ApplyMosaicToSelection();
    void DeleteSelection();
    void Undo();
    void Redo();

    // ---- GIF 动图页签的播放视图 ----
    // 返回 true 表示按键已被 GIF 视图消费（左右方向键逐帧、空格播放/暂停）
    bool HandleGifKey(WPARAM vk);
    void RefreshGifView();

    void EnterGlobalPickMode() { pickingColor_ = true; }

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    Canvas() = default;
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM);

    void OnPaint();
    // 框选虚线 + 四角刻度（普通画布与 GIF 预览共用，坐标是图像像素）
    void DrawRegionOverlay(Gdiplus::Graphics& g, float z) const;
    void OnMouseDown(int x, int y, bool right);
    void OnMouseMove(int x, int y);
    void OnMouseUp(int x, int y);
    void OnMouseWheel(int x, int y, int delta);
    void OnKeyDown(WPARAM vk);
    void OnDoubleClick(int x, int y);
    void BeginDraw(float ix, float iy);
    void UpdateDraw(float ix, float iy);
    void EndDraw(float ix, float iy);
    void PushAndAdd(std::unique_ptr<Annotation> ann);
    bool IsEditMode() const;
    int Margin() const { return 16; }

    enum class DragMode { None, Draw, Move, Resize, Rubber };
    enum class HandleId {
        None = -1,
        NW = 0, N = 1, NE = 2, E = 3, SE = 4, S = 5, SW = 6, W = 7
    };

    HandleId HitResizeHandle(float ix, float iy) const;
    HandleId HitHandleOnAnn(const Annotation* ann, float ix, float iy) const;
    void ResizeSelected(HandleId h, float ix, float iy);

    HWND hwnd_ = nullptr;
    Document* doc_ = nullptr;
    Tool tool_ = Tool::Select;
    bool toolLocked_ = false;
    COLORREF color_ = RGB(255, 0, 0);
    BYTE alpha_ = 255;
    int number_ = 1;
    bool pickingColor_ = false;

    DragMode dragMode_ = DragMode::None;
    float startIx_ = 0, startIy_ = 0;
    float lastIx_ = 0, lastIy_ = 0;
    float moveOriginX_ = 0, moveOriginY_ = 0;
    std::unique_ptr<Annotation> draft_;
    std::unique_ptr<Annotation> moveBackup_;
    RectF resizeStartBounds_{};
    float resizeStartFontSize_ = 0; // 文字拖角点缩放：起点字号
    HandleId activeHandle_ = HandleId::None;

    void OpenTextEditor(int hitIndex);
    // 查看模式「定版」：进行中的编辑自动成型，浮动图片图层烙进底图（走撤销栈，可回退）。
    // 返回 true 表示有编辑被定版
    bool FinalizeForView();
    // 框选区内空白处按下：抠起选区画面（原位置烙白），内容变为可拖动图片图层
    bool BeginRegionContentMove(int rx, int ry, int rw, int rh, float ix, float iy);

    // double buffering
    void* bits_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP memBm_ = nullptr;
    int memW_ = 0, memH_ = 0;
    void EnsureBackbuffer(int w, int h);

    // 覆盖式自绘滚动条状态（替代系统原生滚动条，支持暗色）
    scrollui::State vsb_, hsb_;

    // ---- GIF 视图 ----
    struct GifLayout {
        RECT preview {};
        RECT strip {};
        RECT bottom {};
        int padX = 8;
        int thumbW = 100;
        int thumbH = 46;
        int gap = 4;
        int labelH = 16;
        int contentW = 0;
        int viewW = 0;
    };
    struct GifControls {
        RECT counter {};
        RECT first {};
        RECT prev {};
        RECT play {};
        RECT next {};
        RECT last {};
        RECT loop {};
        RECT saveFrame {};
        RECT saveTab {};
        RECT saveGif {};
        RECT scroll {};
    };

    GifLayout ComputeGifLayout(int w, int h) const;
    GifControls ComputeGifControls(int w, int h) const;
    void DrawGifView(Gdiplus::Graphics& g, int w, int h);
    void DrawGifButton(Gdiplus::Graphics& g, const RECT& rc, const wchar_t* text, bool on,
                       bool primary, bool glyph);
    void GifMouseDown(int x, int y);
    void GifMouseMove(int x, int y);
    void GifMouseUp(int x, int y);
    void GifWheel(int delta, int x, int y);
    void OnGifTimer();
    void UpdateGifTimer();
    bool IsGifDoc() const { return doc_ && doc_->IsGif(); }
    Gdiplus::Bitmap* GifFrameBitmap(int idx);
    Gdiplus::Bitmap* GifThumb(int idx);
    void SetGifFrame(int idx, bool pause);
    // 播放/暂停：不循环且停在末帧时，再点播放会从头开始
    void ToggleGifPlay();
    void GifZoomAt(float factor, int mx, int my);   // Ctrl+滚轮：以光标为中心缩放
    void GifPanBy(int dx, int dy);                  // 滚轮/拖动：平移画面
    void GifClampPan();                             // 平移不能露出空白
    int GifMaxScroll() const;
    void GifEnsureVisible();

    // ---- GIF 预览区当普通截图来编辑 ----
    // 当前帧在客户区的目标矩形（预览绘制与坐标换算共用同一套数字）
    bool GifPreviewRect(Gdiplus::RectF& dst) const;
    // 这个位置按下是否应该进入标注编辑（而不是平移/框选）
    bool GifWantsAnnotationEdit(int x, int y);
    void GifRightClick(int x, int y);              // 缩略图右键：删帧菜单
    void AfterGifEdit();                           // 标注变了：缩略图 + 状态栏 + 刷新
    void AfterGifFramesChanged();                  // 帧增删：清缓存、夹滚动、停播放

    int gifCacheIdx_ = -1;
    int gifCacheRev_ = -1;   // 帧数据版本（Document::gifRev），变了就丢缓存
    std::unique_ptr<Gdiplus::Bitmap> gifCache_;
    bool gifTimerOn_ = false;
    // 交互态
    bool gifDragThumb_ = false;
    bool gifPanDrag_ = false;      // 正在拖动预览画面
    bool gifPanMoved_ = false;     // 本次按下是否真的拖动了（决定松开时是否算点击）
    bool gifMidPan_ = false;       // 中键拖动平移（有绘图工具时也能平移）
    bool gifEditing_ = false;      // 本次按下是在编辑标注（鼠标移动/松开都交给画布逻辑）
    int  gifPanLastX_ = 0;
    int  gifPanLastY_ = 0;
    int gifDragOffset_ = 0;
    int gifPressed_ = 0;   // 按下的按钮编号（见 .cpp 里的 kGif* 常量）
    int gifHover_ = 0;
    int gifScrollPressX_ = 0;
    int gifScrollPressPos_ = 0;
};

