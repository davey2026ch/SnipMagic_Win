# -*- coding: utf-8 -*-
# 两段式验证：direct 路径 + fallback 路径（含进程退出后 cmd 接管的端到端检查）
import os
import subprocess
import sys
import time

ROOT = r"F:\Projects\截图工具"
EXE = os.path.join(ROOT, "test_upswap", "build", "Release", "test_upswap.exe")
WORK = os.path.join(ROOT, "test_upswap", "work")
os.makedirs(WORK, exist_ok=True)

fails = []


def rd(p):
    try:
        with open(p, "rb") as f:
            return f.read()
    except OSError:
        return None


# ---- 用例 1：direct（文件无锁，两次改名直接成功）----
cur = os.path.join(WORK, "cur.exe")
new = os.path.join(WORK, "new.exe")
old = cur + ".old"
for p in (cur, new, old):
    if os.path.exists(p):
        os.remove(p)
open(cur, "wb").write(b"OLD-CONTENT")
open(new, "wb").write(b"NEW-CONTENT")
r = subprocess.run([EXE, "direct", cur, new], capture_output=True, text=True)
print("direct:", r.stdout.strip(), "rc=", r.returncode)
if rd(cur) == b"NEW-CONTENT" and rd(old) == b"OLD-CONTENT" and not os.path.exists(new):
    print("PASS: direct replaced, backup kept, staging consumed")
else:
    print("FAIL: direct state wrong", rd(cur), rd(old))
    fails.append("direct")

# ---- 用例 2：fallback（锁住 cur → 改名失败 → cmd 退出后接管）----
cur2 = os.path.join(WORK, "cur2.exe")
new2 = os.path.join(WORK, "new2.exe")
for p in (cur2, new2):
    if os.path.exists(p):
        os.remove(p)
open(cur2, "wb").write(b"OLD-2")
open(new2, "wb").write(b"NEW-2")
r = subprocess.run([EXE, "fallback", cur2, new2], capture_output=True, text=True)
print("fallback:", r.stdout.strip(), "rc=", r.returncode)
if r.returncode != 0:
    print("FAIL: fallback returned nonzero")
    fails.append("fallback")

# 兜底是本进程退出 3 秒后由 cmd 完成 → 等待后校验
deadline = time.time() + 15
ok = False
while time.time() < deadline:
    time.sleep(1)
    if rd(cur2) == b"NEW-2":
        ok = True
        break
print("PASS: fallback replaced after exit" if ok else "FAIL: fallback not replaced")
if not ok:
    fails.append("fallback-e2e")
    print("cur2 =", rd(cur2))

print("\n%s" % ("ALL PASS" if not fails else "FAILED: " + ",".join(fails)))
sys.exit(0 if not fails else 1)
