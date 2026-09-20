// 验证导出：用真实的 提取内容.md 生成 docx/xlsx，检查 HTML 表格被解析成真表格
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
using namespace Gdiplus;
#include "ziputil.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <string>

static std::string ReadAll(const wchar_t* path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// 与 extract.cpp 中 StripImagesFromLine 一致（本测试 md 无图片，主要是保持流程一致）
static std::string StripImagesFromLineCopy(const std::string& line) {
    std::string out;
    size_t i = 0;
    while (i < line.size()) {
        if (line[i] == '!' && i + 1 < line.size() && line[i + 1] == '[') {
            size_t br1 = line.find("](", i + 2);
            if (br1 != std::string::npos) {
                size_t end = line.find(')', br1 + 2);
                if (end != std::string::npos) { i = end + 1; continue; }
            }
        }
        out.push_back(line[i++]);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '\t')) out.erase(out.begin());
    return out;
}

int main() {
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &gsi, nullptr);
    {
        std::string md = ReadAll(L"F:/Projects/截图工具/问题/提取内容/提取内容.md");

        // 1) 展开后不应再残留 <table
        std::string emd = ziputil::ExpandHtmlTablesToDelim(md);
        int tablesLeft = 0;
        for (size_t p = emd.find("<table"); p != std::string::npos; p = emd.find("<table", p + 1))
            ++tablesLeft;
        int delimLines = 0;
        for (size_t p = emd.find('\x01'); p != std::string::npos; p = emd.find('\x01', p + 1))
            ++delimLines;

        // 2) 复刻 extract.cpp ExportExcel 的建行逻辑
        std::vector<std::vector<std::string>> rows;
        int rowIdx = 0;
        size_t i = 0;
        while (i <= emd.size()) {
            size_t nl = emd.find('\n', i);
            if (nl == std::string::npos) nl = emd.size();
            std::string line = emd.substr(i, nl - i);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string text = StripImagesFromLineCopy(line);
            if (text.find('\x01') != std::string::npos) {
                rows.push_back(ziputil::SplitTableDelimRow(text));
            } else {
                rows.push_back({text});
            }
            rowIdx++;
            if (nl >= emd.size()) break;
            i = nl + 1;
        }

        // 3) 导出两种文件
        bool okX = ziputil::ExportXlsx(L"F:/Projects/截图工具/test_export/out.xlsx", rows, {});
        bool okD = ziputil::ExportDocx(L"F:/Projects/截图工具/test_export/out.docx", md, {});

        // 4) 报告落盘（UTF-8）
        std::string rep;
        rep += "md_bytes=" + std::to_string(md.size()) + "\n";
        rep += "tables_left_after_expand=" + std::to_string(tablesLeft) + "\n";
        rep += "delim_cells_total=" + std::to_string(delimLines) + "\n";
        rep += "xlsx_rows=" + std::to_string(rows.size()) + "\n";
        rep += "export_xlsx=" + std::string(okX ? "OK" : "FAIL") + "\n";
        rep += "export_docx=" + std::string(okD ? "OK" : "FAIL") + "\n";
        rep += "-- multi-cell rows (first 20) --\n";
        int shown = 0;
        for (size_t r = 0; r < rows.size() && shown < 20; ++r) {
            if (rows[r].size() > 1) {
                rep += "row " + std::to_string(r) + " cols=" + std::to_string(rows[r].size()) + " : ";
                for (size_t c = 0; c < rows[r].size(); ++c) {
                    if (c) rep += " | ";
                    rep += rows[r][c];
                }
                rep += "\n";
                ++shown;
            }
        }
        std::ofstream rf(L"F:/Projects/截图工具/test_export/report.txt", std::ios::binary);
        rf.write(rep.data(), (std::streamsize)rep.size());
    }
    Gdiplus::GdiplusShutdown(token);
    return 0;
}
