#pragma once
// GIF 录制器：一个带标题栏、可拖动可缩放的空心窗口（内容区就是录制区域，直接看到桌面），
// 底部控制条按状态在「录制/取消」与「暂停/停止」之间切换。
// 录制在后台线程按配置帧率抓屏，逐帧量化+LZW 压缩后存内存，停止时交给主程序建页签。
#include "util.h"
#include "gifcodec.h"
#include <atomic>
#include <mutex>
#include <thread>

class GifRecorder {
public:
    static GifRecorder& Instance();

    void Open(HWND owner);   // 打开框选窗口（已打开则只前置）
    void Cancel();           // 放弃并关闭
    bool IsOpen() const { return hwnd_ != nullptr; }
    bool IsBusy() const;     // 正在录制或已暂停

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    GifRecorder() = default;
    ~GifRecorder() = default;

    enum class State { Idle, Recording, Paused };

    void BuildWindow(HWND owner);
    void ApplyGeometry();
    void LayoutBar();
    void UpdateButtons();
    void UpdateStatusText();
    void StartRecording();
    void PauseRecording();
    void ResumeRecording();
    void StopRecording();      // 生成 GIF 并交给主程序
    void CloseAll();          // 销毁窗口 + 恢复主窗口
    void CloseWindowOnly();   // 只销毁录制窗口（主窗口仍隐藏，用于停止后先建页签）
    void RestoreOwner();      // 显示并前置主窗口（按原来的最大化状态）
    void WorkerLoop(int x, int y, int w, int h, int fps);
    int BarHeight() const;
    int TitleHeight() const;          // 自绘标题栏高度（不用系统标题栏，避免被裁/主题不一致）
    RECT ContentHole() const;         // 客户区里的"录制内容"矩形（不含标题栏/底栏/边框）
    void OnPaint();
    void DrawTitleBar(Gdiplus::Graphics& g, const RECT& rc);
    int  HitTitleButton(int x, int y) const;   // 0=无 1=最小化 2=最大化/还原 3=关闭
    RECT TitleButtonRect(int id) const;
    void OnTitleButton(int id);
    void ToggleMaximize();
    void DrawBarButton(DRAWITEMSTRUCT* dis, bool primary);

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    int ownerShowCmd_ = SW_SHOWNORMAL;   // 主窗口原来的显示状态（恢复时用）
    HWND btnMain_ = nullptr;
    HWND btnSecond_ = nullptr;
    HWND status_ = nullptr;
    HFONT font_ = nullptr;

    State state_ = State::Idle;
    bool dragResize_ = false;   // 录制中禁止移动/缩放

    std::thread worker_;
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> frameCount_{0};
    std::atomic<int> measuredFps_{0};   // 实测抓帧速率（<=0 表示还没测出来）
    std::mutex mu_;
    gifcodec::Anim anim_;
    RECT region_{};             // 录制区域（屏幕坐标）
    DWORD startTick_ = 0;
    DWORD pausedAccum_ = 0;
    DWORD pauseStart_ = 0;
    int dpi_ = 96;
    int hot_ = -1;              // 悬停的按钮（0=主按钮 1=次按钮）
    int titleHot_ = -1;         // 悬停的标题栏按钮（1/2/3）
    int titlePressed_ = -1;     // 按下的标题栏按钮
    bool maximized_ = false;
    RECT restoreRect_{};        // 最大化之前的位置（还原用）

    static constexpr int kIdMain = 4301;
    static constexpr int kIdSecond = 4302;
    static constexpr int kTimerStatus = 71;
};
