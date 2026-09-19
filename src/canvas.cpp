#include "canvas.h"
#include "settings.h"
#include "textdlg.h"
#include "colorpicker.h"
#include "app.h"

using namespace Gdiplus;

namespace {
const wchar_t* kCanvasClass = L"ScreenshotToolCanvas";
bool g_canvasClassReg = false;

void RegCanvasClass(HINSTANCE hi) {
    if (g_canvasClassReg) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Canvas::WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kCanvasClass;
    RegisterClassExW(&wc);
    g_canvasClassReg = true;
}

AnnType ToolToAnnType(Tool t) {
    switch (t) {
    case Tool::Arrow: return AnnType::Arrow;
    case Tool::Line: return AnnType::Line;
    case Tool::Rect: return AnnType::Rect;
    case Tool::RoundRect: return AnnType::RoundRect;
    case Tool::Ellipse: return AnnType::Ellipse;
    case Tool::FilledRect: return AnnType::FilledRect;
    case Tool::FilledRoundRect: return AnnType::FilledRoundRect;
    case Tool::FilledEllipse: return AnnType::FilledEllipse;
    case Tool::Freehand: return AnnType::Freehand;
    case Tool::Brush: return AnnType::Brush;
    default: return AnnType::Rect;
    }
}
} // namespace

Canvas& Canvas::Instance() {
    static Canvas c;
    return c;
}

bool Canvas::Create(HWND parent, HINSTANCE hi) {
    RegCanvasClass(hi);
    hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        kCanvasClass, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 100, 100,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)), hi, this);
    if (hwnd_) {
        ShowScrollBar(hwnd_, SB_BOTH, FALSE);
    }
    return hwnd_ != nullptr;
}

LRESULT CALLBACK Canvas::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Canvas* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Canvas*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<Canvas*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->Handle(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Canvas::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: OnPaint(); return 0;
    case WM_SIZE: UpdateScrollBars(); Refresh(); return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        SetCapture(hwnd);
        OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
        return 0;
    case WM_RBUTTONDOWN:
        OnMouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
        return 0;
    case WM_MOUSEMOVE:
        if (pickingColor_) {
            // global eyedropper live sample handled in colorpicker
            return 0;
        }
        OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        OnMouseUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONDBLCLK:
        OnDoubleClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_HSCROLL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hwnd, SB_HORZ, &si);
        int pos = si.nPos;
        int code = LOWORD(wParam);
        switch (code) {
        case SB_LINELEFT: pos -= 20; break;
        case SB_LINERIGHT: pos += 20; break;
        case SB_PAGELEFT: pos -= static_cast<int>(si.nPage); break;
        case SB_PAGERIGHT: pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = si.nMin; break;
        case SB_BOTTOM: pos = si.nMax; break;
        default: break;
        }
        si.fMask = SIF_POS;
        si.nPos = pos;
        SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
        GetScrollInfo(hwnd, SB_HORZ, &si);
        if (doc_) doc_->scrollX = si.nPos;
        Refresh();
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hwnd, SB_VERT, &si);
        int pos = si.nPos;
        int code = LOWORD(wParam);
        switch (code) {
        case SB_LINEUP: pos -= 20; break;
        case SB_LINEDOWN: pos += 20; break;
        case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = si.nMin; break;
        case SB_BOTTOM: pos = si.nMax; break;
        default: break;
        }
        si.fMask = SIF_POS;
        si.nPos = pos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd, SB_VERT, &si);
        if (doc_) doc_->scrollY = si.nPos;
        Refresh();
        return 0;
    }
    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void Canvas::EnsureBackbuffer(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (memDc_ && memW_ == w && memH_ == h) return;
    if (memDc_) {
        SelectObject(memDc_, nullptr);
        DeleteObject(memBm_);
        DeleteDC(memDc_);
        memDc_ = nullptr; memBm_ = nullptr; bits_ = nullptr;
    }
    HDC hdc = GetDC(hwnd_);
    memDc_ = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    memBm_ = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits_, nullptr, 0);
    SelectObject(memDc_, memBm_);
    memW_ = w; memH_ = h;
    ReleaseDC(hwnd_, hdc);
}

void Canvas::Refresh() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Canvas::UpdateScrollBars() {
    if (!hwnd_) return;

    auto hideBar = [&](int bar) {
        ShowScrollBar(hwnd_, bar, FALSE);
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0; si.nMax = 0; si.nPos = 0; si.nPage = 0;
        SetScrollInfo(hwnd_, bar, &si, TRUE);
    };

    if (!doc_ || !doc_->base) {
        hideBar(SB_HORZ);
        hideBar(SB_VERT);
        return;
    }

    RECT rc;
    GetClientRect(hwnd_, &rc);
    float z = doc_->zoom;
    int m = Margin();
    int contentW = static_cast<int>(doc_->Width() * z) + m * 2;
    int contentH = static_cast<int>(doc_->Height() * z) + m * 2;

    if (contentW <= rc.right) {
        doc_->scrollX = 0;
        hideBar(SB_HORZ);
    } else {
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = contentW - 1;
        si.nPage = static_cast<UINT>(rc.right);
        si.nPos = doc_->scrollX;
        ShowScrollBar(hwnd_, SB_HORZ, TRUE);
        SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
        GetScrollInfo(hwnd_, SB_HORZ, &si);
        doc_->scrollX = si.nPos;
    }

    if (contentH <= rc.bottom) {
        doc_->scrollY = 0;
        hideBar(SB_VERT);
    } else {
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = contentH - 1;
        si.nPage = static_cast<UINT>(rc.bottom);
        si.nPos = doc_->scrollY;
        ShowScrollBar(hwnd_, SB_VERT, TRUE);
        SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd_, SB_VERT, &si);
        doc_->scrollY = si.nPos;
    }
}

void Canvas::ClientToImage(int cx, int cy, float& ix, float& iy) const {
    float z = doc_ ? doc_->zoom : 1.0f;
    int m = Margin();
    ix = (cx + (doc_ ? doc_->scrollX : 0) - m) / z;
    iy = (cy + (doc_ ? doc_->scrollY : 0) - m) / z;
}

void Canvas::ImageToClient(float ix, float iy, int& cx, int& cy) const {
    float z = doc_ ? doc_->zoom : 1.0f;
    int m = Margin();
    cx = static_cast<int>(ix * z + m - (doc_ ? doc_->scrollX : 0));
    cy = static_cast<int>(iy * z + m - (doc_ ? doc_->scrollY : 0));
}

bool Canvas::IsEditMode() const {
    return tool_ != Tool::View && doc_ != nullptr;
}

void Canvas::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) { EndPaint(hwnd_, &ps); return; }

    EnsureBackbuffer(w, h);
    Graphics g(memDc_);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    COLORREF bg = Settings().CanvasBg();
    SolidBrush bgBrush(ToGpColor(bg));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    if (!doc_ || !doc_->base) {
        FontFamily family(L"Microsoft YaHei");
        Font font(&family, 16, FontStyleRegular, UnitPixel);
        SolidBrush fg(ToGpColor(Settings().TextColor()));
        const wchar_t* msg = L"点击「截图」或按 Ctrl+Shift+R 开始截图";
        RectF layout;
        g.MeasureString(msg, -1, &font, PointF(0, 0), &layout);
        g.DrawString(msg, -1, &font,
                     PointF((w - layout.Width) / 2, (h - layout.Height) / 2),
                     &fg);
        BitBlt(hdc, 0, 0, w, h, memDc_, 0, 0, SRCCOPY);
        EndPaint(hwnd_, &ps);
        return;
    }

    float z = doc_->zoom;
    int m = Margin();
    int ix = m - doc_->scrollX;
    int iy = m - doc_->scrollY;
    int iw = static_cast<int>(doc_->Width() * z);
    int ih = static_cast<int>(doc_->Height() * z);

    // shadow + image
    SolidBrush shadow(Color(60, 0, 0, 0));
    g.FillRectangle(&shadow, ix + 3, iy + 4, iw, ih);
    g.SetInterpolationMode(z >= 1.0f ? InterpolationModeNearestNeighbor
                                      : InterpolationModeHighQualityBicubic);
    g.DrawImage(doc_->base.get(), Rect(ix, iy, iw, ih),
                0, 0, doc_->Width(), doc_->Height(), UnitPixel);

    // annotations in image space scaled
    GraphicsState st = g.Save();
    g.TranslateTransform(static_cast<REAL>(ix), static_cast<REAL>(iy));
    g.ScaleTransform(z, z);
    if (draft_) draft_->Draw(g);
    doc_->DrawAnnotations(g, false);

    // rubber-band or stored region
    if (dragMode_ == DragMode::Rubber || doc_->hasRegion) {
        float l, t, r, b;
        if (dragMode_ == DragMode::Rubber) {
            l = (std::min)(startIx_, lastIx_);
            t = (std::min)(startIy_, lastIy_);
            r = (std::max)(startIx_, lastIx_);
            b = (std::max)(startIy_, lastIy_);
        } else {
            l = doc_->regionL; t = doc_->regionT;
            r = doc_->regionR; b = doc_->regionB;
        }
        RectF region(l, t, r - l, b - t);
        Pen pen(Color(220, 0, 120, 215), 1.5f / (std::max)(0.01f, z));
        pen.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&pen, region);
        // corner ticks
        float tick = 6.0f / z;
        Pen tickPen(Color(255, 0, 120, 215), 2.0f / z);
        g.DrawLine(&tickPen, l, t, l + tick, t);
        g.DrawLine(&tickPen, l, t, l, t + tick);
        g.DrawLine(&tickPen, r, t, r - tick, t);
        g.DrawLine(&tickPen, r, t, r, t + tick);
        g.DrawLine(&tickPen, l, b, l + tick, b);
        g.DrawLine(&tickPen, l, b, l, b - tick);
        g.DrawLine(&tickPen, r, b, r - tick, b);
        g.DrawLine(&tickPen, r, b, r, b - tick);
    }
    g.Restore(st);

    // size tip near rubber band
    if (dragMode_ == DragMode::Rubber) {
        int sw = static_cast<int>(std::fabs(lastIx_ - startIx_));
        int sh = static_cast<int>(std::fabs(lastIy_ - startIy_));
        if (sw > 2 || sh > 2) {
            wchar_t tip[64];
            swprintf_s(tip, L"%d × %d", sw, sh);
            FontFamily family(L"Segoe UI");
            Font font(&family, 12, FontStyleBold, UnitPixel);
            int tx, ty;
            ImageToClient((std::max)(startIx_, lastIx_), (std::min)(startIy_, lastIy_), tx, ty);
            ty -= 28;
            if (ty < 4) ty = 4;
            SolidBrush bg(Color(200, 20, 20, 20));
            SolidBrush fg(Color(255, 255, 255, 255));
            g.FillRectangle(&bg, tx, ty, 80, 22);
            g.DrawString(tip, -1, &font, PointF(static_cast<REAL>(tx + 6), static_cast<REAL>(ty + 2)), &fg);
        }
    }

    BitBlt(hdc, 0, 0, w, h, memDc_, 0, 0, SRCCOPY);
    EndPaint(hwnd_, &ps);
}

void Canvas::ZoomBy(float factor) {
    SetZoom(doc_ ? doc_->zoom * factor : factor);
}

void Canvas::ZoomAt(float factor, int screenX, int screenY) {
    if (!doc_) return;
    float oldZ = doc_->zoom;
    float newZ = oldZ * factor;
    // snap near 100%
    if (std::fabs(newZ - 1.0f) < 0.03f) newZ = 1.0f;
    newZ = (std::max)(0.10f, (std::min)(8.0f, newZ));
    if (std::fabs(newZ - oldZ) < 1e-4f) return;

    float ix, iy;
    ClientToImage(screenX, screenY, ix, iy);
    doc_->zoom = newZ;
    int m = Margin();
    doc_->scrollX = static_cast<int>(ix * newZ + m - screenX);
    doc_->scrollY = static_cast<int>(iy * newZ + m - screenY);
    if (doc_->scrollX < 0) doc_->scrollX = 0;
    if (doc_->scrollY < 0) doc_->scrollY = 0;
    UpdateScrollBars();
    Refresh();
    App::Instance().UpdateStatus();
}

void Canvas::SetZoom(float z) {
    if (!doc_) return;
    if (std::fabs(z - 1.0f) < 0.03f) z = 1.0f;
    doc_->zoom = (std::max)(0.10f, (std::min)(8.0f, z));
    UpdateScrollBars();
    Refresh();
    App::Instance().UpdateStatus();
}

void Canvas::OnMouseWheel(int /*x*/, int /*y*/, int delta) {
    // Spec: Ctrl+wheel: up = zoom out, down = zoom in
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd_, &pt);
        float factor = delta > 0 ? (1.0f / 1.1f) : 1.1f;
        ZoomAt(factor, pt.x, pt.y);
    } else {
        // scroll
        SCROLLINFO si = { sizeof(si), SIF_ALL };
        GetScrollInfo(hwnd_, SB_VERT, &si);
        si.nPos -= (delta > 0 ? 40 : -40);
        si.fMask = SIF_POS;
        SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd_, SB_VERT, &si);
        if (doc_) doc_->scrollY = si.nPos;
        Refresh();
    }
}

void Canvas::OnMouseDown(int x, int y, bool right) {
    if (!doc_ || !doc_->base) return;
    float ix, iy;
    ClientToImage(x, y, ix, iy);
    startIx_ = lastIx_ = ix;
    startIy_ = lastIy_ = iy;

    if (right) {
        draft_.reset();
        doc_->ClearSelection();
        doc_->ClearRegion();
        dragMode_ = DragMode::None;
        Refresh();
        return;
    }

    if (tool_ == Tool::View) return;

    if (tool_ == Tool::Select) {
        // resize handle?
        if (doc_->GetSelected()) {
            auto h = HitResizeHandle(ix, iy);
            if (h != HandleId::None) {
                dragMode_ = DragMode::Resize;
                activeHandle_ = h;
                resizeStartBounds_ = {};
                doc_->GetSelected()->GetBounds(resizeStartBounds_);
                return;
            }
        }
        int hit = doc_->HitTest(ix, iy);
        if (hit >= 0) {
            doc_->ClearSelection();
            doc_->ClearRegion();
            doc_->selectedIdx = hit;
            doc_->annotations[hit]->selected = true;
            dragMode_ = DragMode::Move;
            moveOriginX_ = ix;
            moveOriginY_ = iy;
            moveBackup_ = doc_->annotations[hit]->Clone();
            doc_->PushUndo();
        } else {
            // rubber-band region selection
            doc_->ClearSelection();
            doc_->ClearRegion();
            dragMode_ = DragMode::Rubber;
        }
        Refresh();
        App::Instance().UpdateStatus();
        return;
    }

    if (tool_ == Tool::Text) {
        // 点到已有文字 → 回填编辑；否则新建
        int hit = doc_->HitTest(ix, iy);
        if (hit >= 0 && doc_->annotations[hit]->type == AnnType::Text) {
            auto* t = static_cast<TextAnn*>(doc_->annotations[hit].get());
            TextDialogResult tr = TextDialog::Show(hwnd_, t->style.color, t);
            if (tr.ok && !tr.text.empty()) {
                doc_->PushUndo();
                t->text = tr.text;
                t->fontSize = tr.fontSize;
                t->bold = tr.bold;
                t->transparentBg = tr.transparentBg;
                t->style.color = tr.color;
                t->style.alpha = tr.alpha;
                auto tmp = std::make_unique<Bitmap>(1, 1, PixelFormat32bppARGB);
                Graphics mg(tmp.get());
                t->Measure(mg);
                doc_->ClearSelection();
                t->selected = true;
                doc_->selectedIdx = hit;
            }
            Refresh();
            return;
        }
        TextDialogResult tr = TextDialog::Show(hwnd_, Settings().drawColor);
        if (tr.ok && !tr.text.empty()) {
            doc_->PushUndo();
            auto t = std::make_unique<TextAnn>();
            t->text = tr.text;
            t->fontSize = tr.fontSize;
            t->bold = tr.bold;
            t->transparentBg = tr.transparentBg;
            t->style.color = tr.color;
            t->style.alpha = tr.alpha;
            t->rect = RectF(ix, iy, 10, 10);
            {
                auto tmp = std::make_unique<Bitmap>(1, 1, PixelFormat32bppARGB);
                Graphics mg(tmp.get());
                t->Measure(mg);
            }
            t->selected = true;
            doc_->ClearSelection();
            int idx = static_cast<int>(doc_->annotations.size());
            doc_->annotations.push_back(std::move(t));
            doc_->selectedIdx = idx;
        }
        Refresh();
        return;
    }

    if (tool_ == Tool::Number) {
        doc_->PushUndo();
        auto n = std::make_unique<NumberAnn>();
        n->number = number_;
        n->cx = ix;
        n->cy = iy;
        n->radius = 14.0f;
        n->style.color = color_;
        n->style.alpha = alpha_;
        doc_->ClearSelection();
        int idx = static_cast<int>(doc_->annotations.size());
        n->selected = true;
        doc_->annotations.push_back(std::move(n));
        doc_->selectedIdx = idx;
        Refresh();
        return;
    }

    // drawing tools
    BeginDraw(ix, iy);
}

void Canvas::BeginDraw(float ix, float iy) {
    AnnType at = ToolToAnnType(tool_);
    if (tool_ == Tool::Freehand || tool_ == Tool::Brush) {
        auto f = std::make_unique<FreehandAnn>(tool_ == Tool::Brush);
        f->style.color = color_;
        f->style.alpha = alpha_;
        f->style.thickness = (tool_ == Tool::Brush) ? Settings().brushThickness : Settings().lineThickness;
        f->points.push_back(PointF(ix, iy));
        draft_ = std::move(f);
    } else if (tool_ == Tool::Arrow || tool_ == Tool::Line) {
        auto l = std::make_unique<LineAnn>(tool_ == Tool::Arrow);
        l->x1 = l->x2 = ix;
        l->y1 = l->y2 = iy;
        l->style.color = color_;
        l->style.alpha = alpha_;
        l->style.thickness = Settings().lineThickness;
        draft_ = std::move(l);
    } else {
        auto s = MakeShape(at);
        if (auto* sh = dynamic_cast<ShapeAnn*>(s.get())) {
            sh->rect = RectF(ix, iy, 0, 0);
        }
        s->style.color = color_;
        s->style.alpha = alpha_;
        s->style.thickness = Settings().lineThickness;
        draft_ = std::move(s);
    }
    dragMode_ = DragMode::Draw;
    Refresh();
}

void Canvas::OnMouseMove(int x, int y) {
    if (!doc_) return;
    float ix, iy;
    ClientToImage(x, y, ix, iy);
    lastIx_ = ix; lastIy_ = iy;

    if (dragMode_ == DragMode::Draw && draft_) {
        if (auto* f = dynamic_cast<FreehandAnn*>(draft_.get())) {
            f->points.push_back(PointF(ix, iy));
        } else if (auto* l = dynamic_cast<LineAnn*>(draft_.get())) {
            l->x2 = ix; l->y2 = iy;
            if (util::IsShiftDown()) util::ConstrainAngle(l->x1, l->y1, l->x2, l->y2);
        } else if (auto* sh = dynamic_cast<ShapeAnn*>(draft_.get())) {
            float x2 = ix, y2 = iy;
            if (util::IsShiftDown()) util::ConstrainSquare(startIx_, startIy_, x2, y2);
            sh->rect = util::NormalizeRectF(startIx_, startIy_, x2, y2);
        }
        Refresh();
        return;
    }

    if (dragMode_ == DragMode::Move && moveBackup_) {
        if (doc_ && doc_->selectedIdx >= 0 &&
            doc_->selectedIdx < static_cast<int>(doc_->annotations.size())) {
            float dx = ix - moveOriginX_;
            float dy = iy - moveOriginY_;
            doc_->annotations[doc_->selectedIdx] = moveBackup_->Clone();
            doc_->annotations[doc_->selectedIdx]->selected = true;
            doc_->annotations[doc_->selectedIdx]->Move(dx, dy);
        }
        Refresh();
        return;
    }

    if (dragMode_ == DragMode::Rubber) {
        lastIx_ = ix;
        lastIy_ = iy;
        Refresh();
        return;
    }

    if (dragMode_ == DragMode::Resize) {
        Annotation* sel = doc_->GetSelected();
        if (sel) ResizeSelected(activeHandle_, ix, iy);
        Refresh();
        return;
    }

    // status tip
    App::Instance().SetCanvasHover(ix, iy);
}

// Fix move: store origin at mouse down
// We'll patch OnMouseDown/OnMouseMove for move mode properly in a cleaner way.
// Using startIx_/startIy_ and a cloned original position.

void Canvas::OnMouseUp(int x, int y) {
    if (!doc_) return;
    float ix, iy;
    ClientToImage(x, y, ix, iy);

    if (dragMode_ == DragMode::Draw && draft_) {
        bool add = true;
        if (auto* f = dynamic_cast<FreehandAnn*>(draft_.get())) {
            add = f->points.size() >= 1;
        } else if (auto* l = dynamic_cast<LineAnn*>(draft_.get())) {
            float dx = l->x2 - l->x1, dy = l->y2 - l->y1;
            add = (dx * dx + dy * dy) > 4.0f;
        } else if (auto* sh = dynamic_cast<ShapeAnn*>(draft_.get())) {
            add = sh->rect.Width > 2 || sh->rect.Height > 2;
        }
        if (add) {
            PushAndAdd(std::move(draft_));
        } else {
            draft_.reset();
        }
        dragMode_ = DragMode::None;
        Refresh();
        App::Instance().UpdateStatus();
        return;
    }

    if (dragMode_ == DragMode::Move || dragMode_ == DragMode::Resize) {
        dragMode_ = DragMode::None;
        Refresh();
        return;
    }

    if (dragMode_ == DragMode::Rubber && doc_) {
        float sw = std::fabs(ix - startIx_);
        float sh = std::fabs(iy - startIy_);
        if (sw >= 2 && sh >= 2) {
            doc_->SetRegion(startIx_, startIy_, ix, iy);
            App::Instance().ShowStatusMessage(
                util::Format(L"已框选区域 %d×%d，可马赛克 / Ctrl+C 复制",
                             static_cast<int>(sw), static_cast<int>(sh)));
        } else {
            doc_->ClearRegion();
        }
        dragMode_ = DragMode::None;
        Refresh();
        App::Instance().UpdateStatus();
        return;
    }
    dragMode_ = DragMode::None;
}

// Need move to work: enhance OnMouseDown to clone selected before move, and OnMouseMove applies delta from start.
// I'll re-implement move in OnMouseMove using start positions stored at down time.

void Canvas::OnDoubleClick(int x, int y) {
    if (!doc_ || tool_ == Tool::View) return;
    float ix, iy;
    ClientToImage(x, y, ix, iy);
    int hit = doc_->HitTest(ix, iy);
    if (hit < 0) return;
    auto* ann = doc_->annotations[hit].get();
    if (ann->type == AnnType::Text) {
        auto* t = static_cast<TextAnn*>(ann);
        TextDialogResult tr = TextDialog::Show(hwnd_, t->style.color, t);
        if (tr.ok && !tr.text.empty()) {
            doc_->PushUndo();
            t->text = tr.text;
            t->fontSize = tr.fontSize;
            t->bold = tr.bold;
            t->transparentBg = tr.transparentBg;
            t->style.color = tr.color;
            t->style.alpha = tr.alpha;
            auto tmp = std::make_unique<Bitmap>(1, 1, PixelFormat32bppARGB);
            Graphics mg(tmp.get());
            t->Measure(mg);
            doc_->ClearSelection();
            t->selected = true;
            doc_->selectedIdx = hit;
            Refresh();
        }
    }
}

void Canvas::OnKeyDown(WPARAM vk) {
    if (!doc_) return;
    if (vk == VK_DELETE) {
        DeleteSelection();
    } else if (vk == VK_ESCAPE) {
        draft_.reset();
        doc_->ClearSelection();
        doc_->ClearRegion();
        dragMode_ = DragMode::None;
        Refresh();
    }
}

Canvas::HandleId Canvas::HitResizeHandle(float ix, float iy) const {
    Annotation* sel = doc_ ? doc_->GetSelected() : nullptr;
    if (!sel) return HandleId::None;
    RectF b;
    sel->GetBounds(b);
    b.Inflate(3, 3);
    const float tol = 8.0f;
    PointF pts[8] = {
        {b.X, b.Y},
        {b.X + b.Width / 2, b.Y},
        {b.X + b.Width, b.Y},
        {b.X, b.Y + b.Height / 2},
        {b.X + b.Width, b.Y + b.Height / 2},
        {b.X, b.Y + b.Height},
        {b.X + b.Width / 2, b.Y + b.Height},
        {b.X + b.Width, b.Y + b.Height}
    };
    for (int i = 0; i < 8; ++i) {
        if (std::fabs(pts[i].X - ix) <= tol && std::fabs(pts[i].Y - iy) <= tol)
            return static_cast<HandleId>(i);
    }
    return HandleId::None;
}

void Canvas::ResizeSelected(HandleId h, float ix, float iy) {
    Annotation* sel = doc_->GetSelected();
    if (!sel) return;
    if (sel->type == AnnType::Line || sel->type == AnnType::Arrow) {
        auto* l = static_cast<LineAnn*>(sel);
        if (h == HandleId::NW || h == HandleId::SW || h == HandleId::W || h == HandleId::N) {
            l->x1 = ix; l->y1 = iy;
        } else {
            l->x2 = ix; l->y2 = iy;
        }
        return;
    }
    if (sel->type == AnnType::Number) {
        auto* n = static_cast<NumberAnn*>(sel);
        float dx = ix - n->cx, dy = iy - n->cy;
        n->radius = (std::max)(6.0f, std::sqrt(dx * dx + dy * dy));
        return;
    }

    RectF b = resizeStartBounds_;
    float l = b.X, t = b.Y, r = b.X + b.Width, bt = b.Y + b.Height;
    switch (h) {
    case HandleId::NW: l = ix; t = iy; break;
    case HandleId::N:  t = iy; break;
    case HandleId::NE: r = ix; t = iy; break;
    case HandleId::E:  r = ix; break;
    case HandleId::SE: r = ix; bt = iy; break;
    case HandleId::S:  bt = iy; break;
    case HandleId::SW: l = ix; bt = iy; break;
    case HandleId::W:  l = ix; break;
    default: break;
    }
    RectF nb = util::NormalizeRectF(l, t, r, bt);
    if (nb.Width < 2) nb.Width = 2;
    if (nb.Height < 2) nb.Height = 2;
    sel->SetBounds(nb);
}

void Canvas::PushAndAdd(std::unique_ptr<Annotation> ann) {
    if (!doc_ || !ann) return;
    doc_->PushUndo();
    doc_->ClearSelection();
    ann->selected = true;
    int idx = static_cast<int>(doc_->annotations.size());
    doc_->annotations.push_back(std::move(ann));
    doc_->selectedIdx = idx;
}

void Canvas::CopySelection() {
    if (!doc_) return;
    doc_->CopySelectionToClipboard(nullptr);
    App::Instance().ShowStatusMessage(L"已复制到剪贴板（可粘贴到外部或当前页签）");
}

void Canvas::PasteFromBuffer() {
    if (!doc_) return;
    auto bmp = GlobalPasteBuffer().Clone();
    if (!bmp) {
        App::Instance().ShowStatusMessage(L"剪贴板中没有可粘贴的选区");
        return;
    }
    float ix, iy;
    ClientToImage(80, 80, ix, iy); // paste near top-left visible area
    ix = (std::max)(0.0f, ix);
    iy = (std::max)(0.0f, iy);
    auto img = doc_->CreatePasteFrom(std::move(bmp), ix, iy);
    if (!img) return;
    PushAndAdd(std::move(img));
    Refresh();
    App::Instance().ShowStatusMessage(L"已粘贴图像，可拖动调整位置");
}

void Canvas::ApplyMosaicToSelection() {
    if (!doc_) return;
    if (!doc_->hasRegion && doc_->selectedIdx < 0) {
        App::Instance().ShowStatusMessage(L"请先用「选择」框选要打码的区域");
        return;
    }
    if (doc_->ApplyMosaic(Settings().mosaicSize)) {
        Refresh();
        App::Instance().ShowStatusMessage(L"马赛克已应用");
    }
}

void Canvas::DeleteSelection() {
    if (!doc_) return;
    if (doc_->selectedIdx < 0) return;
    doc_->DeleteSelected();
    Refresh();
}

void Canvas::Undo() {
    if (!doc_) return;
    doc_->Undo();
    Refresh();
}

void Canvas::Redo() {
    if (!doc_) return;
    doc_->Redo();
    Refresh();
}
