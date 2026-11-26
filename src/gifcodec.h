#pragma once
// GIF 编码/解码（自带实现，不依赖第三方库）
//  - 录制时：BGRA 画面 → 中位切分量化到 ≤256 色 → GIF LZW 压缩 → 内存里的帧
//  - 播放时：LZW 解压 → 调色板查表 → 32bpp 位图
//  - 保存时：把帧按 GIF89a 结构拼成文件（每帧带局部调色板，支持逐帧延迟与循环）
#include "util.h"

namespace gifcodec {

// 一帧动图数据（压缩态，省内存；15 秒屏幕录制通常只有几 MB）
struct Frame {
    std::vector<uint8_t> data;      // data[0] = minCodeSize，其后是 LZW 位流（已按 GIF 子块切分）
    std::vector<uint32_t> palette;  // 0x00RRGGBB，长度 = 1<<minCodeSize
    int minCodeSize = 8;
    int delayMs = 40;               // 该帧显示时长
};

struct Anim {
    int width = 0;
    int height = 0;
    bool loop = true;
    std::vector<Frame> frames;

    bool Empty() const { return width <= 0 || height <= 0 || frames.empty(); }
    int TotalMs() const;
    size_t MemoryBytes() const;
    // 平均帧间隔（用于界面显示，取中位数附近）
    int AvgDelayMs() const;
    // 解码第 idx 帧（越界自动取模）
    std::unique_ptr<Gdiplus::Bitmap> Decode(int idx) const;
};

// 量化 + LZW：bgra 为自上而下的 32 位像素（stride = w*4，可带 alpha，忽略之）
bool CompressFrame(const uint8_t* bgra, int w, int h, int delayMs, Frame& out);

// 把 idx 数据解成索引数组（长度 = w*h）
bool DecodeIndices(const Frame& f, int w, int h, std::vector<uint8_t>& indices);

// 写出 GIF 文件
bool WriteGif(const std::wstring& path, const Anim& anim);
// 写出到内存（自检/测试用）
bool WriteGifToMemory(const Anim& anim, std::vector<uint8_t>& out);

} // namespace gifcodec
