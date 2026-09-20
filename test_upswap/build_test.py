# -*- coding: utf-8 -*-
import os
import subprocess
import sys

ROOT = r"F:\Projects\截图工具"
CMAKE = (r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
         r"\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
LOG = os.path.join(ROOT, "test_upswap", "build_test.log")

env = {}
seen = set()
for k, v in os.environ.items():
    ku = k.upper()
    if ku in seen:
        continue
    seen.add(ku)
    env[ku] = v
env.pop('INCLUDE', None)
env.pop('LIB', None)


def run(args, label):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write("\n== %s ==\n" % label)
        p = subprocess.run(args, cwd=ROOT, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = p.stdout.decode("utf-8", errors="replace")
        f.write(out)
        f.write("\nEXIT=%d\n" % p.returncode)
    return p.returncode


rc = run([CMAKE, "-S", os.path.join(ROOT, "test_upswap"),
          "-B", os.path.join(ROOT, "test_upswap", "build"),
          "-G", "Visual Studio 17 2022", "-A", "x64"], "configure")
if rc == 0:
    rc = run([CMAKE, "--build", os.path.join(ROOT, "test_upswap", "build"),
              "--config", "Release"], "build")
print("RC=%d" % rc)
sys.exit(rc)
