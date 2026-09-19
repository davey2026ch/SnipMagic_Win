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

    PushUndo();
    auto composite = RenderComposite();
    if (!composite) return false;
    auto patch = util::PixelateBitmap(composite.get(), x, y, w, h, mosaicSize);
    if (!patch) return false;

    auto img = std::make_unique<ImageAnn>();
    img->image = std::move(patch);
    img->rect = RectF(static_cast<REAL>(x), static_cast<REAL>(y),
                      static_cast<REAL>(img->image->GetWidth()),
                      static_cast<REAL>(img->image->GetHeight()));
    img->style.color = RGB(0, 0, 0);

    if (fromRegion) {
        ClearSelection();
        img->selected = true;
        selectedIdx = static_cast<int>(annotations.size());
        annotations.push_back(std::move(img));
    } else {
        annotations[selectedIdx] = std::move(img);
    }
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

bool Document::CopySelectionToClipboard(Bitmap** outInternal) {
    if (outInternal) *outInternal = nullptr;
    if (!base) return false;

    int x = 0, y = 0, w = 0, h = 0;
    if (!GetRegion(x, y, w, h)) {
        Annotation* sel = GetSelected();
        if (sel) {
            RectF b;
            sel->GetBounds(b);
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

    // 外部剪贴板：默认加 1px 内边框，避免浅色背景上截图边缘看不出来；
    // 应用内粘贴缓冲保持原图（不带边框）。
    auto bordered = util::AddInnerBorder(crop.get(), Gdiplus::Color(255, 160, 160, 160));
    util::BitmapToClipboard(bordered ? bordered.get() : crop.get());
    GlobalPasteBuffer().Set(std::unique_ptr<Bitmap>(
        crop->Clone(0, 0, crop->GetWidth(), crop->GetHeight(), PixelFormat32bppARGB)));
    return true;
}
