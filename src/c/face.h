#pragma once
#include <pebble.h>

#define MAX_EVENTS 10
#define NAME_LEN 128

enum { THEME_DARK, THEME_LIGHT };
enum { BATT_WATCH, BATT_PHONE };

typedef struct {
  int32_t start;
  int32_t end;
  bool all_day;
  char name[NAME_LEN];
} FaceEvent;

typedef struct {
  bool have_weather;
  char temp[12];
  int8_t cond;
  int32_t rise;
  int32_t set;
  char place[8];
  char tz[12];
} PhoneInfo;

typedef struct {
  PhoneInfo info;
  uint8_t nev;
  FaceEvent ev[MAX_EVENTS];
} PhoneData;

typedef struct {
  time_t now_t;
  struct tm now;
  int steps;
  int battery;
  bool charging;
  bool connected;
  bool quiet;
  int unobstructed_h;
  uint8_t theme;
  bool week;
  uint8_t batt_src;
  int phone_batt;
  const PhoneData *ph;
} FaceState;

void face_render(GContext *ctx, const FaceState *st);
