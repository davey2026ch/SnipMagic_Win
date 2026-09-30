# -*- coding: utf-8 -*-
"""诊断：工具栏取色器点「白色」后，HEX 框到底读到了什么"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

user32 = ctypes.windll.user32
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
HWND_TOPMOST = wt.HWND(-1)
DIST = r"F:\Projects\截图工具\dist"
OUT = r"F:\Projects\截图工具\测试文件\color_ui_verify"

lines = []


def log(m):
    lines.append(m)
    print(m, flush=True)


def _text(hwnd):
    n = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 2)
    user32.GetWindowTextW(hwnd, buf, n + 2)
    return buf.value


def _cls(hwnd):
    b = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, b, 256)
    return b.value


def find_windows(cls_name):
    res = []

    @WNDENUMPROC
    def cb(h, l):
        if user32.IsWindowVisible(h) and _cls(h) == cls_name:
            res.append(h)
        return True

    user32.EnumWindows(cb, 0)
    return res


def wait_window(cls_name, timeout=6):
    end = time.time() + timeout
    while time.time() < end:
        ws = find_windows(cls_name)
        if ws:
            return ws[-1]
        time.sleep(0.15)
    return None


def children(hwnd):
    res = []

    @WNDENUMPROC
    def cb(h, l):
        res.append(h)
        return True

    user32.EnumChildWindows(hwnd, cb, 0)
    return res


def rect_of(h):
    r = wt.RECT()
    user32.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def click(x, y):
    user32.SetCursorPos(int(x), int(y))
    time.sleep(0.08)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.06)
    user32.mouse_event(0x0004, 0, 0, 0, 0)


def center(h):
    l, t, r, b = rect_of(h)
    return (l + r) // 2, (t + b) // 2


def wm_gettext(hwnd):
    """直接跨线程 SendMessage WM_GETTEXT，绕过 GetWindowText 的缓存"""
    buf = ctypes.create_unicode_buffer(64)
    res = user32.SendMessageW(hwnd, 0x000D, 63, buf)  # WM_GETTEXT
    return res, buf.value


def dump_edits(tag, picker):
    for h in children(picker):
        if _cls(h) == "Edit":
            t0 = time.time()
            gwt = _text(h)
            dt = (time.time() - t0) * 1000
            sr, sv = wm_gettext(h)
            log("%s Edit id=%d hwnd=0x%X GetWindowText=%r (%.1fms) "
                "WM_GETTEXT=(%d,%r)" % (tag, user32.GetDlgCtrlID(h), h,
                                        gwt, dt, sr, sv))


subprocess.run("taskkill /F /IM SnipMagic.exe", shell=True, capture_output=True)
time.sleep(1.0)
subprocess.Popen([os.path.join(DIST, "SnipMagic.exe")], cwd=DIST)
main = wait_window("ScreenshotToolMainWindow", 12)
time.sleep(1.0)
user32.SetWindowPos(main, HWND_TOPMOST, 0, 0, 0, 0, 0x0001 | 0x0002)

dpi = user32.GetDpiForWindow(main) or 96
s = dpi / 96.0
import ctypes.wintypes as w2
pt = w2.POINT(0, 0)
user32.ClientToScreen(main, ctypes.byref(pt))
ox, oy = pt.x, pt.y
pad, topH, leftW, lh, lgap = 6 * s, 48 * s, 56 * s, 36 * s, 3 * s
cx = ox + pad + (leftW - 2 * pad) / 2
cy = oy + topH + pad + 14 * (lh + lgap) + lh / 2

click(cx, cy)
pk = wait_window("ScreenshotToolColorPicker", 6)
if not pk:
    log("取色器未弹出")
    sys.exit(1)
time.sleep(0.5)
log("picker hwnd=0x%X thread=%d" % (pk, user32.GetWindowThreadProcessId(pk, None)))
dump_edits("初始", pk)

btns = {}
for h in children(pk):
    if _cls(h) == "Button":
        btns[_text(h)] = h
x, y = center(btns["白色"])
click(x, y)
time.sleep(0.1)
dump_edits("+0.1s", pk)
time.sleep(1.0)
dump_edits("+1.1s", pk)
time.sleep(2.0)
dump_edits("+3.1s", pk)

with open(os.path.join(OUT, "diag.txt"), "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
subprocess.run("taskkill /F /IM SnipMagic.exe", shell=True, capture_output=True)
print("DONE")
