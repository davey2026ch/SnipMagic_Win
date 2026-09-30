# -*- coding: utf-8 -*-
"""v3.4.0 附件重传（用户 2026-10-01 最终规格，再次变更）：
release 只保留 exe 附件（自动更新器依赖它），不传 zip。
Gitee 上传中文名 截图大师SnipMagic.exe；GitHub name=SnipMagic.exe + label=中文。
令牌走环境变量 GITEE_TOKEN / GH_TOKEN，不落盘。"""
import hashlib
import json
import os
import sys
import time
import urllib.request

REPO_GITEE = "mrpu2020/SnipMagic_Win"
REPO_GH = "davey2026ch/SnipMagic_Win"
API_GITEE = "https://gitee.com/api/v5/repos/" + REPO_GITEE
API_GH = "https://api.github.com/repos/" + REPO_GH
UP_GH = "https://uploads.github.com/repos/" + REPO_GH

GITEE_RID = 1177600
GH_RID = 400280281
EXE_LOCAL = r"F:\Projects\截图工具\dist\截图大师SnipMagic.exe"
EXE_GITEE_NAME = "截图大师SnipMagic.exe"
EXE_GH_NAME = "SnipMagic.exe"
EXE_GH_LABEL = "截图大师SnipMagic.exe"

GITEE_TOKEN = os.environ.get("GITEE_TOKEN", "")
GH_TOKEN = os.environ.get("GH_TOKEN", "")
if not GITEE_TOKEN or not GH_TOKEN:
    print("NO TOKEN")
    sys.exit(1)

# ---- 前置校验：exe 存在、内嵌版本串 3.4.0（发版铁律）----
content = open(EXE_LOCAL, "rb").read()
exe_sha = hashlib.sha256(content).hexdigest()
has_new = "3.4.0".encode("utf-16-le") in content
has_old = "3.3.0".encode("utf-16-le") in content
print("EXE=%s size=%d" % (EXE_LOCAL, len(content)), flush=True)
print("SHA256=%s" % exe_sha, flush=True)
print("embedded 3.4.0=%s embedded 3.3.0=%s" % (has_new, has_old), flush=True)
if not (has_new and not has_old):
    print("RESULT=FAIL (version string mismatch, refuse to upload)")
    sys.exit(1)

results = []


def log(msg, ok=True):
    results.append((msg, ok))
    print(("OK  " if ok else "FAIL") + " " + msg, flush=True)


def http(url, data=None, headers=None, method="GET", retries=2):
    last = None
    for _ in range(retries + 1):
        try:
            req = urllib.request.Request(url, data=data,
                                         headers=headers or {}, method=method)
            with urllib.request.urlopen(req, timeout=180) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()
        except Exception as e:
            last = e
            time.sleep(2)
    raise last


def multipart_gitee(filename, content):
    boundary = "----snipmagic" + os.urandom(8).hex()
    part = (("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
             "Content-Type: application/octet-stream\r\n\r\n" % (boundary, filename))
            ).encode("utf-8") + content + ("\r\n--%s--\r\n" % boundary).encode("utf-8")
    return part, "multipart/form-data; boundary=" + boundary


# ============ Gitee：列附件 → 只删 zip → 传 exe ============
st, body = http("%s/releases/%d/attach_files?access_token=%s"
                % (API_GITEE, GITEE_RID, GITEE_TOKEN))
files = json.loads(body.decode("utf-8"))
log("Gitee 现有附件: %s" % [f.get("name") for f in files],
    isinstance(files, list))
for f in files:
    st, _ = http("%s/releases/%d/attach_files/%s?access_token=%s"
                 % (API_GITEE, GITEE_RID, f["id"], GITEE_TOKEN), method="DELETE")
    log("Gitee 删除附件 %s: HTTP %d" % (f.get("name"), st), st in (200, 204))
    time.sleep(0.5)

part, ctype = multipart_gitee(EXE_GITEE_NAME, content)
st, body = http("%s/releases/%d/attach_files" % (API_GITEE, GITEE_RID),
                data=part, method="POST",
                headers={"Content-Type": ctype,
                         "Authorization": "token " + GITEE_TOKEN})
log("Gitee 上传 %s: HTTP %d" % (EXE_GITEE_NAME, st), st in (200, 201))
if st not in (200, 201):
    print(body.decode("utf-8", "replace")[:400])

# ============ GitHub：列资产 → 只删 zip → 传 exe → 补中文标签 ============
st, body = http("%s/releases/%d" % (API_GH, GH_RID),
                headers={"Authorization": "Bearer " + GH_TOKEN})
rel = json.loads(body.decode("utf-8"))
assets = rel.get("assets") or []
log("GitHub 现有资产: %s" % [a.get("name") for a in assets], isinstance(assets, list))
for a in assets:
    st, _ = http("%s/releases/assets/%d" % (API_GH, a["id"]),
                 headers={"Authorization": "Bearer " + GH_TOKEN}, method="DELETE")
    log("GitHub 删除资产 %s: HTTP %d" % (a.get("name"), st), st == 204)
    time.sleep(0.5)

url = "%s/releases/%d/assets?name=%s" % (UP_GH, GH_RID, EXE_GH_NAME)
st, body = http(url, data=content, method="POST",
                headers={"Authorization": "Bearer " + GH_TOKEN,
                         "Content-Type": "application/octet-stream"})
log("GitHub 上传 %s: HTTP %d" % (EXE_GH_NAME, st), st in (200, 201))
if st not in (200, 201):
    print(body.decode("utf-8", "replace")[:400])
else:
    aid = json.loads(body.decode("utf-8"))["id"]
    st, body = http("%s/releases/assets/%d" % (API_GH, aid),
                    data=json.dumps({"name": EXE_GH_NAME,
                                     "label": EXE_GH_LABEL}).encode("utf-8"),
                    method="PATCH",
                    headers={"Content-Type": "application/json",
                             "Authorization": "Bearer " + GH_TOKEN})
    log("GitHub 补中文标签: HTTP %d" % st, st == 200)

# ============ 匿名复核（两平台都应只剩 exe 一个真附件）============
import re
SRC_ARCHIVE = re.compile(r"^v?\d+\.\d+\.\d+(\.\d+)?\.(zip|tar\.gz)$")

time.sleep(2)
data = urllib.request.urlopen(
    "https://gitee.com/api/v5/repos/" + REPO_GITEE + "/releases/latest",
    timeout=180).read()
rel = json.loads(data.decode("utf-8"))
real = [a for a in (rel.get("assets") or [])
        if not SRC_ARCHIVE.match(a.get("name", ""))]
log("Gitee 真附件只剩 exe: %s" % [a.get("name") for a in real],
    [a.get("name") for a in real] == [EXE_GITEE_NAME])
for a in real:
    with urllib.request.urlopen(a["browser_download_url"], timeout=180) as r:
        got = hashlib.sha256(r.read()).hexdigest()
    log("Gitee 附件哈希一致=%s" % (got == exe_sha), got == exe_sha)

req = urllib.request.Request(
    "https://api.github.com/repos/" + REPO_GH + "/releases/latest",
    headers={"Accept": "application/vnd.github+json"})
with urllib.request.urlopen(req, timeout=180) as r:
    rel = json.loads(r.read().decode("utf-8"))
assets = rel.get("assets") or []
log("GitHub 资产只剩 exe: %s" % [a.get("name") for a in assets],
    [a.get("name") for a in assets] == [EXE_GH_NAME])
for a in assets:
    ok = (a.get("label") == EXE_GH_LABEL
          and a.get("digest") == "sha256:" + exe_sha)
    log("GitHub label/digest 一致=%s" % ok, ok)

all_ok = all(ok for _, ok in results)
print("RESULT=" + ("PASS" if all_ok else "FAIL"))
sys.exit(0 if all_ok else 1)
