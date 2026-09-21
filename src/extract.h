#pragma once
#include "netutil.h"
#include "document.h"

namespace extract {

struct ExtractImage {
    std::wstring fileName;   // e.g. pic.jpg
    std::wstring relPath;    // images/pic.jpg as in markdown
    std::wstring absPath;    // temp file path
    std::string pngOrRaw;    // raw image bytes (for export without re-read if needed)
    int width = 0;
    int height = 0;
};

struct ExtractResult {
    bool success = false;
    bool cancelled = false;
    std::wstring errorMsg;
    std::wstring markdownWide;
    std::string markdownUtf8;
    std::vector<ExtractImage> images;
    std::wstring workDir; // temp, cleaned after UI
    bool hasImages() const { return !images.empty(); }
};

// Run MinerU extract on bitmap; show progress with cancel; then result dialog.
// Returns after result dialog closes. Temp files are deleted before return.
void RunExtractFlow(HWND owner, Document* doc);

// 提取矢量图：框选区域（或选中的浮动图层）走火山 AI MediaKit 智能抠图，
// 结果为透明底 PNG，直接作为浮动图层落回画布（可拖动 / 可复制到外部应用）。
void RunExtractVector(HWND owner, Document* doc);

// Magic erase on region or brush strokes of active document.
void RunMagicErase(HWND owner, Document* doc);

} // namespace extract
