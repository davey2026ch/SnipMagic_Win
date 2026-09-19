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
    void OnCaptureFinished();
    void AddDocument(std::unique_ptr<Gdiplus::Bitmap> bmp);
    void ActivateDoc(int idx);
    void CloseDoc(int idx);
    void SaveDoc(int idx);
    void SaveAllDocs();
    void SelectTool(Tool t);
    void OpenColorPicker();
    void OpenSettings();
    void UpdateHotkey();
    void UpdateTabBar();
    void LayoutChildren();
    void ApplyTheme();

    int ActiveIndex() const { return activeIdx_; }
    Document* ActiveDoc();

private:
    App() = default;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND, UINT, WPARAM, LPARAM);

    void OnCreate();
    void OnPaint();
    void OnSize();
    void OnCommand(int id);
    void OnKeyDown(WPARAM vk);
    void OnHotkey();
    void OnLButtonDown(int x, int y);
    void OnRButtonDown(int x, int y);
    void OnMouseMove(int x, int y);
    void OnContextMenu(int x, int y);
    void CreateTabMenu(int tabIdx, int x, int y);

    void BuildToolbars();
    int HitTopButton(int x, int y) const;
    int HitLeftButton(int x, int y) const;
    int HitTab(int x, int y) const;

    HINSTANCE hi_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND status_ = nullptr;
    int nextDocId_ = 1;

    std::vector<std::unique_ptr<Document>> docs_;
    int activeIdx_ = -1;

    std::vector<ToolButton> topBtns_;
    std::vector<ToolButton> leftBtns_;
    int numberIndex_ = 0; // 0-based; display 1..20
    int hoverLeft_ = -1;
    int hoverTop_ = -1;
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

    WM_APP_CAPTURE_DONE = WM_APP + 1
};
