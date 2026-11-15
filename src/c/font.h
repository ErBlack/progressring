#pragma once
#include <pebble.h>

typedef struct { uint16_t cp; uint8_t rows[15]; } Glyph;

#define GLYPH_COUNT 444
extern const Glyph GLYPHS[GLYPH_COUNT];
