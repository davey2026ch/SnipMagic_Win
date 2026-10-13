# -*- coding: utf-8 -*-
"""
真机验证（2026-10-13）三项界面改动
A) 「截图 / 长截图」悬浮显示快捷键气泡；其它顶部按钮不弹；移开后消失
B) 取色器：吸管文字按钮 -> 吸管图标按钮（自绘，无文字）；新增「最近使用」4x2 宫格，
   点选一键复用；点「确定」后写入 ini（RecentColors），重启后仍在（含顺序）
C) 文字弹窗：初始客户区 2 倍（1120x680）；可拖动缩放（WS_THICKFRAME）；
   缩放后控件随窗口重排且不越界
产物：本目录 ui_verify_20261013/ 下截图 + result.json
用法：<system python311> verify_ui_20261013.py
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import sys
import time

from PIL import Image, ImageGrab

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

user32 = ctypes.windll.user32
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
HWND_TOPMOST = wt.HWND(-1)
SWP_NOMOVE = 0x0002
SWP_NOSIZE = 0x0001
SWP_NOZORDER = 0x0004
SWP_NOACTIVATE = 0x0010

ROOT = r"F:\Projects\截图工具"
DIST = os.path.join(ROOT, "dist")
INI = os.path.join(DIST, "SnipMagic.ini")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ui_verify_20261013")
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


def find_windows(cls_name, visible_only=True):
    res = []

    @WNDENUMPROC
    def cb(h, l):
        if (not visible_only or user32.IsWindowVisible(h)) and _cls(h) == cls_name:
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


def wait_gone(cls_name, timeout=6):
    end = time.time() + timeout
    while time.time() < end:
        if not find_windows(cls_name):
            return True
        time.sleep(0.15)
    return False


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


def ctrl_rect_client(hwnd, ctrl):
    r = wt.RECT()
    user32.GetWindowRect(ctrl, ctypes.byref(r))
    ox, oy = client_origin(hwnd)
    return (r.left - ox, r.top - oy, r.right - ox, r.bottom - oy)


def click(x, y, settle=0.25):
    user32.SetCursorPos(int(x), int(y))
    time.sleep(0.1)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.06)
    user32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(settle)


def hover(x, y):
    user32.SetCursorPos(int(x), int(y))
    time.sleep(0.1)


def drag(x1, y1, x2, y2):
    user32.SetCursorPos(int(x1), int(y1))
    time.sleep(0.15)
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


def find_child(dlg, cls_name, ctrl_id=None):
    for h in children(dlg):
        if _cls(h) == cls_name and (ctrl_id is None or
                                    user32.GetDlgCtrlID(h) == ctrl_id):
            return h
    return None


def find_child_by_text(dlg, text, cls_name="Static"):
    for h in children(dlg):
        if _cls(h) == cls_name and _text(h) == text:
            return h
    return None


def read_hex(picker):
    # 跨进程读 Edit 内容必须用 WM_GETTEXT（GetWindowTextW 会返回过期缓存）
    h = find_child(picker, "Edit", 1002)
    if not h:
        return None
    buf = ctypes.create_unicode_buffer(64)
    user32.SendMessageW(h, 0x000D, 63, buf)  # WM_GETTEXT
    return buf.value


def wait_hex(picker, expect, timeout=3.0):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        last = read_hex(picker)
        if last is not None and last.upper() == expect.upper():
            return last
        time.sleep(0.15)
    return last


def center_of_rect(r):
    return (r[0] + r[2]) // 2, (r[1] + r[3]) // 2


def center_screen(h):
    return center_of_rect(rect_of(h))


def grab(hwnd, name):
    r = rect_of(hwnd) if hwnd else None
    img = ImageGrab.grab(bbox=r)
    p = os.path.join(OUT, name)
    img.save(p)
    return p


def grab_region(box, name):
    img = ImageGrab.grab(bbox=box)
    p = os.path.join(OUT, name)
    img.save(p)
    return p


def sample(x, y):
    img = ImageGrab.grab(bbox=(int(x), int(y), int(x) + 1, int(y) + 1))
    return img.getpixel((0, 0))[:3]


def rgb2hex(t):
    return "#%02X%02X%02X" % t


def topmost(hwnd):
    user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE)


def bring_to_front(hwnd):
    """宿主程序（WorkBuddy 等）偶尔抢到置顶，点击前重新把被测窗口顶上来"""
    topmost(hwnd)
    user32.BringWindowToTop(hwnd)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.35)


def click_until(wait_cls, x, y, tries=3, timeout=4.0, settle=0.6):
    """点按钮直到目标窗口出现；点空/被别的窗口挡住时自动重点一次"""
    for i in range(tries):
        bring_to_front(main)
        click(x, y, settle=settle)
        w = wait_window(wait_cls, timeout)
        if w:
            if i:
                print("  （第 %d 次点击才生效）" % (i + 1), flush=True)
            return w
    return None


def kill_app():
    subprocess.run("taskkill /F /IM SnipMagic.exe", shell=True, capture_output=True)


def finish(code):
    kill_app()
    time.sleep(0.8)
    restore_ini()   # 还原用户真实配置
    with open(os.path.join(OUT, "result.json"), "w", encoding="utf-8") as f:
        json.dump(R, f, ensure_ascii=False, indent=2)
    all_ok = all(st["ok"] for st in R["steps"])
    print("RESULT=" + ("PASS" if all_ok else "FAIL"), flush=True)
    sys.exit(code)


# ---------------- ini 预置：最近使用颜色 ----------------
# 宫格 24 格 = 前 3 格固定（红/黑/白）+ 21 格历史
SEED = ["#%02X%02X%02X" % ((i * 11 + 5) % 256, (i * 47 + 9) % 256, (i * 97 + 3) % 256)
        for i in range(21)]
assert len(set(SEED)) == 21, "预置颜色必须互不相同"
FIXED = ["#FF0000", "#000000", "#FFFFFF"]
assert not (set(SEED) & set(FIXED)), "预置颜色不能撞上固定色"
# 预置 ini 时故意把 3 个固定色也写进历史（24 条），程序读入后应把固定色剔除、只留 21 条真实历史
INI_SEED_RAW = ",".join(FIXED + SEED)
INI_AFTER_CLEAN = ",".join(SEED)
# 用固定白格子确认一次：固定色不记入历史 → ini 不变
EXPECT_AFTER_WHITE = INI_AFTER_CLEAN
# 再用 HEX 框输入一个非固定色确认：最新在前、挤掉最旧一条
CUSTOM = "#123456"
EXPECT_AFTER_CUSTOM = CUSTOM + "," + ",".join(SEED[:20])


ORIG_INI = None


def backup_ini():
    """备份用户真实配置，测试结束后原样还原"""
    global ORIG_INI
    with open(INI, "rb") as f:
        ORIG_INI = f.read()


def restore_ini():
    if ORIG_INI is not None:
        with open(INI, "wb") as f:
            f.write(ORIG_INI)


def seed_ini():
    """统一行尾（避免把 LF 文件写成两段 [Settings]）后写入预置的最近使用颜色"""
    with open(INI, "rb") as f:
        raw = f.read().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    lines = [ln for ln in raw.split(b"\n") if ln.strip()]
    lines = [ln for ln in lines if not ln.startswith(b"RecentColors=")]
    out = []
    inserted = False
    for ln in lines:
        out.append(ln)
        if not inserted and ln.strip() == b"[Settings]":
            out.append(("RecentColors=" + INI_SEED_RAW).encode("ascii"))
            inserted = True
    if not inserted:
        out.insert(0, b"[Settings]")
        out.insert(1, ("RecentColors=" + INI_SEED_RAW).encode("ascii"))
    with open(INI, "wb") as f:
        f.write(b"\r\n".join(out) + b"\r\n")


def read_ini_recent():
    with open(INI, "rb") as f:
        for ln in f.read().splitlines():
            if ln.startswith(b"RecentColors="):
                return ln.split(b"=", 1)[1].decode("ascii").strip()
    return None


def ini_sections():
    with open(INI, "rb") as f:
        raw = f.read().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return [ln.strip() for ln in raw.split(b"\n") if ln.strip().startswith(b"[")]


kill_app()
time.sleep(1.0)
backup_ini()
seed_ini()
log("已预置 ini 最近使用颜色（含 3 个固定色，共 24 条）: %s" % INI_SEED_RAW, True)

proc = subprocess.Popen([os.path.join(DIST, "SnipMagic.exe")], cwd=DIST)
main = wait_window("ScreenshotToolMainWindow", 12)
if not main:
    log("主窗口未出现", False)
    grab(None, "00-desktop.png")
    finish(1)
time.sleep(1.2)
topmost(main)
time.sleep(0.4)

dpi = user32.GetDpiForWindow(main) or 96
s = dpi / 96.0
ox, oy = client_origin(main)
log("主窗口就绪 DPI=%d 客户区原点=(%d,%d)" % (dpi, ox, oy), True)
grab(main, "01-main.png")

# 启动读配置时应把混进历史的固定色（红/黑/白）剔除
cleaned = read_ini_recent()
log("B0a 启动后 ini 历史色已剔除固定色: %s" % cleaned,
    (cleaned or "").upper() == INI_AFTER_CLEAN.upper())
n_fixed_in_ini = sum(1 for c in (cleaned or "").split(",") if c.upper() in FIXED)
log("B0b ini 历史色里不含红/黑/白（重复项已清掉）", n_fixed_in_ini == 0)


def top_btn_center(idx):
    """顶部第 idx 个按钮中心（宽：截图56 / 长截图66 / 马赛克66，图标+文字自适应）"""
    pad, topH, gap = 6 * s, 48 * s, 6 * s
    widths = [56, 66, 66]
    x = pad
    for i in range(idx):
        x += widths[i] * s + gap
    return (ox + x + widths[idx] * s / 2, oy + (topH - 30 * s) / 2 + 15 * s)


def lbtn_center(idx):
    pad, topH, leftW, lh, lgap = 6 * s, 48 * s, 56 * s, 36 * s, 3 * s
    return (ox + pad + (leftW - 2 * pad) / 2,
            oy + topH + pad + idx * (lh + lgap) + lh / 2)


# 取色器宫格：8 列 x 3 行，格 32 间距 8（客户区坐标）
GRID_X, GRID_Y, CELL, GAP = 258, 46, 32, 8
GRID_COLS = 8


def grid_center(i):
    col, row = i % GRID_COLS, i // GRID_COLS
    return (GRID_X + col * (CELL + GAP) + CELL // 2,
            GRID_Y + row * (CELL + GAP) + CELL // 2)


# ================= A. 悬浮快捷键气泡 =================
tooltip_w = {}

x, y = top_btn_center(0)  # 截图
hover(x, y)
tt = wait_window("ScreenshotToolTooltip", 3)
okA1 = tt is not None
log("A1 悬浮「截图」出现气泡", okA1)
if tt:
    r = rect_of(tt)
    tooltip_w["capture"] = r[2] - r[0]
    # 把气泡窗口一起截进来（含按钮区域）
    grab_region((int(x) - 80, int(y) - 30, r[2] + 30, r[3] + 20),
                "02-tip-capture.png")
else:
    grab(None, "02-tip-capture.png")

x2, y2 = top_btn_center(1)  # 长截图
hover(x2, y2)
# 换按钮后气泡会先隐藏再重新弹出（文字更宽），轮询等到宽度变化为止
tt2 = None
w2 = None
end = time.time() + 3.5
while time.time() < end:
    ws = find_windows("ScreenshotToolTooltip")
    if ws:
        rr = rect_of(ws[-1])
        if (rr[2] - rr[0]) != tooltip_w.get("capture"):
            tt2 = ws[-1]
            w2 = rr[2] - rr[0]
            break
    time.sleep(0.1)
okA2 = tt2 is not None
if tt2:
    r2 = rect_of(tt2)
    tooltip_w["long"] = w2
    grab_region((int(x2) - 80, int(y2) - 30, r2[2] + 30, r2[3] + 20),
                "03-tip-long.png")
log("A2 悬浮「长截图」出现气泡（宽度 %s，截图按钮为 %s）"
    % (w2, tooltip_w.get("capture")), okA2)
log("A3 气泡宽度 截图=%s 长截图=%s（长截图文字更长，应更宽）"
    % (tooltip_w.get("capture"), tooltip_w.get("long")),
    okA2 and tooltip_w.get("long", 0) > tooltip_w.get("capture", 0))

x3, y3 = top_btn_center(2)  # 马赛克（无提示）
hover(x3, y3)
time.sleep(1.0)
okA4 = len(find_windows("ScreenshotToolTooltip")) == 0
log("A4 悬浮「马赛克」不弹气泡", okA4)

# 移到非按钮区域 → 气泡消失
cw, ch = client_size(main)
hover(ox + cw * 0.5, oy + ch * 0.6)
time.sleep(0.6)
okA5 = len(find_windows("ScreenshotToolTooltip")) == 0
log("A5 鼠标移到空白处气泡消失", okA5)
grab(main, "04-main-after-hover.png")


def check_eye_icon(eye_btn, tag):
    """截图吸管按钮并检查：图标确实画出来了、且居中、没被窗口边缘裁掉"""
    l, t, r, b = rect_of(eye_btn)
    p = grab_region((l, t, r, b), "eye-button-%s.png" % tag)
    img = Image.open(p).convert("L")
    px = img.load()
    xs, ys = [], []
    for yy in range(img.height):
        for xx in range(img.width):
            if px[xx, yy] < 120:
                xs.append(xx)
                ys.append(yy)
    if not xs:
        log("B[%s] 吸管图标按钮内没有任何图标像素" % tag, False)
        return
    cx = (min(xs) + max(xs)) / 2.0
    cy = (min(ys) + max(ys)) / 2.0
    n = len(xs)
    ok = (n >= 100 and abs(cx - img.width / 2.0) <= 5
          and abs(cy - img.height / 2.0) <= 5)
    log("B[%s] 吸管图标：像素数=%d 中心=(%.1f,%.1f) 按钮内区=(%dx%d) 中心应≈(%.1f,%.1f)"
        % (tag, n, cx, cy, img.width, img.height,
           img.width / 2.0, img.height / 2.0), ok)


def check_picker_ui(pk, tag):
    """取色器公共断言：尺寸 / 吸管图标按钮（无文字）/ 底部只有确定取消"""
    w, h = client_size(pk)
    log("B[%s] 取色器客户区 %dx%d" % (tag, w, h), 590 <= w <= 615 and 590 <= h <= 615)
    bm = btn_map(pk)
    eye = [h2 for h2 in children(pk)
           if _cls(h2) == "Button" and user32.GetDlgCtrlID(h2) == 1003]
    no_text = bool(eye) and _text(eye[0]) == ""
    has_eye_text = any(t.startswith("吸管") for t in bm.keys())
    log("B[%s] 吸管按钮为图标（存在 id=1003 且无文字的按钮，且没有「吸管…」文字按钮）"
        % tag, no_text and not has_eye_text)
    names = sorted(t for t, hs in bm.items() if t)
    log("B[%s] 按钮只有 确定/取消（红/黑/白快捷按钮已删除）: %s" % (tag, names),
        names == ["取消", "确定"])
    if eye:
        # 按钮完整落在客户区内（此前 600 宽窗口下曾越界被裁）
        r = ctrl_rect_client(pk, eye[0])
        inside = r[0] >= 0 and r[1] >= 0 and r[2] <= w and r[3] <= h
        log("B[%s] 吸管按钮在客户区内 %s" % (tag, r), inside)
        check_eye_icon(eye[0], tag)
    return eye[0] if eye else None


# ================= B. 取色器 =================
x, y = lbtn_center(14)  # 左侧第 15 个 = 颜色
pk = click_until("ScreenshotToolColorPicker", x, y)
if not pk:
    log("B0 工具栏取色器未弹出", False)
    grab(main, "05-miss.png")
    finish(1)
time.sleep(0.4)
grab(pk, "05-picker-toolbar-seed.png")

eye_btn = check_picker_ui(pk, "工具栏")

# 前 3 格是固定色：红 / 黑 / 白（原快捷按钮改到这里，不可被历史色替换）
gx, gy = grid_center(0)
pox, poy = client_origin(pk)
click(pox + gx, poy + gy)
h1 = wait_hex(pk, "#FF0000")
log("B1 宫格第 1 格（固定红）→ HEX=%s" % h1, (h1 or "").upper() == "#FF0000")
grab(pk, "06-picker-grid1.png")

gx, gy = grid_center(1)
click(pox + gx, poy + gy)
h1b = wait_hex(pk, "#000000")
log("B1b 宫格第 2 格（固定黑）→ HEX=%s" % h1b, (h1b or "").upper() == "#000000")

gx, gy = grid_center(2)
click(pox + gx, poy + gy)
h2 = wait_hex(pk, "#FFFFFF")
log("B1c 宫格第 3 格（固定白）→ HEX=%s" % h2, (h2 or "").upper() == "#FFFFFF")

# 第 4 格起是「最近使用」历史色：第 4 格 = 最新的一个预置色
gx, gy = grid_center(3)
click(pox + gx, poy + gy)
h2b = wait_hex(pk, SEED[0])
log("B2 宫格第 4 格（历史第 1 个）→ HEX=%s（期望 %s）" % (h2b, SEED[0]),
    (h2b or "").upper() == SEED[0].upper())

gx, gy = grid_center(9)
click(pox + gx, poy + gy)
h2c = wait_hex(pk, SEED[6])
log("B2b 宫格第 10 格（历史第 7 个）→ HEX=%s（期望 %s）" % (h2c, SEED[6]),
    (h2c or "").upper() == SEED[6].upper())

gx, gy = grid_center(23)
click(pox + gx, poy + gy)
h2d = wait_hex(pk, SEED[20])
log("B2c 宫格最后一格（历史第 21 个）→ HEX=%s（期望 %s）" % (h2d, SEED[20]),
    (h2d or "").upper() == SEED[20].upper())

# 吸管图标按钮：全屏取色仍可用。
# 取色点选在主窗口画布空白处（纯色 230,230,230，屏幕内容不会自己变），
# 期望值直接从冻屏截图里读 —— 桌面上有别的程序窗口，取桌面像素不稳定。
PX, PY = ox + 200, oy + 200
if eye_btn:
    ex, ey = center_screen(eye_btn)
    click(ex, ey, settle=1.2)
    eye_win = wait_window("ScreenshotToolEyedropper", 5)
    log("B3 点吸管图标 → 全屏取色覆盖窗出现", eye_win is not None)
    if eye_win:
        shot = grab(eye_win, "07-eyedropper.png")
        el, et, _, _ = rect_of(eye_win)
        frozen = Image.open(shot).convert("RGB")
        pick_rgb = frozen.getpixel((PX - el, PY - et))   # 冻屏图即当时的屏幕内容
        click(PX, PY, settle=1.0)
        wait_gone("ScreenshotToolEyedropper", 5)
        time.sleep(0.5)
        h3 = wait_hex(pk, rgb2hex(pick_rgb), 3)
        log("B4 全屏取色结果 HEX=%s，冻屏图该点像素=%s"
            % (h3, rgb2hex(pick_rgb)),
            (h3 or "").upper() == rgb2hex(pick_rgb).upper())

# 点固定白格后确定 → 写入 ini（最新在前、去重、上限 21）
gx, gy = grid_center(2)
click(pox + gx, poy + gy)
hw = wait_hex(pk, "#FFFFFF")
log("B5 点固定白格 → HEX=%s" % hw, (hw or "").upper() == "#FFFFFF")
grab(pk, "08-picker-white.png")
click(center_of_rect(rect_of(btn_map(pk)["确定"][0]))[0],
      center_of_rect(rect_of(btn_map(pk)["确定"][0]))[1])
time.sleep(0.6)
recent1 = read_ini_recent()
log("B6 用固定白确认一次后 ini 未新增记录（%s）" % recent1,
    (recent1 or "").upper() == EXPECT_AFTER_WHITE.upper())
secs = ini_sections()
log("B6b ini 只有一段 [Settings]（未被写成重复段）: %s" % secs,
    secs == [b"[Settings]"])
with open(INI, "rb") as _f:
    _raw = _f.read()
_tok = [ln for ln in _raw.replace(b"\r\n", b"\n").split(b"\n")
        if ln.startswith(b"MinerUToken=")]
log("B6c 写盘后未破坏既有配置（MinerUToken 非空）",
    bool(_tok) and len(_tok[-1].split(b"=", 1)[1]) > 10)

# 非固定色照旧入库：用 HEX 框输入 #123456 后确定 → 最新在前、挤掉最旧一条
pk2b = click_until("ScreenshotToolColorPicker", x, y)
if pk2b:
    hx = find_child(pk2b, "Edit", 1002)
    buf2 = ctypes.create_unicode_buffer(CUSTOM)
    user32.SendMessageW(hx, 0x000C, 0, ctypes.cast(buf2, ctypes.c_void_p))  # WM_SETTEXT
    time.sleep(0.3)
    click(*center_of_rect(rect_of(btn_map(pk2b)["确定"][0])))
    time.sleep(0.6)
    recent2 = read_ini_recent()
    log("B6d 用 HEX 输入 %s 确认后 ini=%s" % (CUSTOM, recent2),
        (recent2 or "").upper() == EXPECT_AFTER_CUSTOM.upper())
    log("B6e 历史里依然没有红/黑/白",
        not any(c.upper() in FIXED for c in (recent2 or "").split(",")))
else:
    log("B6d 取色器未重新弹出", False)

# ================= B2. 重开程序验证 ini 回读 =================
kill_app()
time.sleep(1.5)
proc = subprocess.Popen([os.path.join(DIST, "SnipMagic.exe")], cwd=DIST)
main = wait_window("ScreenshotToolMainWindow", 12)
if not main:
    log("B7 重启后主窗口未出现", False)
    finish(1)
time.sleep(1.2)
topmost(main)
ox, oy = client_origin(main)
dpi = user32.GetDpiForWindow(main) or 96
s = dpi / 96.0
time.sleep(0.3)

x, y = lbtn_center(14)
pk2 = click_until("ScreenshotToolColorPicker", x, y)
if not pk2:
    log("B7 重启后取色器未弹出", False)
    finish(1)
time.sleep(0.4)
grab(pk2, "09-picker-after-restart.png")
pox, poy = client_origin(pk2)
# 重启后前 3 格仍是固定红/黑/白（写入过历史色也不会被替换）
for idx, (name, want) in enumerate([("固定红", "#FF0000"), ("固定黑", "#000000"),
                                    ("固定白", "#FFFFFF")]):
    gx, gy = grid_center(idx)
    click(pox + gx, poy + gy)
    hh = wait_hex(pk2, want)
    log("B7 重启后宫格第 %d 格仍是%s（HEX=%s）→ 固定色不被替换"
        % (idx + 1, name, hh), (hh or "").upper() == want)
# 第 4 格起为历史色，顺序应等于 ini 里的顺序（#123456 最新在最前）
ORDER = EXPECT_AFTER_CUSTOM.split(",")
gx, gy = grid_center(3)
click(pox + gx, poy + gy)
h4 = wait_hex(pk2, ORDER[0])
log("B7b 重启后宫格第 4 格=%s（HEX=%s）→ ini 回读成功" % (ORDER[0], h4),
    (h4 or "").upper() == ORDER[0].upper())
gx, gy = grid_center(4)
click(pox + gx, poy + gy)
h5 = wait_hex(pk2, ORDER[1])
log("B8 重启后宫格第 5 格=%s（HEX=%s）→ 顺序正确" % (ORDER[1], h5),
    (h5 or "").upper() == ORDER[1].upper())
gx, gy = grid_center(7)
click(pox + gx, poy + gy)
h5b = wait_hex(pk2, ORDER[4])
log("B8b 重启后宫格第 8 格（历史第 5 个）=%s（HEX=%s）→ 顺序正确" % (ORDER[4], h5b),
    (h5b or "").upper() == ORDER[4].upper())
# 最后一格（第 24 格）= 历史第 21 个：验证宫格确实有 24 格且坐标对得上
gx, gy = grid_center(23)
click(pox + gx, poy + gy)
h5c = wait_hex(pk2, ORDER[20])
log("B8c 点宫格最后一格（历史第 21 个）=%s（HEX=%s）→ 24 格可点" % (ORDER[20], h5c),
    (h5c or "").upper() == ORDER[20].upper())
# 第 24 格右侧之外（客户区里但那一片是空白）不应改变颜色
gx, gy = grid_center(23)
click(pox + gx + CELL, poy + gy, settle=0.5)
h5d = read_hex(pk2)
log("B8d 宫格右侧空白处点击不影响颜色（HEX=%s）" % h5d,
    (h5d or "").upper() == ORDER[20].upper())
grab(pk2, "10-picker-grid-order.png")
bm2 = btn_map(pk2)
click(*center_of_rect(rect_of(bm2["取消"][0])))
time.sleep(0.4)

# ================= C. 文字弹窗 =================
# 先截一张图生成文档（文字工具需要画布文档）
x, y = top_btn_center(0)
hover(ox + cw * 0.5, oy + ch * 0.8)  # 先移开，避免气泡
time.sleep(0.5)
click(x, y, settle=1.8)
drag(ox + 320, oy + 280, ox + 1020, oy + 720)
time.sleep(2.2)
topmost(main)
grab(main, "11-after-capture.png")

x, y = lbtn_center(3)  # 左侧第 4 个 = 文字
click(x, y, settle=0.4)
cw, ch = client_size(main)
click(ox + cw * 0.55, oy + ch * 0.55)
td = wait_window("ScreenshotToolTextDlg", 6)
if not td:
    log("C0 文字弹窗未出现", False)
    grab(None, "12-miss.png")
    finish(1)
time.sleep(0.5)
grab(td, "12-textdlg-default.png")

tw, th = client_size(td)
log("C1 文字弹窗初始客户区 %dx%d（期望 840x510，即原 560x340 的 1.5 倍）" % (tw, th),
    abs(tw - 840) <= 6 and abs(th - 510) <= 6)

style = user32.GetWindowLongW(td, -16)  # GWL_STYLE
log("C2 可拖动缩放（WS_THICKFRAME 0x40000 已置位）", bool(style & 0x00040000))

edit = find_child(td, "Edit", 2001)
if not edit:
    log("C3 未找到内容输入框", False)
    finish(1)
er = ctrl_rect_client(td, edit)
log("C3 输入框 %dx%d（放大后应远大于原来的 504x150）"
    % (er[2] - er[0], er[3] - er[1]),
    (er[2] - er[0]) > 700 and (er[3] - er[1]) > 300)

# —— 文字弹窗里的两个「设置」按钮：复用同一取色器（吸管图标 + 宫格）——
tbm = btn_map(td)
set_btns = sorted(tbm.get("设置", []), key=lambda h: rect_of(h)[0])
log("C4 弹窗内有两个「设置」按钮（文字颜色 / 背景颜色）", len(set_btns) == 2)
if set_btns:
    click(*center_of_rect(rect_of(set_btns[0])))
    tp = wait_window("ScreenshotToolColorPicker", 6)
    if tp:
        time.sleep(0.4)
        grab(tp, "13-textcolor-picker.png")
        check_picker_ui(tp, "文字颜色")
        pox, poy = client_origin(tp)
        gx, gy = grid_center(0)
        click(pox + gx, poy + gy)
        h6 = wait_hex(tp, "#FF0000")
        log("C5 文字颜色取色器第 1 格=固定红（HEX=%s）" % h6,
            (h6 or "").upper() == "#FF0000")
        gx, gy = grid_center(2)
        click(pox + gx, poy + gy)
        h6b = wait_hex(tp, "#FFFFFF")
        log("C5b 文字颜色取色器第 3 格=固定白（HEX=%s）" % h6b,
            (h6b or "").upper() == "#FFFFFF")
        click(*center_of_rect(rect_of(btn_map(tp)["取消"][0])))
        time.sleep(0.4)
    else:
        log("C5 文字颜色取色器未弹出", False)

tbm = btn_map(td)
trans = tbm.get("背景透明", [])
if trans:
    click(*center_of_rect(rect_of(trans[0])))
    time.sleep(0.4)
tbm = btn_map(td)
set_btns2 = sorted(tbm.get("设置", []), key=lambda h: rect_of(h)[0])
if len(set_btns2) == 2 and user32.IsWindowVisible(set_btns2[1]):
    click(*center_of_rect(rect_of(set_btns2[1])))
    bp = wait_window("ScreenshotToolColorPicker", 6)
    if bp:
        time.sleep(0.4)
        grab(bp, "14-bgcolor-picker.png")
        check_picker_ui(bp, "背景颜色")
        click(*center_of_rect(rect_of(btn_map(bp)["取消"][0])))
        time.sleep(0.3)
    else:
        log("C6 背景颜色取色器未弹出", False)
else:
    log("C6 勾掉「背景透明」后背景颜色按钮不可见", False)

# —— 底部只有一行：字号 / 加粗 / 背景透明 / 文字颜色+设置 / 背景颜色+设置 ——
row_ctrls = [
    ("字号框", find_child(td, "Edit", 2002)),
    ("加粗", find_child(td, "Button", 2003)),
    ("背景透明", find_child(td, "Button", 2004)),
    ("文字颜色标签", find_child_by_text(td, "文字颜色")),
    ("文字颜色设置", find_child(td, "Button", 2005)),
    ("背景颜色标签", find_child_by_text(td, "背景颜色")),
    ("背景颜色设置", find_child(td, "Button", 2008)),
]
rects = [(n, ctrl_rect_client(td, h)) for n, h in row_ctrls if h]
centers = [(r[1] + r[3]) / 2.0 for _, r in rects]
log("C6b 底部 7 个控件都在同一行（%d/%d 个找到，纵向中心差=%.1fpx）"
    % (len(rects), len(row_ctrls), (max(centers) - min(centers)) if centers else -1),
    len(rects) == len(row_ctrls) and (max(centers) - min(centers)) <= 8)
ok_btn0 = find_child_by_text(td, "确定", "Button")
if rects and ok_btn0:
    orc = ctrl_rect_client(td, ok_btn0)
    row_bottom = max(r[3] for _, r in rects)
    row_right = max(r[2] for _, r in rects)
    log("C6c 这一行在「确定」上方且不越界（行底=%d 确定顶=%d 行右=%d 客户宽=%d）"
        % (row_bottom, orc[1], row_right, tw),
        row_bottom <= orc[1] and row_right <= tw - 18)

# —— 缩放：拖动边框（等价 SetWindowPos）后控件应重排且不越界 ——
r0 = rect_of(td)
DW = (r0[2] - r0[0]) - tw   # 边框 + 标题栏占用的宽度
DH = (r0[3] - r0[1]) - th
nx, ny = r0[0] + 30, r0[1] + 30
user32.SetWindowPos(td, 0, nx, ny, 780 + DW, 480 + DH,
                    SWP_NOZORDER | SWP_NOACTIVATE)
time.sleep(0.6)
tw2, th2 = client_size(td)
er2 = ctrl_rect_client(td, edit)
log("C7 缩小窗口 → 客户区 %dx%d，输入框 %dx%d"
    % (tw2, th2, er2[2] - er2[0], er2[3] - er2[1]),
    (er2[2] - er2[0]) < (er[2] - er[0]) and
    abs((er2[2] - er2[0]) - (tw2 - 40)) <= 4)

ok_btn = [h for h in children(td) if _cls(h) == "Button" and _text(h) == "确定"]
cancel_btn = [h for h in children(td) if _cls(h) == "Button" and _text(h) == "取消"]


def all_inside(w, hh):
    bad = []
    for c in children(td):
        r = ctrl_rect_client(td, c)
        if r[0] < 0 or r[1] < 0 or r[2] > w or r[3] > hh:
            bad.append("%s(%s)%s" % (_cls(c), _text(c), r))
    return bad


bad = all_inside(tw2, th2)
for b in bad:
    log("   越界控件: " + b, False)
log("C8 缩放后所有子控件都在客户区内", not bad)
if ok_btn and cancel_btn:
    orr = ctrl_rect_client(td, ok_btn[0])
    crr = ctrl_rect_client(td, cancel_btn[0])
    log("C9 确定/取消按钮贴右下角 确定.right=%d 取消.right=%d 客户宽=%d"
        % (orr[2], crr[2], tw2),
        crr[2] <= tw2 - 18 and crr[2] >= tw2 - 30 and orr[2] < crr[0])
grab(td, "15-textdlg-resized.png")

# 缩到允许的最小尺寸（客户区 700x400）：一行控件仍应放得下、不重叠
user32.SetWindowPos(td, 0, nx, ny, 700 + DW, 400 + DH,
                    SWP_NOZORDER | SWP_NOACTIVATE)
time.sleep(0.6)
tw3, th3 = client_size(td)
bad3 = all_inside(tw3, th3)
for b in bad3:
    log("   越界控件: " + b, False)
rects3 = [(n, ctrl_rect_client(td, h)) for n, h in row_ctrls if h]
row_right3 = max(r[2] for _, r in rects3)
row_bottom3 = max(r[3] for _, r in rects3)
orc3 = ctrl_rect_client(td, ok_btn[0]) if ok_btn else (0, 9999, 0, 9999)
log("C11 缩到最小客户区 %dx%d：控件不越界、行右=%d≤%d、行底=%d≤确定顶=%d"
    % (tw3, th3, row_right3, tw3 - 18, row_bottom3, orc3[1]),
    not bad3 and row_right3 <= tw3 - 18 and row_bottom3 <= orc3[1])
grab(td, "17-textdlg-min.png")
# 恢复到可用的舒适尺寸，继续后面的输入测试
user32.SetWindowPos(td, 0, nx, ny, 780 + DW, 480 + DH,
                    SWP_NOZORDER | SWP_NOACTIVATE)
time.sleep(0.4)

# 长文本输入后确定，弹窗应正常关闭
long_text = ("这是一段用于验证长文本输入的文字。" * 12)
buf = ctypes.create_unicode_buffer(long_text)
user32.SendMessageW(edit, 0x000C, 0, ctypes.cast(buf, ctypes.c_void_p))  # WM_SETTEXT
time.sleep(0.3)
btn_ok = btn_map(td).get("确定", [])
if btn_ok:
    click(*center_of_rect(rect_of(btn_ok[0])), settle=0.8)
gone = wait_gone("ScreenshotToolTextDlg", 4)
log("C10 输入长文本后「确定」→ 弹窗关闭", gone)
time.sleep(0.6)
topmost(main)
grab(main, "16-text-inserted.png")

finish(0 if all(st["ok"] for st in R["steps"]) else 1)
