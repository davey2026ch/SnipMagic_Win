# -*- coding: utf-8 -*-
"""
真机回归验证（2026-11-26）：动图页新增的帧内编辑不应影响普通截图流程
1) 动图页：马赛克后 Ctrl+Z（撤销）能把帧像素还原
2) 动图页「存为页签」→ 普通页签：画箭头、撤销、框选马赛克、保存 PNG 全部正常
3) 撤销栈/坐标换算/框选虚线的重构对普通页签没有副作用
产物：本目录 gif_verify_20261126/regress_*.png + regress_result.json

注意：本脚本跑的是**测试构建**，需要先临时加两处钩子并编译（正式版没有这些钩子）：
  1) app.cpp 的 Init() 末尾：造一张 6 帧合成动图（400x240，红块逐帧右移 + 55..150 竖条纹区）；
  2) app.cpp 的 SaveDoc() 顶部：读环境变量 SNIPMAGIC_TEST_SAVE_PATH 直接写盘，跳过保存对话框；
  编译时加 -DSNIPMAGIC_TEST_HOOK（zig 命令见 .workbuddy/memory/2026-11-26.md）。
  ★ 不要用 SendMessage 读状态栏 SB_GETTEXTW：本程序会崩（0xC000041D），判定一律看画面像素/保存出来的文件。
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import time

from PIL import Image

try:
    _u0 = ctypes.windll.user32
    _u0.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    _u0.SetProcessDpiAwarenessContext.restype = wt.BOOL
    _u0.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    pass

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
user32.FindWindowW.restype = ctypes.c_void_p
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

ROOT = r"F:\Projects\截图工具"
EXE = os.path.join(ROOT, "_app_test.exe")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gif_verify_20261126")
os.makedirs(OUT, exist_ok=True)
SAVE_PNG = os.path.join(OUT, "regress_frame.png")
if os.path.exists(SAVE_PNG):
    os.remove(SAVE_PNG)

R = {"steps": []}


def log(msg, ok=True):
    R["steps"].append({"msg": msg, "ok": bool(ok)})
    print(("OK   " if ok else "FAIL ") + msg, flush=True)


class RECT(ctypes.Structure):
    _fields_ = [("left", wt.LONG), ("top", wt.LONG), ("right", wt.LONG), ("bottom", wt.LONG)]


class BIH(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
                ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


class BI(ctypes.Structure):
    _fields_ = [("bmiHeader", BIH), ("bmiColors", wt.DWORD * 3)]


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


def find_children(parent, cls_name):
    res = []

    @WNDENUMPROC
    def cb(h, l):
        if _cls(h) == cls_name:
            res.append(h)
        return True

    user32.EnumChildWindows(parent, cb, 0)
    return res


def win_pid(h):
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
    return pid.value


def get_dpi(h):
    return int(user32.GetDpiForWindow(h) or 96)


def sc(v, dpi):
    return (v * dpi + 48) // 96


def capture(hwnd):
    rc = RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rc))
    w, h = rc.right, rc.bottom
    if w <= 0 or h <= 0:
        return None
    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 2)
    bi = BI()
    bi.bmiHeader.biSize = ctypes.sizeof(BIH)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    return Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")


def click(hwnd, x, y, drag_to=None):
    user32.PostMessageW(hwnd, 0x0201, 1, (y << 16) | (x & 0xFFFF))
    time.sleep(0.05)
    end = (x, y)
    if drag_to:
        for i in range(1, 7):
            ix = x + (drag_to[0] - x) * i // 6
            iy = y + (drag_to[1] - y) * i // 6
            user32.PostMessageW(hwnd, 0x0200, 1, (iy << 16) | (ix & 0xFFFF))
            time.sleep(0.03)
        end = drag_to
    user32.PostMessageW(hwnd, 0x0202, 0, (end[1] << 16) | (end[0] & 0xFFFF))
    time.sleep(0.3)


def key(hwnd, vk):
    user32.PostMessageW(hwnd, 0x0100, vk, 0)
    time.sleep(0.3)


def command(hwnd, cmd):
    user32.PostMessageW(hwnd, 0x0111, cmd, 0)
    time.sleep(0.4)


def kill_existing():
    subprocess.run(["taskkill", "/IM", os.path.basename(EXE), "/F"], capture_output=True)
    time.sleep(0.8)


def main():
    kill_existing()
    env = dict(os.environ)
    env["SNIPMAGIC_TEST_SAVE_PATH"] = SAVE_PNG
    proc = subprocess.Popen([EXE], cwd=ROOT, env=env)
    mainwnd = None
    for _ in range(200):
        for w in find_windows("ScreenshotToolMainWindow"):
            if win_pid(w) == proc.pid:
                mainwnd = w
                break
        if mainwnd:
            break
        time.sleep(0.1)
    if not mainwnd:
        log("主窗口没启动", False)
        return
    time.sleep(1.2)
    canvas = find_children(mainwnd, "ScreenshotToolCanvas")[0]
    rc = RECT()
    user32.GetClientRect(canvas, ctypes.byref(rc))
    dpi = get_dpi(canvas)
    cw, ch = rc.right, rc.bottom

    # 动图页几何（与 canvas_gif.cpp 的 ComputeGifLayout 一致）
    bottomH = sc(28, dpi)
    stripH = sc(46, dpi) + sc(16, dpi) + sc(10, dpi)
    previewH = ch - bottomH - stripH
    fit = min(max(16, cw - sc(24, dpi)) / 400.0, max(16, previewH - sc(24, dpi)) / 240.0, 1.0)
    dw, dh = 400 * fit, 240 * fit
    dx = cw / 2.0 - dw / 2.0
    dy = previewH / 2.0 - dh / 2.0

    def px(ix, iy):
        return (int(round(dx + ix * fit)), int(round(dy + iy * fit)))

    def colors_in(img, ix0, iy0, ix1, iy1):
        a0, b0 = px(ix0, iy0)
        a1, b1 = px(ix1, iy1)
        seen = set()
        for y in range(max(0, b0), min(img.height, b1)):
            for x in range(max(0, a0), min(img.width, a1)):
                seen.add(img.getpixel((x, y)))
        return len(seen)

    # ---- 1) 动图页：马赛克 + 撤销 ----
    key(canvas, 0x27)               # 第 2 帧
    command(mainwnd, 200)           # 选择工具
    click(canvas, *px(55, 60), drag_to=px(150, 180))
    before = capture(canvas)
    command(mainwnd, 101)           # 马赛克
    after = capture(canvas)
    c_before = colors_in(before, 55, 60, 150, 180)
    c_after = colors_in(after, 55, 60, 150, 180)
    log("动图页马赛克生效（颜色数 %d → %d）" % (c_before, c_after), c_after > c_before)
    command(mainwnd, 104)           # 撤销
    undo_img = capture(canvas)
    undo_img.save(os.path.join(OUT, "regress_undo_mosaic.png"))
    c_undo = colors_in(undo_img, 55, 60, 150, 180)
    log("动图页撤销后帧像素还原（颜色数回到 %d）" % c_undo, c_undo <= c_before)
    key(canvas, 0x25)               # 回到第 1 帧

    # ---- 2) 存为页签 → 普通页签 ----
    bh = sc(20, dpi)
    by = ch - bottomH + (bottomH - bh) // 2
    right = cw - sc(8, dpi)
    saveGif = (right - sc(64, dpi), right)
    right -= sc(64, dpi) + sc(4, dpi)
    saveTab = (right - sc(72, dpi), right)
    tabx = (saveTab[0] + saveTab[1]) // 2
    tabby = by + bh // 2
    click(canvas, tabx, tabby)
    time.sleep(1.0)
    normal = capture(canvas)
    normal.save(os.path.join(OUT, "regress_normal_tab.png"))
    log("「存为页签」生成了普通页签（画面已变）",
        normal is not None and normal.getpixel((cw // 2, 8)) != after.getpixel((cw // 2, 8)))

    # 普通页签：画面上是当前帧（含标注），画箭头 / 撤销
    base2 = capture(canvas)
    # 普通页签是 400x240 的图，画在客户区左上角、边距 16px、缩放 100%
    IX, IY = 16, 16
    command(mainwnd, 204)           # 箭头
    click(canvas, IX + 80, IY + 60, drag_to=(IX + 220, IY + 120))
    drawn = capture(canvas)
    changed = sum(1 for y in range(0, min(drawn.height, base2.height), 2)
                  for x in range(0, min(drawn.width, base2.width), 2)
                  if drawn.getpixel((x, y)) != base2.getpixel((x, y)))
    log("普通页签能画箭头（%d 采样点变化）" % changed, changed > 20)
    command(mainwnd, 104)           # 撤销
    undone = capture(canvas)
    back = sum(1 for y in range(0, min(undone.height, base2.height), 2)
               for x in range(0, min(undone.width, base2.width), 2)
               if undone.getpixel((x, y)) != base2.getpixel((x, y)))
    log("普通页签撤销回到画箭头之前（%d 采样点不同）" % back, back < 40)

    # 普通页签：框选 + 马赛克改底图像素
    command(mainwnd, 200)
    click(canvas, IX + 120, IY + 70, drag_to=(IX + 300, IY + 190))
    pre_mos = capture(canvas)
    command(mainwnd, 101)
    post_mos = capture(canvas)
    mos_diff = sum(1 for y in range(IY + 70, IY + 190)
                   for x in range(IX + 120, IX + 300)
                   if pre_mos.getpixel((x, y)) != post_mos.getpixel((x, y)))
    log("普通页签马赛克正常（%d 像素变化）" % mos_diff, mos_diff > 100)

    # 画一个箭头再保存 PNG，确认 PNG 里有箭头
    command(mainwnd, 204)
    click(canvas, IX + 40, IY + 200, drag_to=(IX + 300, IY + 230))
    command(mainwnd, 106)           # 保存（测试构建走环境变量路径）
    time.sleep(1.2)
    if os.path.exists(SAVE_PNG):
        im = Image.open(SAVE_PNG).convert("RGB")
        # 箭头画在图片下方（y 200~230），红块在 y 80~160，互不干扰
        warm = 0
        for y in range(190, min(240, im.height)):
            for x in range(30, min(310, im.width)):
                r, g, b = im.getpixel((x, y))
                if r > 150 and r - b > 60:
                    warm += 1
        log("普通页签保存的 PNG 里能看到箭头（%d 个箭头像素）" % warm, warm > 100)
        im.save(os.path.join(OUT, "regress_saved_png.png"))
    else:
        log("普通页签保存 PNG 失败", False)

    alive = proc.poll() is None
    log("全程程序存活", alive)
    kill_existing()
    with open(os.path.join(OUT, "regress_result.json"), "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=2)
    bad = [s for s in R["steps"] if not s["ok"]]
    print("\n==== %d 项，失败 %d 项 ====" % (len(R["steps"]), len(bad)))
    for s in bad:
        print("  FAIL: " + s["msg"])


main()
