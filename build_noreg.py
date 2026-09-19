# -*- coding: utf-8 -*-
# Build via CMake Visual Studio generator with a deduplicated environment.
# Reason: this agent's shell environment contains duplicate Path/PATH entries,
# which makes MSBuild fail with MSB6001 (System.ArgumentException on env dict).
# reg.exe is security-policy-blocked so vcvars64.bat cannot be used; the VS
# generator sets up its own toolchain env inside msbuild.
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
CMAKE = (r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
         r"\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
LOG = os.path.join(ROOT, "build_noreg.log")

# 1) Clean environment: dedupe case-insensitively, keep first occurrence.
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

build_dir = os.path.join(ROOT, "build")
shutil.rmtree(build_dir, ignore_errors=True)


def run(args, label):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write("\n== %s ==\n" % label)
        p = subprocess.run(args, cwd=ROOT, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = p.stdout.decode("utf-8", errors="replace")
        f.write(out)
        f.write("\nEXIT=%d\n" % p.returncode)
    return p.returncode


def main():
    rc = run([CMAKE, "-S", ROOT, "-B", build_dir,
              "-G", "Visual Studio 17 2022", "-A", "x64"], "configure")
    if rc != 0:
        print("CONFIGURE FAILED")
        return 1
    rc = run([CMAKE, "--build", build_dir, "--config", "Release"], "build")
    if rc != 0:
        print("BUILD FAILED")
        return 1
    print("BUILD OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
