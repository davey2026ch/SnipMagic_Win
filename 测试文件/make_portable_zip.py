# -*- coding: utf-8 -*-
"""打绿色便携版 zip（v3.4.0 起，用户 2026-10-01 指定的固定规格）：

★ 附件规格（用户明确要求，务必遵守）：
1. release 只上传便携版 zip，不上传 截图大师SnipMagic.exe；
2. zip 内只放 截图大师SnipMagic.exe（中文名）+ SnipMagic.ini（干净模板），
   不放 SnipMagic.exe（英文名，重复）。

用法：python make_portable_zip.py [YYYYMMDD]  （默认今天日期）
"""
import hashlib
import os
import sys
import time
import zipfile

ROOT = r"F:\Projects\截图工具"
DIST = os.path.join(ROOT, "dist")

# 干净模板 ini（纯 ASCII，绝不含令牌；与 package.ps1 默认模板一致）
CLEAN_INI = "\r\n".join([
    "[Settings]",
    "Hotkey=Ctrl+Shift+R",
    "HotkeyModifiers=6",
    "HotkeyVk=82",
    "LongHotkey=Ctrl+Shift+E",
    "LongHotkeyModifiers=6",
    "LongHotkeyVk=69",
    "Theme=0",
    "MosaicSize=10",
    "LineThickness=4",
    "BrushThickness=25",
    "ThemeColor=#0078D4",
    "DrawColor=#FF0000",
    "DrawAlpha=255",
]) + "\r\n"
assert "Token" not in CLEAN_INI and "ApiKey" not in CLEAN_INI


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build():
    date = sys.argv[1] if len(sys.argv) > 1 else time.strftime("%Y%m%d")
    zip_path = os.path.join(ROOT, "截图大师SnipMagic-便携版-%s.zip" % date)
    folder = "截图大师SnipMagic"
    entries = [
        (os.path.join(DIST, "截图大师SnipMagic.exe"), folder + "/截图大师SnipMagic.exe"),
        (None, folder + "/SnipMagic.ini"),  # 内容来自 CLEAN_INI
    ]
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for src, arc in entries:
            if src:
                z.write(src, arc)
            else:
                z.writestr(arc, CLEAN_INI.encode("ascii"))

    # 复核：条目清单（必须恰好两项）、CRC、解压哈希与 dist 一致、无密钥
    z = zipfile.ZipFile(zip_path)
    names = [i.filename for i in z.infolist()]
    expect = [folder + "/截图大师SnipMagic.exe", folder + "/SnipMagic.ini"]
    ok = names == expect
    print("条目 %s 清单正确=%s" % (names, ok))
    bad = z.testzip()
    print("CRC 校验:", "OK" if bad is None else "BAD:%s" % bad)
    ok &= bad is None
    ini_text = z.read(folder + "/SnipMagic.ini").decode("ascii")
    leak = ("MinerUToken" in ini_text or "VolcApiKey" in ini_text)
    print("zip 内 ini 含密钥:", leak)
    ok &= not leak
    import tempfile
    tmp = tempfile.mkdtemp()
    z.extract(folder + "/截图大师SnipMagic.exe", tmp)
    same = sha256(os.path.join(tmp, folder, "截图大师SnipMagic.exe")) == \
        sha256(os.path.join(DIST, "截图大师SnipMagic.exe"))
    print("zip 内 exe == dist 中文名 exe:", same)
    ok &= same
    print("zip 大小: %.2f MB" % (os.path.getsize(zip_path) / 1048576.0))
    print("zip sha256:", sha256(zip_path))
    print("RESULT=" + ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(build())
