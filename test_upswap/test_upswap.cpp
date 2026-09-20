// 更新替换逻辑独立测试
// 用法：test_upswap.exe direct <cur> <new>
//       test_upswap.exe fallback <cur> <new>   (调用方需自行锁住 cur 触发兜底)
#include "updater.h"
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 4) { std::printf("args?\n"); return 2; }
    std::wstring mode = argv[1], cur = argv[2], nex = argv[3];

    if (mode == L"direct") {
        bool ok = updater::SwapUpdateFiles(cur, nex, false, false);
        std::printf("direct swap: %s\n", ok ? "OK" : "FAIL");
        return ok ? 0 : 1;
    }
    if (mode == L"fallback") {
        // 锁住 cur（不共享删除/改名）→ 直接改名必失败 → 走 cmd 兜底
        HANDLE h = CreateFileW(cur.c_str(), GENERIC_READ, 0 /*不共享*/,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { std::printf("lock fail\n"); return 2; }
        bool ok = updater::SwapUpdateFiles(cur, nex, false, false);
        std::printf("fallback swap: %s (caller should exit now)\n", ok ? "OK" : "FAIL");
        CloseHandle(h);
        return ok ? 0 : 1;
    }
    std::printf("unknown mode\n");
    return 2;
}
