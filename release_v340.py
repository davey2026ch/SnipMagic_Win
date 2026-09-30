# -*- coding: utf-8 -*-
"""v3.4.0 双平台发版：Gitee + GitHub 建 release、传附件、回读复核。
令牌从环境变量 GITEE_TOKEN / GH_TOKEN 读取，绝不落盘。"""
import hashlib
import json
import os
import re
import sys
import time
import urllib.request

REPO_GITEE = "mrpu2020/SnipMagic_Win"
REPO_GH = "davey2026ch/SnipMagic_Win"
API_GITEE = "https://gitee.com/api/v5/repos/" + REPO_GITEE
API_GH = "https://api.github.com/repos/" + REPO_GH
UP_GH = "https://uploads.github.com/repos/" + REPO_GH

DIST = r"F:\Projects\截图工具\dist"
ROOT = r"F:\Projects\截图工具"
EXE_LOCAL = os.path.join(DIST, "截图大师SnipMagic.exe")
ZIP_LOCAL = os.path.join(ROOT, "截图大师SnipMagic-便携版-20261001.zip")

TAG = "v3.4.0"
TITLE = "v3.4.0 文字颜色与工具栏颜色解耦 · 取色器新增快捷色按钮"
BODY = """## v3.4.0 更新内容

### 文字颜色与工具栏颜色彻底解耦
- 左侧工具栏的颜色与「插入文字」弹窗里的文字颜色、背景颜色从此互不影响
- 插入新文字默认：红色文字 + 白色背景；编辑已有文字仍显示该文字自身的颜色

### 取色器新增快捷色按钮
- 工具栏取色器：「确定」旁新增「白色」，一键切换纯白
- 文字弹窗的两个取色器：新增「红色」「黑色」（排在白色前方），一键切换
- 快捷按钮点击即生效（同步重置为不透明），再点「确定」应用

### 其他
- 附 GUI 真机自动化验证脚本：完整走一遍 工具栏取色器 → 插入文字 → 两个取色器，12 项断言全部通过
"""

GITEE_TOKEN = os.environ.get("GITEE_TOKEN", "")
GH_TOKEN = os.environ.get("GH_TOKEN", "")
if not GITEE_TOKEN or not GH_TOKEN:
    print("NO TOKEN (GITEE_TOKEN / GH_TOKEN)")
    sys.exit(1)


def sha256b(data):
    return hashlib.sha256(data).hexdigest()


def http(url, data=None, headers=None, method="GET", retries=2):
    last = None
    for attempt in range(retries + 1):
        try:
            req = urllib.request.Request(url, data=data,
                                         headers=headers or {}, method=method)
            with urllib.request.urlopen(req, timeout=180) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()
        except Exception as e:  # 网络抖动重试
            last = e
            time.sleep(2)
    raise last


def multipart(filepath, field="file"):
    content = open(filepath, "rb").read()
    boundary = "----snipmagic" + os.urandom(8).hex()
    fn = os.path.basename(filepath)
    part = (("--%s\r\nContent-Disposition: form-data; name=\"%s\"; filename=\"%s\"\r\n"
             "Content-Type: application/octet-stream\r\n\r\n" % (boundary, field, fn))
            ).encode("utf-8") + content + ("\r\n--%s--\r\n" % boundary).encode("utf-8")
    return part, "multipart/form-data; boundary=" + boundary


results = []


def log(msg, ok=True):
    results.append((msg, ok))
    print(("OK  " if ok else "FAIL") + " " + msg, flush=True)


# ============ Gitee ============
st, body = http(API_GITEE + "/releases?access_token=" + GITEE_TOKEN + "&per_page=20")
deleted = False
for rel in json.loads(body.decode("utf-8")):
    if rel.get("tag_name") == TAG:
        http(API_GITEE + "/releases/%d?access_token=" % rel["id"] + GITEE_TOKEN,
             method="DELETE")
        deleted = True
log("Gitee 清理旧同名 release: %s" % ("删过" if deleted else "无"))

payload = json.dumps({
    "access_token": GITEE_TOKEN,
    "tag_name": TAG,
    "target_commitish": "master",
    "name": TITLE,
    "body": BODY,
    "prerelease": False,
}).encode("utf-8")
st, body = http(API_GITEE + "/releases", data=payload,
                headers={"Content-Type": "application/json"}, method="POST")
log("Gitee 建 release: HTTP %d" % st, st in (200, 201))
if st not in (200, 201):
    print(body.decode("utf-8", "replace")[:500])
    sys.exit(1)
rel = json.loads(body.decode("utf-8"))
gitee_rid = rel["id"]
print("  release id:", gitee_rid)

for f in (EXE_LOCAL, ZIP_LOCAL):
    part, ctype = multipart(f)
    st, body = http(API_GITEE + "/releases/%d/attach_files" % gitee_rid,
                    data=part, method="POST",
                    headers={"Content-Type": ctype,
                             "Authorization": "token " + GITEE_TOKEN})
    log("Gitee 上传附件 %s: HTTP %d" % (os.path.basename(f), st), st in (200, 201))
    if st not in (200, 201):
        print(body.decode("utf-8", "replace")[:400])

# ============ GitHub ============
st, body = http(API_GH + "/releases/tags/" + TAG,
                headers={"Authorization": "Bearer " + GH_TOKEN})
if st == 200:
    old = json.loads(body.decode("utf-8"))
    st2, _ = http(API_GH + "/releases/%d" % old["id"],
                  headers={"Authorization": "Bearer " + GH_TOKEN}, method="DELETE")
    log("GitHub 清理旧同名 release: HTTP %d" % st2, st2 in (204, 404))
    time.sleep(1)
else:
    log("GitHub 无同名 release (HTTP %d)" % st, st == 404)

payload = json.dumps({
    "tag_name": TAG,
    "target_commitish": "master",
    "name": TITLE,
    "body": BODY,
    "prerelease": False,
}).encode("utf-8")
st, body = http(API_GH + "/releases", data=payload,
                headers={"Content-Type": "application/json",
                         "Authorization": "Bearer " + GH_TOKEN}, method="POST")
log("GitHub 建 release: HTTP %d" % st, st in (200, 201))
if st not in (200, 201):
    print(body.decode("utf-8", "replace")[:500])
    sys.exit(1)
gh_rel = json.loads(body.decode("utf-8"))
gh_rid = gh_rel["id"]
print("  release id:", gh_rid)

# 附件：exe 与 zip（GitHub 剔中文名 → 传 ASCII 名，再用 PATCH 补中文 label）
gh_assets = [
    (EXE_LOCAL, "SnipMagic.exe", "截图大师SnipMagic.exe"),
    (ZIP_LOCAL, "SnipMagic-20261001.zip", "截图大师SnipMagic-便携版-20261001.zip"),
]
for f, name, label in gh_assets:
    content = open(f, "rb").read()
    url = "%s/releases/%d/assets?name=%s" % (UP_GH, gh_rid,
                                             urllib.parse.quote(name))
    st, body = http(url, data=content, method="POST",
                    headers={"Authorization": "Bearer " + GH_TOKEN,
                             "Content-Type": "application/octet-stream"})
    log("GitHub 上传 %s: HTTP %d" % (name, st), st in (200, 201))
    if st not in (200, 201):
        print(body.decode("utf-8", "replace")[:400])
        continue
    aid = json.loads(body.decode("utf-8"))["id"]
    st, body = http("%s/releases/assets/%d" % (API_GH, aid),
                    data=json.dumps({"name": name, "label": label}).encode("utf-8"),
                    method="PATCH",
                    headers={"Content-Type": "application/json",
                             "Authorization": "Bearer " + GH_TOKEN})
    log("GitHub 补中文标签 %s: HTTP %d" % (label, st), st == 200)

# ============ 匿名复核 ============
exe_sha = sha256b(open(EXE_LOCAL, "rb").read())
zip_sha = sha256b(open(ZIP_LOCAL, "rb").read())
local = {os.path.basename(EXE_LOCAL): exe_sha,
         os.path.basename(ZIP_LOCAL): zip_sha}

st, body = http("https://gitee.com/api/v5/repos/" + REPO_GITEE + "/releases/latest")
latest = json.loads(body.decode("utf-8"))
log("Gitee latest tag = %s" % latest.get("tag_name"),
    latest.get("tag_name") == TAG)
for a in (latest.get("assets") or []):
    n = a.get("name", "")
    # 平台自动附带的源码归档（v3.4.0.zip / v3.4.0.tar.gz）不是我们的附件
    if re.match(r"^v?\d+\.\d+\.\d+(\.\d+)?\.(zip|tar\.gz)$", n):
        continue
    if not n.endswith((".exe", ".zip")):
        continue
    st, data = http(a["browser_download_url"])  # urllib 自动跟 302
    match = local.get(n) == sha256b(data)
    log("Gitee 附件 %s 下载 %d 字节 哈希一致=%s" % (n, len(data), match), match)

st, body = http(API_GH + "/releases/latest",
                headers={"Accept": "application/vnd.github+json"})
latest = json.loads(body.decode("utf-8"))
log("GitHub latest tag = %s" % latest.get("tag_name"),
    latest.get("tag_name") == TAG)
for a in (latest.get("assets") or []):
    n, label = a.get("name", ""), a.get("label", "")
    digest = a.get("digest", "")
    expect = {"SnipMagic.exe": exe_sha, "SnipMagic-20261001.zip": zip_sha}
    if n in expect:
        ok = digest == "sha256:" + expect[n]
        log("GitHub 附件 %s (label=%s) %d 字节 digest 一致=%s"
            % (n, label, a.get("size", 0), ok), ok)

all_ok = all(ok for _, ok in results)
print("RESULT=" + ("PASS" if all_ok else "FAIL"))
sys.exit(0 if all_ok else 1)
