#include "document.h"
#include "settings.h"

using namespace Gdiplus;

PasteBuffer& GlobalPasteBuffer() {
    static PasteBuffer buf;
    return buf;
}

void Document::PushUndo() {
    if (!base) return;
    Snapshot s;
    s.annotations.reserve(annotations.size());
    for (const auto& a : annotations) s.annotations.push_back(a->Clone());
    s.base = std::unique_ptr<Bitmap>(
        base->Clone(0, 0, base->GetWidth(), base->GetHeight(), PixelFormat32bppARGB));
    undoStack.push_back(std::move(s));
    if (undoStack.size() > kMaxUndo) undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void Document::Undo() {
    if (undoStack.empty() || !base) return;
    // save current to redo
    Snapshot cur;
    cur.annotations.reserve(annotations.size());
    for (const auto& a : annotations) cur.annotations.push_back(a->Clone());
    cur.base = std::unique_ptr<Bitmap>(
        base->Clone(0, 0, base->GetWidth(), base->GetHeight(), PixelFormat32bppARGB));
    redoStack.push_back(std::move(cur));

    auto& snap = undoStack.back();
    annotations = std::move(snap.annotations);
    if (snap.base) base = std::move(snap.base);
    undoStack.pop_back();
    selectedIdx = -1;
}

void Document::Redo() {
    if (redoStack.empty() || !base) return;
    Snapshot cur;
    cur.annotations.reserve(annotations.size());
    for (const auto& a : annotations) cur.annotations.push_back(a->Clone());
    cur.base = std::unique_ptr<Bitmap>(
        base->Clone(0, 0, base->GetWidth(), base->GetHeight(), PixelFormat32bppARGB));
    undoStack.push_back(std::move(cur));

    auto& snap = redoStack.back();
    annotations = std::move(snap.annotations);
    if (snap.base) base = std::move(snap.base);
    redoStack.pop_back();
    selectedIdx = -1;
}

void Document::ClearSelection() {
    for (auto& a : annotations) a->selected = false;
    selectedIdx = -1;
}

Annotation* Document::GetSelected() {
    if (selectedIdx < 0 || selectedIdx >= static_cast<int>(annotations.size())) return nullptr;
    return annotations[selectedIdx].get();
}

int Document::HitTest(float x, float y) const {
    for (int i = static_cast<int>(annotations.size()) - 1; i >= 0; --i) {
        if (annotations[i]->HitTest(x, y)) return i;
    }
    return -1;
}

void Document::DrawAnnotations(Graphics& g, bool forExport) const {
    for (const auto& a : annotations) {
        a->Draw(g);
        // 导出 / 查看态由调用方保证 selected 已清空；此处仍避免 forExport 时画控制点
        if (!forExport && a->selected) a->DrawSelection(g);
    }
}

std::unique_ptr<Bitmap> Document::RenderComposite() const {
    if (!base) return nullptr;
    int w = Width(), h = Height();
    auto out = std::make_unique<Bitmap>(w, h, PixelFormat32bppARGB);
    Graphics g(out.get());
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.DrawImage(base.get(), 0, 0, w, h);
    DrawAnnotations(g, true);
    return out;
}

bool Document::SaveAs(const std::wstring& path, bool jpg) const {
    auto bmp = RenderComposite();
    if (!bmp) return false;
    return util::SaveBitmapToFile(bmp.get(), path, jpg);
}

void Document::DeleteSelected() {
    if (selectedIdx < 0 || selectedIdx >= static_cast<int>(annotations.size())) return;
    PushUndo();
    annotations.erase(annotations.begin() + selectedIdx);
    selectedIdx = -1;
}

bool Document::ApplyMosaic(int mosaicSize) {
    if (!base) return false;

    int x = 0, y = 0, w = 0, h = 0;
    bool fromRegion = GetRegion(x, y, w, h);
    if (!fromRegion) {
        if (selectedIdx < 0) return false;
        Annotation* sel = annotations[selectedIdx].get();
        if (!sel) return false;
        RectF bounds;
        sel->GetBounds(bounds);
        x = static_cast<int>(std::floor(bounds.X));
        y = static_cast<int>(std::floor(bounds.Y));
        w = static_cast<int>(std::ceil(bounds.Width));
        h = static_cast<int>(std::ceil(bounds.Height));
    }
    if (w <= 0 || h <= 0) return false;

    // clamp region into base bounds
    int bw = static_cast<int>(base->GetWidth());
    int bh = static_cast<int>(base->GetHeight());
    int cx = (std::max)(0, x);
    int cy = (std::max)(0, y);
    int cw = (std::min)(bw - cx, w);
    int ch = (std::min)(bh - cy, h);
    if (cw <= 0 || ch <= 0) return false;

    PushUndo();
    // 对底图该区域做像素化，然后直接烙回 base：不生成可选中/可拖动的标注图层
    auto patch = util::PixelateBitmap(base.get(), cx, cy, cw, ch, mosaicSize);
    if (!patch) return false;
    {
        Gdiplus::Graphics g(base.get());
        g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.DrawImage(patch.get(), Gdiplus::Rect(cx, cy, cw, ch),
                    0, 0, cw, ch, Gdiplus::UnitPixel);
    }
    patch.reset();

    if (!fromRegion && selectedIdx >= 0) {
        // 选中图层整体打码：图层被烙进底图后移除原标注
        annotations.erase(annotations.begin() + selectedIdx);
        selectedIdx = -1;
    }
    ClearSelection();
    ClearRegion();
    return true;
}

std::unique_ptr<ImageAnn> Document::CreatePasteFrom(std::unique_ptr<Bitmap> bmp, float atX, float atY) {
    if (!bmp) return nullptr;
    auto img = std::make_unique<ImageAnn>();
    img->image = std::move(bmp);
    float w = static_cast<float>(img->image->GetWidth());
    float h = static_cast<float>(img->image->GetHeight());
    if (atX < 0) atX = (std::max)(0.0f, (Width() - w) * 0.5f);
    if (atY < 0) atY = (std::max)(0.0f, (Height() - h) * 0.5f);
    img->rect = RectF(atX, atY, w, h);
    return img;
}

int Document::FlattenImageLayers(bool pushUndo) {
    if (!base) return 0;
    int count = 0;
    for (const auto& a : annotations) {
        if (a && a->type == AnnType::Image) ++count;
    }
    if (count == 0) return 0;
    if (pushUndo) PushUndo();
    {
        // 与导出合成(RenderComposite)相同的绘制设置，保证烙进去的效果和看到的一致
        Graphics g(base.get());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        for (const auto& a : annotations) {
            if (a && a->type == AnnType::Image) a->Draw(g);
        }
    }
    annotations.erase(
        std::remove_if(annotations.begin(), annotations.end(),
                       [](const std::unique_ptr<Annotation>& a) {
                           return a && a->type == AnnType::Image;
                       }),
        annotations.end());
    selectedIdx = -1;
    return count;
}

bool Document::CopySelectionToClipboard(Bitmap** outInternal) {
    if (outInternal) *outInternal = nullptr;
    if (!base) return false;

    int x = 0, y = 0, w = 0, h = 0;
    if (!GetRegion(x, y, w, h)) {
        // 选中浮动图片图层：直接复制图层自身位图——透明底原样保留
        //（不走合成渲染，否则透明区会被底图像素填充；外部剪贴板也不加内边框）
        Annotation* sel = GetSelected();
        if (sel && sel->type == AnnType::Image) {
            auto* ia = static_cast<ImageAnn*>(sel);
            if (ia->image) {
                // 应用内粘贴缓冲：图层自身位图——透明底原样保留，绝不加边框
                auto internal = std::unique_ptr<Bitmap>(
                    ia->image->Clone(0, 0, ia->image->GetWidth(), ia->image->GetHeight(),
                                     PixelFormat32bppARGB));
                if (!internal) return false;
                // 外部剪贴板：按「带边框复制到外部软件」设置决定是否加内边框；
                // 含透明像素的图保持原样，避免在透明底边缘画出悬空的边框线
                auto external = std::unique_ptr<Bitmap>(
                    internal->Clone(0, 0, internal->GetWidth(), internal->GetHeight(),
                                    PixelFormat32bppARGB));
                if (external && Settings().borderCopyToExternal &&
                    !util::BitmapHasAlpha(internal.get())) {
                    if (auto bordered = util::AddInnerBorder(internal.get(),
                                                             Gdiplus::Color(255, 160, 160, 160))) {
                        external = std::move(bordered);
                    }
                }
                util::BitmapToClipboard(external.get()); // CF_DIB + PNG + HTML Format
                GlobalPasteBuffer().Set(std::move(internal));
                return true;
            }
            return false;
        }
        Annotation* selAny = GetSelected();
        if (selAny) {
            RectF b;
            selAny->GetBounds(b);
            x = static_cast<int>(std::floor(b.X));
            y = static_cast<int>(std::floor(b.Y));
            w = static_cast<int>(std::ceil(b.Width));
            h = static_cast<int>(std::ceil(b.Height));
        } else {
            // fallback: whole image
            x = 0; y = 0; w = Width(); h = Height();
        }
    }
    if (w <= 0 || h <= 0) return false;

    auto composite = RenderComposite();
    if (!composite) return false;
    auto crop = util::CropBitmap(composite.get(), x, y, w, h);
    if (!crop) return false;

    // 外部剪贴板：按「带边框复制到外部软件」设置决定是否加 1px 内边框
    //（避免浅色背景上截图边缘看不出来）；应用内粘贴缓冲始终原图（不带边框）。
    std::unique_ptr<Bitmap> external;
    if (Settings().borderCopyToExternal) {
        external = util::AddInnerBorder(crop.get(), Gdiplus::Color(255, 160, 160, 160));
    }
    if (!external) {
        external.reset(crop->Clone(0, 0, crop->GetWidth(), crop->GetHeight(),
                                   PixelFormat32bppARGB));
    }
    util::BitmapToClipboard(external.get()); // CF_DIB + PNG + HTML Format
    GlobalPasteBuffer().Set(std::unique_ptr<Bitmap>(
        crop->Clone(0, 0, crop->GetWidth(), crop->GetHeight(), PixelFormat32bppARGB)));
    return true;
}
