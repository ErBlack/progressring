var AP = require('./airports');

var WX = ['sun', 'partly_cloudy', 'cloudy', 'light_rain', 'heavy_rain', 'light_snow', 'heavy_snow', 'rain_and_snow', 'unknown'];

var queue = [], last = {};
var currentPlace = null, lastWeather = null;
var sending = false;

function pump() {
  if (sending || !queue.length) return;
  sending = true;
  var item = queue.shift();
  Pebble.sendAppMessage(item.dict, function() {
    sending = false;
    pump();
  }, function(e) {
    console.log('send failed: ' + JSON.stringify(e));
    if (!queue.some(function(q) { return q.kind === item.kind; })) queue.push(item);
    setTimeout(function() { sending = false; pump(); }, Math.min(2000 * Math.pow(2, item.tries++), 60000));
  });
}

function send(dict) {
  var kind = Object.keys(dict).sort().join(), json = JSON.stringify(dict);
  if (last[kind] === json) return;
  last[kind] = json;
  queue = queue.filter(function(q) { return q.kind !== kind; });
  queue.push({ dict: dict, kind: kind, tries: 0 });
  pump();
}

function int32(v) {
  return [v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >>> 24) & 255];
}

function shape(props, name, kind) {
  var p = props && props[name];
  return p && p[kind];
}

function onWeather(update) {
  lastWeather = update;
  var list = update.instances || [];
  var here = list.filter(function(i) { return i.instanceId === currentPlace; })[0] || list[0];
  if (!here) return;
  var p = here.properties || {};
  var dict = {};
  var t = shape(p, 'temperature', 'numericValue');
  if (t) dict.TEMP = Math.round(t.value) + (t.unit || '');
  var code = shape(p, 'condition_code', 'longText');
  if (code) dict.COND = WX.indexOf(code.text) < 0 ? 8 : WX.indexOf(code.text);
  var rise = shape(p, 'sunrise', 'timestamp');
  if (rise) dict.RISE = rise.value;
  var set = shape(p, 'sunset', 'timestamp');
  if (set) dict.SET = set.value;
  send(dict);
}

function onPlaces(update) {
  var cur = (update.instances || []).filter(function(i) {
    var c = shape(i.properties, 'current', 'boolean');
    return c && c.value;
  })[0];
  currentPlace = cur ? cur.instanceId : null;
  if (lastWeather) onWeather(lastWeather);
}

function onCalendar(update) {
  var list = update.instances || [];
  var bytes = [list.length];
  list.forEach(function(inst) {
    var p = inst.properties || {};
    var name = shape(p, 'name', 'shortText');
    var start = shape(p, 'starts_at', 'timestamp');
    var end = shape(p, 'ends_at', 'timestamp');
    var allDay = shape(p, 'all_day', 'boolean');
    var raw = unescape(encodeURIComponent(name ? name.text : '')).slice(0, 255);
    bytes = bytes.concat(int32(start ? start.value : 0), int32(end ? end.value : 0), allDay && allDay.value ? 1 : 0, raw.length);
    for (var i = 0; i < raw.length; i++) bytes.push(raw.charCodeAt(i));
  });
  send({ EVENTS: bytes });
}

function subscribe(category, item, properties, onData) {
  if (typeof Pebble.subscribeToSource !== 'function') {
    console.log('plugins unavailable');
    return;
  }
  Pebble.subscribeToSource({
    category: category,
    item: item,
    properties: properties,
    onData: onData,
    onError: function(err) { console.log(category + ' unavailable: ' + err.code + ' ' + err.message); }
  });
}

function nearestAirport(lat, lon) {
  var best = -1, bestD = Infinity, k = Math.cos(lat * Math.PI / 180);
  for (var i = 0; i < AP.lat.length; i++) {
    var dy = AP.lat[i] / 100 - lat, dx = (AP.lon[i] / 100 - lon) * k;
    if (dx > 180 * k) dx -= 360 * k;
    if (dx < -180 * k) dx += 360 * k;
    var d = dx * dx + dy * dy;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best < 0 ? '' : AP.codes.substr(best * 3, 3);
}

function locate() {
  navigator.geolocation.getCurrentPosition(function(pos) {
    send({ PLACE: nearestAirport(pos.coords.latitude, pos.coords.longitude) });
  }, function(err) {
    console.log('location error: ' + err.code + ' ' + err.message);
  }, { timeout: 15000, maximumAge: 600000 });
}

function tzText() {
  var m = -new Date().getTimezoneOffset();
  var sign = m < 0 ? '-' : '+';
  m = Math.abs(m);
  var h = Math.floor(m / 60), r = m % 60;
  return 'UTC' + sign + h + (r ? ':' + (r < 10 ? '0' : '') + r : '');
}

Pebble.addEventListener('ready', function() {
  send({ TZ: tzText() });
  subscribe('preferences', 'weather_location', ['current'], onPlaces);
  subscribe('weather', 'location', ['temperature', 'condition_code', 'sunrise', 'sunset'], onWeather);
  subscribe('calendar', 'event', ['name', 'starts_at', 'ends_at', 'all_day'], onCalendar);
  locate();
  setInterval(function() { locate(); send({ TZ: tzText() }); }, 30 * 60 * 1000);
});
