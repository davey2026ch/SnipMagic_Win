#pragma once
#include "util.h"
#include "document.h"
#include "canvas.h"

// forward
namespace Gdiplus { class Graphics; }

// Top / left toolbar button definition
struct ToolButton {
    int id = 0;
    std::wstring text;      // tooltip / fallback text
    std::wstring tip;       // hover tooltip
    RECT rc {};
    bool toggle = false;
    bool isLeft = false;    // left rail: icon-style
    bool showNumber = false;// number tool shows current value
    bool locked = false;    // 双击锁定：可连续添加同类组件
    Tool tool = Tool::Select;
};

class App {
public:
    static App& Instance();

    bool Init(HINSTANCE hi, int nCmdShow);
    int Run();
    void OnSettingsChanged();
    void UpdateStatus();
    void UpdateTitle();
    void ShowStatusMessage(const std::wstring& msg);
    void SetCanvasHover(float ix, float iy);
    HWND Hwnd() const { return hwnd_; }

    void StartCapture();
    void StartLongCapture();
    void OnCaptureFinished();
    void OnLongRegionSelected();
    void OnLongCaptureFinished(bool hasResult);
    void AddDocument(std::unique_ptr<Gdiplus::Bitmap> bmp);
    void ActivateDoc(int idx);
    // 返回 true 表示已成功保存；用户取消保存对话框或保存失败时返回 false
    bool SaveDoc(int idx);
    void CloseDoc(int idx);
    // 返回 true 表示保存流程已完成；用户取消路径/格式选择时返回 false
    bool SaveAllDocs();
    void SelectTool(Tool t);
    void OpenColorPicker();
    void OpenSettings();
    void UpdateHotkey();
    void UpdateTabBar();
    void LayoutChildren();
    void ApplyTheme();

    int ActiveIndex() const { return activeIdx_; }
    Document* ActiveDoc();

    void StartCompare(int targetIdx);
    void ExitCompare();
    bool IsCompareMode() const { return compareMode_; }
    void OnMainCanvasScrolled();
    void OnComparePaneScrolled();
    void ToggleCompareSyncScroll();

private:
    App() = default;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM);

    void OnCreate();
    void OnPaint();
    void OnSize();
    void OnCommand(int id);
    void OnKeyDown(WPARAM vk);
    void OnHotkey(WPARAM id);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnRButtonDown(int x, int y);
    void OnMouseMove(int x, int y);
    void OnContextMenu(int x, int y);
    void CreateTabMenu(int tabIdx, int x, int y);
    void LayoutCompareButtons();
    void HitTestCompareControls(int x, int y, int& outId) const;

    // 截图启动：先隐藏，定时器到点后再真正开截（按钮/快捷键同一路径）
    void BeginCaptureHide();
    void StartCaptureNow();

    void BuildToolbars();
    int HitTopButton(int x, int y) const;
    int HitLeftButton(int x, int y) const;
    int HitTab(int x, int y) const;

    HINSTANCE hi_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND status_ = nullptr;
    int nextDocId_ = 1;
    bool capturePending_ = false;
    bool longModePending_ = false;

    std::vector<std::unique_ptr<Document>> docs_;
    int activeIdx_ = -1;
    int contextTabIdx_ = -1; // right-clicked tab for context menu
    int compareIdx_ = -1;    // document shown in right compare pane
    bool compareMode_ = false;
    bool compareSyncScroll_ = true; // default on
    RECT syncScrollRc_{};
    RECT exitCompareRc_{};
    bool compareBtnsBuilt_ = false;

    std::vector<ToolButton> topBtns_;
    std::vector<ToolButton> leftBtns_;
    int numberIndex_ = 0; // 0-based; display 1..20
    int hoverLeft_ = -1;
    int hoverTop_ = -1;
    int pressedTop_ = -1; // 顶部按钮按下态（与页签选中同款主题色反馈）
    HWND tipHwnd_ = nullptr;
    std::wstring tipText_;
    bool tipVisible_ = false;

    void ShowTooltip(int x, int y, const std::wstring& text);
    void HideTooltip();
    void ShowNumberMenu(int x, int y);
    void DrawToolIcon(Graphics& g, const ToolButton& b, const RECT& rc,
                      COLORREF iconColor, COLORREF accent) const;

    std::wstring statusMsg_;
    std::wstring hoverInfo_;
    float hoverIx_ = 0, hoverIy_ = 0;
    bool hasHover_ = false;
    // 暗色主题下状态栏四段文字（SBT_OWNERDRAW 需要稳定的字符串指针）
    std::wstring statusPart_[4];

    // layout
    int topH_ = 48;
    int leftW_ = 56;
    int tabH_ = 32;
    int statusH_ = 24;

    int dpi_ = 96;
};

// Command IDs
enum : int {
    ID_CMD_CAPTURE = 100,
    ID_CMD_MOSAIC = 101,
    ID_CMD_SETTINGS = 102,
    ID_CMD_SAVE_ALL = 103,
    ID_CMD_UNDO = 104,
    ID_CMD_REDO = 105,
    ID_CMD_SAVE = 106,
    ID_CMD_COLOR = 107,
    ID_CMD_COPY = 108,
    ID_CMD_PASTE = 109,
    ID_CMD_NUMBER = 110,
    ID_CMD_SYNC_SCROLL = 111,
    ID_CMD_EXIT_COMPARE = 112,
    ID_CMD_EXTRACT = 113,
    ID_CMD_MAGIC_ERASE = 114,
    ID_CMD_LONG_CAPTURE = 115,

    ID_TOOL_SELECT = 200,
    ID_TOOL_BRUSH = 201,
    ID_TOOL_VIEW = 202,
    ID_TOOL_TEXT = 203,
    ID_TOOL_ARROW = 204,
    ID_TOOL_LINE = 205,
    ID_TOOL_PEN = 206,
    ID_TOOL_RECT = 207,
    ID_TOOL_ROUND = 208,
    ID_TOOL_ELLIPSE = 209,
    ID_TOOL_FRECT = 210,
    ID_TOOL_FROUND = 211,
    ID_TOOL_FELLIPSE = 212,
    ID_TOOL_NUMBER = 213,

    ID_NUM_BASE = 600, // 600..619 => number 1..20

    ID_TAB_BASE = 400,
    ID_MENU_CLOSE = 501,
    ID_MENU_SAVE = 502,
    ID_MENU_SAVE_ALL = 503,
    ID_MENU_CLOSE_OTHERS = 504,
    ID_MENU_COMPARE = 505,

    WM_APP_CAPTURE_DONE = WM_APP + 1,
    WM_APP_BEGIN_CAPTURE = WM_APP + 2,
    WM_APP_LONG_REGION = WM_APP + 3,
    WM_APP_LONG_DONE = WM_APP + 4,
    WM_APP_UPDATE_FOUND = WM_APP + 5,   // 后台线程检测到新版本
    WM_APP_UPDATE_READY = WM_APP + 6,   // 新版本包下载完成
    WM_APP_UPDATE_FAILED = WM_APP + 7,  // 下载失败

    kTimerTooltip = 1,
    kTimerBeginCapture = 2,
    kTimerUpdateCheck = 3               // 启动 3 秒后触发一次更新检测
};
