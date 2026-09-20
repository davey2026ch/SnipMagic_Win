#pragma once

#include <wchar.h>

#define APP_NAME        L"截图大师SnipMagic"
#define APP_VERSION     L"2.1.0"
#define APP_VERSION_RC  2,1,0,0
#define APP_VERSION_STR "2.1.0.0"
#define APP_BUILD_TIME  __DATE__ " " __TIME__
#define APP_INI_NAME    L"SnipMagic.ini"

// 将 __DATE__/__TIME__（"Sep 19 2026 17:40:12"）格式化为 "2026-09-19 17:40:12"
inline const wchar_t* AppBuildTimeFormatted() {
    static wchar_t buf[32] = {};
    if (buf[0]) return buf;
    const char* months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    const char* d = __DATE__;
    const char* t = __TIME__;
    int month = 0;
    for (int i = 0; i < 12; ++i) {
        if (d[0] == months[i][0] && d[1] == months[i][1] && d[2] == months[i][2]) {
            month = i + 1;
            break;
        }
    }
    int day = 0, year = 0;
    // __DATE__ = "Mmm dd yyyy"，dd 可能是空格填充（"Sep  9 2026"）
    if (d[4] == ' ') day = d[5] - '0';
    else day = (d[4] - '0') * 10 + (d[5] - '0');
    year = (d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 + (d[10] - '0');
    swprintf_s(buf, L"%04d-%02d-%02d %hs", year, month, day, t);
    return buf;
}
