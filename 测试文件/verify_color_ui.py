# -*- coding: utf-8 -*-
"""
真机验证：颜色界面三项改动（2026-10-01）
1) 左侧工具栏「颜色」取色器：出现「白色」快捷按钮（且无红色/黑色）
2) 插入文字弹窗：文字颜色默认红、背景颜色默认白，与工具栏颜色互不联动
   （先把工具栏颜色改成白色，再开文字弹窗，文字颜色仍是红色 → 证明解耦）
3) 文字弹窗的两个取色器：出现「红色」「黑色」「白色」快捷按钮，点击即生效
产物：本目录 color_ui_verify/ 下 01~09 截图 + result.json
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import sys
import time

from PIL import ImageGrab

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

user32 = ctypes.windll.user32
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
HWND_TOPMOST = wt.HWND(-1)
SWP_NOMOVE = 0x0002
SWP_NOSIZE = 0x0001

ROOT = r"F:\Projects\截图工具"
DIST = os.path.join(ROOT, "dist")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "color_ui_verify")
os.makedirs(OUT, exist_ok=True)

R = {"steps": []}


def log(msg, ok=True):
    R["steps"].append({"msg": msg, "ok": bool(ok)})
    print(("OK  " if ok else "FAIL") + " " + msg, flush=True)


# ---------------- win32 helpers ----------------

def _text(hwnd):
    n = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 2)
    user32.GetWindowTextW(hwnd, buf, n + 2)
    return buf.value


def _cls(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


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


def rect_of(hwnd):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def client_origin(hwnd):
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return pt.x, pt.y


def client_size(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    return r.right, r.bottom


def click(x, y):
    user32.SetCursorPos(int(x), int(y))
    time.sleep(0.08)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.06)
    user32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.2)


def drag(x1, y1, x2, y2):
    user32.SetCursorPos(int(x1), int(y1))
    time.sleep(0.12)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    steps = 14
    for i in range(1, steps + 1):
        user32.SetCursorPos(int(x1 + (x2 - x1) * i / steps),
                            int(y1 + (y2 - y1) * i / steps))
        time.sleep(0.03)
    user32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.4)


def btn_map(dlg):
    m = {}
    for h in children(dlg):
        if _cls(h) == "Button":
            m.setdefault(_text(h), []).append(h)
    return m


def read_hex(picker):
    # 注意：跨进程读 Edit 内容必须用 WM_GETTEXT 消息，
    # GetWindowTextW 在跨进程场景下会返回创建时的过期缓存文本
    for h in children(picker):
        if _cls(h) == "Edit" and user32.GetDlgCtrlID(h) == 1002:
            buf = ctypes.create_unicode_buffer(64)
            user32.SendMessageW(h, 0x000D, 63, buf)  # WM_GETTEXT
            return buf.value
    return None


def is_reddish(h):
    h = (h or "").lstrip("#")
    try:
        r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    except Exception:
        return False
    return r >= 250 and g <= 8 and b <= 8


def wait_hex(picker, expect, timeout=3.0):
    """轮询读取 HEX 框直到等于期望值（界面刷新有轻微延迟，读一次可能拿到旧值）"""
    end = time.time() + timeout
    last = None
    while time.time() < end:
        last = read_hex(picker)
        if last is not None and last.upper() == expect:
            return last
        time.sleep(0.2)
    return last


def center_of(h):
    l, t, r, b = rect_of(h)
    return (l + r) // 2, (t + b) // 2


def click_child(h):
    x, y = center_of(h)
    click(x, y)


def grab(hwnd, name):
    r = rect_of(hwnd) if hwnd else None
    img = ImageGrab.grab(bbox=r)
    p = os.path.join(OUT, name)
    img.save(p)
    return p


def sample(x, y):
    img = ImageGrab.grab(bbox=(int(x), int(y), int(x) + 1, int(y) + 1))
    return img.getpixel((0, 0))[:3]


def topmost(hwnd):
    user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)


def kill_app():
    subprocess.run("taskkill /F /IM SnipMagic.exe", shell=True,
                   capture_output=True)


def finish(code):
    kill_app()
    with open(os.path.join(OUT, "result.json"), "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=2)
    all_ok = all(st["ok"] for st in R["steps"])
    print("RESULT=" + ("PASS" if all_ok else "FAIL"), flush=True)
    sys.exit(code)


# ---------------- 流程 ----------------

was_running = "SnipMagic.exe" in (subprocess.run(
    "tasklist", shell=True, capture_output=True,
    encoding="gbk", errors="ignore").stdout or "")
if was_running:
    log("检测到旧实例正在运行，先结束（旧构建，正在替换为新构建）", True)
kill_app()
time.sleep(1.2)

proc = subprocess.Popen([os.path.join(DIST, "SnipMagic.exe")], cwd=DIST)
main = wait_window("ScreenshotToolMainWindow", 12)
if not main:
    log("主窗口未出现", False)
    grab(None, "00-desktop.png")
    finish(1)
time.sleep(1.0)
topmost(main)
time.sleep(0.4)

dpi = user32.GetDpiForWindow(main) or 96
s = dpi / 96.0
ox, oy = client_origin(main)
log("主窗口就绪 DPI=%d 客户区原点=(%d,%d)" % (dpi, ox, oy), True)
grab(main, "01-main.png")


def lbtn_center(idx):
    # 左侧工具栏第 idx 个自绘按钮（0 起）的客户区中心
    pad, topH, leftW, lh, lgap = 6 * s, 48 * s, 56 * s, 36 * s, 3 * s
    return (ox + pad + (leftW - 2 * pad) / 2,
            oy + topH + pad + idx * (lh + lgap) + lh / 2)


def capture_btn_center():
    # 顶部第一个按钮「截图」（宽 56 逻辑像素）
    pad, topH = 6 * s, 48 * s
    return (ox + pad + 28 * s, oy + (topH - 30 * s) / 2 + 15 * s)


# --- 步骤1：截图一次，生成画布文档 ---
x, y = capture_btn_center()
click(x, y)
time.sleep(1.8)
drag(ox + 300, oy + 260, ox + 1100, oy + 820)
time.sleep(2.2)
topmost(main)
grab(main, "02-after-capture.png")
log("已完成一次截图，画布文档应已生成", True)

# --- 步骤2：工具栏「颜色」取色器 ---
x, y = lbtn_center(14)  # 左侧第 15 个 = 颜色
click(x, y)
pk = wait_window("ScreenshotToolColorPicker", 6)
if not pk:
    log("工具栏取色器未弹出（点击坐标可能不对）", False)
    grab(None, "02b-miss.png")
    finish(1)
bm = btn_map(pk)
names = sorted(bm.keys())
log("工具栏取色器按钮: %s" % names, True)
okA = ("白色" in bm and "确定" in bm and "取消" in bm
       and "红色" not in bm and "黑色" not in bm)
log("断言A 工具栏取色器快捷钮只有「白色」", okA)
grab(pk, "03-toolbar-picker.png")
hex0 = read_hex(pk)
log("工具栏取色器初始 HEX=%s" % hex0, hex0 is not None)

click_child(bm["白色"][0])
hex1 = wait_hex(pk, "#FFFFFF")
log("断言B 点「白色」后 HEX=%s" % hex1, (hex1 or "").upper() == "#FFFFFF")
cw, ch = client_size(pk)
pox, poy = client_origin(pk)
pc = sample(pox + cw - 48 + 14, poy + 24 + 14)
log("断言C 预览色块 RGB=%s" % (pc,), pc == (255, 255, 255))
grab(pk, "04-toolbar-picker-white.png")

click_child(bm["确定"][0])
time.sleep(0.5)

# 复开确认工具栏颜色真的变成白色
click(x, y)
pk2 = wait_window("ScreenshotToolColorPicker", 6)
hex2 = wait_hex(pk2, "#FFFFFF") if pk2 else None
log("断言D 工具栏颜色已保存为白 HEX=%s" % hex2,
    pk2 is not None and (hex2 or "").upper() == "#FFFFFF")
grab(pk2, "05-toolbar-picker-reopen.png")
if pk2:
    click_child(btn_map(pk2)["取消"][0])
time.sleep(0.4)

# --- 步骤3：文字工具 + 画布点击 → 插入文字弹窗 ---
x, y = lbtn_center(3)  # 左侧第 4 个 = 文字
click(x, y)
time.sleep(0.4)
cw, ch = client_size(main)
click(ox + cw * 0.55, oy + ch * 0.55)
td = wait_window("ScreenshotToolTextDlg", 6)
if not td:
    log("插入文字弹窗未出现", False)
    grab(None, "05b-miss.png")
    finish(1)
log("弹窗标题=%s" % _text(td), _text(td) == "插入文字")
grab(td, "06-textdlg.png")
tbm = btn_map(td)
set_btns = sorted(tbm.get("设置", []), key=lambda h: rect_of(h)[0])
log("断言E 弹窗内有两个「设置」按钮", len(set_btns) == 2)

# --- 步骤4：文字颜色取色器（决定性解耦证据：工具栏已是白色，这里应是红色）---
click_child(set_btns[0])
tp = wait_window("ScreenshotToolColorPicker", 6)
if not tp:
    log("文字颜色取色器未弹出", False)
    finish(1)
tbmp = btn_map(tp)
okF = all(k in tbmp for k in ("红色", "黑色", "白色", "确定", "取消"))
log("断言F 文字取色器含「红色」「黑色」「白色」快捷钮", okF)
thex = read_hex(tp)
log("断言G 文字颜色默认=%s（应为红色系，不受工具栏白色影响）" % thex,
    is_reddish(thex))
grab(tp, "07-textcolor-picker.png")

click_child(tbmp["黑色"][0])
h_b = wait_hex(tp, "#000000")
log("断言H 点「黑色」后 HEX=%s" % h_b, (h_b or "").upper() == "#000000")
grab(tp, "08-textcolor-black.png")

click_child(tbmp["红色"][0])
h_r = wait_hex(tp, "#FF0000")
log("断言I 点「红色」后 HEX=%s" % h_r, (h_r or "").upper() == "#FF0000")
click_child(tbmp["确定"][0])
time.sleep(0.4)

# --- 步骤5：背景颜色取色器（先取消勾选「背景透明」，让背景颜色行显示出来）---
tbm2 = btn_map(td)
trans = tbm2.get("背景透明", [])
if trans:
    click_child(trans[0])
    time.sleep(0.4)
tbm2 = btn_map(td)
set_btns2 = sorted(tbm2.get("设置", []), key=lambda h: rect_of(h)[0])
bg_visible = len(set_btns2) == 2 and user32.IsWindowVisible(set_btns2[1])
log("勾掉「背景透明」后背景颜色按钮可见", bool(bg_visible))
if not bg_visible:
    grab(td, "09a-no-bgbtn.png")
    finish(1)
click_child(set_btns2[1])
bp = wait_window("ScreenshotToolColorPicker", 6)
if not bp:
    log("背景颜色取色器未弹出", False)
    grab(None, "09b-miss.png")
    finish(1)
bhex = wait_hex(bp, "#FFFFFF")
log("断言J 背景颜色默认 HEX=%s（应 #FFFFFF）" % bhex,
    (bhex or "").upper() == "#FFFFFF")
bbmp = btn_map(bp)
okK = all(k in bbmp for k in ("红色", "黑色", "白色", "确定", "取消"))
log("断言K 背景取色器含「红色」「黑色」「白色」快捷钮", okK)
grab(bp, "09-bgcolor-picker.png")
click_child(bbmp["黑色"][0])
kb = wait_hex(bp, "#000000")
log("断言L 背景取色器点「黑色」后 HEX=%s" % kb, (kb or "").upper() == "#000000")
bbm = btn_map(bp)
click_child(bbm["取消"][0])
time.sleep(0.3)

# 关闭文字弹窗（取消）
tbm3 = btn_map(td)
click_child(tbm3["取消"][0])
time.sleep(0.3)

finish(0 if all(st["ok"] for st in R["steps"]) else 1)
