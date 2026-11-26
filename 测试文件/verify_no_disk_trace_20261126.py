# -*- coding: utf-8 -*-
"""
真机验证（2026-11-26）：磁盘落盘行为
A) 启动清理：上次异常退出遗留的 %TEMP%\\SnipMagicClip\\*.png 与 mineru_* 目录会被清掉
B) 设置页新增「邮件客户端兼容」开关：默认关、可切换、写进 ini、画面不重叠
C) 退出清理：关掉程序后临时图片不残留

用法：python verify_no_disk_trace_20261126.py
产物：本目录 no_disk_trace_20261126/ 下的截图 + result.json
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import shutil
import subprocess
import time

from PIL import Image, ImageGrab

# 必须最早设置：per-monitor v2，否则坐标被 DPI 虚拟化
try:
    _u0 = ctypes.windll.user32
    _u0.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    _u0.SetProcessDpiAwarenessContext.restype = ctypes.wintypes.BOOL
    _u0.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except Exception:
        pass

user32 = ctypes.windll.user32
user32.FindWindowW.restype = ctypes.c_void_p
user32.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
user32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
user32.ClientToScreen.argtypes = [wt.HWND, ctypes.POINTER(wt.POINT)]
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
user32.GetDlgCtrlID.argtypes = [wt.HWND]

WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE_SRC = os.path.join(ROOT, "out", "bin", "SnipMagic.exe")
TESTDIR = os.path.join(HERE, "cliptest")
OUTDIR = os.path.join(HERE, "no_disk_trace_20261126")
TEMP = os.environ.get("TEMP", r"C:\Users\DELL\AppData\Local\Temp")
CLIPDIR = os.path.join(TEMP, "SnipMagicClip")

IDC_CLIP_HTML = 3020
IDC_OK = 3007
ID_CMD_SETTINGS = 102

results = []


def check(ok, what, extra=""):
    results.append({"ok": bool(ok), "what": what, "extra": str(extra)})
    print(("[PASS] " if ok else "[FAIL] ") + what + (("  <" + str(extra) + ">") if extra else ""))
    return ok


def kill_existing():
    subprocess.run(["taskkill", "/F", "/IM", "SnipMagic.exe"], capture_output=True)


def win_rect(hwnd):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def client_origin(hwnd):
    p = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(p))
    return p.x, p.y


def children(hwnd):
    out = []

    def cb(h, _):
        out.append(h)
        return True

    user32.EnumChildWindows(hwnd, WNDENUMPROC(cb), 0)
    return out


def enum_proc_windows(pid):
    found = []

    def cb(h, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(h, buf, 256)
            cls = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(h, cls, 256)
            found.append((h, buf.value, cls.value))
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


def click(hwnd, x, y):
    lparam = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd, 0x0200, 0, lparam)  # WM_MOUSEMOVE
    user32.PostMessageW(hwnd, 0x0201, 1, lparam)  # WM_LBUTTONDOWN
    user32.PostMessageW(hwnd, 0x0202, 0, lparam)  # WM_LBUTTONUP
    time.sleep(0.35)


def shot(hwnd, name):
    l, t, r, b = win_rect(hwnd)
    img = ImageGrab.grab(bbox=(l, t, r, b))
    path = os.path.join(OUTDIR, name)
    img.save(path)
    return path


def read_ini(path):
    d = {}
    with open(path, "r", encoding="mbcs", errors="replace") as f:
        for line in f:
            line = line.strip()
            if "=" in line and not line.startswith("["):
                k, v = line.split("=", 1)
                d[k.strip()] = v.strip()
    return d


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    kill_existing()
    time.sleep(1.0)
    if os.path.isdir(TESTDIR):
        for _ in range(10):
            shutil.rmtree(TESTDIR, ignore_errors=True)
            if not os.path.isdir(TESTDIR):
                break
            time.sleep(0.3)
    if not os.path.isdir(TESTDIR):
        os.makedirs(TESTDIR)
    exe = os.path.join(TESTDIR, "SnipMagic.exe")
    shutil.copy2(EXE_SRC, exe)

    # ---- A) 制造「上次异常退出」的残留 ----
    os.makedirs(CLIPDIR, exist_ok=True)
    for i in range(3):
        with open(os.path.join(CLIPDIR, "clip_old_%d.png" % i), "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + b"x" * 100)
    mineru = os.path.join(TEMP, "mineru_9999_1234")
    os.makedirs(mineru, exist_ok=True)
    with open(os.path.join(mineru, "slice_0.png"), "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + b"x" * 100)

    proc = subprocess.Popen([exe], cwd=TESTDIR)
    hwnd = None
    for _ in range(80):
        hwnd = user32.FindWindowW("ScreenshotToolMainWindow", None)
        if hwnd:
            break
        time.sleep(0.1)
    check(bool(hwnd), "主窗口已启动")
    if not hwnd:
        return
    time.sleep(1.2)

    # ---- B) 启动清理 ----
    check(not os.path.isdir(CLIPDIR), "启动后 %TEMP%\\SnipMagicClip 已被清空并删除",
          CLIPDIR)
    check(not os.path.isdir(mineru), "启动后遗留的 mineru_* 临时目录已被删除", mineru)

    ini = os.path.join(TESTDIR, "SnipMagic.ini")
    check(os.path.isfile(ini), "ini 已生成在 exe 同目录", ini)
    d = read_ini(ini) if os.path.isfile(ini) else {}
    check(d.get("ClipboardHtmlTempFile") == "0", "默认值 ClipboardHtmlTempFile=0（不落盘）",
          d.get("ClipboardHtmlTempFile"))

    # ---- C) 设置页：新开关 ----
    user32.PostMessageW(hwnd, 0x0111, ID_CMD_SETTINGS, 0)  # WM_COMMAND
    time.sleep(1.5)
    dlg = None
    for h, title, cls in enum_proc_windows(proc.pid):
        if title == "设置":
            dlg = h
            break
    check(bool(dlg), "设置窗口已打开")
    if not dlg:
        return
    shot(dlg, "settings_default.png")

    sw = ok_btn = None
    for h in children(dlg):
        cid = user32.GetDlgCtrlID(h)
        if cid == IDC_CLIP_HTML:
            sw = h
        elif cid == IDC_OK:
            ok_btn = h
    check(sw is not None, "找到「邮件客户端兼容」开关控件")
    check(ok_btn is not None, "找到「确定」按钮")
    if not (sw and ok_btn):
        return

    # 开关位置与「确定」不能重叠（固定像素布局，越界会盖住按钮）
    sr = win_rect(sw)
    orc = win_rect(ok_btn)
    dl, dt, dr, db = win_rect(dlg)
    check(sr[3] < orc[1], "开关在确定按钮上方，不重叠", (sr, orc))
    check(dl < sr[0] and sr[2] < dr and dt < sr[1] and sr[3] < db, "开关完整落在窗口内")

    # 打开开关
    click(sw, 12, 12)
    shot(dlg, "settings_on.png")
    click(ok_btn, 12, 12)
    time.sleep(0.8)
    d = read_ini(ini)
    check(d.get("ClipboardHtmlTempFile") == "1", "点确定后开关状态已写入 ini",
          d.get("ClipboardHtmlTempFile"))

    # 再打开设置：开关应记住「开」
    user32.PostMessageW(hwnd, 0x0111, ID_CMD_SETTINGS, 0)
    time.sleep(1.2)
    dlg2 = None
    for h, title, cls in enum_proc_windows(proc.pid):
        if title == "设置":
            dlg2 = h
            break
    check(bool(dlg2), "设置窗口可再次打开")
    if dlg2:
        shot(dlg2, "settings_reopen_on.png")
        for h in children(dlg2):
            if user32.GetDlgCtrlID(h) == IDC_CLIP_HTML:
                click(h, 12, 12)
                break
        for h in children(dlg2):
            if user32.GetDlgCtrlID(h) == IDC_OK:
                click(h, 12, 12)
                break
        time.sleep(0.8)
        d = read_ini(ini)
        check(d.get("ClipboardHtmlTempFile") == "0", "关掉开关后 ini 回到 0",
              d.get("ClipboardHtmlTempFile"))

    # ---- D) 退出清理 ----
    os.makedirs(CLIPDIR, exist_ok=True)
    with open(os.path.join(CLIPDIR, "clip_exit_1.png"), "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + b"y" * 100)
    user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    time.sleep(1.0)
    # 会弹「确定退出截图工具吗？」确认框，点「是」
    for h, title, cls in enum_proc_windows(proc.pid):
        if cls == "#32770":
            for ch in children(h):
                buf = ctypes.create_unicode_buffer(128)
                user32.GetWindowTextW(ch, buf, 128)
                if buf.value.startswith("是"):
                    click(ch, 12, 12)
                    break
            break
    for _ in range(80):
        if proc.poll() is not None:
            break
        time.sleep(0.1)
    time.sleep(0.6)
    check(proc.poll() is not None, "程序已退出", proc.poll())
    check(not os.path.isdir(CLIPDIR), "退出后临时图片不残留", CLIPDIR)

    with open(os.path.join(OUTDIR, "result.json"), "w", encoding="utf-8") as f:
        json.dump(results, f, ensure_ascii=False, indent=2)
    bad = [r for r in results if not r["ok"]]
    print("\n%s（失败 %d 项）" % ("全部通过" if not bad else "有失败项", len(bad)))


if __name__ == "__main__":
    main()
