# -*- coding: utf-8 -*-
"""v3.4.0 发版打包：校验 exe 内嵌版本串 → 打绿色便携版 zip → 哈希三件套核对"""
import hashlib
import os
import zipfile

ROOT = r"F:\Projects\截图工具"
DIST = os.path.join(ROOT, "dist")
ZIP_PATH = os.path.join(ROOT, "截图大师SnipMagic-便携版-20261001.zip")
ok = True


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# 1) exe 内嵌版本串必须是 3.4.0，且标题里不再出现 3.3.0
for name in ("SnipMagic.exe", "截图大师SnipMagic.exe"):
    data = open(os.path.join(DIST, name), "rb").read()
    has_new = "3.4.0".encode("utf-16-le") in data
    title_new = ("截图大师SnipMagic 3.4.0").encode("utf-16-le") in data
    has_old = "3.3.0".encode("utf-16-le") in data
    print("版本串 %s: 3.4.0=%s 标题=%s 残留3.3.0=%s" % (name, has_new, title_new, has_old))
    if not (has_new and title_new and not has_old):
        ok = False

# 2) 干净模板 ini（纯 ASCII，绝不含令牌）
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

# 3) 打 zip（Python zipfile，中文名条目规范；顶层文件夹，解压即用）
folder = "截图大师SnipMagic"
entries = [
    (os.path.join(DIST, "SnipMagic.exe"), folder + "/SnipMagic.exe"),
    (os.path.join(DIST, "截图大师SnipMagic.exe"), folder + "/截图大师SnipMagic.exe"),
    (None, folder + "/SnipMagic.ini"),  # 内容来自 CLEAN_INI
]
if os.path.exists(ZIP_PATH):
    os.remove(ZIP_PATH)
with zipfile.ZipFile(ZIP_PATH, "w", zipfile.ZIP_DEFLATED) as z:
    for src, arc in entries:
        if src:
            z.write(src, arc)
        else:
            z.writestr(arc, CLEAN_INI.encode("ascii"))

# 4) 复核 zip：条目名、CRC、解压比对哈希、ini 无密钥
z = zipfile.ZipFile(ZIP_PATH)
print("---- zip 条目 ----")
for i in z.infolist():
    print("  %s  %d" % (i.filename, i.file_size))
bad = z.testzip()
print("CRC 校验:", "OK" if bad is None else "BAD:%s" % bad)
if bad is not None:
    ok = False

ini_text = z.read(folder + "/SnipMagic.ini").decode("ascii")
leak = ("MinerUToken" in ini_text or "VolcApiKey" in ini_text)
print("zip 内 ini 含密钥:", leak)
if leak:
    ok = False

import tempfile
tmp = tempfile.mkdtemp()
hashes = {}
for src, arc in entries[:2]:
    z.extract(arc, tmp)
    extracted = os.path.join(tmp, arc)
    hashes[arc] = sha256(extracted)
hashes["dist/SnipMagic.exe"] = sha256(os.path.join(DIST, "SnipMagic.exe"))
hashes["dist/中文名"] = sha256(os.path.join(DIST, "截图大师SnipMagic.exe"))

same_en = hashes[folder + "/SnipMagic.exe"] == hashes["dist/SnipMagic.exe"]
same_cn = hashes[folder + "/截图大师SnipMagic.exe"] == hashes["dist/中文名"]
print("zip 内 exe == dist 英文名:", same_en)
print("zip 内 exe == dist 中文名:", same_cn)
if not (same_en and same_cn):
    ok = False

print("zip 大小: %.2f MB" % (os.path.getsize(ZIP_PATH) / 1048576.0))
print("zip sha256:", sha256(ZIP_PATH))
print("RESULT=" + ("OK" if ok else "FAIL"))
