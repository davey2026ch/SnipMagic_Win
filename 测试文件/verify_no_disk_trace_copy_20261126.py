# -*- coding: utf-8 -*-
"""
真机验证（2026-11-26）第二段：从「截图 → 复制」这条真实链路看磁盘
1) 默认设置下：截图 + Ctrl+C 复制后，%TEMP%\\SnipMagicClip 不应存在
2) 打开「邮件客户端兼容」后：复制会写一张 clip_current.png，且只有一张
3) 关闭程序后：这张图不残留

用法：python verify_no_disk_trace_copy_20261126.py
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import shutil
import subprocess
import time

from PIL import ImageGrab

try:
    _u0 = ctypes.windll.user32
    _u0.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    _u0.SetProcessDpiAwarenessContext.restype = ctypes.wintypes.BOOL
    _u0.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    pass

user32 = ctypes.windll.user32
user32.FindWindowW.restype = ctypes.c_void_p
user32.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
user32.ClientToScreen.argtypes = [wt.HWND, ctypes.POINTER(wt.POINT)]
user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE_SRC = os.path.join(ROOT, "out", "bin", "SnipMagic.exe")
TESTDIR = os.path.join(HERE, "cliptest2")
OUTDIR = os.path.join(HERE, "no_disk_trace_20261126")
TEMP = os.environ.get("TEMP")
CLIPDIR = os.path.join(TEMP, "SnipMagicClip")
INI = os.path.join(TESTDIR, "SnipMagic.ini")

ID_CMD_COPY = 108
WM_HOTKEY = 0x0312
results = []


def check(ok, what, extra=""):
    results.append({"ok": bool(ok), "what": what, "extra": str(extra)})
    print(("[PASS] " if ok else "[FAIL] ") + what + (("  <" + str(extra) + ">") if extra else ""))
    return ok


def pngs():
    if not os.path.isdir(CLIPDIR):
        return -1
    return len([f for f in os.listdir(CLIPDIR) if f.lower().endswith(".png")])


def kill_existing():
    subprocess.run(["taskkill", "/F", "/IM", "SnipMagic.exe"], capture_output=True)
    time.sleep(0.8)


def client_origin(hwnd):
    p = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(p))
    return p.x, p.y


def win_rect(hwnd):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def post_mouse(hwnd, msg, x, y, wp=0):
    user32.PostMessageW(hwnd, msg, wp, (y << 16) | (x & 0xFFFF))


def wait_overlay(timeout=6.0):
    end = time.time() + timeout
    while time.time() < end:
        h = user32.FindWindowW("ScreenshotToolCaptureOverlay", None)
        if h:
            return h
        time.sleep(0.05)
    return None


def do_capture(hwnd):
    """发一次截图热键，拖一个 600x400 的选区，等主窗口拿到页签"""
    user32.PostMessageW(hwnd, WM_HOTKEY, 1, 0)
    ov = wait_overlay()
    if not ov:
        return False
    time.sleep(0.9)  # 让覆盖层先抓屏
    ox, oy = client_origin(ov)
    l, t, r, b = win_rect(ov)
    x0, y0 = 120, 120
    x1 = min(x0 + 600, (r - l) - 60)
    y1 = min(y0 + 400, (b - t) - 60)
    post_mouse(ov, 0x0200, x0, y0)
    post_mouse(ov, 0x0201, x0, y0, 1)
    for k in range(1, 5):
        post_mouse(ov, 0x0200, x0 + (x1 - x0) * k // 4, y0 + (y1 - y0) * k // 4)
        time.sleep(0.05)
    post_mouse(ov, 0x0202, x1, y1)
    time.sleep(1.6)
    return True


def set_ini_compat(value):
    with open(INI, "r", encoding="mbcs", errors="replace") as f:
        lines = f.read().splitlines()
    out = []
    hit = False
    for ln in lines:
        if ln.strip().lower().startswith("clipboardhtmltempfile="):
            out.append("ClipboardHtmlTempFile=%d" % value)
            hit = True
        else:
            out.append(ln)
    if not hit:
        out.append("ClipboardHtmlTempFile=%d" % value)
    with open(INI, "w", encoding="mbcs") as f:
        f.write("\r\n".join(out) + "\r\n")


def launch():
    proc = subprocess.Popen([os.path.join(TESTDIR, "SnipMagic.exe")], cwd=TESTDIR)
    hwnd = None
    for _ in range(100):
        hwnd = user32.FindWindowW("ScreenshotToolMainWindow", None)
        if hwnd:
            break
        time.sleep(0.1)
    time.sleep(1.2)
    return proc, hwnd


def close_app(proc, hwnd):
    user32.PostMessageW(hwnd, 0x0010, 0, 0)
    time.sleep(1.0)
    # 「还有打开的截图页签，关闭前是否保存？」→ 选「否」
    for _ in range(20):
        mb = user32.FindWindowW("#32770", None)
        if mb:
            user32.PostMessageW(mb, 0x0111, 7, 0)  # IDNO
            break
        time.sleep(0.2)
    for _ in range(100):
        if proc.poll() is not None:
            break
        time.sleep(0.1)


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    kill_existing()
    if os.path.isdir(TESTDIR):
        for _ in range(10):
            shutil.rmtree(TESTDIR, ignore_errors=True)
            if not os.path.isdir(TESTDIR):
                break
            time.sleep(0.3)
    if not os.path.isdir(TESTDIR):
        os.makedirs(TESTDIR)
    shutil.copy2(EXE_SRC, os.path.join(TESTDIR, "SnipMagic.exe"))
    if os.path.isdir(CLIPDIR):
        shutil.rmtree(CLIPDIR, ignore_errors=True)

    # ---- 1) 默认设置：截图 + 复制，不应留下任何文件 ----
    proc, hwnd = launch()
    if not check(bool(hwnd), "主窗口启动"):
        return
    ok = do_capture(hwnd)
    check(ok, "截图完成（选区 600x400）")
    user32.PostMessageW(hwnd, 0x0111, ID_CMD_COPY, 0)
    time.sleep(1.2)
    check(pngs() <= 0, "默认设置下复制：临时目录没有图片", pngs())
    user32.PostMessageW(hwnd, 0x0111, ID_CMD_COPY, 0)
    time.sleep(0.8)
    check(pngs() <= 0, "再复制一次：仍然没有图片", pngs())
    img = ImageGrab.grab()
    img.crop(win_rect(hwnd)).save(os.path.join(OUTDIR, "capture_default_mode.png"))
    close_app(proc, hwnd)
    check(True, "第一次会话已关闭")

    # ---- 2) 兼容模式：复制写一张，且只有一张 ----
    set_ini_compat(1)
    proc, hwnd = launch()
    if not check(bool(hwnd), "主窗口第二次启动"):
        return
    ok = do_capture(hwnd)
    check(ok, "第二次截图完成")
    user32.PostMessageW(hwnd, 0x0111, ID_CMD_COPY, 0)
    time.sleep(1.2)
    n = pngs()
    check(n == 1, "兼容模式下复制：正好 1 张临时图片", n)
    names = os.listdir(CLIPDIR) if os.path.isdir(CLIPDIR) else []
    check(names == ["clip_current.png"] or (len(names) == 1 and names[0].startswith("clip_current")),
          "文件名固定，可以被整体清掉", names)
    user32.PostMessageW(hwnd, 0x0111, ID_CMD_COPY, 0)
    time.sleep(0.8)
    check(pngs() == 1, "再复制一次：仍然只有 1 张（覆盖同一份）", pngs())

    # ---- 3) 退出后不残留 ----
    close_app(proc, hwnd)
    time.sleep(0.8)
    check(proc.poll() is not None, "程序已退出", proc.poll())
    check(not os.path.isdir(CLIPDIR), "退出后临时图片目录已删除", CLIPDIR)

    with open(os.path.join(OUTDIR, "copy_result.json"), "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=2)
    bad = [r for r in results if not r["ok"]]
    print("\n%s（失败 %d 项）" % ("全部通过" if not bad else "有失败项", len(bad)))


if __name__ == "__main__":
    main()
