# -*- coding: utf-8 -*-
"""v4.2.2 双平台发版：Gitee + GitHub 建 release、传 exe 附件、匿名复核。
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

TAG = "v4.2.2"
TITLE = "v4.2.2 动图页可直接标注 / 录制带鼠标箭头（含 4.1.x、4.2.x 全部新功能）"
BODY = """## v4.2.2 修复

- **暗色主题下设置页露面亮色块**：两个复选框「截图」「GIF录制」和「带边框复制」开关周围
  有一圈比背景亮的底色，现已与背景完全一致（自绘控件先按弹窗底色铺满再画）

---

## v4.2.0 新增：录制鼠标箭头开关

- 设置里新增一行「录制鼠标箭头」，右侧两个勾选：**截图** / **GIF录制**
- 默认 **截图不勾、GIF录制勾**：普通截图画面保持干净，GIF 录制带箭头方便看的人知道操作在哪
- 箭头**尖**对准真实光标位置，大小跟着屏幕缩放走；长截图不含箭头（多帧拼接会出现一串箭头）

## v4.1.1 修复：GIF 录制帧率与录制窗标题栏

- **录出来的 GIF 不再"快进"**：以前画面静止时合并的帧被卡在 2 秒，录 6 秒只剩 2 秒；
  现在单帧最多 5 秒、超过就复制一帧继续累计，**总时长与真实录制时长一致**
- **状态栏显示实测帧率**（如 `24 fps（实测 5）`）：大区域 + 高帧率达不到设定值时一眼能看出来
  （全屏 4K 受抓屏速度限制，建议 10~15 帧）
- **录制窗标题栏改为自绘**：右上角最小化/最大化/关闭不会再被裁；多显示器缩放不同也不会算错尺寸；
  拖标题栏移动、拖边框缩放、双击标题栏最大化 都保留

## v4.1.0 新增：动图页当成普通截图来编辑

- 动图页上直接用左侧工具标注（箭头、直线、方框、圆、实心块、画笔、荧光笔、序号、文字），
  **标注跟着帧走**（切帧不串），**保存 GIF 时自动写进文件**
- 选择工具可点选/拖动/缩放/删除，Ctrl+Z 撤销（马赛克也能撤）；「保存帧」「存为页签」导出的图同样带标注
- 当前帧的缩略图会显示标注，保存前就能看到效果；播放中一动手画就自动暂停
- **缩略图右键菜单**：删除当前帧 / 删除左侧全部帧（N 帧）/ 删除右侧全部帧（N 帧）/ 保存 GIF
- 动图页里的「提取文字 / 提取矢量图 / 魔法消除」会提示先「存为页签」再操作

### 说明

- 只上传 exe 一个附件，解压即用、不写注册表
- 配置文件 `SnipMagic.ini` 与 exe 同目录，升级时请保留（快捷键和你填过的接口令牌都在里面）
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
                            for v in ("4.2.1", "4.2.0", "4.1.1", "4.1.0", "4.0.1")))

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
