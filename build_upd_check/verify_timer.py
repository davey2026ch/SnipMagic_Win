# -*- coding: utf-8 -*-
"""启动打包后的程序，在启动后 2~9 秒高频轮询该进程的 TCP 连接，
验证启动 3 秒后确实发起了更新检测（对 Gitee 的连接）。"""
import subprocess, time, re, os

EXE = r"F:\Projects\截图工具\截图工具-绿色版\截图工具.exe"

app = subprocess.Popen([EXE])
pid = app.pid
print("PID:", pid)

conn_re = re.compile(r"^\s*TCP\s+\S+[:\]](\d+)\s+([^\s]+):(\d+)\s+\S+\s+" + str(pid) + r"\s*$", re.I)
seen = {}
t0 = time.time()
while time.time() - t0 < 10:
    r = subprocess.run(["netstat", "-ano"], capture_output=True)
    now = time.time() - t0
    for line in r.stdout.decode("gbk", errors="replace").splitlines():
        m = conn_re.match(line)
        if m and m.group(2).lower() not in ("127.0.0.1", "0.0.0.0", "[::1]"):
            key = (m.group(2), m.group(3))
            if key not in seen:
                seen[key] = now
    time.sleep(0.12)

app.terminate()
if seen:
    print("捕获到进程的外联连接：")
    for (ip, port), t in sorted(seen.items(), key=lambda kv: kv[1]):
        print("  t=%.1fs  -> %s:%s" % (t, ip, port))
    hit = [kv for kv in seen.items() if 2.0 <= kv[1] <= 7.0]
    print("判定：", "3 秒窗口内有外联（自动检测已触发）" if hit else "外联不在预期窗口")
else:
    print("未捕获到任何外联连接")
