#pragma once
#include "util.h"

struct ColorResult {
    bool ok = false;
    COLORREF color = RGB(255, 0, 0);
    BYTE alpha = 255;
};

namespace ColorPicker {
// 「确定 / 取消」按钮左侧原有的红/黑/白快捷按钮已取消，
// 这三个常用色改为宫格里固定不可替换的前 3 格（见 colorpicker.cpp 的 kFixedColorCount）
ColorResult Show(HWND owner, COLORREF initial, BYTE initialAlpha = 255);

bool Eyedropper(HWND owner, COLORREF& outColor);
}
