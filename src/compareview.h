#pragma once
#include "util.h"
#include "document.h"
#include "scrollui.h"

// Right-hand read-only pane used by tab compare mode
class CompareView {
public:
    static CompareView& Instance();

    bool Create(HWND parent, HINSTANCE hi);
    HWND Hwnd() const { return hwnd_; }

    void SetDocument(Document* doc);
    Document* GetDocument() const { return doc_; }

    void ShowPane(bool show);
    bool IsPaneVisible() const { return visible_; }

    void Refresh();
    void UpdateScrollBars();
    void ApplyScroll(int scrollX, int scrollY);
    // 自绘滚动条（scrollui）：把新滚动位置写回 ScrollInfo/文档
    void OnCustomScroll(int bar, int pos);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    CompareView() = default;
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM);

    void OnPaint();
    void OnScroll(int bar, WPARAM wParam);
    void NotifyAppScrolled();

    HWND hwnd_ = nullptr;
    Document* doc_ = nullptr;
    bool visible_ = false;

    void* bits_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP memBm_ = nullptr;
    int memW_ = 0, memH_ = 0;
    void EnsureBackbuffer(int w, int h);

    // 覆盖式自绘滚动条状态（替代系统原生滚动条，支持暗色）
    scrollui::State vsb_, hsb_;
};
