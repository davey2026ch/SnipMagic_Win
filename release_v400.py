# -*- coding: utf-8 -*-
"""v4.0.0 双平台发版：Gitee + GitHub 建 release、传 exe 附件、匿名复核。
令牌从环境变量读取，绝不落盘。"""
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

TAG = "v4.0.0"
TITLE = "v4.0.0 GIF 录制与动图播放页 · 全部保存按时间归档 · 页签横向滚动"
BODY = """## v4.0.0 更新内容

### 新增：GIF 录制
- 顶部新增 **「GIF」按钮**（与「截图 / 长截图」同款蓝底白字）。点一下软件主界面会自动隐藏，
  弹出一个 **700×400 的空心框选窗口**（可拖边框放大），中间直接透出桌面，边框和按钮不会被录进去
- 底部按钮随状态切换：`录制 / 取消` -> `暂停 / 停止` -> `继续 / 停止`，实时显示时长、帧数、帧率
- 点 **停止** 自动生成动图并新建页签；点 **取消** 则丢弃并还原主界面
- 抓帧按屏幕**物理像素**，相同画面会自动合并时长，静止画面不会把文件撑大
- 「设置」里新增 **GIF帧率**（默认 24，1–60 可调）

### 新增：动图播放页
- 上方棋盘格预览 + 下方缩略图条（每帧标出帧号与停留毫秒），当前帧高亮
- 播放控制：首帧 / 上一帧 / 播放暂停 / 下一帧 / 末帧，方向键也能逐帧看
- **循环开关**（默认关）：点播放只播一次、停在最后一帧；打开开关才循环
- 预览区 **Ctrl+滚轮缩放画面**、**滚轮 / Shift+滚轮 / 按住拖动平移**；缩略图条上的滚轮仍是左右翻帧
- 三个出口：**保存帧**（当前帧存图片）、**存为页签**（转成可标注的普通页签）、**保存GIF**
- 不自动播放：录完停在第 1 帧，只有点播放按钮（或空格）才开始

### 新增：页签横向滚动
- 页签多到放不下时，页签条下方出现横向滚动条，可拖滑块、点轨道跳转、滚轮左右滚
- 切换到某个页签会自动滚到看得见的位置

### 改进：保存更省事
- **全部保存**：不再问目录和格式，直接在你「下载」里建一个以时间命名的文件夹
  （如 `202611260841`），动图页签存 `.gif`、其余存 `.png`
- **单张保存遇到同名文件**：弹「替换已有文件 / 追加序号再保存」，选追加就自动变成
  `P1_001.png`、`P1_002.png`，不会误覆盖
- **画布 Ctrl+滚轮缩放**：截完图直接滚就能缩放，不用先点一下画布

### 修复
- 窗口改变大小后页签位置会"飘"在中间
- 窗口很矮时左侧工具栏压住页签条、导致点不动页签

### 说明
- 只上传 exe 一个附件，解压即用、不写注册表
- 配置文件 `SnipMagic.ini` 与 exe 同目录，升级时请保留（里面是快捷键和你填过的接口令牌）
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
log("exe 内嵌版本串 == 4.0.0", "4.0.0".encode("utf-16-le") in raw)
log("exe 无 3.6.0 残留", "3.6.0".encode("utf-16-le") not in raw)

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
