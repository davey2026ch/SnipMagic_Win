#include "document.h"
#include "settings.h"
#include <cstring>

using namespace Gdiplus;

namespace {

// 把一张位图整帧压缩成 GIF 帧（像素 → BGRA 连续缓冲 → 量化 + LZW）
bool CompressBitmapToFrame(Bitmap* bmp, int delayMs, gifcodec::Frame& out) {
    if (!bmp) return false;
    const int w = static_cast<int>(bmp->GetWidth());
    const int h = static_cast<int>(bmp->GetHeight());
    if (w <= 0 || h <= 0) return false;
    Rect rc(0, 0, w, h);
    BitmapData bd;
    if (bmp->LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) return false;
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 4);
    const size_t rowBytes = static_cast<size_t>(w) * 4;
    // 行距可能带对齐填充：只拷每行的有效像素，CompressFrame 要求 stride = w*4
    for (int y = 0; y < h; ++y) {
        const void* src = static_cast<uint8_t*>(bd.Scan0) + static_cast<size_t>(y) * bd.Stride;
        memcpy(buf.data() + static_cast<size_t>(y) * rowBytes, src, rowBytes);
    }
    bmp->UnlockBits(&bd);
    return gifcodec::CompressFrame(buf.data(), w, h, delayMs, out);
}

} // namespace

PasteBuffer& GlobalPasteBuffer() {
    static PasteBuffer buf;
    return buf;
}

Document::Document(std::shared_ptr<gifcodec::Anim> anim, int documentId, std::wstring docName)
    : id(documentId), name(std::move(docName)), gif(std::move(anim)) {
    gifFrame = 0;
    gifPlaying = false;   // 录完不自播，等用户点播放
    gifLoop = false;   // 默认不循环：点播放只播一次，打开「循环」开关后才循环

    if (gif && !gif->Empty()) {
        base = gif->Decode(0);
        if (base) {
            gifThumbs.resize(gif->frames.size());
        }
    }
}

void Document::SyncGifBaseToCurrentFrame() {
    if (!IsGif()) return;
    // base 只影响显示与导出，直接换成当前帧解码结果（GIF 页签不参与标注编辑）
    auto bmp = gif->Decode(gifFrame);
    if (bmp) base = std::move(bmp);
}

std::unique_ptr<Bitmap> Document::CurrentGifFrame() const {
    if (!IsGif()) return nullptr;
    return gif->Decode(gifFrame);
}

// ---------------------------------------------------------------------------
// GIF 帧内编辑：标注按帧存、按帧烙
// ---------------------------------------------------------------------------

void Document::GifEnsureAnnSlots() {
    if (!gif) return;
    const size_t n = gif->frames.size();
    if (gifFrameAnns.size() != n) {
        // 变长（正常不会发生；删帧走 GifDeleteFrames 同步删槽位）
        gifFrameAnns.resize(n);
    }
}

void Document::GifStashAnnotations() {
    if (!IsGif()) return;
    GifEnsureAnnSlots();
    if (gifFrame < 0 || gifFrame >= static_cast<int>(gifFrameAnns.size())) return;
    if (annotations.empty()) {
        gifFrameAnns[static_cast<size_t>(gifFrame)].clear();
        return;
    }
    gifFrameAnns[static_cast<size_t>(gifFrame)] = std::move(annotations);
    annotations.clear();
    selectedIdx = -1;
}

void Document::GifLoadAnnotations() {
    if (!IsGif()) return;
    GifEnsureAnnSlots();
    annotations.clear();
    selectedIdx = -1;
    if (gifFrame >= 0 && gifFrame < static_cast<int>(gifFrameAnns.size()))
        annotations = std::move(gifFrameAnns[static_cast<size_t>(gifFrame)]);
}

bool Document::GifFrameHasEdits(int idx) const {
    if (!IsGif() || idx < 0 || idx >= GifFrameCount()) return false;
    if (idx == gifFrame) return !annotations.empty();
    if (idx >= static_cast<int>(gifFrameAnns.size())) return false;
    return !gifFrameAnns[static_cast<size_t>(idx)].empty();
}

int Document::GifEditedFrameCount() const {
    if (!IsGif()) return 0;
    int n = 0;
    const int cnt = GifFrameCount();
    for (int i = 0; i < cnt; ++i) {
        const bool edited = (i == gifFrame)
            ? !annotations.empty()
            : (i < static_cast<int>(gifFrameAnns.size())
               && !gifFrameAnns[static_cast<size_t>(i)].empty());
        if (edited) ++n;
    }
    return n;
}

bool Document::GifAnyEdits() const { return GifEditedFrameCount() > 0; }

std::unique_ptr<Bitmap> Document::RenderGifFrame(int idx) const {
    if (!IsGif()) return nullptr;
    if (idx < 0) idx = 0;
    if (idx >= GifFrameCount()) idx = GifFrameCount() - 1;
    auto bmp = gif->Decode(idx);
    if (!bmp) return nullptr;

    const std::vector<std::unique_ptr<Annotation>>* list = nullptr;
    if (idx == gifFrame) list = &annotations;
    else if (idx < static_cast<int>(gifFrameAnns.size()))
        list = &gifFrameAnns[static_cast<size_t>(idx)];
    if (!list || list->empty()) return bmp;

    Graphics g(bmp.get());
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    for (const auto& a : *list) {
        if (a) a->Draw(g);
    }
    return bmp;
}

int Document::GifBakeAll() {
    if (!IsGif()) return 0;
    GifEnsureAnnSlots();
    // 当前帧的标注先落进槽位，后面按帧统一处理
    GifStashAnnotations();

    int baked = 0;
    for (int i = 0; i < GifFrameCount(); ++i) {
        auto& list = gifFrameAnns[static_cast<size_t>(i)];
        if (list.empty()) continue;
        auto bmp = gif->Decode(i);
        if (!bmp) continue;
        {
            Graphics g(bmp.get());
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            for (const auto& a : list) {
                if (a) a->Draw(g);
            }
        }
        gifcodec::Frame f;
        if (!CompressBitmapToFrame(bmp.get(), gif->frames[static_cast<size_t>(i)].delayMs, f))
            continue;   // 压缩失败：标注留着，不至于白画
        f.delayMs = gif->frames[static_cast<size_t>(i)].delayMs;
        gif->frames[static_cast<size_t>(i)] = std::move(f);
        list.clear();
        ++baked;
    }

    // 没烙成功的帧（极少见）把当前帧的标注放回画布，用户还能继续编辑
    if (gifFrame >= 0 && gifFrame < static_cast<int>(gifFrameAnns.size()) &&
        !gifFrameAnns[static_cast<size_t>(gifFrame)].empty()) {
        annotations = std::move(gifFrameAnns[static_cast<size_t>(gifFrame)]);
    }

    if (baked > 0) {
        selectedIdx = -1;
        ClearRegion();
        gifThumbs.clear();
        ++gifRev;
        undoStack.clear();
        redoStack.clear();
        // 尺寸不变，base 只是显示用副本；换成当前帧解码结果，状态栏/缩略图一致
        if (auto b = gif->Decode(gifFrame)) base = std::move(b);
    }
    return baked;
}

bool Document::GifMosaicCurrentFrame(int x, int y, int w, int h, int mosaicSize) {
    if (!IsGif() || w <= 0 || h <= 0) return false;
    if (gifFrame < 0 || gifFrame >= GifFrameCount()) return false;
    auto bmp = gif->Decode(gifFrame);
    if (!bmp) return false;

    const int bw = static_cast<int>(bmp->GetWidth());
    const int bh = static_cast<int>(bmp->GetHeight());
    const int cx = (std::max)(0, x);
    const int cy = (std::max)(0, y);
    const int cw = (std::min)(bw - cx, w);
    const int ch = (std::min)(bh - cy, h);
    if (cw <= 0 || ch <= 0) return false;

    auto patch = util::PixelateBitmap(bmp.get(), cx, cy, cw, ch, mosaicSize);
    if (!patch) return false;
    {
        Graphics g(bmp.get());
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.DrawImage(patch.get(), Rect(cx, cy, cw, ch), 0, 0, cw, ch, UnitPixel);
    }
    patch.reset();

    gifcodec::Frame f;
    if (!CompressBitmapToFrame(bmp.get(), gif->frames[static_cast<size_t>(gifFrame)].delayMs, f))
        return false;
    f.delayMs = gif->frames[static_cast<size_t>(gifFrame)].delayMs;
    gif->frames[static_cast<size_t>(gifFrame)] = std::move(f);
    if (gifFrame < static_cast<int>(gifThumbs.size()))
        gifThumbs[static_cast<size_t>(gifFrame)].reset();
    ++gifRev;
    return true;
}

bool Document::GifDeleteFrames(int from, int to) {
    if (!IsGif()) return false;
    const int n = GifFrameCount();
    from = (std::max)(0, from);
    to = (std::min)(n - 1, to);
    if (from > to) return false;
    const int cnt = to - from + 1;
    const int rest = n - cnt;
    if (rest < 1) return false;   // 至少留一帧

    GifEnsureAnnSlots();
    GifStashAnnotations();        // 当前帧标注先落槽位，便于同步删除

    int newCur = gifFrame;
    if (gifFrame >= from && gifFrame <= to) newCur = from;      // 当前帧被删：停在原位置
    else if (gifFrame > to) newCur = gifFrame - cnt;
    newCur = (std::max)(0, (std::min)(newCur, rest - 1));

    gif->frames.erase(gif->frames.begin() + from, gif->frames.begin() + to + 1);
    gifFrameAnns.erase(gifFrameAnns.begin() + from, gifFrameAnns.begin() + to + 1);

    gifFrame = newCur;
    gifStripScroll = 0;
    gifPlaying = false;
    gifNextTick = 0;
    annotations.clear();
    selectedIdx = -1;
    GifLoadAnnotations();
    gifThumbs.clear();
    ClearRegion();
    undoStack.clear();
    redoStack.clear();
    ++gifRev;
    if (auto b = gif->Decode(gifFrame)) base = std::move(b);
    return true;
}

void Document::PushUndo() {
    if (!base) return;
    Snapshot s;
    s.annotations.reserve(annotations.size());
    for (const auto& a : annotations) s.annotations.push_back(a->Clone());
    s.base = std::unique_ptr<Bitmap>(
        base->Clone(0, 0, base->GetWidth(), base->GetHeight(), PixelFormat32bppARGB));
    // GIF：记下当前帧的压缩数据，撤销马赛克这类「改像素」的操作时能还原
    if (IsGif() && gifFrame >= 0 && gifFrame < GifFrameCount()) {
        s.gifIdx = gifFrame;
        s.gifFrameData = gif->frames[static_cast<size_t>(gifFrame)];
    }
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
    if (IsGif() && gifFrame >= 0 && gifFrame < GifFrameCount()) {
        cur.gifIdx = gifFrame;
        cur.gifFrameData = gif->frames[static_cast<size_t>(gifFrame)];
    }
    redoStack.push_back(std::move(cur));

    auto& snap = undoStack.back();
    annotations = std::move(snap.annotations);
    if (snap.base) base = std::move(snap.base);
    if (snap.gifIdx >= 0 && snap.gifIdx < GifFrameCount()) {
        gif->frames[static_cast<size_t>(snap.gifIdx)] = snap.gifFrameData;
        gifThumbs.clear();
        ++gifRev;
    }
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
    if (IsGif() && gifFrame >= 0 && gifFrame < GifFrameCount()) {
        cur.gifIdx = gifFrame;
        cur.gifFrameData = gif->frames[static_cast<size_t>(gifFrame)];
    }
    undoStack.push_back(std::move(cur));

    auto& snap = redoStack.back();
    annotations = std::move(snap.annotations);
    if (snap.base) base = std::move(snap.base);
    if (snap.gifIdx >= 0 && snap.gifIdx < GifFrameCount()) {
        gif->frames[static_cast<size_t>(snap.gifIdx)] = snap.gifFrameData;
        gifThumbs.clear();
        ++gifRev;
    }
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

bool Document::SaveAs(const std::wstring& path, bool jpg) {
    // GIF 动图页签：先把这一轮在画面上的标注（箭头、文字、马赛克…）烙进对应帧，
    // 再写 .gif —— 用户「编辑后保存」的内容就自动进到 GIF 文件里。
    if (IsGif()) {
        GifBakeAll();
        return gifcodec::WriteGif(path, *gif);
    }
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

int Document::FlattenAll(bool pushUndo) {
    if (!base || annotations.empty()) return 0;
    if (pushUndo) PushUndo();
    {
        // 与导出合成(RenderComposite)相同的绘制设置，保证烙进去的效果和看到的一致
        Graphics g(base.get());
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        DrawAnnotations(g, true);
    }
    int count = static_cast<int>(annotations.size());
    annotations.clear();
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
                util::BitmapToClipboard(external.get(), Settings().clipboardHtmlTempFile); // CF_DIB + PNG + HTML Format
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

    auto composite = IsGif() ? RenderGifFrame(gifFrame) : RenderComposite();
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
    util::BitmapToClipboard(external.get(), Settings().clipboardHtmlTempFile); // CF_DIB + PNG + HTML Format
    GlobalPasteBuffer().Set(std::unique_ptr<Bitmap>(
        crop->Clone(0, 0, crop->GetWidth(), crop->GetHeight(), PixelFormat32bppARGB)));
    return true;
}
