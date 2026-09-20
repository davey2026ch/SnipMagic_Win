# -*- coding: utf-8 -*-
# 复核：小号字体的超长聊天式图片，整图上传 MinerU 是否识别全错
import io
import json
import time
import zipfile

import requests
from PIL import Image, ImageDraw, ImageFont

INI = r"F:\Projects\截图工具\截图工具-绿色版\截图工具.ini"
OUT = r"F:\Projects\截图工具\test_wechat"
BASE = "https://mineru.net/api/v4"


def read_token():
    for line in io.open(INI, encoding="utf-8", errors="replace"):
        if line.startswith("MinerUToken="):
            return line.split("=", 1)[1].strip()
    return None


TOKEN = read_token()
HDR = {"Authorization": "Bearer " + TOKEN}


def make_chat_image(path, w=1400, lines=200):
    try:
        font = ImageFont.truetype("msyh.ttc", 15)
        font2 = ImageFont.truetype("msyh.ttc", 13)
    except Exception:
        font = ImageFont.load_default()
        font2 = font
    line_h = 30
    h = lines * line_h + 60
    im = Image.new("RGB", (w, h), (245, 245, 245))
    d = ImageDraw.Draw(im)
    names = ["张伟", "李静", "王强", "刘敏", "陈晨"]
    texts = [
        "这个方案我看过了，整体思路没问题，细节再核一遍",
        "好的，那我下午三点前把修订版发到群里",
        "收到",
        "数据口径要先统一，不然汇总的时候会对不上",
        "嗯嗯",
        "下周例会的材料谁来准备？建议分工明确一下",
        "好的",
        "我刚查了后台，昨天的转化率比前天高了 2.3 个点",
    ]
    y = 30
    for i in range(lines):
        name = names[i % len(names)]
        msg = texts[i % len(texts)]
        d.ellipse((20, y, 52, y + 32), fill=(100, 140, 200))
        d.text((64, y - 2), name + "  14:%02d" % (i % 60), fill=(150, 150, 150), font=font2)
        d.text((64, y + 14), msg + "（编号 %04d）" % i, fill=(30, 30, 30), font=font)
        y += line_h
    im.save(path)
    return im.size


def run_one(path, label):
    req = {"files": [{"name": label + ".png", "is_ocr": True}],
           "model_version": "vlm", "language": "ch",
           "enable_table": True, "enable_formula": False}
    r = requests.post(BASE + "/file-urls/batch", headers=HDR, json=req, timeout=60)
    data = r.json().get("data", {})
    batch_id = data.get("batch_id")
    url = (data.get("file_urls") or [None])[0]
    with open(path, "rb") as f:
        up = requests.put(url, data=f.read(), timeout=300)
    print(label, "upload", up.status_code, flush=True)
    start = time.time()
    while True:
        time.sleep(4)
        poll = requests.get(BASE + "/extract-results/batch/" + batch_id,
                            headers=HDR, timeout=60)
        er = (poll.json().get("data", {}).get("extract_result") or [])
        if er and er[0].get("state") in ("done", "failed"):
            st = er[0].get("state")
            if st == "failed":
                return {"label": label, "error": er[0].get("err_msg")}
            z = requests.get(er[0].get("full_zip_url"), timeout=300)
            zf = zipfile.ZipFile(io.BytesIO(z.content))
            md = ""
            for nm in zf.namelist():
                if nm.endswith("full.md"):
                    md = zf.read(nm).decode("utf-8", "replace")
                    break
            return {"label": label, "markdown": md,
                    "elapsed": round(time.time() - start, 1)}
        if time.time() - start > 420:
            return {"label": label, "error": "timeout"}


def main():
    path = OUT + r"\probe_chat_tall.png"
    size = make_chat_image(path)
    print("chat image size:", size, flush=True)
    res = run_one(path, "chat_tall")
    with open(OUT + r"\probe_smalltext_result.json", "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    md = res.get("markdown", "")
    print("error:", res.get("error"))
    print("md chars:", len(md))
    # 统计能找回多少编号（200 条消息编号 0000..0199）
    import re
    found = set(re.findall(r"编号\s*(\d{4})", md))
    print("recovered ids:", len(found), "/200")
    print("md head:", md[:300].replace("\n", " "))


if __name__ == "__main__":
    main()
