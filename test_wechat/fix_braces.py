# -*- coding: utf-8 -*-
import io

p = 'longstitch_test_wechat.cpp'
lines = io.open(p, encoding='utf-8').read().split('\n')
out = []
added = 0
for ln in lines:
    if ln.lstrip().startswith('if (!Skip(') or ln.lstrip().startswith('std::printf("\\n%s'):
        j = len(out) - 1
        while j >= 0 and out[j].strip() == '':
            j -= 1
        if j >= 0 and out[j].strip() == '}':
            out.append('    }')
            added += 1
    out.append(ln)
io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(out))

src = io.open(p, encoding='utf-8').read()
depth = src.count('{') - src.count('}')
print('added =', added, ' final depth =', depth)
