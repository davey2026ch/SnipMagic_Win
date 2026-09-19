#pragma once
#include "util.h"

using namespace Gdiplus;

enum class AnnType {
    Freehand,   // 画笔
    Brush,      // 笔刷
    Line,
    Arrow,
    Rect,
    RoundRect,
    Ellipse,
    FilledRect,
    FilledRoundRect,
    FilledEllipse,
    Text,
    Number,
    Image       // 粘贴 / 马赛克贴片
};

struct AnnStyle {
    COLORREF color = RGB(255, 0, 0);
    int thickness = 4;
    BYTE alpha = 255;
};

class Annotation {
public:
    AnnType type;
    AnnStyle style;
    bool selected = false;

    explicit Annotation(AnnType t) : type(t) {}
    virtual ~Annotation() = default;

    virtual std::unique_ptr<Annotation> Clone() const = 0;
    virtual void Draw(Graphics& g) const = 0;
    virtual bool HitTest(float x, float y) const = 0;
    virtual void GetBounds(RectF& rc) const = 0;
    virtual void Move(float dx, float dy) = 0;
    virtual void SetBounds(const RectF& rc) = 0;

    void DrawSelection(Graphics& g) const;
};

// ---- concrete annotations ----

class FreehandAnn : public Annotation {
public:
    std::vector<PointF> points;
    FreehandAnn() : Annotation(AnnType::Freehand) {}
    explicit FreehandAnn(bool isBrush)
        : Annotation(isBrush ? AnnType::Brush : AnnType::Freehand) {}
    std::unique_ptr<Annotation> Clone() const override;
    void Draw(Graphics& g) const override;
    bool HitTest(float x, float y) const override;
    void GetBounds(RectF& rc) const override;
    void Move(float dx, float dy) override;
    void SetBounds(const RectF& rc) override;
};

class LineAnn : public Annotation {
public:
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    bool isArrow = false;
    LineAnn() : Annotation(AnnType::Line) {}
    explicit LineAnn(bool arrow) : Annotation(arrow ? AnnType::Arrow : AnnType::Line), isArrow(arrow) {}
    std::unique_ptr<Annotation> Clone() const override;
    void Draw(Graphics& g) const override;
    bool HitTest(float x, float y) const override;
    void GetBounds(RectF& rc) const override;
    void Move(float dx, float dy) override;
    void SetBounds(const RectF& rc) override;
};

class ShapeAnn : public Annotation {
public:
    RectF rect{0, 0, 0, 0};
    bool filled = false;
    bool rounded = false;
    bool ellipse = false;
    ShapeAnn() : Annotation(AnnType::Rect) {}
    ShapeAnn(AnnType t) : Annotation(t) {
        filled = (t == AnnType::FilledRect || t == AnnType::FilledRoundRect || t == AnnType::FilledEllipse);
        rounded = (t == AnnType::RoundRect || t == AnnType::FilledRoundRect);
        ellipse = (t == AnnType::Ellipse || t == AnnType::FilledEllipse);
    }
    std::unique_ptr<Annotation> Clone() const override;
    void Draw(Graphics& g) const override;
    bool HitTest(float x, float y) const override;
    void GetBounds(RectF& rc) const override;
    void Move(float dx, float dy) override;
    void SetBounds(const RectF& rc) override;
};

class TextAnn : public Annotation {
public:
    std::wstring text;
    float fontSize = 20.0f;
    bool bold = false;
    bool transparentBg = true;
    COLORREF bgColor = RGB(255, 255, 255);
    RectF rect{0, 0, 200, 40};
    TextAnn() : Annotation(AnnType::Text) {}
    std::unique_ptr<Annotation> Clone() const override;
    void Draw(Graphics& g) const override;
    bool HitTest(float x, float y) const override;
    void GetBounds(RectF& rc) const override;
    void Move(float dx, float dy) override;
    void SetBounds(const RectF& rc) override;

    // Measure preferred size
    void Measure(Graphics& g);
};

class NumberAnn : public Annotation {
public:
    int number = 1; // 1..20
    float cx = 0, cy = 0;
    float radius = 14.0f;
    NumberAnn() : Annotation(AnnType::Number) {}
    std::unique_ptr<Annotation> Clone() const override;
    void Draw(Graphics& g) const override;
    bool HitTest(float x, float y) const override;
    void GetBounds(RectF& rc) const override;
    void Move(float dx, float dy) override;
    void SetBounds(const RectF& rc) override;
};

class ImageAnn : public Annotation {
public:
    std::unique_ptr<Bitmap> image;
    RectF rect{0, 0, 0, 0};
    ImageAnn() : Annotation(AnnType::Image) {}
    std::unique_ptr<Annotation> Clone() const override;
    void Draw(Graphics& g) const override;
    bool HitTest(float x, float y) const override;
    void GetBounds(RectF& rc) const override;
    void Move(float dx, float dy) override;
    void SetBounds(const RectF& rc) override;
};

// Factory helpers
std::unique_ptr<Annotation> MakeShape(AnnType t);
const wchar_t* AnnTypeLabel(AnnType t);
