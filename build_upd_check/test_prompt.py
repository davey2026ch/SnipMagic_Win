# -*- coding: utf-8 -*-
"""端到端验证：本地版本 1.2.9 < 远端 1.3.0，启动 3 秒后应自动弹出更新询问框。
找到进程弹出的对话框（#32770），置顶并截图。"""
import subprocess, time

app = subprocess.Popen([r"F:\Projects\截图工具\dist\ScreenshotTool.exe"])
time.sleep(8)  # 覆盖 3 秒检测 + 网络往返

shot = r'''
import ctypes, ctypes.wintypes as wt, time
from PIL import ImageGrab
ctypes.windll.shcore.SetProcessDpiAwareness(2)
u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi
target_pid = None
hits = []
@ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
def cb(hwnd, _):
    global target_pid
    if not u32.IsWindowVisible(hwnd):
        return True
    cls = ctypes.create_unicode_buffer(64)
    u32.GetClassNameW(hwnd, cls, 64)
    if cls.value != "#32770":
        return True
    pid = ctypes.c_ulong()
    u32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    h = k32.OpenProcess(0x0400, False, pid.value)
    if not h:
        return True
    buf = ctypes.create_unicode_buffer(300)
    psapi.GetModuleFileNameExW(h, None, buf, 300)
    k32.CloseHandle(h)
    if "ScreenshotTool" in buf.value:
        title = ctypes.create_unicode_buffer(64)
        u32.GetWindowTextW(hwnd, title, 64)
        hits.append((hwnd, title.value, pid.value))
    return True
u32.EnumWindows(cb, 0)
if hits:
    hwnd, title, pid = hits[0]
    rct = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(rct))
    u32.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002)
    time.sleep(0.6)
    ImageGrab.grab(bbox=(rct.left, rct.top, rct.right, rct.bottom)).save(
        r"F:\Projects\截图工具\build_upd_check\update_prompt.png")
    print("DIALOG FOUND:", title, "pid:", pid)
else:
    print("NO DIALOG")
'''
r2 = subprocess.run([r"C:\Users\SKY\AppData\Local\Programs\Python\Python311\python.exe", "-c", shot],
                    capture_output=True)
print(r2.stdout.decode(errors="replace").strip())
if r2.stderr:
    print("ERR:", r2.stderr.decode(errors="replace").strip()[:300])
app.terminate()
