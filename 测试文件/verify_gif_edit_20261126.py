# -*- coding: utf-8 -*-
"""
真机验证（2026-11-26）GIF 播放页两项优化
A) 画面可以像普通截图一样编辑：左侧工具在预览画面上标注，标注跟着帧走，
   保存 GIF 时自动写进文件（帧像素 + 重新压缩）；保存后画面与文件一致。
B) 缩略图右键菜单：删除当前帧 / 删除左侧全部帧 / 删除右侧全部帧；保存时同步。
产物：本目录 gif_verify_20261126/ 下的截图 + 保存出来的 gif + result.json
用法：python verify_gif_edit_20261126.py

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

from PIL import Image, ImageGrab

# 必须最早设置：per-monitor v2，否则坐标被 DPI 虚拟化，和程序的物理像素对不上
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
gdi32 = ctypes.windll.gdi32
user32.FindWindowW.restype = ctypes.c_void_p
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

ROOT = r"F:\Projects\截图工具"
EXE = os.path.join(ROOT, "_app_test.exe")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gif_verify_20261126")
os.makedirs(OUT, exist_ok=True)
SAVED_GIF = os.path.join(OUT, "edited.gif")
if os.path.exists(SAVED_GIF):
    os.remove(SAVED_GIF)

R = {"steps": []}


def log(msg, ok=True):
    R["steps"].append({"msg": msg, "ok": bool(ok)})
    print(("OK   " if ok else "FAIL ") + msg, flush=True)


# ---------------------------------------------------------------- win32 基础
class RECT(ctypes.Structure):
    _fields_ = [("left", wt.LONG), ("top", wt.LONG), ("right", wt.LONG), ("bottom", wt.LONG)]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
                ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wt.DWORD * 3)]


def _cls(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def find_children(parent, cls_name):
    res = []

    @WNDENUMPROC
    def cb(h, l):
        if _cls(h) == cls_name:
            res.append(h)
        return True

    user32.EnumChildWindows(parent, cb, 0)
    return res


def find_windows(cls_name):
    res = []

    @WNDENUMPROC
    def cb(h, l):
        if user32.IsWindowVisible(h) and _cls(h) == cls_name:
            res.append(h)
        return True

    user32.EnumWindows(cb, 0)
    return res


def wait_window(cls_name, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        ws = find_windows(cls_name)
        if ws:
            return ws[0]
        time.sleep(0.1)
    return None


def get_dpi(hwnd):
    u = ctypes.windll.user32
    if hasattr(u, "GetDpiForWindow"):
        d = u.GetDpiForWindow(hwnd)
        if d:
            return int(d)
    return 96


def scale(v, dpi):
    return (v * dpi + 48) // 96


def capture(hwnd):
    """PrintWindow 抓子窗口（锁屏也能抓到内容），返回 PIL.Image"""
    rc = RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rc))
    w, h = rc.right, rc.bottom
    if w <= 0 or h <= 0:
        return None
    hdc = user32.GetDC(hwnd)
    memdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(memdc, bmp)
    ok = user32.PrintWindow(hwnd, memdc, 2)
    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h           # 自上而下
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    bi.bmiHeader.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(memdc, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(memdc)
    user32.ReleaseDC(hwnd, hdc)
    img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")
    return img if ok else img


def post_click(hwnd, x, y, drag_to=None, button="left"):
    down = 0x0201 if button == "left" else 0x0204
    up = 0x0202 if button == "left" else 0x0205
    mk = 0x0001 if button == "left" else 0x0002
    lp = (y << 16) | (x & 0xFFFF)
    user32.PostMessageW(hwnd, down, mk, lp)
    time.sleep(0.05)
    end = (x, y)
    if drag_to:
        steps = 6
        for i in range(1, steps + 1):
            ix = x + (drag_to[0] - x) * i // steps
            iy = y + (drag_to[1] - y) * i // steps
            user32.PostMessageW(hwnd, 0x0200, mk, (iy << 16) | (ix & 0xFFFF))
            time.sleep(0.03)
        end = drag_to
    # 松手点必须用拖动终点：框选区域是按 up 的坐标算的
    user32.PostMessageW(hwnd, up, 0, (end[1] << 16) | (end[0] & 0xFFFF))
    time.sleep(0.25)


def post_key(hwnd, vk):
    user32.PostMessageW(hwnd, 0x0100, vk, 0)   # WM_KEYDOWN
    time.sleep(0.2)


def post_command(hwnd, cmd):
    user32.PostMessageW(hwnd, 0x0111, cmd, 0)
    time.sleep(0.35)


def real_key(vk):
    user32.keybd_event(vk, 0, 0, 0)
    time.sleep(0.08)
    user32.keybd_event(vk, 0, 2, 0)
    time.sleep(0.25)


def gif_frame_count(path):
    """读帧数后立刻关闭句柄：PIL 不关会占着文件，后续保存写不进去"""
    with Image.open(path) as im:
        return getattr(im, "n_frames", 1)


def gif_frames(path):
    out = []
    with Image.open(path) as im:
        for i in range(getattr(im, "n_frames", 1)):
            im.seek(i)
            out.append(im.convert("RGB").copy())
    return out


# ------------------------------------------------------------------ 布局复算
def gif_layout(cw, ch, dpi):
    padX = scale(8, dpi)
    thumbW = scale(100, dpi)
    thumbH = scale(46, dpi)
    gap = scale(4, dpi)
    labelH = scale(16, dpi)
    bottomH = scale(28, dpi)
    stripH = thumbH + labelH + scale(10, dpi)
    bottom_top = ch - bottomH
    strip_top = bottom_top - stripH
    preview_h = strip_top
    fw, fh = 400, 240
    availW = max(16, cw - scale(24, dpi))
    availH = max(16, preview_h - scale(24, dpi))
    fitZ = 1.0
    if fw > availW or fh > availH:
        fitZ = min(availW / fw, availH / fh)
    z = fitZ                      # gifZoom = 1
    dw, dh = fw * z, fh * z
    dx = cw / 2.0 - dw / 2.0
    dy = preview_h / 2.0 - dh / 2.0
    return dict(padX=padX, thumbW=thumbW, thumbH=thumbH, gap=gap, labelH=labelH,
                bottomH=bottomH, strip_top=strip_top, strip_bottom=bottom_top,
                bottom_top=bottom_top, dx=dx, dy=dy, z=z, dw=dw, dh=dh, cw=cw, ch=ch)


def thumb_center(L, idx, scroll=0):
    return (int(L["padX"] + idx * (L["thumbW"] + L["gap"]) + L["thumbW"] / 2 - scroll),
            int(L["strip_top"] + scale(5, 96) + L["thumbH"] / 2))


def close_app(proc):
    if proc.poll() is None:
        try:
            subprocess.run(["taskkill", "/PID", str(proc.pid), "/F"],
                           capture_output=True, timeout=10)
        except Exception:
            pass


def kill_existing():
    subprocess.run(["taskkill", "/IM", os.path.basename(EXE), "/F"], capture_output=True)
    for _ in range(20):
        found = find_windows("ScreenshotToolMainWindow")
        alive = subprocess.run(["tasklist", "/FI", "IMAGENAME eq " + os.path.basename(EXE)],
                               capture_output=True, text=True, errors="ignore").stdout
        if not found and os.path.basename(EXE) not in alive:
            return
        time.sleep(0.3)


def win_pid(hwnd):
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value


# --------------------------------------------------------------------- 主流程
def main():
    kill_existing()
    env = dict(os.environ)
    env["SNIPMAGIC_TEST_SAVE_PATH"] = SAVED_GIF
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
        log("主窗口没有启动（或不属于本次进程）", False)
        close_app(proc)
        return
    log("主窗口已启动 (pid=%d)" % proc.pid)
    try:
        user32.SetForegroundWindow(mainwnd)
    except Exception:
        pass
    time.sleep(1.6)

    canvas = None
    for _ in range(60):
        cs = find_children(mainwnd, "ScreenshotToolCanvas")
        if cs:
            canvas = cs[0]
            break
        time.sleep(0.1)
    if not canvas:
        log("找不到画布子窗口", False)
        close_app(proc)
        return
    log("找到画布子窗口")

    time.sleep(0.6)
    rc = RECT()
    user32.GetClientRect(canvas, ctypes.byref(rc))
    dpi = get_dpi(canvas)
    L = gif_layout(rc.right, rc.bottom, dpi)
    log("画布 %dx%d dpi=%d 预览帧 rect=(%.1f,%.1f,%.0fx%.0f) 缩放=%.3f"
        % (rc.right, rc.bottom, dpi, L["dx"], L["dy"], L["dw"], L["dh"], L["z"]))

    base = capture(canvas)
    if base is None:
        log("抓不到画布画面", False)
        close_app(proc)
        return
    base.save(os.path.join(OUT, "01_frame1_baseline.png"))

    # ---- 帧像素 → 画布坐标 ----
    def px(ix, iy):
        return (int(round(L["dx"] + ix * L["z"])), int(round(L["dy"] + iy * L["z"])))

    def region_has_warm(img, ix0, iy0, ix1, iy1, pad=4):
        """区域内是否有"暖色"（箭头默认橙红）像素"""
        x0, y0 = px(min(ix0, ix1), min(iy0, iy1))
        x1, y1 = px(max(ix0, ix1), max(iy0, iy1))
        x0, y0 = max(0, x0 - pad), max(0, y0 - pad)
        x1, y1 = min(img.width - 1, x1 + pad), min(img.height - 1, y1 + pad)
        n = 0
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                r, g, b = img.getpixel((x, y))
                if r > 150 and r - b > 60:
                    n += 1
        return n

    # ---- A1：用箭头工具在画面上画一条箭头 ----
    post_command(mainwnd, 204)          # ID_TOOL_ARROW
    a, b = (215, 25), (335, 65)         # 帧像素坐标（避开红块区域）
    post_click(canvas, *px(*a), drag_to=px(*b))
    img1 = capture(canvas)
    img1.save(os.path.join(OUT, "02_frame1_arrow.png"))
    warm = region_has_warm(img1, a[0], a[1], b[0], b[1])
    log("画面上画出箭头（区域暖色像素 %d 个）" % warm, warm > 40)
    diff_px = sum(1 for y in range(0, min(base.height, img1.height), 2)
                  for x in range(0, min(base.width, img1.width), 2)
                  if base.getpixel((x, y)) != img1.getpixel((x, y)))
    log("画面确实变了（%d 采样点不同）" % diff_px, diff_px > 50)

    # ---- A2：标注跟着帧走 ----
    post_key(canvas, 0x27)              # → 下一帧
    img2 = capture(canvas)
    img2.save(os.path.join(OUT, "03_frame2_clean.png"))
    warm2 = region_has_warm(img2, a[0], a[1], b[0], b[1])
    log("切到第 2 帧后箭头不跟着跑（暖色像素 %d）" % warm2, warm2 < 10)

    # ---- A3：马赛克打在第 2 帧像素上 ----
    post_command(mainwnd, 200)          # ID_TOOL_SELECT
    mx0, my0, mx1, my1 = 55, 60, 150, 180
    post_click(canvas, *px(mx0, my0), drag_to=px(mx1, my1))
    before_mosaic = capture(canvas)
    post_command(mainwnd, 101)          # ID_CMD_MOSAIC
    after_mosaic = capture(canvas)
    after_mosaic.save(os.path.join(OUT, "04_frame2_mosaic.png"))
    changed = 0
    x0, y0 = px(mx0, my0)
    x1, y1 = px(mx1, my1)
    for y in range(max(0, y0), min(after_mosaic.height, y1)):
        for x in range(max(0, x0), min(after_mosaic.width, x1)):
            if before_mosaic.getpixel((x, y)) != after_mosaic.getpixel((x, y)):
                changed += 1

    def color_count(img, ix0, iy0, ix1, iy1):
        """区域内的不同颜色个数：1px 细条纹原本只有几种色，打码后出现大量平均色"""
        a0, b0 = px(ix0, iy0)
        a1, b1 = px(ix1, iy1)
        seen = set()
        for yy in range(max(0, b0), min(img.height, b1)):
            for xx in range(max(0, a0), min(img.width, a1)):
                seen.add(img.getpixel((xx, yy)))
        return len(seen)

    c_before = color_count(before_mosaic, mx0, my0, mx1, my1)
    c_after = color_count(after_mosaic, mx0, my0, mx1, my1)
    log("马赛克把细条纹糊成块（区域颜色数 %d → %d，%d 像素变化）"
        % (c_before, c_after, changed),
        changed > 100 and c_after > c_before + 2)

    # ---- A4：回到第 1 帧，箭头还在 ----
    post_key(canvas, 0x25)              # ← 上一帧
    img1b = capture(canvas)
    img1b.save(os.path.join(OUT, "05_frame1_back.png"))
    warm = region_has_warm(img1b, a[0], a[1], b[0], b[1])
    log("切回第 1 帧箭头还在（暖色像素 %d）" % warm, warm > 40)
    # 箭头周围会画出选中框/控制点，比较时把这圈排除掉
    ax0, ay0 = px(min(a[0], b[0]) - 40, min(a[1], b[1]) - 40)
    ax1, ay1 = px(max(a[0], b[0]) + 40, max(a[1], b[1]) + 40)
    back_same = 0
    preview_bottom = int(L["strip_top"])
    for y in range(0, min(preview_bottom, img1.height, img1b.height), 2):
        for x in range(0, min(img1.width, img1b.width), 2):
            if ax0 <= x <= ax1 and ay0 <= y <= ay1:
                continue
            if img1.getpixel((x, y)) != img1b.getpixel((x, y)):
                back_same += 1
    log("第 1 帧除箭头外画面完全一致（%d 采样点不同）" % back_same, back_same < 20)

    # ---- A5：保存 GIF（测试构建走环境变量路径，跳过对话框） ----
    post_command(mainwnd, 106)          # ID_CMD_SAVE
    for _ in range(40):
        if os.path.exists(SAVED_GIF) and os.path.getsize(SAVED_GIF) > 0:
            break
        time.sleep(0.2)
    time.sleep(0.4)
    if not (os.path.exists(SAVED_GIF) and os.path.getsize(SAVED_GIF) > 0):
        log("保存 GIF 失败（文件没生成）", False)
    else:
        log("已生成 GIF：%s（%d 字节）" % (os.path.basename(SAVED_GIF), os.path.getsize(SAVED_GIF)))
        frames = gif_frames(SAVED_GIF)
        R["saved_frames"] = len(frames)
        log("保存的 GIF 有 %d 帧（应为 6）" % len(frames), len(frames) == 6)
        if len(frames) >= 2:
            f1, f2 = frames[0], frames[1]
            # 箭头：在第 1 帧的箭头位置找暖色像素；第 2 帧同一位置应该没有
            def warm_count(im2, ix0, iy0, ix1, iy1):
                n = 0
                for y in range(max(0, iy0), min(im2.height, iy1)):
                    for x in range(max(0, ix0), min(im2.width, ix1)):
                        r, g, b = im2.getpixel((x, y))
                        if r > 150 and r - b > 60:
                            n += 1
                return n
            w1 = warm_count(f1, a[0], a[1], b[0], b[1])
            w2 = warm_count(f2, a[0], a[1], b[0], b[1])
            log("GIF 第 1 帧里能找出箭头（暖色 %d）" % w1, w1 > 40)
            log("GIF 第 2 帧同位置没有箭头（暖色 %d）" % w2, w2 < 10)
            # 马赛克：第 2 帧被糊成色块（颜色种类暴增），第 1 帧仍是干净条纹
            def colors_in(im2, ix0, iy0, ix1, iy1):
                seen = set()
                for yy in range(max(0, iy0), min(im2.height, iy1)):
                    for xx in range(max(0, ix0), min(im2.width, ix1)):
                        seen.add(im2.getpixel((xx, yy)))
                return len(seen)
            c1 = colors_in(f1, mx0, my0, mx1, my1)
            c2 = colors_in(f2, mx0, my0, mx1, my1)
            log("GIF 第 1 帧仍是干净条纹（颜色数 %d）" % c1, c1 <= 6)
            log("GIF 第 2 帧已被马赛克（颜色数 %d）" % c2, c2 > c1 + 4)
            f2.save(os.path.join(OUT, "06_saved_frame2.png"))
            f1.save(os.path.join(OUT, "07_saved_frame1.png"))
        # 保存后画面应与保存前基本一致（帧被重新量化，颜色会有极小抖动）
        after_save = capture(canvas)
        after_save.save(os.path.join(OUT, "08_after_save.png"))
        tot = 0
        n = 0
        for y in range(0, min(after_save.height, img1b.height), 2):
            for x in range(0, min(after_save.width, img1b.width), 2):
                p1 = after_save.getpixel((x, y))
                p2 = img1b.getpixel((x, y))
                tot += abs(p1[0] - p2[0]) + abs(p1[1] - p2[1]) + abs(p1[2] - p2[2])
                n += 1
        mean = tot / max(1, n) / 3.0
        warm_after_save = region_has_warm(after_save, a[0], a[1], b[0], b[1])
        log("保存后画面基本没跳变（平均色差 %.2f，箭头仍在 %d）" % (mean, warm_after_save),
            mean < 8.0 and warm_after_save > 40)

    # ---- B1：缩略图右键菜单 ----
    def open_thumb_menu(idx):
        tx, ty = thumb_center(L, idx)
        user32.PostMessageW(canvas, 0x0204, 0x0002, (ty << 16) | (tx & 0xFFFF))
        time.sleep(0.9)
        h = user32.FindWindowW("#32768", None)
        # 只认本进程的菜单，避免捡到别的程序的弹出菜单
        if h and win_pid(h) != proc.pid:
            return None
        return h

    def menu_pick(popup, n):
        """弹出菜单没有初始选中项：发 n 次 DOWN 就是第 n 项，再 ENTER 执行"""
        for _ in range(n):
            user32.PostMessageW(popup, 0x0100, 0x28, 0)   # VK_DOWN
            time.sleep(0.3)
        user32.PostMessageW(popup, 0x0100, 0x0D, 0)       # VK_RETURN
        time.sleep(0.9)

    popup = open_thumb_menu(2)
    shot = ImageGrab.grab()
    shot.save(os.path.join(OUT, "09_thumb_menu.png"))
    log("缩略图右键弹出菜单（%s）" % ("找到菜单窗口" if popup else "没找到菜单窗口"),
        bool(popup))
    if popup:
        menu_pick(popup, 3)          # 第 3 项：删除右侧全部帧
    img_after_del = capture(canvas)
    img_after_del.save(os.path.join(OUT, "10_after_delete_right.png"))
    log("删帧后程序存活、画面可抓取", img_after_del is not None)

    post_command(mainwnd, 106)          # 再保存一次：文件帧数应变成 3
    time.sleep(1.2)
    n3 = gif_frame_count(SAVED_GIF)
    R["frames_after_delete_right"] = n3
    log("删右侧后重新保存，GIF 只剩 %d 帧（应为 3）" % n3, n3 == 3)

    # 删左侧全部帧（当前在第 3 帧 idx=2 → 应剩 1 帧）
    popup = open_thumb_menu(2)
    if popup:
        menu_pick(popup, 2)          # 第 2 项：删除左侧全部帧
    time.sleep(0.5)
    post_command(mainwnd, 106)
    time.sleep(1.2)
    n1 = gif_frame_count(SAVED_GIF)
    R["frames_after_delete_left"] = n1
    log("删左侧后重新保存，GIF 只剩 %d 帧（应为 1）" % n1, n1 == 1)

    # 只剩 1 帧：菜单里删除项应灰掉，ESC 关闭后帧数不变
    popup = open_thumb_menu(0)
    shot = ImageGrab.grab()
    shot.save(os.path.join(OUT, "11_menu_single_frame.png"))
    log("只剩 1 帧时右键菜单仍可打开", bool(popup))
    if popup:
        user32.PostMessageW(popup, 0x0100, 0x1B, 0)   # VK_ESCAPE
        time.sleep(0.6)
    alive = proc.poll() is None
    log("ESC 关闭菜单后程序存活", alive)
    post_command(mainwnd, 106)
    time.sleep(1.2)
    nf = gif_frame_count(SAVED_GIF)
    log("单帧状态再保存仍是 %d 帧（没被清空）" % nf, nf == 1)

    close_app(proc)
    log("结束")
    with open(os.path.join(OUT, "result.json"), "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=2)
    bad = [s for s in R["steps"] if not s["ok"]]
    print("\n==== %d 项，失败 %d 项 ====" % (len(R["steps"]), len(bad)))
    for s in bad:
        print("  FAIL: " + s["msg"])


main()
