# -*- coding: utf-8 -*-
# 探针：启动应用 -> 置顶 -> 点长截图 -> 枚举窗口 + 进程存活 + 覆盖层矩形
import ctypes
import subprocess
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

EXE = r"F:\Projects\截图工具\dist\ScreenshotTool.exe"

vx = user32.GetSystemMetrics(76)
vy = user32.GetSystemMetrics(77)
vw = user32.GetSystemMetrics(78)
vh = user32.GetSystemMetrics(79)


def click(x, y):
    nx = int((x - vx) * 65535 / (vw - 1))
    ny = int((y - vy) * 65535 / (vh - 1))
    user32.mouse_event(0x8001, nx, ny, 0, 0)
    time.sleep(0.08)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.06)
    user32.mouse_event(0x0004, 0, 0, 0, 0)


def enum_all():
    found = []
    WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(hwnd, _):
        r = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        cbuf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cbuf, 256)
        if "ScreenshotTool" in cbuf.value:
            found.append((hwnd, cbuf.value, [r.left, r.top, r.right, r.bottom],
                          bool(user32.IsWindowVisible(hwnd))))
        return True
    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


proc = subprocess.Popen([EXE])
print("pid", proc.pid)
main = None
for _ in range(40):
    time.sleep(0.3)
    ws = [w for w in enum_all() if w[1] == "ScreenshotToolMainWindow"]
    if ws:
        main = ws[0]
        break
print("main", main)
user32.SetWindowPos(main[0], -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
time.sleep(0.8)
click(main[2][0] + 162, main[2][1] + 38)
print("clicked")
for i in range(6):
    time.sleep(0.5)
    alive = proc.poll() is None
    ws = enum_all()
    print("t+%.1fs alive=%s wins=%s" % (0.5 * (i + 1), alive, ws))
