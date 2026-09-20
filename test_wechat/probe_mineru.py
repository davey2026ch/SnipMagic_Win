# -*- coding: utf-8 -*-
# MinerU 接口实测：A=细长整图 B=正常图(对照) C=切片批次 —— 验证"图片太大导致识别全错"及切片修复方案
import io
import json
import time
import zipfile

import requests
from PIL import Image

INI = r"F:\Projects\截图工具\截图工具-绿色版\截图工具.ini"
SRC = r"F:\Projects\截图工具\测试文件\测试图片（图文混排）.png"
OUT = r"F:\Projects\截图工具\test_wechat"
BASE = "https://mineru.net/api/v4"


def read_token():
    for line in io.open(INI, encoding="utf-8", errors="replace"):
        if line.startswith("MinerUToken="):
            return line.split("=", 1)[1].strip()
    return None


TOKEN = read_token()
HDR = {"Authorization": "Bearer " + TOKEN}


def make_images():
    im = Image.open(SRC).convert("RGB")
    w, h = im.size
    n = 6
    tall = Image.new("RGB", (w, h * n), (255, 255, 255))
    for i in range(n):
        tall.paste(im, (0, i * h))
    tall_path = OUT + r"\probe_tall.png"
    tall.save(tall_path)
    slices = []
    for i in range(n):
        p = OUT + ("\\probe_slice_%d.png" % i)
        tall.crop((0, i * h, w, (i + 1) * h)).save(p)
        slices.append(p)
    return tall_path, slices


def run_batch(name_path_pairs, label):
    print("=== %s: files=%d ===" % (label, len(name_path_pairs)), flush=True)
    files = [{"name": n, "is_ocr": True} for n, _ in name_path_pairs]
    req = {"files": files, "model_version": "vlm", "language": "ch",
           "enable_table": True, "enable_formula": False}
    r = requests.post(BASE + "/file-urls/batch", headers=HDR, json=req, timeout=60)
    print("create batch:", r.status_code, flush=True)
    if r.status_code != 200:
        return {"label": label, "error": r.text[:300]}
    data = r.json().get("data", {})
    batch_id = data.get("batch_id")
    urls = data.get("file_urls", [])
    if not batch_id or len(urls) != len(name_path_pairs):
        return {"label": label, "error": "bad batch resp: " + r.text[:300]}

    for (n, path), url in zip(name_path_pairs, urls):
        with open(path, "rb") as f:
            body = f.read()
        up = requests.put(url, data=body, timeout=300)
        print("upload %s: %d (%d bytes)" % (n, up.status_code, len(body)), flush=True)
        if up.status_code not in (200, 204):
            return {"label": label, "error": "upload failed %d" % up.status_code}

    start = time.time()
    results = []
    while True:
        time.sleep(4)
        poll = requests.get(BASE + "/extract-results/batch/" + batch_id,
                            headers=HDR, timeout=60)
        if poll.status_code != 200:
            continue
        pdata = poll.json().get("data", {})
        er = pdata.get("extract_result") or []
        states = [e.get("state") for e in er]
        print("poll:", states, flush=True)
        if er and all(s in ("done", "failed") for s in states):
            for e in er:
                if e.get("state") == "failed":
                    results.append({"error": e.get("err_msg", "failed")})
                    continue
                zurl = e.get("full_zip_url")
                if not zurl:
                    results.append({"error": "no zip url"})
                    continue
                z = requests.get(zurl, timeout=300)
                md = ""
                try:
                    zf = zipfile.ZipFile(io.BytesIO(z.content))
                    for nm in zf.namelist():
                        if nm.endswith("full.md"):
                            md = zf.read(nm).decode("utf-8", "replace")
                            break
                except Exception as ex:
                    results.append({"error": "zip: " + repr(ex)})
                    continue
                results.append({"markdown": md})
            break
        if time.time() - start > 480:
            return {"label": label, "error": "timeout", "states": states}
    return {"label": label, "results": results,
            "elapsed": round(time.time() - start, 1)}


def md_head(md, n=260):
    t = (md or "").replace("\n", " ").strip()
    return t[:n]


def main():
    tall_path, slices = make_images()
    out = {}

    # B: 对照（正常图）
    out["B_normal"] = run_batch([("normal.png", SRC)], "B normal 1195x1139")
    # A: 细长整图
    out["A_tall"] = run_batch([("tall.png", tall_path)], "A tall 1195x6834")
    # C: 切片批次
    pairs = [("slice_%d.png" % i, p) for i, p in enumerate(slices)]
    out["C_sliced"] = run_batch(pairs, "C sliced batch x6")

    with open(OUT + r"\probe_mineru_result.json", "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)

    for k, v in out.items():
        print("\n##### %s #####" % k)
        if "error" in v:
            print("ERROR:", v["error"])
            continue
        for i, r in enumerate(v.get("results", [])):
            if "error" in r:
                print("[%d] ERROR: %s" % (i, r["error"]))
            else:
                print("[%d] md(%d chars): %s" % (i, len(r["markdown"]), md_head(r["markdown"])))


if __name__ == "__main__":
    main()
