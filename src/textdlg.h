#pragma once
#include "util.h"

struct TextDialogResult {
    bool ok = false;
    std::wstring text;
    float fontSize = 20.0f;
    bool bold = false;
    bool transparentBg = true;
    COLORREF color = RGB(255, 0, 0);
    BYTE alpha = 255;
    COLORREF bgColor = RGB(255, 255, 255);
};

class TextAnn;

namespace TextDialog {
TextDialogResult Show(HWND owner, COLORREF initialColor,
                      TextAnn* existing = nullptr);
}
