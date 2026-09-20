# -*- coding: utf-8 -*-
# Gitee 发版：创建 v2.0.0 release + 上传附件 + 匿名复核（令牌从环境变量读，不落盘）
import hashlib
import io
import json
import os
import sys
import urllib.request
import urllib.parse

REPO = "mrpu2020/SnipMagic_Win"
API = "https://gitee.com/api/v5/repos/" + REPO
TOKEN = os.environ.get("GITEE_TOKEN", "")
EXE = r"F:\Projects\截图工具\截图大师SnipMagic-绿色版\截图大师SnipMagic.exe"

BODY = """## v2.0.0 更新内容

### 长截图：微信聊天记录重复问题根治
- 对齐算法引入加长上下文消歧，重复短消息（如"好的""收到"）不再导致整段内容重复拼接
- 撕裂帧 / 懒加载重排 / 新消息插入等不稳定画面自动跳过，不再烙进长图
- 大片空白背景不再干扰位移判断；顺带减少内容缺失
- 合成回归 7 个场景（重复消息/大空白/撕裂/插入/快慢滚/稠密对照）全部零重复

### 内容提取（OCR）：超长图识别全错修复
- 超高图片自动切片（带间重叠）→ 批次上传 → 按序合并、接缝去重
- 此前整图上传会被云端压缩导致文字丢失 1/3、顺序错乱；切片后实测全部正确

### 新功能
- 框选区域后点「提取内容」，只识别选区内内容（不框选仍是整图识别）
- 选区内按下鼠标可把画面"抠起"搬走，原位置自动填白并烙进底图（非浮层，保存后依然生效）
"""


def http(url, data=None, headers=None, method="GET"):
    req = urllib.request.Request(url, data=data, headers=headers or {}, method=method)
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.status, r.read()


def main():
    if not TOKEN:
        print("NO TOKEN")
        return 1

    # 0) 若已存在 v2.0.0 release 则先删（附件不可替换）
    st, body = http(API + "/releases?access_token=" + TOKEN + "&per_page=20")
    for rel in json.loads(body.decode("utf-8")):
        if rel.get("tag_name") in ("v2.1.0", "2.1.0"):
            st2, _ = http(API + "/releases/" + str(rel["id"]) +
                          "?access_token=" + TOKEN, method="DELETE")
            print("deleted old release", rel["id"], st2)

    # 1) 创建 release（target_commitish 必填）
    payload = json.dumps({
        "access_token": TOKEN,
        "tag_name": "v2.1.0",
        "target_commitish": "master",
        "name": "v2.1.0 产品更名 SnipMagic · 自动更新修复 · 选区精确提取/抠起烙白",
        "body": BODY,
        "prerelease": False,
    }).encode("utf-8")
    st, body = http(API + "/releases", data=payload,
                    headers={"Content-Type": "application/json"}, method="POST")
    print("create release:", st)
    if st not in (200, 201):
        print(body.decode("utf-8", "replace")[:400])
        return 1
    rel = json.loads(body.decode("utf-8"))
    rid = rel["id"]
    print("release id:", rid)

    # 2) 上传附件（令牌必须放 URL query）
    exe_bytes = open(EXE, "rb").read()
    boundary = "----screenshottool" + os.urandom(8).hex()
    fn = "截图大师SnipMagic.exe".encode("utf-8")
    part = (("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
             "Content-Type: application/octet-stream\r\n\r\n" % (boundary, fn.decode("utf-8")))
            ).encode("utf-8") + exe_bytes + ("\r\n--%s--\r\n" % boundary).encode("utf-8")
    url = (API + "/releases/%d/attach_files?access_token=" % rid) + TOKEN
    st, body = http(url, data=part, method="POST",
                    headers={"Content-Type": "multipart/form-data; boundary=" + boundary})
    print("attach:", st)
    if st not in (200, 201):
        print(body.decode("utf-8", "replace")[:400])
        return 1

    # 3) 匿名复核 latest
    st, body = http("https://gitee.com/api/v5/repos/" + REPO + "/releases/latest")
    latest = json.loads(body.decode("utf-8"))
    assets = latest.get("assets") or []
    print("latest tag:", latest.get("tag_name"), "assets:", [a.get("name") for a in assets])
    dl = None
    for a in assets:
        if a.get("name") in ("截图工具.exe", "%E6%88%AA%E5%9B%BE%E5%B7%A5%E5%85%B7.exe"):
            dl = a.get("browser_download_url")
    if not dl:
        print("no asset url found")
        return 1
    st, data = http(dl)
    local_sha = hashlib.sha256(exe_bytes).hexdigest()
    remote_sha = hashlib.sha256(data).hexdigest()
    print("download:", st, "size:", len(data))
    print("sha local :", local_sha[:16])
    print("sha remote:", remote_sha[:16])
    print("SHA MATCH:", local_sha == remote_sha)
    return 0 if local_sha == remote_sha else 1


if __name__ == "__main__":
    sys.exit(main())
