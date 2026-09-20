# -*- coding: utf-8 -*-
# 真机长截图驱动 · 阶段1 v3：按 PID 找应用主窗口；Esc 退微信多选；PostMessage 启动长截图；框选
import ctypes
import json
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass
from PIL import ImageGrab

OUT = r"F:\Projects\截图工具\test_wechat"
WM_COMMAND = 0x0111
ID_CMD_LONG_CAPTURE = 115

vx = user32.GetSystemMetrics(76)
vy = user32.GetSystemMetrics(77)
vw = user32.GetSystemMetrics(78)
vh = user32.GetSystemMetrics(79)


def to_norm(x, y):
    return (int((x - vx) * 65535 / (vw - 1)), int((y - vy) * 65535 / (vh - 1)))


MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_ABSOLUTE = 0x8000


def mouse_move(x, y):
    nx, ny = to_norm(x, y)
    user32.mouse_event(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, nx, ny, 0, 0)


def click(x, y):
    mouse_move(x, y)
    time.sleep(0.06)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    time.sleep(0.05)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)


def drag(x0, y0, x1, y1, steps=24, dur=0.5):
    mouse_move(x0, y0)
    time.sleep(0.1)
    user32.mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0)
    for i in range(1, steps + 1):
        xi = x0 + (x1 - x0) * i // steps
        yi = y0 + (y1 - y0) * i // steps
        mouse_move(xi, yi)
        time.sleep(dur / steps)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)


def enum_all():
    found = []
    WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(hwnd, _):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        r = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        n = user32.GetWindowTextLengthW(hwnd)
        tbuf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, tbuf, n + 1)
        cbuf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cbuf, 256)
        found.append({
            "hwnd": hwnd, "pid": pid.value, "title": tbuf.value, "cls": cbuf.value,
            "rect": [r.left, r.top, r.right, r.bottom],
            "visible": bool(user32.IsWindowVisible(hwnd)),
        })
        return True
    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


def pid_of_exe(substr):
    # 用 tasklist 找进程 PID
    import subprocess
    out = subprocess.run(["tasklist"], capture_output=True, text=True,
                         encoding="gbk", errors="replace").stdout
    pids = []
    for line in out.splitlines():
        if substr.lower() in line.lower():
            parts = line.split()
            for p in parts:
                if p.isdigit():
                    pids.append(int(p))
                    break
    return pids


info = {}
wins = enum_all()
app_wins = [w for w in wins if w["cls"] == "ScreenshotToolMainWindow"]
info["app_wins"] = app_wins
main = app_wins[0] if app_wins else None
info["main"] = main

# 0) 退出微信多选：点微信标题栏空白再按 Esc
wechat = [w for w in wins if w["title"] == "微信"]
if wechat:
    wr = wechat[0]["rect"]
    click((wr[0] + wr[2]) // 2, wr[1] + 15)  # 标题栏
    time.sleep(0.3)
    user32.keybd_event(0x1B, 0, 0, 0)
    time.sleep(0.08)
    user32.keybd_event(0x1B, 0, 2, 0)
    time.sleep(0.3)

# 1) PostMessage 启动长截图
if main:
    user32.PostMessageW(main["hwnd"], WM_COMMAND, ID_CMD_LONG_CAPTURE, 0)
time.sleep(1.5)
img = ImageGrab.grab()
img.save(OUT + r"\stage1_overlay.png")

bars = [w for w in enum_all() if "LongCaptureBar" in w["cls"]]
info["bars_after_cmd"] = bars

# 2) 框选聊天区
rx0, ry0, rx1, ry1 = 500, 520, 1960, 1790
if not bars:
    drag(rx0, ry0, rx1, ry1)
    time.sleep(1.2)
    img = ImageGrab.grab()
    img.save(OUT + r"\stage2_region.png")
    bars = [w for w in enum_all() if "LongCaptureBar" in w["cls"]]
info["bars_after_drag"] = bars
info["region"] = [rx0, ry0, rx1, ry1]

with open(OUT + r"\drive_stage1.json", "w", encoding="utf-8") as f:
    json.dump(info, f, ensure_ascii=False, indent=1)
print("bars:", len(bars), "main:", main["hwnd"] if main else None)
