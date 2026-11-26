# -*- coding: utf-8 -*-
"""v4.2.3 双平台发版：Gitee + GitHub 建 release、传 exe 附件、匿名复核。
令牌从环境变量读取（GITEE_TOKEN / GH_TOKEN），绝不写进文件。"""
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

TAG = "v4.2.3"
TITLE = "v4.2.3 复制不再在磁盘留缓存（默认零落盘）+ 修复带页签退出的崩溃"
BODY = """## v4.2.3 隐私：复制不再往磁盘写缓存

- 以前「复制到剪贴板」会在系统临时目录写一张截图（而且最多要等 24 小时才清掉）；
  **现在默认一张都不写**，图片直接跟着剪贴板走
- 设置里新增开关「**邮件客户端兼容**」（默认关闭）：只有个别老式邮件客户端（例如 Foxmail）
  粘贴时不显示图片，才需要打开它。打开后仍会在临时目录留一张**固定名字**的临时图
  （磁盘上永远最多一张，覆盖同一份），**程序一关就删**
- 启动时自动清掉上次异常退出（崩溃 / 强制结束 / 断电）留下的临时图，
  以及「提取内容」失败时可能残留的临时目录

### 其它环节本来就不落盘（这次一并确认）

- **截图 / 长截图 / GIF 录制**：抓屏、拼接、抓帧全部在内存里完成
- **所有编辑**：箭头、方框、画笔、荧光笔、马赛克、文字、序号，以及动图页的逐帧标注、
  删除帧、撤销 / 重做，都只在内存里改
- **保存**：只写你选定的那一个文件（默认「下载」；「全部保存」是下载里建一个时间命名的文件夹），
  **不会在别的路径再存一份副本**

## 修复

- **带着未保存的页签关程序会闪退**：进程以「访问冲突」结束（错误码 0xC0000005）。现已正常退出
- **复制偶尔失效**：剪贴板被输入法、剪贴板历史等程序短暂占用时，这次复制（包括清理动作）会被跳过；
  现在会自动重试

## 说明

- 只上传 exe 一个附件，解压即用、不写注册表
- 配置文件 `SnipMagic.ini` 与 exe 同目录，升级时请保留（快捷键和你填过的接口令牌都在里面）
- 系统层面另有两处会留痕、与本程序无关：Windows 剪贴板历史（Win+V）、资源管理器缩略图缓存；
  要彻底避免复制过的图被留存，建议在系统设置里关掉剪贴板历史
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
        except Exception as e:
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
    print(("OK   " if ok else "FAIL ") + msg, flush=True)


# ============ 本地检查 ============
raw = open(EXE_LOCAL, "rb").read()
exe_sha = sha256b(raw)
print("本地 exe: %s  %d 字节  sha256=%s" % (EXE_LOCAL, len(raw), exe_sha), flush=True)
ver = TAG.lstrip("v")
log("exe 内嵌版本串 == %s" % ver, ver.encode("utf-16-le") in raw)
log("exe 无旧版本残留", all(v.encode("utf-16-le") not in raw
                            for v in ("4.2.2", "4.2.1", "4.2.0", "4.1.1", "4.1.0", "4.0.1")))

# ============ Gitee ============
st, body = http(API_GITEE + "/releases?access_token=" + GITEE_TOKEN + "&per_page=20")
deleted = False
try:
    for rel in json.loads(body.decode("utf-8")):
        if rel.get("tag_name") == TAG:
            http(API_GITEE + "/releases/%d?access_token=" % rel["id"] + GITEE_TOKEN,
                 method="DELETE")
            deleted = True
except Exception as e:
    print("枚举 Gitee releases 失败:", e)
log("Gitee 清理同名旧 release: %s" % ("已删" if deleted else "无"))

payload = json.dumps({
    "access_token": GITEE_TOKEN,
    "tag_name": TAG,
    "target_commitish": "master",
    "name": TITLE,
    "body": BODY,
    "prerelease": False,
}, ensure_ascii=False).encode("utf-8")
st, body = http(API_GITEE + "/releases", data=payload,
                headers={"Content-Type": "application/json; charset=utf-8"}, method="POST")
log("Gitee 建 release: HTTP %d" % st, st in (200, 201))
if st not in (200, 201):
    print(body.decode("utf-8", "replace")[:600])
    sys.exit(1)
gitee_rid = json.loads(body.decode("utf-8"))["id"]
print("  Gitee release id:", gitee_rid, flush=True)

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
    log("GitHub 清理同名旧 release: HTTP %d" % st2, st2 in (204, 404))
    time.sleep(1)
else:
    log("GitHub 无同名 release (HTTP %d)" % st, st == 404)

payload = json.dumps({
    "tag_name": TAG,
    "target_commitish": "master",
    "name": TITLE,
    "body": BODY,
    "prerelease": False,
}, ensure_ascii=False).encode("utf-8")
st, body = http(API_GH + "/releases", data=payload,
                headers={"Content-Type": "application/json; charset=utf-8",
                         "Authorization": "Bearer " + GH_TOKEN}, method="POST")
log("GitHub 建 release: HTTP %d" % st, st in (200, 201))
if st not in (200, 201):
    print(body.decode("utf-8", "replace")[:600])
    sys.exit(1)
gh_rid = json.loads(body.decode("utf-8"))["id"]
print("  GitHub release id:", gh_rid, flush=True)

url = "%s/releases/%d/assets?name=%s" % (UP_GH, gh_rid, urllib.parse.quote("SnipMagic.exe"))
st, body = http(url, data=raw, method="POST",
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
    log("GitHub 补中文 label: HTTP %d" % st, st == 200)
else:
    print(body.decode("utf-8", "replace")[:400])

# ============ 匿名复核 ============
st, body = http("https://gitee.com/api/v5/repos/" + REPO_GITEE + "/releases/latest")
latest = json.loads(body.decode("utf-8"))
log("Gitee latest tag = %s" % latest.get("tag_name"), latest.get("tag_name") == TAG)
seen = 0
for a in (latest.get("assets") or []):
    n = a.get("name", "")
    if re.match(r"^v?\d+\.\d+\.\d+(\.\d+)?\.(zip|tar\.gz)$", n):
        continue
    if not n.endswith(".exe"):
        continue
    seen += 1
    st, data = http(a["browser_download_url"])
    match = (n == "截图大师SnipMagic.exe") and sha256b(data) == exe_sha
    log("Gitee 附件 %s 下载 %d 字节 哈希一致=%s" % (n, len(data), match), match)
log("Gitee 附件只有 exe 一个", seen == 1)

st, body = http(API_GH + "/releases/latest",
                headers={"Accept": "application/vnd.github+json"})
latest = json.loads(body.decode("utf-8"))
log("GitHub latest tag = %s" % latest.get("tag_name"), latest.get("tag_name") == TAG)
assets = latest.get("assets") or []
log("GitHub 附件只有 exe 一个", len(assets) == 1)
for a in assets:
    n, label, digest = a.get("name", ""), a.get("label", ""), a.get("digest", "")
    ok = (n == "SnipMagic.exe" and label == "截图大师SnipMagic.exe"
          and digest == "sha256:" + exe_sha)
    log("GitHub 附件 %s (label=%s) %d 字节 digest 一致=%s"
        % (n, label, a.get("size", 0), ok), ok)

print("\n本地 exe sha256 = %s" % exe_sha)
print("RESULT=" + ("PASS" if all(ok for _, ok in results) else "FAIL"))
