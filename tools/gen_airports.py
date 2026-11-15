import csv
import io
import urllib.request
from pathlib import Path

URL = 'https://davidmegginson.github.io/ourairports-data/airports.csv'
out = Path(__file__).resolve().parent.parent / 'src' / 'pkjs' / 'airports.js'
codes, lats, lons = [], [], []
with urllib.request.urlopen(URL) as f:
    for r in csv.DictReader(io.TextIOWrapper(f, encoding='utf-8')):
        if r['type'] in ('large_airport', 'medium_airport') and r['scheduled_service'] == 'yes' and len(r['iata_code']) == 3:
            codes.append(r['iata_code'])
            lats.append(round(float(r['latitude_deg']) * 100))
            lons.append(round(float(r['longitude_deg']) * 100))
out.write_text('module.exports={codes:"' + ''.join(codes) + '",lat:[' + ','.join(map(str, lats)) + '],lon:[' + ','.join(map(str, lons)) + ']};\n', encoding='utf-8')
print(len(codes), 'airports,', out.stat().st_size, 'bytes')
