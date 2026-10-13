# -*- coding: utf-8 -*-
"""暗色主题下取色器外观检查（吸管图标 + 最近使用宫格 + 当前颜色条）"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time
from PIL import ImageGrab

ctypes.windll.shcore.SetProcessDpiAwareness(2)
user32 = ctypes.windll.user32
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

ROOT = r"F:\Projects\截图工具"
DIST = os.path.join(ROOT, "dist")
INI = os.path.join(DIST, "SnipMagic.ini")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ui_verify_20261013")


def _cls(h):
    b = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(h, b, 256)
    return b.value


def find(cls, visible=True):
    res = []

    @WNDENUMPROC
    def cb(h, l):
        if (not visible or user32.IsWindowVisible(h)) and _cls(h) == cls:
            res.append(h)
        return True
    user32.EnumWindows(cb, 0)
    return res


def wait(cls, timeout=8):
    end = time.time() + timeout
    while time.time() < end:
        w = find(cls)
        if w:
            return w[-1]
        time.sleep(0.15)
    return None


def rect(h):
    r = wt.RECT()
    user32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def client_origin(h):
    p = wt.POINT(0, 0)
    user32.ClientToScreen(h, ctypes.byref(p))
    return p.x, p.y


def read_ini():
    with open(INI, "rb") as f:
        return f.read()


def set_theme(v, raw=None):
    raw = read_ini() if raw is None else raw
    raw = raw.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    out = []
    for ln in raw.split(b"\n"):
        if not ln.strip():
            continue
        out.append(b"Theme=%d" % v if ln.startswith(b"Theme=") else ln)
    with open(INI, "wb") as f:
        f.write(b"\r\n".join(out) + b"\r\n")


def click(x, y):
    user32.SetCursorPos(int(x), int(y))
    time.sleep(0.1)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.06)
    user32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.3)


def shoot(h, name):
    img = ImageGrab.grab(bbox=rect(h))
    p = os.path.join(OUT, name)
    img.save(p)
    print("saved", p, flush=True)


subprocess.run("taskkill /F /IM SnipMagic.exe", shell=True, capture_output=True)
time.sleep(1.0)
ORIG = read_ini()
set_theme(2)  # 暗色
proc = subprocess.Popen([os.path.join(DIST, "SnipMagic.exe")], cwd=DIST)
main = wait("ScreenshotToolMainWindow", 12)
if not main:
    print("main window missing")
    sys.exit(1)
time.sleep(1.2)
dpi = user32.GetDpiForWindow(main) or 96
s = dpi / 96.0
ox, oy = client_origin(main)
# 左侧第 15 个按钮 = 颜色
pad, topH, leftW, lh, lgap = 6 * s, 48 * s, 56 * s, 36 * s, 3 * s
bx = ox + pad + (leftW - 2 * pad) / 2
by = oy + topH + pad + 14 * (lh + lgap) + lh / 2
click(bx, by)
pk = wait("ScreenshotToolColorPicker", 6)
if not pk:
    print("picker missing")
    sys.exit(1)
time.sleep(0.6)
shoot(pk, "20-picker-dark.png")
# 悬浮吸管按钮，检查悬浮态
cx, cy = client_origin(pk)
user32.SetCursorPos(int(cx + 524 + 23), int(cy + 495 + 16))
time.sleep(0.5)
shoot(pk, "21-picker-dark-eye-hover.png")
subprocess.run("taskkill /F /IM SnipMagic.exe", shell=True, capture_output=True)
time.sleep(0.8)
# 还原原始配置（含用户令牌），避免测试改动用户 ini
set_theme(0, ORIG)
print("theme restored to 0")
