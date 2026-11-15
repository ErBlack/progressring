import unicodedata
from pathlib import Path

root = Path(__file__).resolve().parent.parent
ranges = [(0x20, 0x7E), (0xA0, 0x17F), (0x400, 0x45F), (0x490, 0x491), (0x2010, 0x203A), (0x20AC, 0x20AC), (0x2116, 0x2116), (0x2122, 0x2122)]

glyphs, cur, rows, bitmap = {}, None, [], False
for line in (root / 'tools' / 'ter-u14n.bdf').read_text(encoding='latin-1').splitlines():
    t = line.split()
    if not t:
        continue
    if t[0] == 'ENCODING':
        cur = int(t[1])
    elif t[0] == 'BITMAP':
        bitmap, rows = True, []
    elif t[0] == 'ENDCHAR':
        glyphs[cur], bitmap = rows, False
    elif bitmap:
        rows.append(int(t[0], 16))


def draw(ch):
    d = unicodedata.normalize('NFD', ch)
    if len(d) == 2 and unicodedata.combining(d[1]) == 230 and ord(d[0]) in glyphs:
        accent = [r for r in dict.fromkeys(glyphs[ord(ch)][:2]) if r]
        return [0] * (2 - len(accent)) + accent + [0] + glyphs[ord(d[0])][2:]
    return [0] + glyphs[ord(ch)]


font = {}
for a, b in ranges:
    for c in range(a, b + 1):
        ch = chr(c)
        up = ch.upper() if len(ch.upper()) == 1 else ch
        if ord(up) in glyphs and unicodedata.category(up)[0] != 'C':
            font[c] = draw(up)

h = ['#pragma once', '#include <pebble.h>', '',
     'typedef struct { uint16_t cp; uint8_t rows[15]; } Glyph;', '',
     f'#define GLYPH_COUNT {len(font)}',
     'extern const Glyph GLYPHS[GLYPH_COUNT];']
c = ['#include "font.h"', '', 'const Glyph GLYPHS[GLYPH_COUNT] = {']
c += [f'  {{{cp}, {{{", ".join(map(str, r))}}}}},' for cp, r in sorted(font.items())]
c.append('};')
(root / 'src' / 'c' / 'font.h').write_text('\n'.join(h) + '\n', encoding='utf-8')
(root / 'src' / 'c' / 'font.c').write_text('\n'.join(c) + '\n', encoding='utf-8')
print(len(font), 'glyphs')
