// GIF 动图页签的播放视图
//   上：透明底棋盘格 + 当前帧预览（Ctrl+滚轮缩放、滚轮/拖动平移；播放只由播放按钮触发）
//   中：缩略图条（序号 + 每帧毫秒数，点击切帧）
//   下：横向滚动条 + 播放控制（首帧/上一帧/播放暂停/下一帧/末帧、循环、保存帧、存为页签、保存GIF）
#include "canvas.h"
#include "settings.h"
#include "app.h"
#include <cmath>

using namespace Gdiplus;

namespace {

enum : int {
    kGifNone = 0,
    kGifFirst,
    kGifPrev,
    kGifPlay,
    kGifNext,
    kGifLast,
    kGifLoop
};

int CanvasDpi(HWND h) { return util::GetDpiForWindowSafe(h); }

// 透明底棋盘格贴图（缓存 16x16，用纹理刷平铺，避免每帧画上万个小方块）
Bitmap* CheckerBitmap(bool dark) {
    static std::unique_ptr<Bitmap> lightBmp;
    static std::unique_ptr<Bitmap> darkBmp;
    std::unique_ptr<Bitmap>& slot = dark ? darkBmp : lightBmp;
    if (!slot) {
        slot = std::make_unique<Bitmap>(16, 16, PixelFormat32bppARGB);
        Graphics g(slot.get());
        const COLORREF c1 = dark ? RGB(38, 38, 38) : RGB(255, 255, 255);
        const COLORREF c2 = dark ? RGB(52, 52, 52) : RGB(226, 226, 226);
        SolidBrush b1(ToGpColor(c1));
        SolidBrush b2(ToGpColor(c2));
        g.FillRectangle(&b1, 0, 0, 16, 16);
        g.FillRectangle(&b2, 8, 0, 8, 8);
        g.FillRectangle(&b2, 0, 8, 8, 8);
    }
    return slot.get();
}

void FillChecker(Graphics& g, const RECT& rc, bool dark) {
    Bitmap* tex = CheckerBitmap(dark);
    if (!tex) return;
    TextureBrush tb(tex);
    tb.SetWrapMode(WrapModeTile);
    g.FillRectangle(&tb, static_cast<INT>(rc.left), static_cast<INT>(rc.top),
                    static_cast<INT>(rc.right - rc.left), static_cast<INT>(rc.bottom - rc.top));
}

void FillTriangle(Graphics& g, REAL x, REAL y, REAL s, bool right, const Color& c) {
    SolidBrush b(c);
    PointF p[3];
    if (right) {
        p[0] = PointF(x, y);
        p[1] = PointF(x, y + s);
        p[2] = PointF(x + s * 0.9f, y + s / 2);
    } else {
        p[0] = PointF(x + s * 0.9f, y);
        p[1] = PointF(x + s * 0.9f, y + s);
        p[2] = PointF(x, y + s / 2);
    }
    g.FillPolygon(&b, p, 3);
}

} // namespace

Canvas::GifLayout Canvas::ComputeGifLayout(int w, int h) const {
    GifLayout L;
    const int dpi = CanvasDpi(hwnd_);
    L.padX = util::Scale(8, dpi);
    L.thumbW = util::Scale(100, dpi);
    L.thumbH = util::Scale(46, dpi);
    L.gap = util::Scale(4, dpi);
    L.labelH = util::Scale(16, dpi);
    const int bottomH = util::Scale(28, dpi);
    const int stripH = L.thumbH + L.labelH + util::Scale(10, dpi);
    L.bottom = { 0, h - bottomH, w, h };
    L.strip = { 0, h - bottomH - stripH, w, h - bottomH };
    L.preview = { 0, 0, w, L.strip.top };
    const int n = doc_ ? doc_->GifFrameCount() : 0;
    L.contentW = (n > 0) ? (L.padX * 2 + n * L.thumbW + (n - 1) * L.gap) : 0;
    L.viewW = w;
    return L;
}

Canvas::GifControls Canvas::ComputeGifControls(int w, int h) const {
    GifControls C;
    const int dpi = CanvasDpi(hwnd_);
    const int bottomH = util::Scale(28, dpi);
    const int bh = util::Scale(20, dpi);
    const int by = h - bottomH + (bottomH - bh) / 2;
    int right = w - util::Scale(8, dpi);
    auto place = [&](int px) {
        RECT r = { right - px, by, right, by + bh };
        right -= px + util::Scale(4, dpi);
        return r;
    };
    C.saveGif = place(util::Scale(64, dpi));
    C.saveTab = place(util::Scale(72, dpi));
    C.saveFrame = place(util::Scale(60, dpi));
    right -= util::Scale(6, dpi);
    // 「循环」是一个开关：文字 + 右侧滑动钮，整块都能点
    C.loop = place(util::Scale(78, dpi));
    right -= util::Scale(4, dpi);
    C.last = place(util::Scale(26, dpi));
    C.next = place(util::Scale(26, dpi));
    C.play = place(util::Scale(32, dpi));
    C.prev = place(util::Scale(26, dpi));
    C.first = place(util::Scale(26, dpi));
    right -= util::Scale(6, dpi);
    const int cntW = util::Scale(108, dpi);
    C.counter = { (std::max)(0, right - cntW), by, right, by + bh };
    const int sbH = util::Scale(14, dpi);
    const int sbY = h - bottomH + (bottomH - sbH) / 2;
    const int scrollW = (std::max)(util::Scale(60, dpi),
                                   static_cast<int>(C.counter.left) - util::Scale(10, dpi));
    C.scroll = { util::Scale(8, dpi), sbY, util::Scale(8, dpi) + scrollW, sbY + sbH };
    return C;
}

Bitmap* Canvas::GifFrameBitmap(int idx) {
    if (!IsGifDoc()) return nullptr;
    if (idx == gifCacheIdx_ && gifCache_ && gifCacheRev_ == doc_->gifRev) return gifCache_.get();
    auto bmp = doc_->gif->Decode(idx);
    if (!bmp) return nullptr;
    gifCache_ = std::move(bmp);
    gifCacheIdx_ = idx;
    gifCacheRev_ = doc_->gifRev;
    return gifCache_.get();
}

// 当前帧在客户区的目标矩形。预览绘制与鼠标坐标换算共用这一套数字，
// 保证"画上去的位置"和"看到的帧像素"严丝合缝。
bool Canvas::GifPreviewRect(RectF& dst) const {
    dst = RectF(0, 0, 0, 0);
    if (!IsGifDoc() || !hwnd_) return false;
    const int fw = doc_->gif->width;
    const int fh = doc_->gif->height;
    if (fw <= 0 || fh <= 0) return false;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    const int dpi = CanvasDpi(hwnd_);
    const int availW = (std::max)(16, static_cast<int>(L.preview.right) - util::Scale(24, dpi));
    const int availH = (std::max)(16, static_cast<int>(L.preview.bottom) - util::Scale(24, dpi));
    float fitZ = 1.0f;
    if (fw > availW || fh > availH) {
        fitZ = (std::min)(static_cast<float>(availW) / fw, static_cast<float>(availH) / fh);
    }
    const float z = fitZ * doc_->gifZoom;
    const REAL dw = fw * z;
    const REAL dh = fh * z;
    const REAL dx = L.preview.right / 2.0f - dw / 2.0f + doc_->gifPanX;
    const REAL dy = L.preview.bottom / 2.0f - dh / 2.0f + doc_->gifPanY;
    dst = RectF(dx, dy, dw, dh);
    return dw >= 1.0f && dh >= 1.0f;
}

// 这个位置按下，是否应该进入"标注编辑"而不是平移/框选
bool Canvas::GifWantsAnnotationEdit(int x, int y) {
    if (!IsGifDoc() || tool_ == Tool::View) return false;
    float ix = 0, iy = 0;
    ClientToImage(x, y, ix, iy);
    if (ix < 0 || iy < 0 || ix > static_cast<float>(doc_->gif->width) ||
        iy > static_cast<float>(doc_->gif->height))
        return false;   // 点在画面外（棋盘格上）不当编辑

    // 文字 / 序号 / 各种图形与画笔：点画面就是画
    if (tool_ != Tool::Select) return true;

    // 选择工具：命中已有标注（或它的控制点）才编辑
    if (doc_->GetSelected() &&
        HitHandleOnAnn(doc_->GetSelected(), ix, iy) != HandleId::None)
        return true;
    if (doc_->HitTest(ix, iy) >= 0) return true;
    if (doc_->GetSelected()) {
        RectF b;
        doc_->GetSelected()->GetBounds(b);
        if (util::PtInRectF(b, ix, iy)) return true;
    }
    // 空白处：没放大时拖框选（马赛克/复制用），放大了就留给平移
    return doc_->gifZoom <= 1.001f;
}

Bitmap* Canvas::GifThumb(int idx) {
    if (!IsGifDoc()) return nullptr;
    const int n = doc_->GifFrameCount();
    if (idx < 0 || idx >= n) return nullptr;
    if (static_cast<int>(doc_->gifThumbs.size()) < n)
        doc_->gifThumbs.resize(static_cast<size_t>(n));
    if (doc_->gifThumbs[static_cast<size_t>(idx)])
        return doc_->gifThumbs[static_cast<size_t>(idx)].get();

    // 该帧上还有没保存的标注：缩略图也要显示标注，做到"所见即保存后"
    auto full = doc_->GifFrameHasEdits(idx) ? doc_->RenderGifFrame(idx) : doc_->gif->Decode(idx);
    if (!full) return nullptr;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    if (L.thumbW <= 0 || L.thumbH <= 0) return nullptr;

    auto th = std::make_unique<Bitmap>(L.thumbW, L.thumbH, PixelFormat32bppARGB);
    {
        Graphics g(th.get());
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.Clear(Color(255, 255, 255, 255));
        const int fw = static_cast<int>(full->GetWidth());
        const int fh = static_cast<int>(full->GetHeight());
        if (fw > 0 && fh > 0) {
            const float z = (std::min)(static_cast<float>(L.thumbW) / fw,
                                       static_cast<float>(L.thumbH) / fh);
            const int dw = (std::max)(1, static_cast<int>(fw * z + 0.5f));
            const int dh = (std::max)(1, static_cast<int>(fh * z + 0.5f));
            g.DrawImage(full.get(), Rect((L.thumbW - dw) / 2, (L.thumbH - dh) / 2, dw, dh),
                        0, 0, fw, fh, UnitPixel);
        }
    }
    doc_->gifThumbs[static_cast<size_t>(idx)] = std::move(th);
    return doc_->gifThumbs[static_cast<size_t>(idx)].get();
}

int Canvas::GifMaxScroll() const {
    if (!IsGifDoc() || !hwnd_) return 0;
    RECT rc;
    GetClientRect(hwnd_, const_cast<RECT*>(&rc));
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    return (std::max)(0, L.contentW - L.viewW);
}

void Canvas::GifEnsureVisible() {
    if (!IsGifDoc() || !hwnd_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    const int idx = doc_->gifFrame;
    const int cellLeft = L.padX + idx * (L.thumbW + L.gap) - doc_->gifStripScroll;
    const int cellRight = cellLeft + L.thumbW;
    if (cellLeft < L.padX) {
        doc_->gifStripScroll = idx * (L.thumbW + L.gap);
    } else if (cellRight > rc.right - L.padX) {
        doc_->gifStripScroll += (cellRight - (rc.right - L.padX));
    }
    doc_->gifStripScroll = (std::max)(0, (std::min)(doc_->gifStripScroll, GifMaxScroll()));
}

void Canvas::ToggleGifPlay() {
    if (!IsGifDoc()) return;
    const int n = doc_->GifFrameCount();
    if (doc_->gifPlaying) {
        doc_->gifPlaying = false;             // 播放中 -> 暂停
    } else {
        // 不循环时播到末帧就停住；再点播放要能从第一帧重新开始
        if (!doc_->gifLoop && n > 0 && doc_->gifFrame >= n - 1) {
            doc_->gifFrame = 0;
            gifCacheIdx_ = -1;
        }
        doc_->gifPlaying = true;
    }
    doc_->gifNextTick = 0;
    UpdateGifTimer();
    GifEnsureVisible();
    Refresh();
    App::Instance().UpdateStatus();
}

void Canvas::SetGifFrame(int idx, bool pause) {
    if (!IsGifDoc()) return;
    const int n = doc_->GifFrameCount();
    if (n <= 0) return;
    int f = idx;
    if (f < 0) f = doc_->gifLoop ? n - 1 : 0;
    if (f >= n) f = doc_->gifLoop ? 0 : n - 1;
    if (f != doc_->gifFrame) {
        // 标注跟着帧走：先把当前帧的标注收进它的槽位，切帧后再把目标帧的装回来
        doc_->GifStashAnnotations();
        doc_->gifFrame = f;
        doc_->GifLoadAnnotations();
        // 撤销栈只对同一帧有意义，切帧即清空，避免把别的帧的标注"撤销回来"
        doc_->undoStack.clear();
        doc_->redoStack.clear();
        doc_->ClearRegion();
        gifEditing_ = false;
        dragMode_ = DragMode::None;
        draft_.reset();
    }
    doc_->gifFrame = f;
    doc_->gifNextTick = 0;
    if (pause) {
        doc_->gifPlaying = false;
        UpdateGifTimer();
    }
    GifEnsureVisible();
    Refresh();
    App::Instance().UpdateStatus();
}

void Canvas::DrawGifButton(Graphics& g, const RECT& rc, const wchar_t* text, bool on,
                           bool primary, bool glyph) {
    (void)glyph;
    auto& s = Settings();
    const bool dark = s.IsDarkTheme();
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    Color fill = on ? ToGpColor(s.themeColor)
                    : ToGpColor(dark ? RGB(56, 56, 56) : RGB(250, 250, 250));
    if (primary && !on) fill = ToGpColor(s.themeColor);
    SolidBrush br(fill);
    g.FillRectangle(&br, rc.left, rc.top, w, h);
    Pen p(ToGpColor(s.BorderColor()), 1);
    g.DrawRectangle(&p, rc.left, rc.top, w - 1, h - 1);

    const Color tc = (on || primary) ? Color(255, 255, 255) : ToGpColor(s.TextColor());
    SolidBrush fg(tc);
    FontFamily fam(L"Microsoft YaHei");
    Font font(&fam, static_cast<REAL>(util::Scale(12, CanvasDpi(hwnd_))), FontStyleRegular, UnitPixel);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    RectF layout(static_cast<REAL>(rc.left), static_cast<REAL>(rc.top),
                 static_cast<REAL>(w), static_cast<REAL>(h));
    g.DrawString(text, -1, &font, layout, &fmt, &fg);
}

void Canvas::DrawGifView(Graphics& g, int w, int h) {
    auto& s = Settings();
    const bool dark = s.IsDarkTheme();
    const int dpi = CanvasDpi(hwnd_);
    GifLayout L = ComputeGifLayout(w, h);
    GifControls C = ComputeGifControls(w, h);
    const int n = doc_->GifFrameCount();

    // ---- 预览 ----
    FillChecker(g, L.preview, dark);
    if (n > 0) {
        if (doc_->gifFrame < 0 || doc_->gifFrame >= n) doc_->gifFrame = 0;
        Bitmap* frame = GifFrameBitmap(doc_->gifFrame);
        RectF dst;
        if (frame && GifPreviewRect(dst)) {
            const int fw = static_cast<int>(frame->GetWidth());
            const int fh = static_cast<int>(frame->GetHeight());
            const float z = (fw > 0) ? dst.Width / static_cast<float>(fw) : 1.0f;
            const int dw = (std::max)(1, static_cast<int>(dst.Width + 0.5f));
            const int dh = (std::max)(1, static_cast<int>(dst.Height + 0.5f));
            const int dx = static_cast<int>(std::floor(dst.X + 0.5f));
            const int dy = static_cast<int>(std::floor(dst.Y + 0.5f));
            // 放大后画面会超出预览区，裁掉溢出的部分（不能画到缩略图条上）
            Region oldClip;
            g.GetClip(&oldClip);
            g.SetClip(Rect(0, 0, w, L.preview.bottom));
            g.SetInterpolationMode(z < 1.0f ? InterpolationModeHighQualityBicubic
                                            : InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            SolidBrush shadow(Color(45, 0, 0, 0));
            g.FillRectangle(&shadow, dx - 1, dy - 1, dw + 2, dh + 2);
            g.DrawImage(frame, Rect(dx, dy, dw, dh), 0, 0, fw, fh, UnitPixel);

            // 标注层：与普通截图同一套标注，坐标是帧像素，按预览倍率缩放，
            // 并裁在画面之内（画到棋盘格上会让人误以为超出画面也算）
            {
                Region frameClip;
                g.GetClip(&frameClip);
                g.SetClip(Rect(dx, dy, dw, dh));
                GraphicsState st = g.Save();
                g.TranslateTransform(static_cast<REAL>(dx), static_cast<REAL>(dy));
                g.ScaleTransform(z, z);
                if (draft_ && tool_ != Tool::View) draft_->Draw(g);
                doc_->DrawAnnotations(g, false);
                DrawRegionOverlay(g, z);
                g.Restore(st);
                g.SetClip(&frameClip);
            }
            g.SetClip(&oldClip);
        }
    }

    // ---- 缩略图条 ----
    FillChecker(g, L.strip, dark);
    {
        Region old;
        g.GetClip(&old);
        g.SetClip(Rect(0, L.strip.top, w, L.strip.bottom - L.strip.top));
        const int ty = L.strip.top + util::Scale(5, dpi);
        int x = L.padX - doc_->gifStripScroll;
        FontFamily fam(L"Microsoft YaHei");
        Font fIdx(&fam, static_cast<REAL>(util::Scale(11, dpi)), FontStyleRegular, UnitPixel);
        Font fMs(&fam, static_cast<REAL>(util::Scale(10, dpi)), FontStyleRegular, UnitPixel);
        for (int i = 0; i < n; ++i) {
            const RECT cell = { x, ty, x + L.thumbW, ty + L.thumbH };
            if (cell.right > 0 && cell.left < w) {
                const bool cur = (i == doc_->gifFrame);
                if (cur) {
                    SolidBrush hl(ToGpColor(s.themeColor, 60));
                    g.FillRectangle(&hl, cell.left - 2, cell.top - 2, L.thumbW + 4, L.thumbH + 4);
                }
                Bitmap* th = GifThumb(i);
                if (th) {
                    g.DrawImage(th, Rect(cell.left, cell.top, L.thumbW, L.thumbH),
                                0, 0, L.thumbW, L.thumbH, UnitPixel);
                }
                Pen bp(cur ? ToGpColor(s.themeColor) : ToGpColor(s.BorderColor()),
                       cur ? 2.0f : 1.0f);
                g.DrawRectangle(&bp, cell.left, cell.top, L.thumbW - 1, L.thumbH - 1);

                SolidBrush lb(cur ? ToGpColor(s.themeColor) : ToGpColor(s.TextColor()));
                StringFormat lf;
                lf.SetAlignment(StringAlignmentNear);
                lf.SetLineAlignment(StringAlignmentCenter);
                RectF lr(static_cast<REAL>(cell.left), static_cast<REAL>(cell.bottom),
                         static_cast<REAL>(L.thumbW), static_cast<REAL>(L.labelH));
                g.DrawString(std::to_wstring(i).c_str(), -1, &fIdx, lr, &lf, &lb);
                StringFormat rf;
                rf.SetAlignment(StringAlignmentFar);
                rf.SetLineAlignment(StringAlignmentCenter);
                std::wstring ms =
                    std::to_wstring(doc_->gif->frames[static_cast<size_t>(i)].delayMs) + L" ms";
                g.DrawString(ms.c_str(), -1, &fMs, lr, &rf, &lb);
            }
            x += L.thumbW + L.gap;
        }
        g.SetClip(&old);
    }

    // ---- 底部控制条 ----
    SolidBrush barBg(ToGpColor(dark ? RGB(40, 40, 40) : RGB(242, 242, 242)));
    g.FillRectangle(&barBg, L.bottom.left, L.bottom.top, w, L.bottom.bottom - L.bottom.top);
    Pen sep(ToGpColor(s.BorderColor()), 1);
    g.DrawLine(&sep, 0, L.bottom.top, w, L.bottom.top);

    // 横向滚动条（内容超出宽度才画）
    const int maxScroll = (std::max)(0, L.contentW - L.viewW);
    const RECT& sbr = C.scroll;
    if (maxScroll > 0 && sbr.right > sbr.left) {
        const int sbW = sbr.right - sbr.left;
        const int sbH = sbr.bottom - sbr.top;
        SolidBrush track(ToGpColor(dark ? RGB(52, 52, 52) : RGB(250, 250, 250)));
        g.FillRectangle(&track, sbr.left, sbr.top, sbW, sbH);
        Pen edge(ToGpColor(s.BorderColor()), 1);
        g.DrawRectangle(&edge, sbr.left, sbr.top, sbW - 1, sbH - 1);
        int thumbW = (std::max)(util::Scale(28, dpi),
                                static_cast<int>(static_cast<long long>(sbW) * sbW / L.contentW));
        if (thumbW > sbW) thumbW = sbW;
        const int range = sbW - thumbW;
        const int thumbX = sbr.left +
                           static_cast<int>(static_cast<long long>(doc_->gifStripScroll) *
                                            range / maxScroll);
        SolidBrush thumb(ToGpColor(dark ? RGB(100, 100, 100) : RGB(190, 190, 190)));
        g.FillRectangle(&thumb, thumbX, sbr.top + 2, thumbW, sbH - 4);
    }

    // 帧计数
    {
        FontFamily fam(L"Microsoft YaHei");
        Font f(&fam, static_cast<REAL>(util::Scale(12, dpi)), FontStyleRegular, UnitPixel);
        SolidBrush fg(ToGpColor(s.TextColor()));
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        const int cur = (n > 0) ? doc_->gifFrame + 1 : 0;
        const int delay = (n > 0)
                              ? doc_->gif->frames[static_cast<size_t>(doc_->gifFrame)].delayMs : 0;
        std::wstring txt = util::Format(L"%d/%d · %dms", cur, n, delay);
        RectF lr(static_cast<REAL>(C.counter.left), static_cast<REAL>(C.counter.top),
                 static_cast<REAL>(C.counter.right - C.counter.left),
                 static_cast<REAL>(C.counter.bottom - C.counter.top));
        g.DrawString(txt.c_str(), -1, &f, lr, &fmt, &fg);
    }

    // 图形按钮底板
    auto glyphBtn = [&](const RECT& rc, bool active) {
        SolidBrush bg(ToGpColor(active ? (dark ? RGB(70, 70, 70) : RGB(226, 226, 226))
                                      : (dark ? RGB(56, 56, 56) : RGB(250, 250, 250))));
        g.FillRectangle(&bg, static_cast<INT>(rc.left), static_cast<INT>(rc.top),
                        static_cast<INT>(rc.right - rc.left), static_cast<INT>(rc.bottom - rc.top));
        Pen p(ToGpColor(s.BorderColor()), 1);
        g.DrawRectangle(&p, static_cast<INT>(rc.left), static_cast<INT>(rc.top),
                        static_cast<INT>(rc.right - rc.left - 1),
                        static_cast<INT>(rc.bottom - rc.top - 1));
    };
    glyphBtn(C.first, gifHover_ == kGifFirst);
    glyphBtn(C.prev, gifHover_ == kGifPrev);
    glyphBtn(C.play, gifHover_ == kGifPlay);
    glyphBtn(C.next, gifHover_ == kGifNext);
    glyphBtn(C.last, gifHover_ == kGifLast);

    const Color glyphCol = ToGpColor(s.TextColor());
    const REAL gs = static_cast<REAL>(util::Scale(9, dpi));
    auto center = [&](const RECT& rc, REAL& cx, REAL& cy) {
        cx = (rc.left + rc.right) / 2.0f;
        cy = (rc.top + rc.bottom) / 2.0f;
    };
    REAL cx = 0, cy = 0;

    center(C.first, cx, cy);
    FillTriangle(g, cx - gs - 1.0f, cy - gs / 2, gs, false, glyphCol);
    FillTriangle(g, cx + 1.0f, cy - gs / 2, gs, false, glyphCol);
    center(C.prev, cx, cy);
    FillTriangle(g, cx - gs / 2, cy - gs / 2, gs, false, glyphCol);
    center(C.next, cx, cy);
    FillTriangle(g, cx - gs / 2, cy - gs / 2, gs, true, glyphCol);
    center(C.last, cx, cy);
    FillTriangle(g, cx - gs - 1.0f, cy - gs / 2, gs, true, glyphCol);
    FillTriangle(g, cx + 1.0f, cy - gs / 2, gs, true, glyphCol);
    center(C.play, cx, cy);
    if (doc_->gifPlaying) {
        SolidBrush b(glyphCol);
        g.FillRectangle(&b, cx - gs * 0.55f, cy - gs * 0.55f, gs * 0.42f, gs * 1.1f);
        g.FillRectangle(&b, cx + gs * 0.13f, cy - gs * 0.55f, gs * 0.42f, gs * 1.1f);
    } else {
        FillTriangle(g, cx - gs * 0.45f, cy - gs * 0.55f, gs * 1.1f, true, glyphCol);
    }

    // 「循环」开关：左边文字，右边滑动钮（灰=关，主题色=开）
    {
        const bool hover = (gifHover_ == kGifLoop);
        const bool on = doc_->gifLoop;
        const int dpi = CanvasDpi(hwnd_);
        const int swW = util::Scale(34, dpi);
        const int swH = util::Scale(18, dpi);
        const int knobIn = util::Scale(2, dpi);
        RECT sw;
        sw.right = C.loop.right - util::Scale(4, dpi);
        sw.left = sw.right - swW;
        sw.top = C.loop.top + (C.loop.bottom - C.loop.top - swH) / 2;
        sw.bottom = sw.top + swH;

        // 轨道（两端半圆 + 中间矩形拼出胶囊形）
        COLORREF trackCol = on ? s.themeColor
                               : (dark ? RGB(96, 96, 96) : RGB(198, 198, 198));
        if (hover && !on) trackCol = dark ? RGB(118, 118, 118) : RGB(178, 178, 178);
        SolidBrush tb(ToGpColor(trackCol));
        g.FillEllipse(&tb, sw.left, sw.top, swH, swH);
        g.FillEllipse(&tb, sw.right - swH, sw.top, swH, swH);
        g.FillRectangle(&tb, sw.left + swH / 2, sw.top, swW - swH, swH);

        // 滑块
        const int knob = swH - knobIn * 2;
        const int kx = on ? (sw.right - knobIn - knob) : (sw.left + knobIn);
        SolidBrush kb(Color(255, 255, 255, 255));
        g.FillEllipse(&kb, kx, sw.top + knobIn, knob, knob);

        // 文字
        FontFamily fam(L"Microsoft YaHei");
        Font f(&fam, static_cast<REAL>(util::Scale(12, dpi)), FontStyleRegular, UnitPixel);
        SolidBrush fg(ToGpColor(s.TextColor()));
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentNear);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF lr(static_cast<REAL>(C.loop.left), static_cast<REAL>(C.loop.top),
                 static_cast<REAL>(sw.left - C.loop.left - util::Scale(4, dpi)),
                 static_cast<REAL>(C.loop.bottom - C.loop.top));
        g.DrawString(L"循环", -1, &f, lr, &fmt, &fg);
    }
    DrawGifButton(g, C.saveFrame, L"保存帧", false, false, false);
    DrawGifButton(g, C.saveTab, L"存为页签", false, false, false);
    DrawGifButton(g, C.saveGif, L"保存GIF", false, false, false);
}

void Canvas::GifMouseDown(int x, int y) {
    if (!IsGifDoc()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    GifControls C = ComputeGifControls(rc.right, rc.bottom);
    POINT pt = { x, y };

    auto togglePlay = [&]() { ToggleGifPlay(); };

    if (PtInRect(&C.play, pt)) { togglePlay(); return; }
    if (PtInRect(&C.first, pt)) { SetGifFrame(0, true); return; }
    if (PtInRect(&C.prev, pt)) { SetGifFrame(doc_->gifFrame - 1, true); return; }
    if (PtInRect(&C.next, pt)) { SetGifFrame(doc_->gifFrame + 1, true); return; }
    if (PtInRect(&C.last, pt)) { SetGifFrame(doc_->GifFrameCount() - 1, true); return; }
    if (PtInRect(&C.loop, pt)) {
        doc_->gifLoop = !doc_->gifLoop;
        App::Instance().ShowStatusMessage(doc_->gifLoop ? L"已开启循环播放"
                                                        : L"已关闭循环：点播放只播一次");
        Refresh();
        return;
    }
    if (PtInRect(&C.saveFrame, pt)) {
        // 保存帧＝当前帧 + 这一帧上的标注（和保存 GIF 后看到的一致）
        App::Instance().SaveFrameInteractive(
            doc_->RenderGifFrame(doc_->gifFrame),
            doc_->name + L"_" + std::to_wstring(doc_->gifFrame + 1));
        return;
    }
    if (PtInRect(&C.saveTab, pt)) {
        auto bmp = doc_->RenderGifFrame(doc_->gifFrame);
        if (bmp) {
            const int f = doc_->gifFrame + 1;
            App::Instance().AddDocument(std::move(bmp));
            App::Instance().ShowStatusMessage(
                util::Format(L"当前帧（第 %d 帧）已存为新页签", f));
        }
        return;
    }
    if (PtInRect(&C.saveGif, pt)) {
        App::Instance().SaveDoc(App::Instance().ActiveIndex());
        return;
    }

    // 横向滚动条
    const int maxScroll = GifMaxScroll();
    if (maxScroll > 0 && PtInRect(&C.scroll, pt) && L.contentW > 0) {
        const int sbW = C.scroll.right - C.scroll.left;
        int thumbW = (std::max)(util::Scale(28, CanvasDpi(hwnd_)),
                                static_cast<int>(static_cast<long long>(sbW) * sbW / L.contentW));
        if (thumbW > sbW) thumbW = sbW;
        const int range = sbW - thumbW;
        const int thumbX = C.scroll.left +
                           static_cast<int>(static_cast<long long>(doc_->gifStripScroll) *
                                            range / maxScroll);
        if (x >= thumbX && x < thumbX + thumbW) {
            gifDragThumb_ = true;
            gifDragOffset_ = x - thumbX;
        } else {
            int target = maxScroll;
            if (range > 0) {
                target = static_cast<int>(
                    static_cast<long long>(x - C.scroll.left - thumbW / 2) * maxScroll / range);
            }
            doc_->gifStripScroll = (std::max)(0, (std::min)(target, maxScroll));
            gifDragThumb_ = true;
            gifDragOffset_ = thumbW / 2;
            Refresh();
        }
        return;
    }

    // 缩略图条：点选帧
    if (y >= L.strip.top && y < L.strip.bottom) {
        const int n = doc_->GifFrameCount();
        if (n > 0 && (L.thumbW + L.gap) > 0) {
            int idx = (x - L.padX + doc_->gifStripScroll) / (L.thumbW + L.gap);
            if (idx >= 0 && idx < n) {
                int cellLeft = L.padX + idx * (L.thumbW + L.gap) - doc_->gifStripScroll;
                if (x >= cellLeft) SetGifFrame(idx, true);
            }
        }
        return;
    }
    // 预览区：先在画面里"编辑"（标注/框选），否则当拖动平移
    if (PtInRect(&L.preview, pt)) {
        const bool wantEdit = GifWantsAnnotationEdit(x, y);
        if (wantEdit) {
            // 播放中开始标注：先停下，否则松手时可能已经跳到别的帧上去了
            if (doc_->gifPlaying) {
                doc_->gifPlaying = false;
                doc_->gifNextTick = 0;
                UpdateGifTimer();
                App::Instance().UpdateStatus();
            }
            gifEditing_ = true;
            OnMouseDown(x, y, false);
            return;
        }
        gifPanDrag_ = true;
        gifPanMoved_ = false;
        gifPanLastX_ = x;
        gifPanLastY_ = y;
    }
}

void Canvas::GifMouseMove(int x, int y) {
    if (!IsGifDoc()) return;
    // 中键拖动：任何时候都能平移画面（选了绘图工具也不挡）
    if (gifMidPan_) {
        const int dx = x - gifPanLastX_;
        const int dy = y - gifPanLastY_;
        gifPanLastX_ = x;
        gifPanLastY_ = y;
        if (dx || dy) GifPanBy(dx, dy);
        return;
    }
    // 正在画/拖标注：整条鼠标流程交给画布逻辑（坐标换算已是帧像素）
    if (gifEditing_) {
        OnMouseMove(x, y);
        return;
    }

    RECT rc;
    GetClientRect(hwnd_, &rc);
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    GifControls C = ComputeGifControls(rc.right, rc.bottom);

    if (gifDragThumb_) {
        const int maxScroll = GifMaxScroll();
        const int sbW = C.scroll.right - C.scroll.left;
        if (L.contentW > 0 && sbW > 0) {
            int thumbW = (std::max)(util::Scale(28, CanvasDpi(hwnd_)),
                                    static_cast<int>(static_cast<long long>(sbW) * sbW / L.contentW));
            if (thumbW > sbW) thumbW = sbW;
            const int range = sbW - thumbW;
            if (range > 0 && maxScroll > 0) {
                int pos = static_cast<int>(
                    static_cast<long long>(x - C.scroll.left - gifDragOffset_) * maxScroll / range);
                doc_->gifStripScroll = (std::max)(0, (std::min)(pos, maxScroll));
                Refresh();
            }
        }
        return;
    }

    POINT pt = { x, y };
    if (gifPanDrag_) {
        const int dx = x - gifPanLastX_;
        const int dy = y - gifPanLastY_;
        const int adx = dx < 0 ? -dx : dx;
        const int ady = dy < 0 ? -dy : dy;
        if (!gifPanMoved_ && (adx + ady) > util::Scale(3, CanvasDpi(hwnd_))) {
            gifPanMoved_ = true;
        }
        if (gifPanMoved_) GifPanBy(dx, dy);
        gifPanLastX_ = x;
        gifPanLastY_ = y;
        (void)pt;
        return;
    }

    int hov = kGifNone;
    if (PtInRect(&C.first, pt)) hov = kGifFirst;
    else if (PtInRect(&C.prev, pt)) hov = kGifPrev;
    else if (PtInRect(&C.play, pt)) hov = kGifPlay;
    else if (PtInRect(&C.next, pt)) hov = kGifNext;
    else if (PtInRect(&C.last, pt)) hov = kGifLast;
    else if (PtInRect(&C.loop, pt)) hov = kGifLoop;
    if (hov != gifHover_) {
        gifHover_ = hov;
        Refresh();
    }
}

void Canvas::GifMouseUp(int x, int y) {
    if (gifEditing_) {
        gifEditing_ = false;
        OnMouseUp(x, y);
        AfterGifEdit();
        return;
    }
    if (gifPanDrag_) {
        gifPanDrag_ = false;
        gifPanMoved_ = false;
        gifDragThumb_ = false;
        return;
    }
    gifDragThumb_ = false;
}

// 标注变了：当前帧缩略图要重画，状态栏的「未保存编辑」也要更新
void Canvas::AfterGifEdit() {
    if (!IsGifDoc()) return;
    if (doc_->gifFrame >= 0 &&
        doc_->gifFrame < static_cast<int>(doc_->gifThumbs.size())) {
        doc_->gifThumbs[static_cast<size_t>(doc_->gifFrame)].reset();
    }
    Refresh();
    App::Instance().UpdateStatus();
}

// 帧被增删/像素被改：解码缓存和缩略图全部失效，滚动条夹回范围，播放停下
void Canvas::AfterGifFramesChanged() {
    if (!IsGifDoc()) return;
    gifCacheIdx_ = -1;
    gifCache_.reset();
    gifCacheRev_ = doc_->gifRev;
    gifEditing_ = false;
    gifDragThumb_ = false;
    gifPanDrag_ = false;
    gifMidPan_ = false;
    gifPressed_ = 0;
    doc_->gifPlaying = false;
    doc_->gifNextTick = 0;
    doc_->gifStripScroll =
        (std::max)(0, (std::min)(doc_->gifStripScroll, GifMaxScroll()));
    UpdateGifTimer();
    Refresh();
    App::Instance().UpdateStatus();
}

// 缩略图右键菜单：删当前帧 / 删左侧全部 / 删右侧全部（GIF 文件在保存时自动更新）
void Canvas::GifRightClick(int x, int y) {
    if (!IsGifDoc()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    const int n = doc_->GifFrameCount();
    if (n <= 0) return;
    // 只在缩略图条上有反应（含序号/毫秒那一行）
    if (y < L.strip.top || y >= L.strip.bottom) return;
    if ((L.thumbW + L.gap) <= 0) return;
    int idx = (x - L.padX + doc_->gifStripScroll) / (L.thumbW + L.gap);
    if (idx < 0 || idx >= n) return;
    const int cellLeft = L.padX + idx * (L.thumbW + L.gap) - doc_->gifStripScroll;
    if (x < cellLeft || x > cellLeft + L.thumbW) return;
    if (idx != doc_->gifFrame) SetGifFrame(idx, true);

    const int leftCnt = idx;
    const int rightCnt = n - idx - 1;

    HMENU menu = CreatePopupMenu();
    const UINT delFlags = (n > 1) ? MF_STRING : (MF_STRING | MF_GRAYED);
    AppendMenuW(menu, delFlags, 1, L"删除当前帧");
    std::wstring leftText = leftCnt > 0
        ? util::Format(L"删除左侧全部帧（%d 帧）", leftCnt)
        : std::wstring(L"删除左侧全部帧");
    std::wstring rightText = rightCnt > 0
        ? util::Format(L"删除右侧全部帧（%d 帧）", rightCnt)
        : std::wstring(L"删除右侧全部帧");
    AppendMenuW(menu, (leftCnt > 0 ? MF_STRING : (MF_STRING | MF_GRAYED)), 2, leftText.c_str());
    AppendMenuW(menu, (rightCnt > 0 ? MF_STRING : (MF_STRING | MF_GRAYED)), 3, rightText.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"保存 GIF（把编辑写入文件）");

    POINT sp = { x, y };
    ClientToScreen(hwnd_, &sp);
    const int cmd = TrackPopupMenu(menu,
                                   TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                                   sp.x, sp.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (cmd == 0) return;

    int from = 0, to = 0;
    switch (cmd) {
    case 1: from = idx; to = idx; break;
    case 2: from = 0; to = idx - 1; break;
    case 3: from = idx + 1; to = n - 1; break;
    case 4: App::Instance().SaveDoc(App::Instance().ActiveIndex()); return;
    default: return;
    }
    const int delCnt = to - from + 1;
    if (delCnt <= 0) return;
    if (!doc_->GifDeleteFrames(from, to)) {
        App::Instance().ShowStatusMessage(L"至少需要保留 1 帧");
        return;
    }
    AfterGifFramesChanged();
    App::Instance().ShowStatusMessage(
        util::Format(L"已删除 %d 帧，剩余 %d 帧（保存 GIF 时写入文件）",
                     delCnt, doc_->GifFrameCount()));
}

// ---- 预览缩放 / 平移 ----
namespace {
// 适应可视区的基准倍率（大于 1 表示原图比可视区小，按 1:1 显示）
float GifFitScale(const RECT& preview, int fw, int fh, int dpi) {
    const int availW = (std::max)(16, static_cast<int>(preview.right) - util::Scale(24, dpi));
    const int availH = (std::max)(16, static_cast<int>(preview.bottom) - util::Scale(24, dpi));
    if (fw <= availW && fh <= availH) return 1.0f;
    return (std::min)(static_cast<float>(availW) / fw, static_cast<float>(availH) / fh);
}
}  // namespace

void Canvas::GifZoomAt(float factor, int mx, int my) {
    if (!IsGifDoc()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    Bitmap* frame = GifFrameBitmap(doc_->gifFrame);
    if (!frame) return;
    const int dpi = CanvasDpi(hwnd_);
    const int fw = static_cast<int>(frame->GetWidth());
    const int fh = static_cast<int>(frame->GetHeight());
    if (fw <= 0 || fh <= 0) return;

    const float fitZ = GifFitScale(L.preview, fw, fh, dpi);
    const float oldZoom = doc_->gifZoom;
    float z = oldZoom * factor;
    if (std::fabs(z - 1.0f) < 0.02f) z = 1.0f;      // 接近"适应"时吸附，方便回正
    z = (std::max)(0.25f, (std::min)(8.0f, z));
    if (std::fabs(z - oldZoom) < 1e-3f) return;

    // 让光标底下的那个图像点缩放前后停在原地
    const float oldS = fitZ * oldZoom;
    const float oldDw = fw * oldS;
    const float oldDh = fh * oldS;
    const float oldDx = L.preview.right / 2.0f - oldDw / 2.0f + doc_->gifPanX;
    const float oldDy = L.preview.bottom / 2.0f - oldDh / 2.0f + doc_->gifPanY;
    const float u = oldS > 0.0f ? (mx - oldDx) / oldS : 0.0f;
    const float v = oldS > 0.0f ? (my - oldDy) / oldS : 0.0f;

    doc_->gifZoom = z;
    const float newS = fitZ * z;
    const float newDw = fw * newS;
    const float newDh = fh * newS;
    doc_->gifPanX = mx - (L.preview.right / 2.0f - newDw / 2.0f) - u * newS;
    doc_->gifPanY = my - (L.preview.bottom / 2.0f - newDh / 2.0f) - v * newS;
    GifClampPan();
    Refresh();
    App::Instance().UpdateStatus();
}

void Canvas::GifClampPan() {
    if (!IsGifDoc()) return;
    if (doc_->gifZoom <= 1.0001f) {
        doc_->gifPanX = 0.0f;
        doc_->gifPanY = 0.0f;
        return;
    }
    RECT rc;
    GetClientRect(hwnd_, &rc);
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    Bitmap* frame = GifFrameBitmap(doc_->gifFrame);
    if (!frame) {
        doc_->gifPanX = 0.0f;
        doc_->gifPanY = 0.0f;
        return;
    }
    const int dpi = CanvasDpi(hwnd_);
    const int fw = static_cast<int>(frame->GetWidth());
    const int fh = static_cast<int>(frame->GetHeight());
    const float s = GifFitScale(L.preview, fw, fh, dpi) * doc_->gifZoom;
    const float mx = (std::max)(0.0f, (fw * s - L.preview.right) / 2.0f);
    const float my = (std::max)(0.0f, (fh * s - L.preview.bottom) / 2.0f);
    doc_->gifPanX = (std::max)(-mx, (std::min)(mx, doc_->gifPanX));
    doc_->gifPanY = (std::max)(-my, (std::min)(my, doc_->gifPanY));
}

void Canvas::GifPanBy(int dx, int dy) {
    if (!IsGifDoc()) return;
    if (dx == 0 && dy == 0) return;
    doc_->gifPanX += static_cast<float>(dx);
    doc_->gifPanY += static_cast<float>(dy);
    GifClampPan();
    Refresh();
}

void Canvas::GifWheel(int delta, int x, int y) {
    if (!IsGifDoc()) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    GifLayout L = ComputeGifLayout(rc.right, rc.bottom);
    const int dpi = CanvasDpi(hwnd_);
    // 缩略图条上：滚轮左右翻帧（保持原行为）
    if (y >= L.strip.top) {
        const int step = util::Scale(200, dpi);
        doc_->gifStripScroll -= (delta > 0 ? step : -step);
        doc_->gifStripScroll = (std::max)(0, (std::min)(doc_->gifStripScroll, GifMaxScroll()));
        Refresh();
        return;
    }
    POINT pt = { x, y };
    if (!PtInRect(&L.preview, pt)) return;
    const int step = util::Scale(60, dpi);
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        GifZoomAt(delta > 0 ? (1.0f / 1.1f) : 1.1f, x, y);   // 与原逻辑一致：上滚缩小、下滚放大
    } else if (GetKeyState(VK_SHIFT) & 0x8000) {
        GifPanBy(delta > 0 ? step : -step, 0);                // Shift+滚轮：左右平移
    } else {
        GifPanBy(0, delta > 0 ? step : -step);                // 滚轮：上下平移
    }
}

void Canvas::OnGifTimer() {
    if (!IsGifDoc() || !doc_->gifPlaying) {
        UpdateGifTimer();
        return;
    }
    const int n = doc_->GifFrameCount();
    if (n <= 1) return;
    const ULONGLONG now = GetTickCount64();
    if (doc_->gifNextTick == 0) {
        int d = doc_->gif->frames[static_cast<size_t>(doc_->gifFrame)].delayMs;
        doc_->gifNextTick = now + static_cast<ULONGLONG>((std::max)(10, d));
        return;
    }
    if (now < doc_->gifNextTick) return;

    int next = doc_->gifFrame + 1;
    if (next >= n) {
        if (doc_->gifLoop) {
            next = 0;
        } else {
            doc_->gifPlaying = false;
            doc_->gifNextTick = 0;
            UpdateGifTimer();
            Refresh();
            return;
        }
    }
    doc_->gifFrame = next;
    const int d = doc_->gif->frames[static_cast<size_t>(next)].delayMs;
    doc_->gifNextTick = now + static_cast<ULONGLONG>((std::max)(10, d));
    GifEnsureVisible();
    Refresh();
    App::Instance().UpdateStatus();
}

bool Canvas::HandleGifKey(WPARAM vk) {
    if (!IsGifDoc()) return false;
    switch (vk) {
    case VK_LEFT:
        SetGifFrame(doc_->gifFrame - 1, true);
        return true;
    case VK_RIGHT:
        SetGifFrame(doc_->gifFrame + 1, true);
        return true;
    case VK_HOME:
        SetGifFrame(0, true);
        return true;
    case VK_END:
        SetGifFrame(doc_->GifFrameCount() - 1, true);
        return true;
    case VK_SPACE: {
        ToggleGifPlay();
        return true;
    }
    case VK_DELETE:
        // 删掉选中的标注（删帧走缩略图右键菜单，避免误删整帧）
        if (doc_->GetSelected()) {
            DeleteSelection();
            AfterGifEdit();
        }
        return true;
    default:
        return false;
    }
}
