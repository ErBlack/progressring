import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
icons = json.loads((root / 'tools' / 'icons.json').read_text(encoding='utf-8'))

h = ['#pragma once', '#include <pebble.h>', '',
     'typedef struct { uint8_t w; uint8_t h; const char *px; } Icon;', '']
c = ['#include "assets.h"', '']
for name, rows in icons.items():
    w, n = len(rows[0]), len(rows)
    assert all(len(r) == w for r in rows), name
    h.append(f'extern const Icon ICON_{name};')
    c.append(f'const Icon ICON_{name} = {{{w}, {n}, "{"".join(rows)}"}};')

(root / 'src' / 'c' / 'assets.h').write_text('\n'.join(h) + '\n', encoding='utf-8')
(root / 'src' / 'c' / 'assets.c').write_text('\n'.join(c) + '\n', encoding='utf-8')
print(len(icons), 'icons')
