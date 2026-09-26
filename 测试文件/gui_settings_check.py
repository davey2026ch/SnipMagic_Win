# -*- coding: utf-8 -*-
# GUI 端到端验证（单脚本进程内完成，避免沙箱回收进程树）：
#  1. 启动 dist/SnipMagic.exe
#  2. PostMessage WM_COMMAND(102) 打开设置对话框
#  3. 截图核对新增「带边框复制」行渲染
#  4. 程序化点击开关（IDC 3016），截图/采样核对开关状态切换
#  5. 切回开启态，点确定，读 ini 确认 BorderCopyToExternal=1 已写入
#  6. 退出测试实例
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import sys
import time

from PIL import ImageGrab

user32 = ctypes.windll.user32
dwmapi = ctypes.windll.dwmapi
gdi32 = ctypes.windll.gdi32

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

OUT = {
    "steps": [],
    "ok": False,
}


def log(msg):
    OUT["steps"].append(msg)
    print(msg)


def find_window_by_class(cls):
    result = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(hwnd, lparam):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, buf, 256)
        if buf.value == cls:
            result.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return result[0] if result else None


def window_rect(hwnd):
    rect = wt.RECT()
    if dwmapi.DwmGetWindowAttribute(hwnd, 9, ctypes.byref(rect), ctypes.sizeof(rect)) != 0:
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
    return (rect.left, rect.top, rect.right, rect.bottom)


def shot(hwnd, path):
    l, t, r, b = window_rect(hwnd)
    time.sleep(0.6)
    img = ImageGrab.grab(bbox=(l, t, r, b))
    img.save(path)
    return path, (r - l, b - t)


def main():
    root = r"F:\Projects\截图工具"
    td = os.path.join(root, "测试文件")
    exe = os.path.join(root, "dist", "SnipMagic.exe")
    ini = os.path.join(exe and os.path.dirname(exe), "SnipMagic.ini")

    # 0) 确认没有旧实例（单实例互斥，占用会误测旧程序）
    out = subprocess.run(["tasklist"], capture_output=True).stdout.decode(
        "gbk", errors="ignore").lower()
    if "snipmagic" in out:
        log("SKIP: SnipMagic 已在运行，避免干扰用户实例，终止测试")
        OUT["ok"] = False
        return 1

    # 1) 启动
    proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
    log("启动 exe pid=%d" % proc.pid)
    hwnd = None
    for _ in range(50):
        time.sleep(0.2)
        hwnd = find_window_by_class("ScreenshotToolMainWindow")
        if hwnd:
            break
    if not hwnd:
        log("FAIL: 主窗口未出现")
        return 1
    log("主窗口 hwnd=%#x" % hwnd)
    user32.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0003)  # TOPMOST, NOMOVE|NOSIZE
    time.sleep(0.5)

    # 2) 打开设置
    user32.PostMessageW(hwnd, 0x0111, 102, 0)  # WM_COMMAND, ID_CMD_SETTINGS=102
    dlg = None
    for _ in range(50):
        time.sleep(0.2)
        dlg = find_window_by_class("ScreenshotToolSettingsDlg")
        if dlg:
            break
    if not dlg:
        log("FAIL: 设置对话框未打开")
        proc.terminate()
        return 1
    log("设置对话框 hwnd=%#x" % dlg)
    p1, size1 = shot(dlg, os.path.join(td, "settings_dialog_default.png"))
    log("对话框截图 %s 尺寸=%s" % (p1, size1))

    # 3) 找开关控件 IDC 3016
    sw = user32.GetDlgItem(dlg, 3016)
    if not sw:
        log("FAIL: 未找到带边框复制开关(IDC 3016)")
        proc.terminate()
        return 1
    r = wt.RECT()
    user32.GetWindowRect(sw, ctypes.byref(r))
    log("开关 rect=(%d,%d,%d,%d)" % (r.left, r.top, r.right, r.bottom))

    def track_color():
        img = ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom))
        w, h = img.size
        px = img.load()
        # 采样开关轨道中部（避开两侧滑块），取中间行多个点的平均
        samples = [px[w // 2, h // 6], px[w // 2, h // 2], px[w // 2, h - h // 6]]
        avg = tuple(sum(c[i] for c in samples) // len(samples) for i in range(3))
        return avg

    c0 = track_color()
    log("初始开关轨道平均色=%s (蓝=开启/灰=关闭)" % (c0,))
    is_on0 = c0[2] > c0[0] + 40  # 蓝色明显占优 → 开

    # 4) 点击一次 → 应变为关闭
    user32.SendMessageW(sw, 0x00F5, 0, 0)  # BM_CLICK
    time.sleep(0.3)
    c1 = track_color()
    log("点击后轨道平均色=%s" % (c1,))
    is_on1 = c1[2] > c1[0] + 40
    toggled = (is_on0 == True) and (is_on1 == False)
    log("状态切换 开->关: %s" % ("OK" if toggled else "FAIL"))
    p2, _ = shot(dlg, os.path.join(td, "settings_dialog_off.png"))

    # 5) 再点一次回到开启，点确定
    user32.SendMessageW(sw, 0x00F5, 0, 0)
    time.sleep(0.3)
    c2 = track_color()
    is_on2 = c2[2] > c2[0] + 40
    log("再次点击后回到开: %s (色=%s)" % ("OK" if is_on2 else "FAIL", c2))

    okbtn = user32.GetDlgItem(dlg, 3007)
    user32.SendMessageW(okbtn, 0x00F5, 0, 0)
    time.sleep(0.8)
    dlg_gone = not find_window_by_class("ScreenshotToolSettingsDlg")
    log("点确定后对话框关闭: %s" % ("OK" if dlg_gone else "FAIL"))

    # 6) 读 ini 确认持久化
    with open(ini, "rb") as f:
        data = f.read()
    try:
        text = data.decode("utf-16")
    except Exception:
        text = data.decode("gbk", errors="replace")
    has_key = "BorderCopyToExternal=1" in text
    log("ini 含 BorderCopyToExternal=1: %s" % ("OK" if has_key else "FAIL"))

    # 7) 退出测试实例
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except Exception:
        proc.kill()
    log("测试实例已退出")

    OUT["ok"] = toggled and is_on2 and dlg_gone and has_key
    return 0 if OUT["ok"] else 1


if __name__ == "__main__":
    rc = main()
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "gui_settings_check_result.json"), "w", encoding="utf-8") as f:
        json.dump(OUT, f, ensure_ascii=False, indent=2)
    sys.exit(rc)
