# -*- coding: utf-8 -*-
"""查看上次便携版 zip 的内容结构 + 本次打包"""
import os
import sys
import zipfile

ROOT = r"F:\Projects\截图工具"
OLD = os.path.join(ROOT, "截图大师SnipMagic-便携版-20260926.zip")

if len(sys.argv) > 1 and sys.argv[1] == "inspect":
    z = zipfile.ZipFile(OLD)
    for i in z.infolist():
        print(i.filename, i.file_size)
    # 顺便看 ini 内容是否含密钥
    for i in z.infolist():
        if i.filename.endswith(".ini"):
            data = z.read(i)
            text = data.decode("utf-8", errors="replace")
            print("---- ini content ----")
            print(text)
            print("---- has token:", ("MinerUToken" in text or "VolcApiKey" in text))
