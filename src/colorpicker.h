#pragma once
#include "util.h"

// Modal color picker: hue wheel + brightness + RGB/alpha + HEX + eyedropper
struct ColorResult {
    bool ok = false;
    COLORREF color = RGB(255, 0, 0);
    BYTE alpha = 255;
};

namespace ColorPicker {
// returns picked color
ColorResult Show(HWND owner, COLORREF initial, BYTE initialAlpha = 255);

// Fullscreen eyedropper: freeze screen + 10x magnifier + HEX
// Returns true if a color was picked
bool Eyedropper(HWND owner, COLORREF& outColor);
}
