#pragma once
#include "util.h"

struct ColorResult {
    bool ok = false;
    COLORREF color = RGB(255, 0, 0);
    BYTE alpha = 255;
};

namespace ColorPicker {
// showQuickWhite: 在确定按钮左侧提供「白色」快捷按钮
ColorResult Show(HWND owner, COLORREF initial, BYTE initialAlpha = 255,
                 bool showQuickWhite = false);

bool Eyedropper(HWND owner, COLORREF& outColor);
}
