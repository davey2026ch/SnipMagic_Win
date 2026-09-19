#include "annotation.h"
#include "settings.h"

using namespace Gdiplus;

void Annotation::DrawSelection(Graphics& g) const {
    RectF rc;
    GetBounds(rc);
    rc.Inflate(3, 3);
    Pen pen(Color(255, 0, 120, 215), 1.0f);
    pen.SetDashStyle(DashStyleDash);
    g.DrawRectangle(&pen, rc);

    // 8 handles
    const float hs = 5.0f;
    SolidBrush br(Color(255, 0, 120, 215));
    PointF pts[8] = {
        {rc.X, rc.Y},
        {rc.X + rc.Width / 2, rc.Y},
        {rc.X + rc.Width, rc.Y},
        {rc.X, rc.Y + rc.Height / 2},
        {rc.X + rc.Width, rc.Y + rc.Height / 2},
        {rc.X, rc.Y + rc.Height},
        {rc.X + rc.Width / 2, rc.Y + rc.Height},
        {rc.X + rc.Width, rc.Y + rc.Height}
    };
    for (auto& p : pts) {
        g.FillRectangle(&br, p.X - hs / 2, p.Y - hs / 2, hs, hs);
    }
}

static void ApplyPenStyle(Pen& pen, const AnnStyle& s, float scaleHint = 1.0f) {
    float w = static_cast<float>(s.thickness) * scaleHint;
    if (w < 1) w = 1;
    pen.SetWidth(w);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);
    pen.SetColor(ToGpColor(s.color, s.alpha));
}

// ---- Freehand / Brush ----
std::unique_ptr<Annotation> FreehandAnn::Clone() const {
    auto a = std::make_unique<FreehandAnn>(type == AnnType::Brush);
    a->style = style;
    a->points = points;
    return a;
}

void FreehandAnn::Draw(Graphics& g) const {
    if (points.size() < 2) {
        if (points.size() == 1) {
            float r = style.thickness * 0.5f;
            SolidBrush br(ToGpColor(style.color, style.alpha));
            g.FillEllipse(&br, points[0].X - r, points[0].Y - r, r * 2, r * 2);
        }
        return;
    }
    float w = (std::max)(1.0f, static_cast<float>(style.thickness));
    Pen pen(ToGpColor(style.color, style.alpha), w);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);
    g.DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
}

bool FreehandAnn::HitTest(float x, float y) const {
    float tol = static_cast<float>(style.thickness) * 0.5f + 4.0f;
    if (points.size() == 1) {
        float dx = x - points[0].X, dy = y - points[0].Y;
        return dx * dx + dy * dy <= tol * tol;
    }
    for (size_t i = 1; i < points.size(); ++i) {
        if (util::DistToSegment(x, y, points[i - 1].X, points[i - 1].Y, points[i].X, points[i].Y) <= tol)
            return true;
    }
    return false;
}

void FreehandAnn::GetBounds(RectF& rc) const {
    if (points.empty()) { rc = RectF(0, 0, 0, 0); return; }
    float minX = points[0].X, minY = points[0].Y;
    float maxX = minX, maxY = minY;
    for (auto& p : points) {
        minX = (std::min)(minX, p.X); maxX = (std::max)(maxX, p.X);
        minY = (std::min)(minY, p.Y); maxY = (std::max)(maxY, p.Y);
    }
    float pad = style.thickness * 0.5f;
    rc = RectF(minX - pad, minY - pad, maxX - minX + pad * 2, maxY - minY + pad * 2);
}

void FreehandAnn::Move(float dx, float dy) {
    for (auto& p : points) { p.X += dx; p.Y += dy; }
}

void FreehandAnn::SetBounds(const RectF& rc) {
    RectF cur;
    GetBounds(cur);
    if (cur.Width < 1e-3f || cur.Height < 1e-3f) return;
    float sx = rc.Width / cur.Width;
    float sy = rc.Height / cur.Height;
    for (auto& p : points) {
        p.X = rc.X + (p.X - cur.X) * sx;
        p.Y = rc.Y + (p.Y - cur.Y) * sy;
    }
}

// ---- Line / Arrow ----
std::unique_ptr<Annotation> LineAnn::Clone() const {
    auto a = std::make_unique<LineAnn>(isArrow);
    a->style = style;
    a->x1 = x1; a->y1 = y1; a->x2 = x2; a->y2 = y2;
    return a;
}

void LineAnn::Draw(Graphics& g) const {
    float dx = x2 - x1, dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;

    if (!isArrow) {
        float w = (std::max)(1.0f, static_cast<float>(style.thickness));
        Pen pen(ToGpColor(style.color, style.alpha), w);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        g.DrawLine(&pen, x1, y1, x2, y2);
        return;
    }

    // WeChat-style arrow: thin tail, thick head
    float ux = dx / len, uy = dy / len;
    float nx = -uy, ny = ux;
    float bodyT = static_cast<float>(style.thickness) * 0.35f; // tail half-ish
    float headW = static_cast<float>(style.thickness) * 2.2f;
    float headL = (std::max)(12.0f, style.thickness * 4.0f);
    if (headL > len * 0.7f) headL = len * 0.7f;

    float hx = x2, hy = y2;
    float bx = x2 - ux * headL, by = y2 - uy * headL;

    // body polygon (tapered): from tail to arrow base
    PointF body[4] = {
        {x1 + nx * bodyT, y1 + ny * bodyT},
        {bx + nx * bodyT, by + ny * bodyT},
        {bx - nx * bodyT, by - ny * bodyT},
        {x1 - nx * bodyT, y1 - ny * bodyT}
    };
    SolidBrush br(ToGpColor(style.color, style.alpha));
    g.FillPolygon(&br, body, 4);

    // head triangle
    PointF head[3] = {
        {hx, hy},
        {bx + nx * headW, by + ny * headW},
        {bx - nx * headW, by - ny * headW}
    };
    g.FillPolygon(&br, head, 3);
}

bool LineAnn::HitTest(float x, float y) const {
    float tol = static_cast<float>(style.thickness) * 0.5f + 5.0f;
    return util::DistToSegment(x, y, x1, y1, x2, y2) <= tol;
}

void LineAnn::GetBounds(RectF& rc) const {
    float pad = static_cast<float>(style.thickness) + (isArrow ? style.thickness * 2.0f : 0.0f) + 2;
    rc = util::NormalizeRectF(x1 - pad, y1 - pad, x2 + pad, y2 + pad);
}

void LineAnn::Move(float dx, float dy) {
    x1 += dx; y1 += dy; x2 += dx; y2 += dy;
}

void LineAnn::SetBounds(const RectF& rc) {
    RectF cur;
    GetBounds(cur);
    float pad = static_cast<float>(style.thickness) + (isArrow ? style.thickness * 2.0f : 0.0f) + 2;
    // Map original endpoints into new rect (approximately using content box)
    RectF content(cur.X + pad, cur.Y + pad, (std::max)(1.0f, cur.Width - pad * 2), (std::max)(1.0f, cur.Height - pad * 2));
    RectF ncontent(rc.X + pad, rc.Y + pad, (std::max)(1.0f, rc.Width - pad * 2), (std::max)(1.0f, rc.Height - pad * 2));
    float sx = ncontent.Width / content.Width;
    float sy = ncontent.Height / content.Height;
    x1 = ncontent.X + (x1 - content.X) * sx;
    y1 = ncontent.Y + (y1 - content.Y) * sy;
    x2 = ncontent.X + (x2 - content.X) * sx;
    y2 = ncontent.Y + (y2 - content.Y) * sy;
}

// ---- Shapes ----
std::unique_ptr<Annotation> MakeShape(AnnType t) {
    return std::make_unique<ShapeAnn>(t);
}

std::unique_ptr<Annotation> ShapeAnn::Clone() const {
    auto a = std::make_unique<ShapeAnn>(type);
    a->style = style;
    a->rect = rect;
    a->filled = filled;
    a->rounded = rounded;
    a->ellipse = ellipse;
    return a;
}

void ShapeAnn::Draw(Graphics& g) const {
    if (rect.Width < 0.5f || rect.Height < 0.5f) return;
    Color c = ToGpColor(style.color, style.alpha);
    if (filled) {
        SolidBrush br(c);
        if (ellipse) {
            g.FillEllipse(&br, rect);
        } else if (rounded) {
            float r = (std::min)(rect.Width, rect.Height) * 0.2f;
            if (r < 1) r = 1;
            GraphicsPath path;
            // rounded rectangle path
            float x = rect.X, y = rect.Y, w = rect.Width, h = rect.Height;
            path.AddArc(x, y, r * 2, r * 2, 180, 90);
            path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
            path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
            path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
            path.CloseFigure();
            g.FillPath(&br, &path);
        } else {
            g.FillRectangle(&br, rect);
        }
    } else {
        float w = (std::max)(1.0f, static_cast<float>(style.thickness));
        Pen pen(ToGpColor(style.color, style.alpha), w);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        pen.SetLineJoin(LineJoinRound);
        if (ellipse) {
            g.DrawEllipse(&pen, rect);
        } else if (rounded) {
            float r = (std::min)(rect.Width, rect.Height) * 0.2f;
            if (r < 1) r = 1;
            GraphicsPath path;
            float x = rect.X, y = rect.Y, ww = rect.Width, h = rect.Height;
            path.AddArc(x, y, r * 2, r * 2, 180, 90);
            path.AddArc(x + ww - r * 2, y, r * 2, r * 2, 270, 90);
            path.AddArc(x + ww - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
            path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
            path.CloseFigure();
            g.DrawPath(&pen, &path);
        } else {
            g.DrawRectangle(&pen, rect);
        }
    }
}

bool ShapeAnn::HitTest(float x, float y) const {
    if (filled) return util::PtInRectF(rect, x, y);
    // outline hit
    RectF outer = rect;
    RectF inner = rect;
    float t = static_cast<float>(style.thickness) + 4.0f;
    outer.Inflate(t, t);
    inner.Inflate(-t, -t);
    if (!util::PtInRectF(outer, x, y)) return false;
    if (inner.Width > 0 && inner.Height > 0 && util::PtInRectF(inner, x, y)) return false;
    return true;
}

void ShapeAnn::GetBounds(RectF& rc) const {
    float pad = filled ? 0.0f : static_cast<float>(style.thickness) * 0.5f;
    rc = RectF(rect.X - pad, rect.Y - pad, rect.Width + pad * 2, rect.Height + pad * 2);
}

void ShapeAnn::Move(float dx, float dy) {
    rect.X += dx; rect.Y += dy;
}

void ShapeAnn::SetBounds(const RectF& rc) {
    rect = rc;
}

// ---- Text ----
std::unique_ptr<Annotation> TextAnn::Clone() const {
    auto a = std::make_unique<TextAnn>();
    a->style = style;
    a->text = text;
    a->fontSize = fontSize;
    a->bold = bold;
    a->transparentBg = transparentBg;
    a->bgColor = bgColor;
    a->rect = rect;
    return a;
}

void TextAnn::Measure(Graphics& g) {
    FontFamily family(L"Microsoft YaHei");
    INT styleBits = FontStyleRegular | (bold ? FontStyleBold : 0);
    Font font(&family, fontSize, styleBits, UnitPixel);
    StringFormat fmt;
    fmt.SetTrimming(StringTrimmingNone);
    // 默认最大宽度，避免插入后变成极长一条；之后可用角点调整
    const float maxW = 260.0f;
    RectF layout(0, 0, maxW, 0);
    RectF bound;
    g.MeasureString(text.c_str(), -1, &font, layout, &fmt, &bound);
    float pad = 6.0f;
    rect.Width = (std::min)(maxW, bound.Width + pad * 2);
    if (rect.Width < fontSize) rect.Width = fontSize;
    rect.Height = (std::max)(bound.Height + pad * 2, fontSize * 1.2f);
}

void TextAnn::Draw(Graphics& g) const {
    FontFamily family(L"Microsoft YaHei");
    INT styleBits = FontStyleRegular | (bold ? FontStyleBold : 0);
    Font font(&family, fontSize, styleBits, UnitPixel);

    if (!transparentBg) {
        SolidBrush bg(ToGpColor(bgColor, 255));
        g.FillRectangle(&bg, rect);
    }

    SolidBrush br(ToGpColor(style.color, style.alpha));
    StringFormat fmt;
    fmt.SetFormatFlags(StringFormatFlagsNoClip);
    fmt.SetTrimming(StringTrimmingNone);
    RectF layout = rect;
    layout.X += 4;
    layout.Y += 4;
    layout.Width = (std::max)(8.0f, rect.Width - 8);
    layout.Height = (std::max)(8.0f, rect.Height - 8);
    // 按框宽度自动换行，便于用角点调整布局
    g.DrawString(text.c_str(), -1, &font, layout, &fmt, &br);
}

bool TextAnn::HitTest(float x, float y) const {
    return util::PtInRectF(rect, x, y);
}

void TextAnn::GetBounds(RectF& rc) const {
    rc = rect;
}

void TextAnn::Move(float dx, float dy) {
    rect.X += dx; rect.Y += dy;
}

void TextAnn::SetBounds(const RectF& rc) {
    rect = rc;
}

// ---- Number ----
std::unique_ptr<Annotation> NumberAnn::Clone() const {
    auto a = std::make_unique<NumberAnn>();
    a->style = style;
    a->number = number;
    a->cx = cx; a->cy = cy; a->radius = radius;
    return a;
}

void NumberAnn::Draw(Graphics& g) const {
    float r = radius;
    SolidBrush br(ToGpColor(style.color, style.alpha));
    g.FillEllipse(&br, cx - r, cy - r, r * 2, r * 2);

    // white border ring
    Pen pen(Color(255, 255, 255, 255), 2.0f);
    g.DrawEllipse(&pen, cx - r, cy - r, r * 2, r * 2);

    wchar_t buf[8] = {};
    if (number >= 1 && number <= 20) {
        // Use ASCII number for crisp rendering; circled unicode may miss font
        swprintf_s(buf, L"%d", number);
    } else {
        swprintf_s(buf, L"%d", number);
    }
    FontFamily family(L"Segoe UI");
    float fs = r * 1.2f;
    Font font(&family, fs, FontStyleBold, UnitPixel);
    SolidBrush tb(Color(255, 255, 255, 255));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    RectF layout(cx - r, cy - r, r * 2, r * 2);
    g.DrawString(buf, -1, &font, layout, &fmt, &tb);
}

bool NumberAnn::HitTest(float x, float y) const {
    float dx = x - cx, dy = y - cy;
    float r = radius + 4.0f;
    return dx * dx + dy * dy <= r * r;
}

void NumberAnn::GetBounds(RectF& rc) const {
    rc = RectF(cx - radius, cy - radius, radius * 2, radius * 2);
}

void NumberAnn::Move(float dx, float dy) {
    cx += dx; cy += dy;
}

void NumberAnn::SetBounds(const RectF& rc) {
    cx = rc.X + rc.Width / 2;
    cy = rc.Y + rc.Height / 2;
    radius = (std::max)(6.0f, (std::min)(rc.Width, rc.Height) * 0.5f);
}

// ---- Image ----
std::unique_ptr<Annotation> ImageAnn::Clone() const {
    auto a = std::make_unique<ImageAnn>();
    a->style = style;
    a->rect = rect;
    if (image) {
        a->image = std::unique_ptr<Bitmap>(image->Clone(0, 0, image->GetWidth(), image->GetHeight(), PixelFormat32bppARGB));
    }
    return a;
}

void ImageAnn::Draw(Graphics& g) const {
    if (!image) return;
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(image.get(), rect);
}

bool ImageAnn::HitTest(float x, float y) const {
    return util::PtInRectF(rect, x, y);
}

void ImageAnn::GetBounds(RectF& rc) const {
    rc = rect;
}

void ImageAnn::Move(float dx, float dy) {
    rect.X += dx; rect.Y += dy;
}

void ImageAnn::SetBounds(const RectF& rc) {
    rect = rc;
}

const wchar_t* AnnTypeLabel(AnnType t) {
    switch (t) {
    case AnnType::Freehand: return L"画笔";
    case AnnType::Brush: return L"笔刷";
    case AnnType::Line: return L"直线";
    case AnnType::Arrow: return L"箭头";
    case AnnType::Rect: return L"矩形框";
    case AnnType::RoundRect: return L"圆角矩形框";
    case AnnType::Ellipse: return L"椭圆";
    case AnnType::FilledRect: return L"实心矩形";
    case AnnType::FilledRoundRect: return L"实心圆角矩形";
    case AnnType::FilledEllipse: return L"实心椭圆";
    case AnnType::Text: return L"文字";
    case AnnType::Number: return L"序号";
    case AnnType::Image: return L"图像";
    }
    return L"?";
}
