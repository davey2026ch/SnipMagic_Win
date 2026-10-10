# -*- coding: utf-8 -*-
"""v3.5.0 双平台发版：Gitee + GitHub 建 release、传附件（只传 exe）、匿名复核。
令牌从环境变量 GITEE_TOKEN / GH_TOKEN 读取，绝不落盘。"""
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

REPO_GITEE = "mrpu2020/SnipMagic_Win"
REPO_GH = "davey2026ch/SnipMagic_Win"
API_GITEE = "https://gitee.com/api/v5/repos/" + REPO_GITEE
API_GH = "https://api.github.com/repos/" + REPO_GH
UP_GH = "https://uploads.github.com/repos/" + REPO_GH

DIST = r"F:\Projects\截图工具\dist"
EXE_LOCAL = os.path.join(DIST, "截图大师SnipMagic.exe")

TAG = "v3.5.0"
TITLE = "v3.5.0 查看模式全量定版 · 文字重影修复 · 关闭确认框置顶"
BODY = """## v3.5.0 更新内容

### 查看模式：所有浮动层一次性定版
- 点「查看」那一刻，画布上所有浮动层（图形、文字、序号、贴图）全部烙进图片本身
- 烙死后切到任何工具都无法再编辑之前的层；Ctrl+Z 或顶部撤销按钮可一步找回

### 修复文字弹窗输入框重影
- 在文字中间插入内容、按退格删除时，不再出现"重影 / 看着像没删掉"的显示错误

### 修复关闭确认框被弹窗挡住
- 取色器、文字弹窗等开着时点主窗口右上角关闭，「是否保存」确认框现在置顶显示
- 后续的保存路径选择框、JPG/PNG 格式选择框同样不会被挡住
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

part, ctype = multipart(EXE_LOCAL)
st, body = http(API_GITEE + "/releases/%d/attach_files" % gitee_rid,
                data=part, method="POST",
                headers={"Content-Type": ctype,
                         "Authorization": "token " + GITEE_TOKEN})
log("Gitee 上传附件 截图大师SnipMagic.exe: HTTP %d" % st, st in (200, 201))
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

# GitHub 剔中文名 → 传 ASCII 名，再用 PATCH 补中文 label（网页显示中文）
content = open(EXE_LOCAL, "rb").read()
url = "%s/releases/%d/assets?name=%s" % (UP_GH, gh_rid,
                                         urllib.parse.quote("SnipMagic.exe"))
st, body = http(url, data=content, method="POST",
                headers={"Authorization": "Bearer " + GH_TOKEN,
                         "Content-Type": "application/octet-stream"})
log("GitHub 上传 SnipMagic.exe: HTTP %d" % st, st in (200, 201))
if st in (200, 201):
    aid = json.loads(body.decode("utf-8"))["id"]
    st, body = http("%s/releases/assets/%d" % (API_GH, aid),
                    data=json.dumps({"name": "SnipMagic.exe",
                                     "label": "截图大师SnipMagic.exe"}).encode("utf-8"),
                    method="PATCH",
                    headers={"Content-Type": "application/json",
                             "Authorization": "Bearer " + GH_TOKEN})
    log("GitHub 补中文标签: HTTP %d" % st, st == 200)
else:
    print(body.decode("utf-8", "replace")[:400])

# ============ 匿名复核 ============
exe_sha = sha256b(open(EXE_LOCAL, "rb").read())
print("本地 exe sha256 = %s" % exe_sha)

st, body = http("https://gitee.com/api/v5/repos/" + REPO_GITEE + "/releases/latest")
latest = json.loads(body.decode("utf-8"))
log("Gitee latest tag = %s" % latest.get("tag_name"),
    latest.get("tag_name") == TAG)
seen_gitee = False
for a in (latest.get("assets") or []):
    n = a.get("name", "")
    # 平台自动附带的源码归档（v3.5.0.zip / v3.5.0.tar.gz）不是我们的附件
    if re.match(r"^v?\d+\.\d+\.\d+(\.\d+)?\.(zip|tar\.gz)$", n):
        continue
    if not n.endswith(".exe"):
        continue
    seen_gitee = True
    st, data = http(a["browser_download_url"])  # urllib 自动跟 302
    match = (n == "截图大师SnipMagic.exe") and sha256b(data) == exe_sha
    log("Gitee 附件 %s 下载 %d 字节 哈希一致=%s" % (n, len(data), match), match)
log("Gitee 附件只有 exe 一个: %s" % seen_gitee, seen_gitee)

st, body = http(API_GH + "/releases/latest",
                headers={"Accept": "application/vnd.github+json"})
latest = json.loads(body.decode("utf-8"))
log("GitHub latest tag = %s" % latest.get("tag_name"),
    latest.get("tag_name") == TAG)
assets = latest.get("assets") or []
log("GitHub 附件只有 exe 一个: %s" % (len(assets) == 1), len(assets) == 1)
for a in assets:
    n, label = a.get("name", ""), a.get("label", "")
    digest = a.get("digest", "")
    ok = (n == "SnipMagic.exe" and label == "截图大师SnipMagic.exe"
          and digest == "sha256:" + exe_sha)
    log("GitHub 附件 %s (label=%s) %d 字节 digest 一致=%s"
        % (n, label, a.get("size", 0), ok), ok)

all_ok = all(ok for _, ok in results)
print("RESULT=" + ("PASS" if all_ok else "FAIL"))
sys.exit(0 if all_ok else 1)
