#pragma once
// 覆盖式自绘滚动条：替换系统原生滚动条（原生滚动条无法适配暗色）。
// 仍复用窗口的 ScrollInfo（SetScrollInfo/GetScrollInfo）作为唯一数据源，
// 原生条始终保持隐藏，由视图在其自身绘制流程末尾调用 Draw 覆盖在右/下边缘。
#include <windows.h>
#include <algorithm>
#include <functional>
#include "gdiplus.h"

namespace scrollui {

constexpr int kBarW = 12;      // 滚动条占位宽度
constexpr int kThumbInset = 3; // 滑块与轨道边缘的留白
constexpr int kMinThumb = 28;  // 滑块最小长度

struct State {
    bool dragging = false;
    int grabOffset = 0; // 按下点相对滑块顶/左边缘的偏移
};

using ApplyFn = std::function<void(int bar, int newPos)>;

struct Geom {
    bool vVis = false, hVis = false;
    RECT vTrack{}, hTrack{};
    RECT vThumb{}, hThumb{};
};

// 从窗口 ScrollInfo 读取单个方向的滚动状态；maxPos = 最大滚动位置
inline bool Metrics(HWND hwnd, int bar, int& pos, int& maxPos, int& page, int& total) {
    SCROLLINFO si = { sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS };
    GetScrollInfo(hwnd, bar, &si);
    page = static_cast<int>(si.nPage);
    pos = si.nPos;
    maxPos = si.nMax + 1 - si.nMin - page;
    total = si.nMax + 1 - si.nMin;
    if (maxPos < 0) maxPos = 0;
    return maxPos > 0 && page > 0;
}

inline Geom Compute(HWND hwnd, const RECT& client) {
    Geom gm;
    int pos = 0, maxPos = 0, page = 0, total = 1;
    gm.vVis = Metrics(hwnd, SB_VERT, pos, maxPos, page, total);
    gm.hVis = Metrics(hwnd, SB_HORZ, pos, maxPos, page, total);
    if (!gm.vVis && !gm.hVis) return gm;

    gm.vTrack = { client.right - kBarW, 0, client.right,
                  client.bottom - (gm.hVis ? kBarW : 0) };
    gm.hTrack = { 0, client.bottom - kBarW,
                  client.right - (gm.vVis ? kBarW : 0), client.bottom };

    int vPos = 0, vMax = 0, vPage = 0, vTotal = 1;
    int hPos = 0, hMax = 0, hPage = 0, hTotal = 1;
    Metrics(hwnd, SB_VERT, vPos, vMax, vPage, vTotal);
    Metrics(hwnd, SB_HORZ, hPos, hMax, hPage, hTotal);

    if (gm.vVis) {
        int len = static_cast<int>(gm.vTrack.bottom - gm.vTrack.top);
        int thumb = (std::max)(kMinThumb, len * vPage / (std::max)(1, vTotal));
        int span = (std::max)(1, len - thumb);
        int top = static_cast<int>(gm.vTrack.top) +
                  (vMax > 0 ? span * vPos / vMax : 0);
        int bottom = (std::min)(top + thumb, static_cast<int>(gm.vTrack.bottom));
        gm.vThumb = { gm.vTrack.left, top, gm.vTrack.right, bottom };
    }
    if (gm.hVis) {
        int len = static_cast<int>(gm.hTrack.right - gm.hTrack.left);
        int thumb = (std::max)(kMinThumb, len * hPage / (std::max)(1, hTotal));
        int span = (std::max)(1, len - thumb);
        int left = static_cast<int>(gm.hTrack.left) +
                   (hMax > 0 ? span * hPos / hMax : 0);
        int right = (std::min)(left + thumb, static_cast<int>(gm.hTrack.right));
        gm.hThumb = { left, gm.hTrack.top, right, gm.hTrack.bottom };
    }
    return gm;
}

inline void Fill(Gdiplus::Graphics& g, const RECT& r, Gdiplus::Color c) {
    Gdiplus::SolidBrush br(c);
    g.FillRectangle(&br, r.left, r.top, r.right - r.left, r.bottom - r.top);
}

inline void Draw(Gdiplus::Graphics& g, const Geom& gm, const State& v, const State& h,
                 bool dark) {
    if (!gm.vVis && !gm.hVis) return;
    Gdiplus::Color track(255, dark ? 40 : 236, dark ? 40 : 236, dark ? 40 : 236);
    Gdiplus::Color thumb(255, dark ? 95 : 190, dark ? 95 : 190, dark ? 95 : 190);
    Gdiplus::Color thumbActive(255, dark ? 135 : 160, dark ? 135 : 160,
                               dark ? 135 : 160);

    if (gm.vVis) {
        Fill(g, gm.vTrack, track);
        RECT t = { gm.vThumb.left + kThumbInset, gm.vThumb.top + 1,
                   gm.vThumb.right - kThumbInset, gm.vThumb.bottom - 1 };
        Fill(g, t, v.dragging ? thumbActive : thumb);
    }
    if (gm.hVis) {
        Fill(g, gm.hTrack, track);
        RECT t = { gm.hThumb.left + 1, gm.hThumb.top + kThumbInset,
                   gm.hThumb.right - 1, gm.hThumb.bottom - kThumbInset };
        Fill(g, t, h.dragging ? thumbActive : thumb);
    }
}

// 按下：命中轨道返回 true（事件已被滚动条消费）
// 轨道空白处点击 = 翻页；按在滑块上 = 进入拖动
inline bool Down(HWND hwnd, const Geom& gm, int x, int y, State& v, State& h,
                 const ApplyFn& apply) {
    if (gm.vVis && PtInRect(&gm.vTrack, { x, y })) {
        int pos, maxPos, page, total;
        Metrics(hwnd, SB_VERT, pos, maxPos, page, total);
        if (y < gm.vThumb.top) apply(SB_VERT, (std::max)(0, pos - page));
        else if (y > gm.vThumb.bottom) apply(SB_VERT, (std::min)(maxPos, pos + page));
        else { v.dragging = true; v.grabOffset = y - gm.vThumb.top; }
        return true;
    }
    if (gm.hVis && PtInRect(&gm.hTrack, { x, y })) {
        int pos, maxPos, page, total;
        Metrics(hwnd, SB_HORZ, pos, maxPos, page, total);
        if (x < gm.hThumb.left) apply(SB_HORZ, (std::max)(0, pos - page));
        else if (x > gm.hThumb.right) apply(SB_HORZ, (std::min)(maxPos, pos + page));
        else { h.dragging = true; h.grabOffset = x - gm.hThumb.left; }
        return true;
    }
    return false;
}

// 移动：仅在拖动滑块时消费事件
inline bool Move(HWND hwnd, int x, int y, State& v, State& h, const ApplyFn& apply) {
    if (!v.dragging && !h.dragging) return false;
    RECT rc;
    GetClientRect(hwnd, &rc);
    Geom gm = Compute(hwnd, rc);
    if (v.dragging && gm.vVis) {
        int pos, maxPos, page, total;
        Metrics(hwnd, SB_VERT, pos, maxPos, page, total);
        int span = (std::max)(1, static_cast<int>((gm.vTrack.bottom - gm.vTrack.top) -
                                     (gm.vThumb.bottom - gm.vThumb.top)));
        int np = (y - v.grabOffset - gm.vTrack.top) * maxPos / span;
        np = (std::max)(0, (std::min)(maxPos, np));
        if (np != pos) apply(SB_VERT, np);
        return true;
    }
    if (h.dragging && gm.hVis) {
        int pos, maxPos, page, total;
        Metrics(hwnd, SB_HORZ, pos, maxPos, page, total);
        int span = (std::max)(1, static_cast<int>((gm.hTrack.right - gm.hTrack.left) -
                                     (gm.hThumb.right - gm.hThumb.left)));
        int np = (x - h.grabOffset - gm.hTrack.left) * maxPos / span;
        np = (std::max)(0, (std::min)(maxPos, np));
        if (np != pos) apply(SB_HORZ, np);
        return true;
    }
    return true;
}

// 抬起：返回之前是否处于拖动
inline bool Up(State& v, State& h) {
    bool was = v.dragging || h.dragging;
    v.dragging = h.dragging = false;
    return was;
}

} // namespace scrollui
