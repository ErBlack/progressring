import json
import struct
import subprocess
import time
from datetime import datetime, timedelta
from pathlib import Path

root = Path(__file__).resolve().parent.parent
app = json.loads((root / 'build' / 'appinfo.json').read_text())
keys = app['appKeys']
data = json.loads((root / 'tools' / 'shots.json').read_text())
out = root / 'screenshots'
gif = root / 'build' / 'gif'
out.mkdir(exist_ok=True)
gif.mkdir(parents=True, exist_ok=True)


def pebble(*args):
    subprocess.run(['pebble', *args, '--emulator', 'emery'], cwd=root, check=True, stdout=subprocess.DEVNULL)


def at(hm, day=0):
    h, m = map(int, hm.split(':'))
    return int((datetime.now().replace(hour=h, minute=m, second=0, microsecond=0) + timedelta(days=day)).timestamp())


def restart():
    subprocess.run(['pebble', 'kill'], stdout=subprocess.DEVNULL)
    pebble('install')
    time.sleep(10)


def send(s):
    ev = bytes([len(s['events'])])
    for e in s['events']:
        name = e['name'].encode()
        d = e.get('day', 0)
        ev += struct.pack('<iiBB', at(e['start'], d), at(e['end'], d), e.get('all_day', False), len(name)) + name
    pebble('send-app-message', '--app-uuid', app['uuid'],
           '--string', f"{keys['TEMP']}={s['temp']}", f"{keys['PLACE']}={s['place']}", f"{keys['TZ']}={s['tz']}",
           '--int', f"{keys['COND']}={s['cond']}", f"{keys['RISE']}={at(s['rise'])}", f"{keys['SET']}={at(s['set'])}",
           '--bytes', f"{keys['EVENTS']}={ev.hex()}")


def shot(hm, steps, battery, charging, path):
    pebble('emu-battery', '--percent', str(battery), *(['--charging'] if charging else []))
    pebble('emu-steps', str(steps))
    pebble('emu-set-time', hm + ':00')
    time.sleep(2)
    pebble('screenshot', '--no-open', str(path))
    print(path)


g = data['gif']
restart()
send(g)
frames = []
for i in range(g['frames']):
    m = i * g['step_min']
    awake = min(max(m - 6 * 60, 0), 16 * 60) / (16 * 60)
    b0, b1 = g['battery']
    frames.append(gif / f'{i:02d}.png')
    shot(f'{m // 60:02d}:{m % 60:02d}', round(g['steps'] * awake), round(b0 + (b1 - b0) * i / (g['frames'] - 1)), False, frames[-1])
subprocess.run(['magick', '-delay', str(g['delay_ms'] // 10), '-loop', '0', *map(str, frames), str(out / 'emery_0.gif')], check=True)
print(out / 'emery_0.gif')

restart()
for n, s in enumerate(data['shots'], 1):
    send(s)
    shot(s['time'], s['steps'], s['battery'], s.get('charging'), out / f'emery_{n}.png')
