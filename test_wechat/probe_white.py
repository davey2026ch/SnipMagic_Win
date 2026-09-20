# -*- coding: utf-8 -*-
import ctypes
from ctypes import wintypes

user32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

pt = wintypes.POINT(2400, 1000)
hwnd = user32.WindowFromPoint(pt)
buf = ctypes.create_unicode_buffer(256)
user32.GetClassNameW(hwnd, buf, 256)
tbuf = ctypes.create_unicode_buffer(256)
user32.GetWindowTextW(hwnd, tbuf, 256)
pid = wintypes.DWORD()
user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
r = wintypes.RECT()
user32.GetWindowRect(hwnd, ctypes.byref(r))
print("at(2400,1000): hwnd=", hwnd, "cls=", buf.value, "title=", tbuf.value,
      "pid=", pid.value, "rect=", [r.left, r.top, r.right, r.bottom],
      "hung=", user32.IsHungAppWindow(hwnd))

root = user32.GetAncestor(hwnd, 2)  # GA_ROOT
user32.GetClassNameW(root, buf, 256)
user32.GetWindowTextW(root, tbuf, 256)
user32.GetWindowRect(root, ctypes.byref(r))
print("root:", "cls=", buf.value, "title=", tbuf.value,
      "rect=", [r.left, r.top, r.right, r.bottom],
      "hung=", user32.IsHungAppWindow(root))
