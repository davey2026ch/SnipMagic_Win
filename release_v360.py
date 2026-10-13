# -*- coding: utf-8 -*-
"""v3.6.0 双平台发版：Gitee + GitHub 建 release、传附件（只传 exe）、匿名复核。
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

TAG = "v3.6.0"
TITLE = "v3.6.0 快捷键悬浮提示 · 取色器焕新（吸管图标 + 最近使用 8 色）· 文字弹窗 1.5 倍可缩放"
BODY = """## v3.6.0 更新内容

### 快捷键随手可见
- 鼠标停在顶部「截图 / 长截图」按钮上，会弹出气泡显示当前快捷键（如 Ctrl+Shift+R）
- 在设置里改了快捷键，气泡内容会自动跟着变

### 取色器全面焕新（左侧工具栏「颜色」、文字颜色、背景颜色 三处通用）
- 原来的文字按钮「吸管（全屏取色）」换成了**吸管图标**，一眼就能看懂；鼠标停上去有文字说明
- 新增**「最近使用」颜色区**：记住最近用过的 8 个颜色，点一下即可复用
- 这 8 个颜色会存进软件的配置文件，关掉软件再打开依然在，越用越顺手
- 「当前颜色」改成一条大色块，半透明时能直接看出透明度

### 文字输入弹窗更好用
- 窗口放大到原来的 1.5 倍，输入长文本更宽敞
- 边框可以随意拖动缩放，里面的内容会自动跟着排布
- 「文字颜色 / 背景颜色」入口挪到「字号、加粗」同一行，底部更紧凑
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
    # 平台自动附带的源码归档（v3.6.0.zip / v3.6.0.tar.gz）不是我们的附件
    if re.match(r"^v?\d+\.\d+\.\d+(\.\d+)?\.(zip|tar\.gz)$", n):
        continue
    if not n.endswith(".exe"):
        continue
    seen_gitee = True
    st, data = http(a["browser_download_url"])  # urllib 自动跟 302
    match = (n == "截图大师SnipMagic.exe") and sha256b(data) == exe_sha
    log("Gitee 附件 %s 下载 %d 字节 哈希一致=%s" % (n, len(data), match), match)
log("Gitee 附件只有 exe 一个: %s" % seen_gitee, seen_gitee)

# 内嵌版本串复核（防止「更新成功后仍被提示更新」）
ver_ok = ("3.6.0".encode("utf-16-le") in open(EXE_LOCAL, "rb").read()) and \
         ("3.5.0".encode("utf-16-le") not in open(EXE_LOCAL, "rb").read())
log("exe 内嵌版本串 == %s 且无旧版本残留" % TAG, ver_ok)

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
