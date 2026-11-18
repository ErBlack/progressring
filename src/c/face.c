#include "face.h"
#include "assets.h"
#include "font.h"

#define W 200
#define H 228
#define MAXCP 48

enum { BAND = 4, TICK_W = 2, MARK_W = 2, MARK_OVER = 2, RING_GAP = 5 };
enum { GAP = 5, PAD = 5, INNER = 6, LINE_GAP = 5 };
enum { DIG_W = 37, DIG_T = 9, DIG_GAP = 5, COLON_W = 12, COLON = 6 };
enum { ADV = 8, TH = 10 };

#define C_BLACK 0xC0
#define C_WHITE 0xFF
#define C_DARK 0xD5
#define C_LIGHT 0xEA
#define C_EVENT 0xF4
#define C_DAY 0xF9
#define C_NIGHT 0xC6

static uint8_t *s_row[H];
static uint8_t s_bg, s_fg, s_ghost, s_dim;
static int16_t s_min[H], s_max[H];
static int s_U;

static inline void px(int x, int y, uint8_t c) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  if (x < s_min[y] || x > s_max[y]) return;
  s_row[y][x] = c;
}

static void rect(int x0, int y0, int x1, int y1, uint8_t c) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) px(x, y, c);
}

static void frame(int x0, int y0, int x1, int y1, uint8_t c) {
  for (int x = x0; x <= x1; x++) { px(x, y0, c); px(x, y1, c); }
  for (int y = y0; y <= y1; y++) { px(x0, y, c); px(x1, y, c); }
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static float ffloor(float v) { int i = (int)v; return (v < (float)i) ? (float)(i - 1) : (float)i; }
static float fabsf_(float v) { return v < 0 ? -v : v; }

typedef struct { uint16_t cp[MAXCP]; int n; } Str;

static void str_set(Str *s, const char *u) {
  s->n = 0;
  const uint8_t *p = (const uint8_t *)u;
  while (*p && s->n < MAXCP) {
    uint16_t cp;
    if (*p < 0x80) { cp = *p++; }
    else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
    else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
    else { p++; while ((*p & 0xC0) == 0x80) p++; cp = '?'; }
    s->cp[s->n++] = cp;
  }
}

static const Glyph *glyph(uint16_t cp) {
  int lo = 0, hi = GLYPH_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (GLYPHS[mid].cp == cp) return &GLYPHS[mid];
    if (GLYPHS[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
  }
  return &GLYPHS[0];
}

static void gcols(uint16_t cp, int *a, int *b) {
  const Glyph *g = glyph(cp);
  int lo = 8, hi = -1;
  for (int c = 0; c < 8; c++)
    for (int r = 0; r < 15; r++)
      if (g->rows[r] & (0x80 >> c)) { if (c < lo) lo = c; if (c > hi) hi = c; }
  if (hi < 0) { lo = 0; hi = 7; }
  *a = lo; *b = hi;
}

static int ink_w(const Str *s) {
  if (!s->n) return 0;
  int a0, b0, a1, b1;
  gcols(s->cp[0], &a0, &b0);
  gcols(s->cp[s->n - 1], &a1, &b1);
  return (s->n - 1) * ADV + b1 - a0 + 1;
}

static int ink_w_c(const char *u) { Str s; str_set(&s, u); return ink_w(&s); }

static void raw_text(int x, int y, const Str *s, uint8_t col) {
  for (int i = 0; i < s->n; i++) {
    const Glyph *g = glyph(s->cp[i]);
    for (int r = 0; r < 15; r++)
      for (int c = 0; c < 8; c++)
        if (g->rows[r] & (0x80 >> c)) px(x + i * ADV + c, y - 3 + r, col);
  }
}

static void text(int x, int y, const Str *s, uint8_t col) {
  if (!s->n) return;
  int a, b;
  gcols(s->cp[0], &a, &b);
  raw_text(x - a, y, s, col);
}

static void text_r(int xr, int y, const Str *s, uint8_t col) {
  if (!s->n) return;
  int a, b;
  gcols(s->cp[s->n - 1], &a, &b);
  raw_text(xr - (s->n - 1) * ADV - b, y, s, col);
}

static void text_c(int x, int y, const char *u, uint8_t col) { Str s; str_set(&s, u); text(x, y, &s, col); }
static void text_rc(int xr, int y, const char *u, uint8_t col) { Str s; str_set(&s, u); text_r(xr, y, &s, col); }

static void icon(int x, int y, const Icon *ic, uint8_t col) {
  for (int r = 0; r < ic->h; r++)
    for (int c = 0; c < ic->w; c++)
      if (ic->px[r * ic->w + c] == '#') px(x + c, y + r, col);
}

static const char *BOLT8[8] = {"...##", "..##.", ".##..", "#####", "#####", "..##.", ".##..", "##..."};

static void battery_px(char *out, int pct, bool charging) {
  const Icon *ic = &ICON_BATT;
  int n = (8 * pct + 50) / 100;
  for (int r = 0; r < ic->h; r++)
    for (int c = 0; c < ic->w; c++) {
      char ch = ic->px[r * ic->w + c];
      if (ch == 'f') ch = (charging ? BOLT8[r - 3][c - 2] == '#' : (10 - r) < n) ? '#' : '.';
      out[r * ic->w + c] = ch;
    }
}

static const char SEG_ON[10][8] = {"abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgedc", "abc", "abcdefg", "abcdfg"};

static void seg_px(int x, int y, bool ghost) {
  if (ghost) { if (!((x + y) & 1)) px(x, y, s_ghost); }
  else px(x, y, s_fg);
}

static void hseg(int x0, int x1, int yc, int t, bool ghost) {
  int hl = t >> 1;
  for (int r = -hl; r <= hl; r++) {
    int k = r < 0 ? -r : r;
    for (int x = x0 + k + 2; x < x1 - k - 1; x++) seg_px(x, yc + r, ghost);
  }
}

static void vseg(int xc, int y0, int y1, int t, bool ghost) {
  int hl = t >> 1;
  for (int q = -hl; q <= hl; q++) {
    int k = q < 0 ? -q : q;
    for (int y = y0 + k + 2; y < y1 - k - 1; y++) seg_px(xc + q, y, ghost);
  }
}

static void draw_seg(char s, int L, int R, int T, int M, int B, int t, bool ghost) {
  switch (s) {
    case 'a': hseg(L, R, T, t, ghost); break;
    case 'g': hseg(L, R, M, t, ghost); break;
    case 'd': hseg(L, R, B, t, ghost); break;
    case 'f': vseg(L, T, M, t, ghost); break;
    case 'b': vseg(R, T, M, t, ghost); break;
    case 'e': vseg(L, M, B, t, ghost); break;
    case 'c': vseg(R, M, B, t, ghost); break;
  }
}

static void digit(int x0, int y0, int d, int w, int t, int h, int *oT, int *oM, int *oB) {
  int hl = t >> 1, L = x0 + hl, R = x0 + w - 1 - hl, T = y0 + hl, B = y0 + h - 1 - hl, M = (T + B) >> 1;
  const char *on = SEG_ON[d];
  for (const char *s = "abcdefg"; *s; s++)
    if (!strchr(on, *s)) draw_seg(*s, L, R, T, M, B, t, true);
  for (const char *s = on; *s; s++) draw_seg(*s, L, R, T, M, B, t, false);
  *oT = T; *oM = M; *oB = B;
}

static int s_Ws, s_Hs;
static float s_PER, s_BND[4];

static float wrap_s(float s) { return s - ffloor(s / s_PER) * s_PER; }
static float sd(float a, float b) { return wrap_s(a - b + s_PER / 2) - s_PER / 2; }
static int cl(int v, int n) { return imin(imax(v, 0), n - 1); }

static float s_at(int x, int y) {
  int ex = imin(x, W - 1 - x), ey = imin(y, s_U - 1 - y);
  float cx = (W - 1) / 2.0f, cy = (s_U - 1) / 2.0f;
  if (ey <= ex && y < cy) return wrap_s(s_BND[3] + cl(x - BAND, s_Ws) + 0.5f);
  if (ex < ey && x > cx) return s_BND[0] + cl(y - BAND, s_Hs) + 0.5f;
  if (ey <= ex) return s_BND[1] + cl(W - 1 - BAND - x, s_Ws) + 0.5f;
  return s_BND[2] + cl(s_U - 1 - BAND - y, s_Hs) + 0.5f;
}

static void side_xy(int i, int k, int d, int *x, int *y) {
  switch (i) {
    case 0: *x = W - 1 - d; *y = BAND + k; break;
    case 1: *x = W - 1 - BAND - k; *y = s_U - 1 - d; break;
    case 2: *x = d; *y = s_U - 1 - BAND - k; break;
    default: *x = BAND + k; *y = d; break;
  }
}

static float idx(float h) { return h / 24.0f * s_PER; }

static float sym_r(float v) {
  v = wrap_s(v);
  float c = v < s_PER / 2 ? s_PER / 4 : 3 * s_PER / 4, d = v - c;
  float m = ffloor(fabsf_(d) + 0.5f - 1e-4f);
  return c + (d > 0 ? m : d < 0 ? -m : 0);
}

static float pos(float h, int w) {
  float s0 = sym_r(idx(h));
  for (int i = 0; i < 4; i++)
    if (fabsf_(sd(s0, s_BND[i])) < w / 2.0f) s0 = s_BND[i] + w / 2.0f;
  return s0;
}

static void notch_at(float s0, int a, int b, uint8_t col, int w) {
  for (int i = 0; i < 4; i++) {
    int len = (i & 1) ? s_Ws : s_Hs;
    for (int k = 0; k < len; k++) {
      if (fabsf_(sd(s_BND[i] + k + 0.5f, s0)) >= w / 2.0f) continue;
      for (int d = a; d <= b; d++) { int x, y; side_xy(i, k, d, &x, &y); px(x, y, col); }
    }
  }
}

typedef struct { float t, e; const FaceEvent *src; } Ev;

static void fmt_hm(char *out, size_t n, time_t t) {
  struct tm *lt = localtime(&t);
  snprintf(out, n, "%02d:%02d", lt->tm_hour, lt->tm_min);
}

static void fmt_dur(char *out, size_t n, int m) {
  if (m < 0) m = 0;
  if (m >= 60) snprintf(out, n, "%dH %02dM", m / 60, m % 60);
  else snprintf(out, n, "%dM", m);
}

static bool is_day(float v, float a, float b) { return a <= b ? v >= a && v < b : v >= a || v < b; }

static int round_i(float v) { return (int)ffloor(v + 0.5f); }

static void tile(int x0, int y0, int x1, int h, int pv, const Icon *ic, int slot, bool center, const char *u) {
  int ix = x0 + 1 + PAD, iy = y0 + 1 + pv, HC = h - 2 - 2 * pv;
  frame(x0, y0, x1, y0 + h - 1, s_fg);
  if (!slot) slot = ic->w;
  icon(ix + (center ? (slot - ic->w) >> 1 : 0), iy + ((HC - ic->h) >> 1), ic, s_fg);
  if (u) text_c(ix + slot + INNER, iy + ((HC - TH) >> 1), u, s_fg);
}

static const Icon *WX_ICONS[9] = {&ICON_CLEAR, &ICON_PARTLY, &ICON_CLOUDY, &ICON_RAIN_L, &ICON_RAIN_H,
                                  &ICON_SNOW_L, &ICON_SNOW_H, &ICON_SLEET, &ICON_UNKNOWN};

static void push(Str *l, int *nl, const Str *x) {
  if (*nl < 3) l[*nl] = *x;
  (*nl)++;
}

static int wrap_words(const Str *s, int n, Str l[3]) {
  int nl = 0, i = 0;
  Str cur = {.n = 0};
  while (i < s->n) {
    while (i < s->n && s->cp[i] == ' ') i++;
    int a = i;
    while (i < s->n && s->cp[i] != ' ') i++;
    while (i - a > n) {
      if (cur.n) { push(l, &nl, &cur); cur.n = 0; }
      Str piece = {.n = n};
      memcpy(piece.cp, s->cp + a, n * sizeof(uint16_t));
      push(l, &nl, &piece);
      a += n;
    }
    int r = i - a;
    if (!r) continue;
    if (cur.n && cur.n + 1 + r > n) { push(l, &nl, &cur); cur.n = 0; }
    if (cur.n) cur.cp[cur.n++] = ' ';
    memcpy(cur.cp + cur.n, s->cp + a, r * sizeof(uint16_t));
    cur.n += r;
  }
  if (cur.n) push(l, &nl, &cur);
  if (nl > 2) {
    nl = 2;
    if (l[1].n >= n) l[1].n = n - 1;
    l[1].cp[l[1].n++] = 0x2026;
  }
  return nl;
}

static int row_y(int iy, int HC, int n, int k) {
  int gg = LINE_GAP;
  while ((HC - TH * n - (n - 1) * gg) % 2) gg++;
  return iy + (HC - TH * n - (n - 1) * gg) / 2 + k * (TH + gg);
}

void face_render(GContext *ctx, const FaceState *st) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  bool light = st->theme == THEME_LIGHT;
  s_bg = light ? C_WHITE : C_BLACK;
  s_fg = light ? C_BLACK : C_WHITE;
  s_ghost = light ? C_LIGHT : C_DARK;
  s_dim = light ? C_DARK : C_LIGHT;
  for (int y = 0; y < H; y++) {
    GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, y);
    s_row[y] = ri.data; s_min[y] = ri.min_x; s_max[y] = ri.max_x;
    for (int x = ri.min_x; x <= ri.max_x; x++) ri.data[x] = s_bg;
  }

  const PhoneData *ph = st->ph;
  const PhoneInfo *pi = &ph->info;
  bool conn = st->connected;
  s_U = imin(st->unobstructed_h, H);
  int U = s_U;
  bool peek = U < H;

  s_Ws = W - 2 * BAND; s_Hs = U - 2 * BAND; s_PER = 2.0f * s_Ws + 2.0f * s_Hs;
  s_BND[0] = s_Ws / 2.0f; s_BND[1] = s_BND[0] + s_Hs; s_BND[2] = 1.5f * s_Ws + s_Hs; s_BND[3] = 1.5f * s_Ws + 2 * s_Hs;

  time_t mid = st->now_t - (st->now.tm_hour * 3600 + st->now.tm_min * 60 + st->now.tm_sec);
  float now = st->now.tm_hour + st->now.tm_min / 60.0f;
  bool have_sun = pi->rise > 0 && pi->set > 0;
  float rise = have_sun ? (pi->rise - mid) / 3600.0f : 0, set = have_sun ? (pi->set - mid) / 3600.0f : 0;
  rise -= 24 * ffloor(rise / 24);
  set -= 24 * ffloor(set / 24);

  float iRise = idx(rise), iSet = idx(set), iNow = idx(now);
  const uint8_t sky[2][2] = {{C_DARK, C_NIGHT}, {C_LIGHT, C_DAY}};
  for (int y = 0; y < U; y++)
    for (int x = 0; x < W; x++) {
      if (y >= BAND && y < U - BAND && x >= BAND && x < W - BAND) { x = W - BAND - 1; continue; }
      float s = s_at(x, y);
      int i = (int)wrap_s(ffloor(s));
      px(x, y, sky[is_day(i, iRise, iSet)][i >= iNow]);
    }

  Ev evs[MAX_EVENTS]; int ne = 0;
  const FaceEvent *all_day = NULL;
  for (int i = 0; i < ph->nev; i++) {
    const FaceEvent *e = &ph->ev[i];
    if (e->all_day) {
      if (!all_day && e->start <= st->now_t && st->now_t < e->end) all_day = e;
      continue;
    }
    float t = (e->start - mid) / 3600.0f, en = (e->end - mid) / 3600.0f;
    if (t >= 24 || en <= 0) continue;
    evs[ne].t = t < 0 ? 0 : t; evs[ne].e = en > 24 ? 24 : en; evs[ne].src = e; ne++;
  }
  const Ev *going = NULL;
  for (int i = 0; i < ne; i++) if (evs[i].src->start <= st->now_t && st->now_t < evs[i].src->end) { going = &evs[i]; break; }
  const FaceEvent *next = NULL;
  for (int i = 0; i < ph->nev && !next; i++)
    if (!ph->ev[i].all_day && ph->ev[i].start > st->now_t && ph->ev[i].start < mid + 2 * 86400) next = &ph->ev[i];

  if (conn && going) {
    float evA = idx(going->t), evB = idx(going->e);
    for (int y = 0; y < U; y++)
      for (int x = 0; x < W; x++) {
        if (y >= BAND && y < U - BAND && x >= BAND && x < W - BAND) { x = W - BAND - 1; continue; }
        float s = s_at(x, y);
        if (s >= evA && s < evB) px(x, y, C_EVENT);
      }
  }
  for (int hh = 0; hh < 24; hh++) {
    notch_at(pos(hh, TICK_W), 0, 0, s_bg, TICK_W);
    notch_at(pos(hh, TICK_W), BAND - 1, BAND - 1, s_bg, TICK_W);
  }
  if (conn)
    for (int i = 0; i < ne; i++)
      if (evs[i].src->start > st->now_t) notch_at(pos(evs[i].t, MARK_W), 0, BAND - 1 + MARK_OVER, C_EVENT, MARK_W);
  notch_at(pos(now, MARK_W), 0, BAND - 1 + MARK_OVER, s_fg, MARK_W);

  int L0 = BAND + RING_GAP, R0 = W - 1 - L0, T0 = L0, B0 = U - 1 - L0;
  int iw = R0 - L0 + 1;
  int dh = 2 * DIG_W - DIG_T;
  int rowC = 2 + imax(13, TH), clkC = 2 + dh + TH, hEv = 2 + 2 * PAD + 3 * TH + 2 * LINE_GAP;
  int pT = PAD, pC = PAD, pB = PAD;
  if (peek) {
    int tot = (B0 - T0 + 1) - 2 * GAP - 2 * rowC - clkC;
    int32_t best = -1;
    for (int a = 0; a <= 30; a++)
      for (int b = 0; b <= 30; b++) {
        int r = tot - 2 * a - 2 * b;
        if (r < 0 || r % 3) continue;
        int c = r / 3, v[7] = {a, a, c, c, c, b, b}, sum = 0;
        for (int k = 0; k < 7; k++) sum += v[k];
        int32_t sc = a != b ? 49 : 0;
        for (int k = 0; k < 7; k++) { int d = 7 * v[k] - sum; sc += 2 * d * d; }
        if (best < 0 || sc < best) { best = sc; pT = a; pC = c; pB = b; }
      }
  }
  int hTop = rowC + 2 * pT, hSmall = rowC + 2 * pB;

  int y = T0;
  int wIco = ICON_NIGHT.w;
  for (int i = 0; i < 9; i++) wIco = imax(wIco, WX_ICONS[i]->w);
  int xC = L0 + 2 + 2 * PAD + wIco + INNER + imax(ink_w_c("-40\xC2\xB0" "C"), ink_w_c("100\xC2\xB0" "F")) + GAP;

  bool wx_ok = conn && pi->have_weather;
  bool night = !is_day(now, rise, set);
  const Icon *wic = &ICON_CLOUDY;
  if (wx_ok) {
    int c = (pi->cond >= 0 && pi->cond < 9) ? pi->cond : 8;
    wic = (c == 0 && night) ? &ICON_NIGHT : WX_ICONS[c];
  }
  tile(L0, y, xC - GAP - 1, hTop, pT, wic, wIco, wic == &ICON_UNKNOWN, wx_ok ? pi->temp : "--");

  int pw = R0 - xC + 1 - 2 - 2 * PAD - ICON_PIN.w - INNER;
  const char *place = pi->place[0] ? pi->place : "--";
  tile(xC, y, R0, hTop, pT, &ICON_PIN, 0, false, place);
  Str sp, stz;
  str_set(&sp, place);
  str_set(&stz, pi->tz);
  int pl = ink_w(&sp);
  if (pl + ADV + ink_w(&stz) > pw && strncmp(pi->tz, "UTC", 3) == 0) str_set(&stz, pi->tz + 3);
  if (pl + ADV + ink_w(&stz) <= pw)
    text(xC + 1 + PAD + ICON_PIN.w + INNER + pl + ADV, y + 1 + pT + ((hTop - 2 - 2 * pT - TH) >> 1), &stz, s_fg);
  y += hTop + GAP;

  int hTime = clkC + 3 * pC + (peek ? 0 : (B0 - T0 + 1) - (2 * hTop + clkC + 3 * pC + hEv + 3 * GAP));
  int extra = hTime - clkC - 3 * pC;
  frame(L0, y, R0, y + hTime - 1, s_fg);
  int gw = 4 * DIG_W + 2 * DIG_GAP + COLON_W, hp = (iw - 2 - gw) / 2, x = L0 + 1 + hp;
  int xs[4] = {x, x + DIG_W + DIG_GAP, x + 2 * DIG_W + DIG_GAP + COLON_W, x + 3 * DIG_W + 2 * DIG_GAP + COLON_W};
  int Y0 = y + 1 + pC;
  int dd[4] = {st->now.tm_hour / 10, st->now.tm_hour % 10, st->now.tm_min / 10, st->now.tm_min % 10};
  int gT = 0, gM = 0, gB = 0;
  for (int i = 0; i < 4; i++) digit(xs[i], Y0, dd[i], DIG_W, DIG_T, dh, &gT, &gM, &gB);
  int cx0 = x + 2 * DIG_W + DIG_GAP + (COLON_W - COLON) / 2;
  int cys[2] = {(gT + gM - 4) >> 1, (gM + gB - 4) >> 1};
  for (int i = 0; i < 2; i++) rect(cx0, cys[i], cx0 + COLON - 1, cys[i] + COLON - 1, s_fg);
  static const char *DOW[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char *MON[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  int l1 = Y0 + dh + pC + extra;
  text_c(L0 + 1 + PAD, l1, DOW[st->now.tm_wday], s_fg);
  char date[16];
  snprintf(date, sizeof date, "%02d %s %d", st->now.tm_mday, MON[st->now.tm_mon], st->now.tm_year + 1900);
  text_rc(R0 - PAD - 1, l1, date, s_fg);
  y += hTime + GAP;

  if (!peek) {
    bool live = going && conn;
    frame(L0, y, R0, y + hEv - 1, live ? C_EVENT : s_fg);
    int ix = L0 + 1 + PAD, iy = y + 1 + PAD, tx = ix + ICON_CAL.w + INNER, HC = 3 * TH + 2 * LINE_GAP;
    int icy = iy + ((HC - ICON_CAL.h) >> 1);
    const FaceEvent *shown = going ? going->src : next && (next->start < mid + 86400 || !all_day) ? next : all_day;
    icon(ix, icy, &ICON_CAL, live ? C_EVENT : s_fg);
    if (!conn || !shown) text_c(tx, row_y(iy, HC, 1, 0), conn ? "NO EVENTS" : "NO DATA", s_dim);
    else {
      char rem[16] = "", span[24];
      if (shown->all_day) snprintf(span, sizeof span, "ALL DAY");
      else {
        fmt_dur(rem, sizeof rem, round_i(((live ? shown->end : shown->start) - st->now_t) / 60.0f));
        char a[8], b[8];
        fmt_hm(a, sizeof a, shown->start);
        fmt_hm(b, sizeof b, shown->end);
        snprintf(span, sizeof span, "%s-%s", a, b);
      }
      Str name, l[3];
      str_set(&name, shown->name);
      int n = wrap_words(&name, (R0 - PAD - tx + 1) / ADV, l);
      for (int k = 0; k < n; k++) text(tx, row_y(iy, HC, n + 1, k), &l[k], s_fg);
      text_c(tx, row_y(iy, HC, n + 1, n), span, s_fg);
      text_rc(R0 - PAD - 1, row_y(iy, HC, n + 1, n), rem, s_fg);
    }
    y += hEv + GAP;
  }

  const Icon *sts[3] = {&ICON_PHONEX, &ICON_BELLX, &ICON_PHONE};
  int slot = 0;
  for (int i = 0; i < 3; i++) slot = imax(slot, sts[i]->w);
  int wS = 2 + 2 * PAD + slot, wB = 2 + 2 * PAD + ICON_BATT.w + INNER + ink_w_c("100%");
  int xS = R0 - wS + 1, xB = xS - GAP - wB;

  char steps[12];
  if (st->steps >= 0) snprintf(steps, sizeof steps, "%d", st->steps); else snprintf(steps, sizeof steps, "--");
  tile(L0, y, xB - GAP - 1, hSmall, pB, &ICON_STEPS, 0, false, steps);

  char batt[8];
  snprintf(batt, sizeof batt, "%d%%", st->battery);
  char bpx[ICON_BATT.w * ICON_BATT.h];
  battery_px(bpx, st->battery, st->charging);
  Icon bat = {ICON_BATT.w, ICON_BATT.h, bpx};
  tile(xB, y, xS - GAP - 1, hSmall, pB, &bat, 0, false, batt);

  tile(xS, y, R0, hSmall, pB, sts[!conn ? 0 : st->quiet ? 1 : 2], slot, true, NULL);

  graphics_release_frame_buffer(ctx, fb);
}
