#pragma once
#include "util.h"

enum class CaptureMode {
    Region = 0,
    Long   = 1
};

// Fullscreen region-capture overlay (dark mask + rubber band + size tip)
class CaptureOverlay {
public:
    static CaptureOverlay& Instance();

    void Start(HWND owner, CaptureMode mode = CaptureMode::Region);
    bool IsOpen() const { return hwnd_ != nullptr; }
    void Cancel();

    CaptureMode Mode() const { return mode_; }
    // 选区在屏幕坐标系中的矩形（长截图用）
    RECT LastRegionScreen() const { return lastRegionScreen_; }

    // 按钮 / 快捷键共用：截图前强制隐藏本进程窗口
    static void ForceHideForCapture(HWND owner);
    static void UncloakAndShow(HWND hwnd);

    // Result access
    bool HasResult() const { return hasResult_; }
    std::unique_ptr<Gdiplus::Bitmap> TakeResult();

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    CaptureOverlay() = default;

    void OnPaint(HDC hdc);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnMouseMove(int x, int y);
    void OnKey(WPARAM vk);
    void FinishCapture();
    void CaptureVirtualScreen();

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    bool dragging_ = false;
    bool finishing_ = false; // 主动收尾中，忽略 CAPTURECHANGED
    int startX_ = 0, startY_ = 0;
    int curX_ = 0, curY_ = 0;
    RECT sel_ = {};
    int vx_ = 0, vy_ = 0; // overlay/虚拟屏原点
    CaptureMode mode_ = CaptureMode::Region;
    RECT lastRegionScreen_ = {};

    std::unique_ptr<Gdiplus::Bitmap> screen_;
    std::unique_ptr<Gdiplus::Bitmap> result_;
    bool hasResult_ = false;
};