# -*- coding: utf-8 -*-
"""把取色器上的吸管图标按钮单独截图并放大，便于对照参考图检查造型（亮/暗主题各一张）。"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time
from PIL import Image, ImageGrab

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


def find(cls):
    res = []

    @WNDENUMPROC
    def cb(h, l):
        if user32.IsWindowVisible(h) and _cls(h) == cls:
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


def children(h):
    res = []

    @WNDENUMPROC
    def cb(x, l):
        res.append(x)
        return True
    user32.EnumChildWindows(h, cb, 0)
    return res


def read_ini():
    with open(INI, "rb") as f:
        return f.read()


def set_theme(v, raw):
    raw = raw.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    out = [b"Theme=%d" % v if ln.startswith(b"Theme=") else ln
           for ln in raw.split(b"\n") if ln.strip()]
    with open(INI, "wb") as f:
        f.write(b"\r\n".join(out) + b"\r\n")


def shoot_icon(theme_name, zoom=8):
    proc = subprocess.Popen([os.path.join(DIST, "SnipMagic.exe")], cwd=DIST)
    main = wait("ScreenshotToolMainWindow", 12)
    if not main:
        print("main missing")
        return False
    time.sleep(1.3)
    dpi = user32.GetDpiForWindow(main) or 96
    s = dpi / 96.0
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(main, ctypes.byref(pt))
    ox, oy = pt.x, pt.y
    pad, topH, leftW, lh, lgap = 6 * s, 48 * s, 56 * s, 36 * s, 3 * s
    bx = ox + pad + (leftW - 2 * pad) / 2
    by = oy + topH + pad + 14 * (lh + lgap) + lh / 2
    user32.SetCursorPos(int(bx), int(by))
    time.sleep(0.1)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.06)
    user32.mouse_event(0x0004, 0, 0, 0, 0)
    pk = wait("ScreenshotToolColorPicker", 6)
    if not pk:
        print("picker missing")
        return False
    time.sleep(0.6)
    eye = [c for c in children(pk)
           if _cls(c) == "Button" and user32.GetDlgCtrlID(c) == 1003]
    if not eye:
        print("eye button missing")
        return False
    r = rect(eye[0])
    img = ImageGrab.grab(bbox=r)
    big = img.resize((img.width * zoom, img.height * zoom), Image.NEAREST)
    p = os.path.join(OUT, "eye-zoom-%s.png" % theme_name)
    big.save(p)
    print("saved", p, img.size)
    # 悬浮态
    user32.SetCursorPos((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
    time.sleep(0.4)
    img2 = ImageGrab.grab(bbox=r)
    p2 = os.path.join(OUT, "eye-zoom-%s-hover.png" % theme_name)
    img2.resize((img2.width * zoom, img2.height * zoom), Image.NEAREST).save(p2)
    print("saved", p2)
    subprocess.run("taskkill /F /IM SnipMagic.exe", shell=True, capture_output=True)
    time.sleep(0.8)
    return True


subprocess.run("taskkill /F /IM SnipMagic.exe", shell=True, capture_output=True)
time.sleep(1.0)
ORIG = read_ini()
ok = True
set_theme(0, read_ini())
ok &= shoot_icon("light")
set_theme(2, read_ini())
ok &= shoot_icon("dark")
set_theme(0, ORIG)   # 还原用户配置
print("RESULT=" + ("PASS" if ok else "FAIL"))
