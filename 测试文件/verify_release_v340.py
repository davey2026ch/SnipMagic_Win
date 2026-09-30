# -*- coding: utf-8 -*-
"""独立复核 v3.4.0 双平台 release（匿名，只读，不改动）"""
import hashlib
import json
import os
import re
import sys
import urllib.request

DIST = r"F:\Projects\截图工具\dist"
EXE_LOCAL = os.path.join(DIST, "截图大师SnipMagic.exe")
ZIP_LOCAL = r"F:\Projects\截图工具\截图大师SnipMagic-便携版-20261001.zip"
TAG = "v3.4.0"
# 平台自动附带的源码归档：v3.4.0.zip / v3.4.0.tar.gz（Gitee）或 tag.zip/tar.gz（GitHub）
SRC_ARCHIVE = re.compile(r"^v?\d+\.\d+\.\d+(\.\d+)?\.(zip|tar\.gz)$")

exe_sha = hashlib.sha256(open(EXE_LOCAL, "rb").read()).hexdigest()
zip_sha = hashlib.sha256(open(ZIP_LOCAL, "rb").read()).hexdigest()
local = {"截图大师SnipMagic.exe": exe_sha,
         "截图大师SnipMagic-便携版-20261001.zip": zip_sha}

ok_all = True


def fetch(url):
    with urllib.request.urlopen(url, timeout=180) as r:
        return r.read()


# ---- Gitee ----
data = fetch("https://gitee.com/api/v5/repos/mrpu2020/SnipMagic_Win/releases/latest")
rel = json.loads(data.decode("utf-8"))
print("Gitee tag:", rel.get("tag_name"), "title:", rel.get("name"))
ok_all &= rel.get("tag_name") == TAG
for a in rel.get("assets") or []:
    n = a.get("name", "")
    if SRC_ARCHIVE.match(n):
        print("  (跳过平台源码归档 %s)" % n)
        continue
    got = hashlib.sha256(fetch(a["browser_download_url"])).hexdigest()
    match = local.get(n) == got
    print("  附件 %s  哈希一致=%s" % (n, match))
    ok_all &= match

# ---- GitHub ----
req = urllib.request.Request(
    "https://api.github.com/repos/davey2026ch/SnipMagic_Win/releases/latest",
    headers={"Accept": "application/vnd.github+json"})
with urllib.request.urlopen(req, timeout=180) as r:
    rel = json.loads(r.read().decode("utf-8"))
print("GitHub tag:", rel.get("tag_name"), "title:", rel.get("name"))
ok_all &= rel.get("tag_name") == TAG
expect = {"SnipMagic.exe": ("截图大师SnipMagic.exe", exe_sha),
          "SnipMagic-20261001.zip": ("截图大师SnipMagic-便携版-20261001.zip", zip_sha)}
for a in rel.get("assets") or []:
    n, label, digest = a.get("name", ""), a.get("label", ""), a.get("digest", "")
    if n not in expect:
        print("  (非预期附件 %s)" % n)
        ok_all = False
        continue
    want_label, want_sha = expect[n]
    ok = (label == want_label) and (digest == "sha256:" + want_sha)
    print("  附件 %s (label=%s) label与digest一致=%s" % (n, label, ok))
    ok_all &= ok

print("RESULT=" + ("PASS" if ok_all else "FAIL"))
sys.exit(0 if ok_all else 1)
