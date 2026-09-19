#pragma once
#include "util.h"
#include "document.h"

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

    void SetDocument(Document* doc) { doc_ = doc; Refresh(); }
    Document* GetDocument() const { return doc_; }

    void SetTool(Tool t) { tool_ = t; dragMode_ = DragMode::None; Refresh(); }
    Tool GetTool() const { return tool_; }

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

    // image <-> client
    void ClientToImage(int cx, int cy, float& ix, float& iy) const;
    void ImageToClient(float ix, float iy, int& cx, int& cy) const;

    void CopySelection();
    void PasteFromBuffer();
    void ApplyMosaicToSelection();
    void DeleteSelection();
    void Undo();
    void Redo();

    void EnterGlobalPickMode() { pickingColor_ = true; }

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    Canvas() = default;
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM);

    void OnPaint();
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
    void ResizeSelected(HandleId h, float ix, float iy);

    HWND hwnd_ = nullptr;
    Document* doc_ = nullptr;
    Tool tool_ = Tool::Select;
    COLORREF color_ = RGB(255, 0, 0);
    BYTE alpha_ = 255;
    int number_ = 1;
    bool pickingColor_ = false;

    DragMode dragMode_ = DragMode::None;
    float startIx_ = 0, startIy_ = 0;
    float lastIx_ = 0, lastIy_ = 0;
    float moveOriginX_ = 0, moveOriginY_ = 0;
    int pendingTextEdit_ = -1; // 点击文字本体时待打开编辑（未拖动则编辑）
    std::unique_ptr<Annotation> draft_;
    std::unique_ptr<Annotation> moveBackup_;
    RectF resizeStartBounds_{};
    HandleId activeHandle_ = HandleId::None;

    void OpenTextEditor(int hitIndex);

    // double buffering
    void* bits_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP memBm_ = nullptr;
    int memW_ = 0, memH_ = 0;
    void EnsureBackbuffer(int w, int h);
};
