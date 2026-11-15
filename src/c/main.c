#include <pebble.h>
#include "face.h"

enum { P_VER = 1, P_INFO = 2, P_NEV = 3, P_EV0 = 10 };
#define DATA_VER 3

static Window *s_win;
static Layer *s_layer;
static PhoneData s_ph, s_old;

static void save(void) {
  if (memcmp(&s_old.info, &s_ph.info, sizeof s_ph.info)) persist_write_data(P_INFO, &s_ph.info, sizeof s_ph.info);
  if (s_old.nev != s_ph.nev) persist_write_int(P_NEV, s_ph.nev);
  for (int i = 0; i < MAX_EVENTS; i++) {
    if (i >= s_ph.nev) { if (i < s_old.nev) persist_delete(P_EV0 + i); }
    else if (i >= s_old.nev || memcmp(&s_old.ev[i], &s_ph.ev[i], sizeof(FaceEvent))) persist_write_data(P_EV0 + i, &s_ph.ev[i], sizeof(FaceEvent));
  }
}

static void load(void) {
  memset(&s_ph, 0, sizeof s_ph);
  s_ph.info.cond = 8;
  if (persist_read_int(P_VER) != DATA_VER) {
    persist_write_int(P_VER, DATA_VER);
    persist_write_data(P_INFO, &s_ph.info, sizeof s_ph.info);
    persist_write_int(P_NEV, 0);
    return;
  }
  persist_read_data(P_INFO, &s_ph.info, sizeof s_ph.info);
  int n = persist_read_int(P_NEV);
  s_ph.nev = n < 0 ? 0 : n > MAX_EVENTS ? MAX_EVENTS : n;
  for (int i = 0; i < s_ph.nev; i++) persist_read_data(P_EV0 + i, &s_ph.ev[i], sizeof(FaceEvent));
}

static void copy_str(char *dst, size_t n, const Tuple *t) {
  if (t && t->type == TUPLE_CSTRING) snprintf(dst, n, "%s", t->value->cstring);
}

static void copy_name(char *dst, const uint8_t *src, int len) {
  if (len >= NAME_LEN) {
    len = NAME_LEN - 1;
    while (len > 0 && (src[len] & 0xC0) == 0x80) len--;
  }
  memcpy(dst, src, len);
  dst[len] = 0;
}

static void inbox(DictionaryIterator *it, void *ctx) {
  Tuple *t;
  memcpy(&s_old, &s_ph, sizeof s_ph);
  if ((t = dict_find(it, MESSAGE_KEY_TEMP))) { copy_str(s_ph.info.temp, sizeof s_ph.info.temp, t); s_ph.info.have_weather = true; }
  if ((t = dict_find(it, MESSAGE_KEY_COND))) s_ph.info.cond = t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_RISE))) s_ph.info.rise = t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_SET))) s_ph.info.set = t->value->int32;
  if ((t = dict_find(it, MESSAGE_KEY_PLACE))) copy_str(s_ph.info.place, sizeof s_ph.info.place, t);
  if ((t = dict_find(it, MESSAGE_KEY_TZ))) copy_str(s_ph.info.tz, sizeof s_ph.info.tz, t);
  if ((t = dict_find(it, MESSAGE_KEY_EVENTS)) && t->type == TUPLE_BYTE_ARRAY && t->length) {
    const uint8_t *d = t->value->data, *p = d + 1, *end = d + t->length;
    int n = 0, total = *d;
    while (n < total && n < MAX_EVENTS && end - p >= 10 && end - p >= 10 + p[9]) {
      FaceEvent *e = &s_ph.ev[n++];
      memcpy(&e->start, p, 4);
      memcpy(&e->end, p + 4, 4);
      e->all_day = p[8];
      copy_name(e->name, p + 10, p[9]);
      p += 10 + p[9];
    }
    s_ph.nev = n;
  }
  if (memcmp(&s_old, &s_ph, sizeof s_ph)) { save(); layer_mark_dirty(s_layer); }
}

static void update(Layer *layer, GContext *ctx) {
  FaceState st;
  st.now_t = time(NULL);
  st.now = *localtime(&st.now_t);
  st.steps = -1;
#if defined(PBL_HEALTH)
  time_t day = time_start_of_today();
  if (health_service_metric_accessible(HealthMetricStepCount, day, st.now_t) & HealthServiceAccessibilityMaskAvailable)
    st.steps = (int)health_service_sum_today(HealthMetricStepCount);
#endif
  BatteryChargeState b = battery_state_service_peek();
  st.battery = b.charge_percent;
  st.charging = b.is_charging;
  st.connected = connection_service_peek_pebble_app_connection();
  st.quiet = quiet_time_is_active();
  st.unobstructed_h = layer_get_unobstructed_bounds(layer).size.h;
  st.ph = &s_ph;
  face_render(ctx, &st);
}

static void on_tick(struct tm *t, TimeUnits u) { layer_mark_dirty(s_layer); }
static void on_battery(BatteryChargeState s) { layer_mark_dirty(s_layer); }
static void on_conn(bool c) { layer_mark_dirty(s_layer); }
static void on_area(void *ctx) { layer_mark_dirty(s_layer); }
#if defined(PBL_HEALTH)
static void on_health(HealthEventType e, void *ctx) {
  if (e == HealthEventMovementUpdate || e == HealthEventSignificantUpdate) layer_mark_dirty(s_layer);
}
#endif

static void win_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update);
  layer_add_child(root, s_layer);
}

static void win_unload(Window *w) { layer_destroy(s_layer); }

static void init(void) {
  load();
  s_win = window_create();
  window_set_window_handlers(s_win, (WindowHandlers){.load = win_load, .unload = win_unload});
  window_stack_push(s_win, false);
  tick_timer_service_subscribe(MINUTE_UNIT, on_tick);
  battery_state_service_subscribe(on_battery);
  connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler = on_conn});
  unobstructed_area_service_subscribe((UnobstructedAreaHandlers){.did_change = on_area}, NULL);
#if defined(PBL_HEALTH)
  health_service_events_subscribe(on_health, NULL);
#endif
  app_message_register_inbox_received(inbox);
  app_message_open(app_message_inbox_size_maximum(), 0);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  unobstructed_area_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(s_win);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
