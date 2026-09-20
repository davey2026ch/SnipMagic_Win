# -*- coding: utf-8 -*-
# 真机长截图全流程：启动应用 -> PostMessage 长截图 -> 框选 -> 慢速滚轮 -> 完成 -> 复制 -> 分析
import ctypes
import json
import subprocess
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass
from PIL import ImageGrab

OUT = r"F:\Projects\截图工具\test_wechat"
EXE = r"F:\Projects\截图工具\dist\ScreenshotTool.exe"
WM_COMMAND = 0x0111
ID_CMD_LONG_CAPTURE = 115
ID_CMD_COPY = 108

vx = user32.GetSystemMetrics(76)
vy = user32.GetSystemMetrics(77)
vw = user32.GetSystemMetrics(78)
vh = user32.GetSystemMetrics(79)
MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_ABSOLUTE = 0x8000
MOUSEEVENTF_WHEEL = 0x0800


def to_norm(x, y):
    return (int((x - vx) * 65535 / (vw - 1)), int((y - vy) * 65535 / (vh - 1)))


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
        mouse_move(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps)
        time.sleep(dur / steps)
    user32.mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0)


def wheel(notches):
    user32.mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -120 * notches, 0)


def key_tap(vk):
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(0.06)
    user32.keybd_event(vk, 0, 2, 0)


def enum_all():
    found = []
    WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def cb(hwnd, _):
        r = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        n = user32.GetWindowTextLengthW(hwnd)
        tbuf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, tbuf, n + 1)
        cbuf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cbuf, 256)
        found.append({"hwnd": hwnd, "title": tbuf.value, "cls": cbuf.value,
                      "rect": [r.left, r.top, r.right, r.bottom],
                      "visible": bool(user32.IsWindowVisible(hwnd))})
        return True
    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found


def find_main():
    ws = [w for w in enum_all() if w["cls"] == "ScreenshotToolMainWindow"]
    return ws[0] if ws else None


log = {"steps": []}


def step(name, data=None):
    log["steps"].append({"name": name, "data": data, "t": time.time()})
    print("[step]", name, data if data is not None else "")


try:
    # 0) 启动应用（同一进程生命周期内，防止被回收）
    proc = subprocess.Popen([EXE])
    step("launch", proc.pid)
    main = None
    for _ in range(30):
        time.sleep(0.3)
        main = find_main()
        if main:
            break
    step("main_window", main)

    # 1) 退出微信可能的多选状态：点标题栏 + Esc 两次
    wechat = [w for w in enum_all() if w["title"] == "微信"]
    if wechat:
        wr = wechat[0]["rect"]
        click((wr[0] + wr[2]) // 2, wr[1] + 15)
        time.sleep(0.3)
        key_tap(0x1B)
        time.sleep(0.2)
        key_tap(0x1B)
        time.sleep(0.3)
        # 把微信提到最前
        user32.SetWindowPos(wechat[0]["hwnd"], -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
        time.sleep(0.8)
    step("wechat_ready", wechat[0]["rect"] if wechat else None)

    # 2) PostMessage 启动长截图
    if main:
        user32.PostMessageW(main["hwnd"], WM_COMMAND, ID_CMD_LONG_CAPTURE, 0)
    time.sleep(1.5)
    ImageGrab.grab().save(OUT + r"\s1_overlay.png")
    step("overlay_shot")

    # 3) 框选聊天区
    rx0, ry0, rx1, ry1 = 500, 520, 1960, 1790
    drag(rx0, ry0, rx1, ry1)
    time.sleep(1.2)
    bars = [w for w in enum_all() if "LongCaptureBar" in w["cls"]]
    step("bar", bars)
    ImageGrab.grab().save(OUT + r"\s2_bar.png")
    if not bars:
        raise RuntimeError("long capture bar not found")

    # 4) 慢速向下滚动（悬停在聊天区中部）
    mouse_move(1200, 1000)
    time.sleep(0.3)
    for i in range(42):
        wheel(1)
        time.sleep(0.33)
        if i % 10 == 9:
            ImageGrab.grab().save(OUT + ("\\s3_scroll_%02d.png" % i))
    step("scrolled")

    # 5) 点「完成」（按钮在控制条内偏移 376+55, 32）
    bars = [w for w in enum_all() if "LongCaptureBar" in w["cls"]]
    if bars:
        br = bars[0]["rect"]
        click(br[0] + 376 + 55, br[1] + 32)
        step("clicked_done", br)
    time.sleep(2.0)
    # 可能有「缺失/接缝异常」警告弹窗：回车关掉
    key_tap(0x0D)
    time.sleep(1.0)
    ImageGrab.grab().save(OUT + r"\s4_result.png")
    step("result_shot")

    # 6) 复制整图并取剪贴板
    main = find_main()
    if main:
        user32.PostMessageW(main["hwnd"], WM_COMMAND, ID_CMD_COPY, 0)
    time.sleep(1.0)
    img = ImageGrab.grabclipboard()
    if img is not None:
        img.save(OUT + r"\longshot.png")
        step("clipboard_saved", list(img.size))
    else:
        step("clipboard_empty")

    # 7) 收尾：不杀进程，留给用户看结果
    step("done")
except Exception as e:
    step("error", repr(e))
    key_tap(0x1B)

with open(OUT + r"\drive_full_log.json", "w", encoding="utf-8") as f:
    json.dump(log, f, ensure_ascii=False, indent=1, default=str)
print("done")
