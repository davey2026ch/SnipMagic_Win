# -*- coding: utf-8 -*-
# v3.5.0 发版前产物自检：内嵌版本串 + 双副本哈希一致性
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")
out = []


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


names = ["SnipMagic.exe", "截图大师SnipMagic.exe"]
hashes = {}
for n in names:
    p = os.path.join(DIST, n)
    if not os.path.exists(p):
        out.append("MISSING: " + p)
        print("\n".join(out))
        sys.exit(1)
    hashes[n] = sha256(p)
    with open(p, "rb") as f:
        data = f.read()
    has_new = "3.5.0".encode("utf-16-le") in data
    has_old = "3.4.0".encode("utf-16-le") in data
    has_new_str = b"3.5.0.0" in data
    out.append("%s: size=%d new(utf16)=%s old(utf16)=%s new(ansi)=%s" % (
        n, len(data), has_new, has_old, has_new_str))
    out.append("  sha256=%s" % hashes[n])

same = hashes[names[0]] == hashes[names[1]]
out.append("双副本哈希一致: %s" % same)

ok = (hashes[names[0]] == hashes[names[1]])
with open(os.path.join(DIST, names[0]), "rb") as f:
    d = f.read()
ok = ok and ("3.5.0".encode("utf-16-le") in d) and ("3.4.0".encode("utf-16-le") not in d)
out.append("SELF_CHECK=%s" % ("PASS" if ok else "FAIL"))
print("\n".join(out))
sys.exit(0 if ok else 1)
