# -*- coding: utf-8 -*-
# 真机长截图全流程 v3：恢复微信(去置顶) -> 点长截图 -> 像素校验遮罩 -> 框选 -> 慢滚 -> 完成 -> 复制保存
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
from PIL import Image, ImageGrab

OUT = r"F:\Projects\截图工具\test_wechat"
EXE = r"F:\Projects\截图工具\dist\ScreenshotTool.exe"
WM_COMMAND = 0x0111
ID_CMD_COPY = 108

vx = user32.GetSystemMetrics(76)
vy = user32.GetSystemMetrics(77)
vw = user32.GetSystemMetrics(78)
vh = user32.GetSystemMetrics(79)
M_MOVE, M_DOWN, M_UP, M_ABS, M_WHEEL = 1, 2, 4, 0x8000, 0x0800


def to_norm(x, y):
    return (int((x - vx) * 65535 / (vw - 1)), int((y - vy) * 65535 / (vh - 1)))


def mouse_move(x, y):
    nx, ny = to_norm(x, y)
    user32.mouse_event(M_MOVE | M_ABS, nx, ny, 0, 0)


def click(x, y):
    mouse_move(x, y)
    time.sleep(0.08)
    user32.mouse_event(M_DOWN, 0, 0, 0, 0)
    time.sleep(0.06)
    user32.mouse_event(M_UP, 0, 0, 0, 0)


def drag(x0, y0, x1, y1, steps=30, dur=0.7):
    mouse_move(x0, y0)
    time.sleep(0.15)
    user32.mouse_event(M_DOWN, 0, 0, 0, 0)
    for i in range(1, steps + 1):
        mouse_move(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps)
        time.sleep(dur / steps)
    user32.mouse_event(M_UP, 0, 0, 0, 0)


def wheel(n):
    user32.mouse_event(M_WHEEL, 0, 0, -120 * n, 0)


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


def mean_luma(img, box=None):
    if box:
        img = img.crop(box)
    g = img.convert("L").resize((160, 90))
    return sum(g.getdata()) / (160 * 90)


log = {"steps": []}


def step(name, data=None):
    log["steps"].append({"name": name, "data": str(data)})
    print("[step]", name, data if data is not None else "")


try:
    # 0) 微信：恢复、去置顶、确认非多选状态
    wechat = [w for w in enum_all() if w["title"] == "微信"]
    wh = wechat[0]["hwnd"]
    user32.ShowWindow(wh, 9)
    time.sleep(0.5)
    user32.SetWindowPos(wh, -2, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)  # NOTOPMOST
    time.sleep(0.3)
    user32.SetWindowPos(wh, -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
    time.sleep(0.3)
    user32.SetWindowPos(wh, -2, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
    time.sleep(0.5)
    click(600, 600)  # 给微信焦点（点在左侧会话列表空白）
    time.sleep(0.3)
    key_tap(0x1B)    # 若处于多选则退出（此时微信前台）
    time.sleep(0.4)
    # 若 Esc 把微信最小化了，再恢复一次
    if not user32.IsWindowVisible(wh):
        user32.ShowWindow(wh, 9)
        time.sleep(0.5)
    ImageGrab.grab().save(OUT + r"\v3_wechat.png")
    step("wechat_ready")

    # 1) 启动应用
    proc = subprocess.Popen([EXE])
    step("launch", proc.pid)
    main = None
    for _ in range(40):
        time.sleep(0.3)
        main = find_main()
        if main:
            break
    mr = main["rect"]
    step("main", mr)

    # 把应用窗口提到最前（微信盖住了它的顶栏按钮区），点完按钮它自己会隐藏
    user32.SetWindowPos(main["hwnd"], -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040)
    time.sleep(0.8)
    ImageGrab.grab().save(OUT + chr(92) + "v3_appfront.png")

    # 2) 点「长截图」（按钮2，主窗口左上角起第2个）
    btn_x, btn_y = mr[0] + 162, mr[1] + 38
    click(btn_x, btn_y)
    step("click_long", [btn_x, btn_y])

    # 3) 像素校验遮罩出现（整屏亮度显著下降）
    base = None
    dim_ok = False
    for i in range(10):
        time.sleep(0.4)
        shot = ImageGrab.grab()
        l = mean_luma(shot, (0, 0, 1920, 2160))  # 左屏
        if base is None:
            base = mean_luma(Image.open(OUT + r"\v3_wechat.png"), (0, 0, 1920, 2160))
        if l < base * 0.82:
            dim_ok = True
            break
    shot.save(OUT + r"\v3_overlay.png")
    step("overlay_dim", {"ok": dim_ok, "luma": round(l, 1), "base": round(base, 1)})
    if not dim_ok:
        raise RuntimeError("overlay not dimmed")

    # 4) 框选聊天区
    rx0, ry0, rx1, ry1 = 500, 520, 1960, 1790
    drag(rx0, ry0, rx1, ry1)
    time.sleep(1.5)
    ImageGrab.grab().save(OUT + r"\v3_bar.png")
    bars = [w for w in enum_all() if "LongCaptureBar" in w["cls"]]
    step("bar", bars)
    if not bars:
        raise RuntimeError("no bar")
    br = bars[0]["rect"]

    # 5) 慢速下滚（悬停聊天区中部）
    mouse_move(1200, 1000)
    time.sleep(0.3)
    for i in range(42):
        wheel(1)
        time.sleep(0.33)
    ImageGrab.grab().save(OUT + r"\v3_scrolled.png")
    step("scrolled")

    # 6) 完成
    bars = [w for w in enum_all() if "LongCaptureBar" in w["cls"]]
    if bars:
        br = bars[0]["rect"]
    click(br[0] + 431, br[1] + 32)
    step("done_click", br)
    time.sleep(2.0)
    key_tap(0x0D)
    time.sleep(1.0)
    ImageGrab.grab().save(OUT + r"\v3_result.png")

    # 7) 复制整图
    main = find_main()
    if main:
        user32.PostMessageW(main["hwnd"], WM_COMMAND, ID_CMD_COPY, 0)
    time.sleep(1.2)
    img = ImageGrab.grabclipboard()
    if img is not None:
        img.save(OUT + r"\longshot.png")
        step("saved", list(img.size))
    else:
        step("clipboard_empty")
    step("done")
except Exception as e:
    step("error", repr(e))
    key_tap(0x1B)

with open(OUT + r"\drive_full3_log.json", "w", encoding="utf-8") as f:
    json.dump(log, f, ensure_ascii=False, indent=1, default=str)
print("done")
