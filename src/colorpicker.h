#pragma once
#include "util.h"

struct ColorResult {
    bool ok = false;
    COLORREF color = RGB(255, 0, 0);
    BYTE alpha = 255;
};

namespace ColorPicker {
// 快捷色按钮位标志：在「确定」按钮左侧提供一键设置的快捷按钮
enum QuickColor {
    kQuickWhite = 0x1, // 白色
    kQuickRed   = 0x2, // 红色
    kQuickBlack = 0x4, // 黑色
};

// quickButtons：kQuickWhite/kQuickRed/kQuickBlack 的组合，0 表示不显示快捷按钮
ColorResult Show(HWND owner, COLORREF initial, BYTE initialAlpha = 255,
                 int quickButtons = 0);

bool Eyedropper(HWND owner, COLORREF& outColor);
}
