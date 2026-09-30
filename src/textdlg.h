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
// existing 非空：编辑已有文字，用该文字自身的颜色初始化；
// existing 为空：插入新文字，使用弹窗固定的默认色，
// 与左侧工具栏的颜色设置互不联动。
TextDialogResult Show(HWND owner, TextAnn* existing = nullptr);
}
