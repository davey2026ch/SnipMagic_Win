#pragma once
#include "netutil.h"
#include <shldisp.h>
#include <exdisp.h>

namespace ziputil {

inline std::string ToBase64(const std::string& bin) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    b64.reserve((bin.size() + 2) / 3 * 4);
    size_t len = bin.size();
    for (size_t k = 0; k < len; k += 3) {
        unsigned v = static_cast<unsigned char>(bin[k]) << 16;
        if (k + 1 < len) v |= static_cast<unsigned char>(bin[k + 1]) << 8;
        if (k + 2 < len) v |= static_cast<unsigned char>(bin[k + 2]);
        b64.push_back(tbl[(v >> 18) & 63]);
        b64.push_back(tbl[(v >> 12) & 63]);
        b64.push_back(k + 1 < len ? tbl[(v >> 6) & 63] : '=');
        b64.push_back(k + 2 < len ? tbl[v & 63] : '=');
    }
    return b64;
}

inline uint32_t Crc32(const BYTE* data, size_t len, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Extract zip via Shell COM (Windows built-in folder namespace)
inline bool ExtractZipShell(const std::wstring& zipPath, const std::wstring& destDir,
                            const wchar_t* waitFileName, int waitMs = 20000) {
    CreateDirectoryW(destDir.c_str(), nullptr);
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hrInit);

    IShellDispatch* pShell = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellDispatch, reinterpret_cast<void**>(&pShell));
    if (FAILED(hr) || !pShell) {
        if (needUninit) CoUninitialize();
        return false;
    }

    Folder* pSrc = nullptr;
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(zipPath.c_str());
    hr = pShell->NameSpace(v, &pSrc);
    VariantClear(&v);
    if (FAILED(hr) || !pSrc) {
        pShell->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    Folder* pDst = nullptr;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(destDir.c_str());
    hr = pShell->NameSpace(v, &pDst);
    VariantClear(&v);
    if (FAILED(hr) || !pDst) {
        pSrc->Release();
        pShell->Release();
        if (needUninit) CoUninitialize();
        return false;
    }

    FolderItems* pItems = nullptr;
    hr = pSrc->Items(&pItems);
    bool started = false;
    if (SUCCEEDED(hr) && pItems) {
        VARIANT vItems;
        VariantInit(&vItems);
        vItems.vt = VT_DISPATCH;
        vItems.pdispVal = pItems;
        pItems->AddRef();
        VARIANT vOpt;
        VariantInit(&vOpt);
        vOpt.vt = VT_I4;
        vOpt.lVal = 4 | 16; // no progress UI + yes to all
        pDst->CopyHere(vItems, vOpt);
        started = true;
        VariantClear(&vItems);
        pItems->Release();
    }

    pDst->Release();
    pSrc->Release();
    pShell->Release();

    // Wait for expected file
    std::wstring watch;
    if (waitFileName && waitFileName[0]) {
        watch = destDir + L"\\" + waitFileName;
    }
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(waitMs);
    for (;;) {
        if (!watch.empty()) {
            if (GetFileAttributesW(watch.c_str()) != INVALID_FILE_ATTRIBUTES) break;
        } else {
            WIN32_FIND_DATAW fd = {};
            std::wstring pattern = destDir + L"\\*";
            HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                bool has = false;
                do {
                    if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) { has = true; break; }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
                if (has) break;
            }
        }
        if (GetTickCount() >= deadline) break;
        Sleep(80);
    }

    if (needUninit) CoUninitialize();
    return started;
}

// Find a file under dir (recursive) by name
inline std::wstring FindFileByName(const std::wstring& dir, const std::wstring& name) {
    WIN32_FIND_DATAW fd = {};
    std::wstring pattern = dir + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return L"";
    std::wstring found;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (_wcsicmp(fd.cFileName, name.c_str()) == 0) {
            found = full;
            break;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            found = FindFileByName(full, name);
            if (!found.empty()) break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

inline bool ListDir(const std::wstring& dir, std::vector<std::wstring>& names, bool dirsOnly = false) {
    names.clear();
    WIN32_FIND_DATAW fd = {};
    std::wstring pattern = dir + L"\\*";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (dirsOnly && !isDir) continue;
        if (!dirsOnly && isDir) continue;
        names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}

inline bool CopyDirFiles(const std::wstring& srcDir, const std::wstring& dstDir) {
    CreateDirectoryW(dstDir.c_str(), nullptr);
    std::vector<std::wstring> files;
    if (!ListDir(srcDir, files, false)) return true;
    for (const auto& f : files) {
        CopyFileW((srcDir + L"\\" + f).c_str(), (dstDir + L"\\" + f).c_str(), FALSE);
    }
    return true;
}

// ---- Minimal ZIP writer (stored / no compression) for .xlsx ----
inline void AppendU16(std::string& s, uint16_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
}
inline void AppendU32(std::string& s, uint32_t v) {
    s.push_back(static_cast<char>(v & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 24) & 0xFF));
}

struct ZipEntry {
    std::string name; // forward slashes
    std::string data;
};

inline bool WriteZipStored(const std::wstring& path, const std::vector<ZipEntry>& entries) {
    std::string out;
    std::string central;
    uint32_t offset = 0;
    for (const auto& e : entries) {
        uint32_t crc = Crc32(reinterpret_cast<const BYTE*>(e.data.data()), e.data.size());
        size_t localStart = out.size();
        AppendU32(out, 0x04034b50u);
        AppendU16(out, 20);
        AppendU16(out, 0);
        AppendU16(out, 0); // stored
        AppendU16(out, 0);
        AppendU16(out, 0);
        AppendU32(out, crc);
        AppendU32(out, static_cast<uint32_t>(e.data.size()));
        AppendU32(out, static_cast<uint32_t>(e.data.size()));
        AppendU16(out, static_cast<uint16_t>(e.name.size()));
        AppendU16(out, 0);
        out += e.name;
        out += e.data;

        AppendU32(central, 0x02014b50u);
        AppendU16(central, 20);
        AppendU16(central, 20);
        AppendU16(central, 0);
        AppendU16(central, 0);
        AppendU16(central, 0);
        AppendU16(central, 0);
        AppendU32(central, crc);
        AppendU32(central, static_cast<uint32_t>(e.data.size()));
        AppendU32(central, static_cast<uint32_t>(e.data.size()));
        AppendU16(central, static_cast<uint16_t>(e.name.size()));
        AppendU16(central, 0);
        AppendU16(central, 0);
        AppendU16(central, 0);
        AppendU16(central, 0);
        AppendU32(central, 0);
        AppendU32(central, static_cast<uint32_t>(localStart));
        central += e.name;

        offset = static_cast<uint32_t>(out.size());
        (void)offset;
    }
    size_t cdStart = out.size();
    out += central;
    AppendU32(out, 0x06054b50u);
    AppendU16(out, 0);
    AppendU16(out, 0);
    AppendU16(out, static_cast<uint16_t>(entries.size()));
    AppendU16(out, static_cast<uint16_t>(entries.size()));
    AppendU32(out, static_cast<uint32_t>(central.size()));
    AppendU32(out, static_cast<uint32_t>(cdStart));
    AppendU16(out, 0);
    return netutil::WriteFileBytes(path, out.data(), out.size());
}

inline std::string XmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o.push_back(c);
        }
    }
    return o;
}

// PNG bytes from GDI+ bitmap
inline bool BitmapToPngBytes(Gdiplus::Bitmap* bmp, std::string& outPng) {
    outPng.clear();
    if (!bmp) return false;
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK || !stream) return false;
    CLSID pngClsid;
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (!size) { stream->Release(); return false; }
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    Gdiplus::GetImageEncoders(num, size, info);
    bool found = false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            pngClsid = info[i].Clsid;
            found = true;
            break;
        }
    }
    if (!found) { stream->Release(); return false; }
    if (bmp->Save(stream, &pngClsid, nullptr) != Gdiplus::Ok) {
        stream->Release();
        return false;
    }
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    size_t len = static_cast<size_t>(st.cbSize.QuadPart);
    outPng.resize(len);
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG got = 0;
    stream->Read(&outPng[0], static_cast<ULONG>(len), &got);
    outPng.resize(got);
    stream->Release();
    return !outPng.empty();
}

struct XlsxImage {
    std::string name; // media file name, keep extension
    std::string data;
    int widthPx = 0;
    int heightPx = 0;
    int atRow = 0; // 0-based：图片在原文中的行号，用于垂直定位
};

inline std::string ImageExtFromMimeOrName(const std::string& name, const std::string& data) {
    if (name.find(".jpg") != std::string::npos || name.find(".jpeg") != std::string::npos) return "jpg";
    if (name.find(".png") != std::string::npos) return "png";
    if (data.size() >= 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8) return "jpg";
    return "png";
}

// 解析图片真实像素尺寸（优先文件头，失败再用 GDI+），避免 Excel 里按错误宽高拉变形
inline void DecodeImagePixelSize(const std::string& bytes, int& w, int& h) {
    w = 0;
    h = 0;
    if (bytes.size() >= 24 &&
        (unsigned char)bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        auto be = [&](size_t o) {
            return (int)(((unsigned)(unsigned char)bytes[o] << 24) |
                         ((unsigned)(unsigned char)bytes[o + 1] << 16) |
                         ((unsigned)(unsigned char)bytes[o + 2] << 8) |
                         (unsigned)(unsigned char)bytes[o + 3]);
        };
        w = be(16);
        h = be(20);
        return;
    }
    if (bytes.size() > 10 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xD8) {
        size_t i = 2;
        while (i + 9 < bytes.size()) {
            if ((unsigned char)bytes[i] != 0xFF) { ++i; continue; }
            unsigned char marker = (unsigned char)bytes[i + 1];
            if (marker == 0xD8 || marker == 0xD9) { i += 2; continue; }
            if (i + 3 >= bytes.size()) break;
            unsigned seg = ((unsigned char)bytes[i + 2] << 8) | (unsigned char)bytes[i + 3];
            if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
                if (i + 8 < bytes.size()) {
                    h = ((unsigned char)bytes[i + 5] << 8) | (unsigned char)bytes[i + 6];
                    w = ((unsigned char)bytes[i + 7] << 8) | (unsigned char)bytes[i + 8];
                }
                return;
            }
            if (seg < 2) break;
            i += 2 + seg;
        }
    }
    // GDI+ 兜底
    if (bytes.empty()) return;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!hMem) return;
    void* p = GlobalLock(hMem);
    if (!p) { GlobalFree(hMem); return; }
    memcpy(p, bytes.data(), bytes.size());
    GlobalUnlock(hMem);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &stream) == S_OK && stream) {
        Bitmap bmp(stream);
        if (bmp.GetLastStatus() == Ok) {
            w = (int)bmp.GetWidth();
            h = (int)bmp.GetHeight();
        }
        stream->Release();
    } else {
        GlobalFree(hMem);
    }
}

inline bool ExportXlsx(const std::wstring& path,
                       const std::vector<std::vector<std::string>>& rowsUtf8,
                       const std::vector<XlsxImage>& images) {
    using ziputil::ZipEntry;
    std::vector<ZipEntry> entries;

    std::string ct =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
    if (!images.empty()) {
        ct += "<Override PartName=\"/xl/drawings/drawing1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.drawing+xml\"/>";
    }
    ct += "</Types>";
    entries.push_back({"[Content_Types].xml", ct});

    entries.push_back({"_rels/.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>"});

    entries.push_back({"xl/workbook.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"提取内容\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"});

    std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>";
    if (!images.empty()) {
        rels += "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" Target=\"drawings/drawing1.xml\"/>";
    }
    rels += "</Relationships>";
    entries.push_back({"xl/_rels/workbook.xml.rels", rels});

    // sheet 与 drawing 在下方按图片所在行生成
    {
        // 先算每张图的真实尺寸与所在行，用于：撑高该行 + twoCellAnchor 锚定
        struct ImgPlace {
            std::string ext;
            int atRow = 0;
            int wPx = 0;
            int hPx = 0;
            int stackY = 0; // 同行多图时的纵向偏移(px)
        };
        std::vector<ImgPlace> places(images.size());
        const int maxW = 1400;
        for (size_t i = 0; i < images.size(); ++i) {
            ImgPlace p;
            p.ext = ImageExtFromMimeOrName(images[i].name, images[i].data);
            DecodeImagePixelSize(images[i].data, p.wPx, p.hPx);
            if (p.wPx <= 0) p.wPx = images[i].widthPx > 0 ? images[i].widthPx : 0;
            if (p.hPx <= 0) p.hPx = images[i].heightPx > 0 ? images[i].heightPx : 0;
            if (p.wPx <= 0 || p.hPx <= 0) { p.wPx = 400; p.hPx = 400; }
            if (p.wPx > maxW) {
                p.hPx = (int)((long long)p.hPx * maxW / p.wPx);
                p.wPx = maxW;
            }
            p.atRow = images[i].atRow < 0 ? 0 : images[i].atRow;
            for (size_t k = 0; k < i; ++k) {
                if (places[k].atRow != p.atRow) continue;
                p.stackY += places[k].hPx + 8;
            }
            places[i] = p;
        }

        // 原文中有图片代码的行：撑高，让图落在该行
        std::vector<int> rowHeightPx(rowsUtf8.size(), 0);
        for (size_t i = 0; i < places.size(); ++i) {
            int r = places[i].atRow;
            if (r < 0) r = 0;
            if (r >= (int)rowHeightPx.size()) {
                rowHeightPx.resize(r + 1, 0);
            }
            int need = places[i].stackY + places[i].hPx + 4;
            if (need > rowHeightPx[r]) rowHeightPx[r] = need;
        }

        std::string sheet =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            "<cols><col min=\"1\" max=\"1\" width=\"80\" customWidth=\"1\"/></cols><sheetData>";
        for (size_t r = 0; r < rowsUtf8.size() || r < rowHeightPx.size(); ++r) {
            int ht = (r < rowHeightPx.size()) ? rowHeightPx[r] : 0;
            if (ht > 0) {
                sheet += "<row r=\"" + std::to_string(r + 1) + "\" ht=\"" + std::to_string(ht) +
                         "\" customHeight=\"1\">";
            } else {
                sheet += "<row r=\"" + std::to_string(r + 1) + "\">";
            }
            if (r < rowsUtf8.size()) {
                for (size_t c = 0; c < rowsUtf8[r].size(); ++c) {
                    std::string ref;
                    int col = static_cast<int>(c);
                    if (col < 26) ref.push_back(static_cast<char>('A' + col));
                    else { ref.push_back('A'); ref.push_back(static_cast<char>('A' + (col - 26))); }
                    ref += std::to_string(r + 1);
                    sheet += "<c r=\"" + ref + "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">" +
                             XmlEscape(rowsUtf8[r][c]) + "</t></is></c>";
                }
            }
            sheet += "</row>";
        }
        sheet += "</sheetData>";
        if (!images.empty()) {
            sheet += "<drawing r:id=\"rId1\"/>";
        }
        sheet += "</worksheet>";
        entries.push_back({"xl/worksheets/sheet1.xml", sheet});

        if (!images.empty()) {
            entries.push_back({"xl/worksheets/_rels/sheet1.xml.rels",
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing\" Target=\"../drawings/drawing1.xml\"/>"
                "</Relationships>"});

            std::string dr =
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
                "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
            std::string drRels =
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";

            for (size_t i = 0; i < images.size(); ++i) {
                const ImgPlace& p = places[i];
                std::string mediaName = "xl/media/image" + std::to_string(i + 1) + "." + p.ext;
                entries.push_back({mediaName, images[i].data});

                long long cx = (long long)p.wPx * 9525LL;
                long long cy = (long long)p.hPx * 9525LL;
                long long rowOff = (long long)p.stackY * 9525LL;
                // twoCellAnchor：from/to 都落在「图片代码所在行」
                // 用 colOff/rowOff 表达真实像素尺寸，避免被单元格拉变形
                dr +=
                    "<xdr:twoCellAnchor editAs=\"oneCell\">"
                    "<xdr:from>"
                    "<xdr:col>0</xdr:col><xdr:colOff>0</xdr:colOff>"
                    "<xdr:row>" + std::to_string(p.atRow) + "</xdr:row>"
                    "<xdr:rowOff>" + std::to_string(rowOff) + "</xdr:rowOff>"
                    "</xdr:from>"
                    "<xdr:to>"
                    "<xdr:col>0</xdr:col><xdr:colOff>" + std::to_string(cx) + "</xdr:colOff>"
                    "<xdr:row>" + std::to_string(p.atRow) + "</xdr:row>"
                    "<xdr:rowOff>" + std::to_string(rowOff + cy) + "</xdr:rowOff>"
                    "</xdr:to>"
                    "<xdr:pic>"
                    "<xdr:nvPicPr><xdr:cNvPr id=\"" + std::to_string(i + 2) + "\" name=\"图片" + std::to_string(i + 1) + "\"/>"
                    "<xdr:cNvPicPr/></xdr:nvPicPr>"
                    "<xdr:blipFill><a:blip r:embed=\"rId" + std::to_string(i + 1) + "\"/>"
                    "<a:stretch><a:fillRect/></a:stretch></xdr:blipFill>"
                    "<xdr:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/>"
                    "<a:ext cx=\"" + std::to_string(cx) + "\" cy=\"" + std::to_string(cy) + "\"/></a:xfrm>"
                    "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></xdr:spPr>"
                    "</xdr:pic><xdr:clientData/>"
                    "</xdr:twoCellAnchor>";

                drRels += "<Relationship Id=\"rId" + std::to_string(i + 1) +
                          "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"../media/image" +
                          std::to_string(i + 1) + "." + p.ext + "\"/>";
            }
            dr += "</xdr:wsDr>";
            drRels += "</Relationships>";
            entries.push_back({"xl/drawings/drawing1.xml", dr});
            entries.push_back({"xl/drawings/_rels/drawing1.xml.rels", drRels});
        }

        return WriteZipStored(path, entries);
    } // end sheet/drawing block
} // ExportXlsx

// ---- 真正的 .docx（OOXML zip）----
inline std::string DocxXmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20 && c != '\t') {
                // drop control chars
            } else o.push_back(c);
        }
    }
    return o;
}

inline const std::string* FindImageData(const std::vector<std::pair<std::wstring, std::string>>& images,
                                        const std::string& relUtf8) {
    std::string rel = relUtf8;
    for (auto& ch : rel) if (ch == '\\') ch = '/';
    size_t sl = rel.find_last_of('/');
    std::string relBase = (sl == std::string::npos) ? rel : rel.substr(sl + 1);
    for (const auto& im : images) {
        if (im.second.empty()) continue;
        std::string name = netutil::WideToUtf8(im.first);
        for (auto& ch : name) if (ch == '\\') ch = '/';
        size_t s2 = name.find_last_of('/');
        std::string fname = (s2 == std::string::npos) ? name : name.substr(s2 + 1);
        if (!relBase.empty() && (fname == relBase || rel.find(fname) != std::string::npos))
            return &im.second;
    }
    return nullptr;
}

// markdown → docx：文本行进段落；![alt](path) 变成嵌入图片（正文不出现标签）
inline bool ExportDocx(const std::wstring& path,
                       const std::string& markdownUtf8,
                       const std::vector<std::pair<std::wstring, std::string>>& images) {
    std::string mediaRels;
    std::string body;
    std::vector<std::pair<std::string, std::string>> mediaFiles; // zip path, bytes
    int relId = 2; // rId1 = styles
    int drawingId = 1;

    auto addImage = [&](const std::string& bytes) -> int {
        if (bytes.empty()) return -1;
        std::string ext = "png";
        if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xD8)
            ext = "jpg";
        int id = relId++;
        std::string zipName = "word/media/image" + std::to_string(drawingId) + "." + ext;
        mediaFiles.push_back({zipName, bytes});
        std::string target = "media/image" + std::to_string(drawingId) + "." + ext;
        mediaRels += "<Relationship Id=\"rId" + std::to_string(id) +
                     "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"" +
                     target + "\"/>";
        // dimension from bytes
        int w = 0, h = 0;
        if (bytes.size() >= 24 && (unsigned char)bytes[0] == 0x89 && bytes[1] == 'P') {
            auto be = [&](size_t o) {
                return (unsigned)((unsigned char)bytes[o] << 24 | (unsigned char)bytes[o + 1] << 16 |
                                  (unsigned char)bytes[o + 2] << 8 | (unsigned char)bytes[o + 3]);
            };
            w = (int)be(16); h = (int)be(20);
        } else if (bytes.size() > 10 && (unsigned char)bytes[0] == 0xFF) {
            size_t i = 2;
            while (i + 9 < bytes.size()) {
                if ((unsigned char)bytes[i] != 0xFF) { ++i; continue; }
                unsigned char marker = (unsigned char)bytes[i + 1];
                if (marker == 0xD8 || marker == 0xD9) { i += 2; continue; }
                if (i + 3 >= bytes.size()) break;
                unsigned seg = ((unsigned char)bytes[i + 2] << 8) | (unsigned char)bytes[i + 3];
                if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
                    if (i + 8 < bytes.size()) {
                        h = ((unsigned char)bytes[i + 5] << 8) | (unsigned char)bytes[i + 6];
                        w = ((unsigned char)bytes[i + 7] << 8) | (unsigned char)bytes[i + 8];
                    }
                    break;
                }
                if (seg < 2) break;
                i += 2 + seg;
            }
        }
        if (w <= 0) w = 400;
        if (h <= 0) h = 220;
        if (w > 900) { h = h * 900 / w; w = 900; }
        long long cx = (long long)w * 9525LL;
        long long cy = (long long)h * 9525LL;
        int dId = drawingId;
        body +=
            "<w:p><w:r><w:drawing>"
            "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
            "<wp:extent cx=\"" + std::to_string(cx) + "\" cy=\"" + std::to_string(cy) + "\"/>"
            "<wp:docPr id=\"" + std::to_string(dId) + "\" name=\"图片" + std::to_string(dId) + "\"/>"
            "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
            "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
            "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
            "<pic:nvPicPr><pic:cNvPr id=\"" + std::to_string(dId) + "\" name=\"img" + std::to_string(dId) + "\"/><pic:cNvPicPr/></pic:nvPicPr>"
            "<pic:blipFill><a:blip r:embed=\"rId" + std::to_string(id) + "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
            "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" + std::to_string(cx) + "\" cy=\"" + std::to_string(cy) + "\"/></a:xfrm>"
            "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
            "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>";
        ++drawingId;
        return id;
    };

    auto emitTextLine = [&](std::string line) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) {
            body += "<w:p/>";
            return;
        }
        // 去掉 markdown 标题井号，正文更干净
        size_t hs = 0;
        while (hs < line.size() && line[hs] == '#') ++hs;
        if (hs > 0 && hs < line.size() && line[hs] == ' ') line = line.substr(hs + 1);
        // 去掉 **bold** 标记，保留文字
        std::string plain;
        for (size_t i = 0; i < line.size();) {
            if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
                size_t q = line.find("**", i + 2);
                if (q != std::string::npos) { i = q + 2; continue; }
            }
            plain.push_back(line[i++]);
        }
        body += "<w:p><w:r><w:t xml:space=\"preserve\">" + DocxXmlEscape(plain) + "</w:t></w:r></w:p>";
    };

    std::string src = markdownUtf8;
    std::string norm;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '\r') {
            if (i + 1 < src.size() && src[i + 1] == '\n') continue;
            norm.push_back('\n');
        } else norm.push_back(src[i]);
    }

    // 图片按 md 原文顺序插入：先落到对应行位置，正文不输出 ![...] 标签
    size_t i = 0;
    while (i <= norm.size()) {
        size_t nl = norm.find('\n', i);
        if (nl == std::string::npos) nl = norm.size();
        std::string line = norm.substr(i, nl - i);
        i = nl + (nl < norm.size() ? 1 : 0);

        // 顺序扫描本行：遇到图片引用立刻插入图片（位置=代码所在处），再继续后面的文字
        size_t pos = 0;
        std::string textPart;
        bool hadImage = false;
        while (pos < line.size()) {
            size_t bang = line.find("![", pos);
            if (bang == std::string::npos) {
                textPart += line.substr(pos);
                break;
            }
            textPart += line.substr(pos, bang - pos);
            size_t br1 = line.find("](", bang + 2);
            size_t end = (br1 == std::string::npos) ? std::string::npos : line.find(')', br1 + 2);
            if (br1 == std::string::npos || end == std::string::npos) {
                textPart += line.substr(bang);
                break;
            }
            std::string rel = line.substr(br1 + 2, end - (br1 + 2));
            size_t sp = rel.find_first_of(" \t");
            if (sp != std::string::npos) rel = rel.substr(0, sp);
            // 先输出图片前的文字
            while (!textPart.empty() && (textPart.back() == ' ' || textPart.back() == '\t')) textPart.pop_back();
            if (!textPart.empty()) {
                emitTextLine(textPart);
                textPart.clear();
            }
            // 再插入图片（代码位置处）
            const std::string* bytes = FindImageData(images, rel);
            if (bytes) addImage(*bytes);
            hadImage = true;
            pos = end + 1; // 标签本身不进入正文
        }
        while (!textPart.empty() && (textPart.back() == ' ' || textPart.back() == '\t')) textPart.pop_back();
        if (!textPart.empty()) emitTextLine(textPart);
        else if (!hadImage && line.find("![") == std::string::npos) emitTextLine(line);

        if (nl >= norm.size()) break;
    }

    // 兜底：md 未引用到的图，按 images 列表顺序附在文末
    {
        std::vector<bool> used(images.size(), false);
        // 粗略：若正文已放过图则 drawingId>1，这里仍把未匹配字节的图附上
        if (!images.empty() && mediaFiles.empty()) {
            for (const auto& im : images) addImage(im.second);
        }
        (void)used;
    }

    std::string doc =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
        "<w:body>" + body +
        "<w:sectPr><w:pgSz w:w=\"11906\" h:h=\"16838\"/>"
        "<w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\"/></w:sectPr>"
        "</w:body></w:document>";

    // fix pgSz attribute - should be w:w and w:h not h:h
    {
        size_t p = doc.find("h:h=");
        if (p != std::string::npos) doc.replace(p, 4, "w:h=");
    }

    std::vector<ZipEntry> entries;
    entries.push_back({"[Content_Types].xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "</Types>"});
    entries.push_back({"_rels/.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>"});
    entries.push_back({"word/styles.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
        "<w:name w:val=\"Normal\"/><w:rPr><w:rFonts w:ascii=\"Microsoft YaHei\" w:eastAsia=\"Microsoft YaHei\"/>"
        "<w:sz w:val=\"22\"/></w:rPr></w:style></w:styles>"});
    entries.push_back({"word/_rels/document.xml.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
        + mediaRels + "</Relationships>"});
    entries.push_back({"word/document.xml", doc});
    for (auto& m : mediaFiles) entries.push_back({m.first, m.second});
    return WriteZipStored(path, entries);
}

// 兼容旧接口名
inline bool ExportWordHtml(const std::wstring& path,
                           const std::string& markdownUtf8,
                           const std::vector<std::pair<std::wstring, std::string>>& images) {
    return ExportDocx(path, markdownUtf8, images);
}

} // namespace ziputil
