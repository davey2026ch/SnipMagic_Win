# -*- coding: utf-8 -*-
"""冒烟 v2：临时结束用户旧实例 → 启动 dist 新 exe 验标题 3.4.0 → 关闭 → 把用户原来的程序拉回来"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import time

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass
user32 = ctypes.windll.user32
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

USER_APP = r"E:\装机必备\003 常用办公软件\截图大师SnipMagic\截图大师SnipMagic.exe"


def _cls(h):
    b = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(h, b, 256)
    return b.value


def _text(h):
    n = user32.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 2)
    user32.GetWindowTextW(h, b, n + 2)
    return b.value


def main_title():
    res = []

    @WNDENUMPROC
    def cb(h, l):
        if user32.IsWindowVisible(h) and _cls(h) == "ScreenshotToolMainWindow":
            res.append(_text(h))
        return True

    user32.EnumWindows(cb, 0)
    return res[0] if res else None


def kill_all():
    for exe in ("SnipMagic.exe", "截图大师SnipMagic.exe"):
        subprocess.run("taskkill /F /IM %s" % exe, shell=True, capture_output=True)


# 1) 记录用户旧实例标题（应为 3.3.0），然后结束
old_title = main_title()
kill_all()
time.sleep(1.2)

# 2) 启动 dist 新版验证标题
subprocess.Popen([r"F:\Projects\截图工具\dist\SnipMagic.exe"],
                 cwd=r"F:\Projects\截图工具\dist")
title = None
end = time.time() + 12
while time.time() < end and not title:
    title = main_title()
    if not title:
        time.sleep(0.3)
new_title = title
kill_all()
time.sleep(1.0)

# 3) 把用户原来的程序拉回前台状态
subprocess.Popen([USER_APP])
time.sleep(1.5)
restored = main_title()

print("旧实例标题=%r" % old_title)
print("新版标题=%r" % new_title)
print("用户程序已恢复=%r" % restored)
ok = bool(new_title) and "3.4.0" in new_title
print("RESULT=" + ("OK" if ok else "FAIL"))
