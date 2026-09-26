# -*- coding: utf-8 -*-
# Build & run the CF_HTML + settings persistence test with cl.exe.
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
TD = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(TD, "test_clip_build.log")


def find_toolchain():
    import glob
    msvc = sorted(glob.glob(
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
        r"\VC\Tools\MSVC\*"))[-1]
    sdk_inc = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\Include\*"))[-1]
    sdk_libs = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\Lib\*"))[-1]
    include = [os.path.join(msvc, "include"),
               os.path.join(sdk_inc, "ucrt"), os.path.join(sdk_inc, "um"),
               os.path.join(sdk_inc, "shared"), os.path.join(sdk_inc, "cppwinrt")]
    lib = [os.path.join(msvc, "lib", "x64"),
           os.path.join(sdk_libs, "ucrt", "x64"), os.path.join(sdk_libs, "um", "x64")]
    return include, lib, os.path.join(msvc, "bin", "Hostx64", "x64")


def main():
    include, lib, bindir = find_toolchain()
    env = {}
    seen = set()
    for k, v in os.environ.items():
        ku = k.upper()
        if ku in seen:
            continue
        seen.add(ku)
        env[ku] = v
    env.pop("INCLUDE", None)
    env.pop("LIB", None)
    env["INCLUDE"] = os.pathsep.join(include)
    env["LIB"] = os.pathsep.join(lib)
    env["PATH"] = bindir + os.pathsep + env.get("PATH", "")

    exe = os.path.join(TD, "test_clip_settings.exe")
    obj = os.path.join(TD, "test_clip_settings.obj")
    log = LOG
    for f in (exe, obj, log):
        if os.path.exists(f):
            os.remove(f)

    cmd = [os.path.join(bindir, "cl.exe"), "/nologo", "/utf-8", "/EHsc", "/O2",
           "/Fe:" + exe, "/I" + SRC, os.path.join(TD, "test_clip_settings.cpp"),
           os.path.join(SRC, "settings.cpp"), os.path.join(SRC, "document.cpp"),
           os.path.join(SRC, "annotation.cpp"),
           "/link", "/MACHINE:X64", "/SUBSYSTEM:CONSOLE",
           "user32.lib", "gdi32.lib", "gdiplus.lib", "shell32.lib", "ole32.lib",
           "advapi32.lib"]
    with open(log, "a", encoding="utf-8") as f:
        p = subprocess.run(cmd, cwd=TD, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        f.write(p.stdout.decode("utf-8", errors="replace"))
        f.write("\nCL EXIT=%d\n" % p.returncode)
        if p.returncode != 0 or not os.path.exists(exe):
            print("COMPILE FAILED, see log")
            return 1
        p2 = subprocess.run([exe], cwd=TD, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out2 = p2.stdout.decode("utf-8", errors="replace")
        f.write(out2)
        print(out2)
    return 0 if "RESULT: PASS" in out2 else 1


import subprocess
if __name__ == "__main__":
    sys.exit(main())
