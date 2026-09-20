# -*- coding: utf-8 -*-
# 找到桌面上的微信窗口，输出矩形并截图（只截该窗口区域）
import ctypes
import json
import sys
from ctypes import wintypes

user32 = ctypes.windll.user32
dwmapi = ctypes.windll.dwmapi
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

from PIL import ImageGrab

results = []


def enum_cb(hwnd, _):
    if not user32.IsWindowVisible(hwnd):
        return True
    length = user32.GetWindowTextLengthW(hwnd)
    if length == 0:
        return True
    buf = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(hwnd, buf, length + 1)
    title = buf.value
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    # 取进程名
    proc = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid.value)
    name = ""
    if proc:
        nbuf = ctypes.create_unicode_buffer(260)
        size = wintypes.DWORD(260)
        try:
            ctypes.windll.kernel32.QueryFullProcessImageNameW(proc, 0, nbuf, ctypes.byref(size))
            name = nbuf.value
        except Exception:
            pass
        ctypes.windll.kernel32.CloseHandle(proc)
    if "WeChat" in name or "wechat" in name.lower() or "微信" in title:
        rect = wintypes.RECT()
        # 扩展边框（排除阴影）
        ok = dwmapi.DwmGetWindowAttribute(hwnd, 9, ctypes.byref(rect), ctypes.sizeof(rect))
        if ok != 0:
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
        results.append({
            "hwnd": hwnd,
            "title": title,
            "proc": name,
            "rect": [rect.left, rect.top, rect.right, rect.bottom],
            "w": rect.right - rect.left,
            "h": rect.bottom - rect.top,
        })
    return True


WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows(WNDENUMPROC(enum_cb), 0)

out_path = r"F:\Projects\截图工具\test_wechat\wechat_probe.png"
picked = None
if results:
    # 选面积最大的
    picked = max(results, key=lambda r: r["w"] * r["h"])
    l, t, r, b = picked["rect"]
    # 提到最前再截
    user32.SetWindowPos(picked["hwnd"], -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
    import time
    time.sleep(1.0)
    img = ImageGrab.grab(bbox=(l, t, r, b))
    img.save(out_path)

with open(r"F:\Projects\截图工具\test_wechat\wechat_probe.json", "w", encoding="utf-8") as f:
    json.dump({"windows": results, "picked": picked, "shot": out_path}, f, ensure_ascii=False, indent=1)
print("count=", len(results))
