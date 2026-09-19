#pragma once
#include "util.h"
#include "longstitch.h"
#include <memory>
#include <string>
#include <vector>

// 长截图会话：框选后的抓帧循环 + 控制条 UI + 拼接
class LongCapture {
public:
    static LongCapture& Instance();

    void Start(HWND owner, const RECT& regionScreen);
    bool IsActive() const { return active_; }
    void Finish();
    void Cancel();
    std::unique_ptr<Gdiplus::Bitmap> TakeResult();
    // 最近一次会话的拼接问题（Cleanup 后仍可读）
    int LastGapCount() const { return lastGaps_; }
    int LastSuspectCount() const { return lastSuspects_; }

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK BorderProc(HWND, UINT, WPARAM, LPARAM);

private:
    LongCapture() = default;

    void OnTimer();
    void OnPaint(HWND hwnd);
    void UpdateStatusText();
    void LayoutUi();
    bool CaptureRegionFrame();
    void Cleanup(bool restoreOwner, bool success);
    HWND CreateBar(HINSTANCE hi);

    HWND owner_ = nullptr;
    HWND bar_ = nullptr;
    HWND border_ = nullptr;
    HWND timerHwnd_ = nullptr; // 定时器宿主（优先 bar_，失败则用隐藏窗）
    HWND btnDone_ = nullptr;
    HWND btnCancel_ = nullptr;
    HWND statusH_ = nullptr; // 上排：行数等简短状态
    HWND tipH_ = nullptr;    // 下排：完整操作提示（整行，不与按钮抢宽度）

    RECT region_ = {};
    bool active_ = false;
    bool captureInFlight_ = false;
    bool finishing_ = false;
    int failStreak_ = 0;
    DWORD startTick_ = 0;
    DWORD lastTick_ = 0;
    bool firstTickDone_ = false;

    std::unique_ptr<longstitch::Stitcher> stitcher_;
    std::unique_ptr<Gdiplus::Bitmap> result_;
    std::wstring statusText_;
    std::wstring tipText_;
    bool alignFailHint_ = false;
    int lastGaps_ = 0;
    int lastSuspects_ = 0;

    // 主题色（启动会话时按设置取一次）
    bool darkTheme_ = false;
    COLORREF barBg_ = RGB(232, 243, 255);
    COLORREF barFg_ = RGB(40, 40, 40);
    COLORREF barBorder_ = RGB(170, 205, 240);
    COLORREF barAccent_ = RGB(0, 120, 212);

    void ApplyBarTheme();
    std::wstring BuildStatusText() const;
    std::wstring BuildTipText() const;
    void ApplyStatusToUi();

    void* kbHook_ = nullptr;
};
