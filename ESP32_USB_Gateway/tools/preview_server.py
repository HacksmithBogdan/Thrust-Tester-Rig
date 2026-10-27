"""Local UI verification server. All USB data here is explicitly simulated.

Run: python tools/preview_server.py --port 8765
The embedded production firmware never generates simulated measurements.
"""
import argparse
import json
import math
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

WEB = Path(__file__).resolve().parents[1] / 'web'
LOCK = threading.Lock()
START = time.monotonic()
SETTINGS = dict(ssid='ThrustTester', ip='192.168.4.1', baud=9600, unit='kgf')
CAPS = ['tare', 'calibrate', 'set_config', 'get_config', 'save_config']
MODE = 'legacy_debug'
CONFIG = dict(calibration_factor=30310, poles=20, shunt_ohms=0.000192, divider_ratio=34.13, persisted=True)
COMMAND = dict(state='idle', message='', request_id=0)
SAMPLES = []
RUNS = {}
RECORDING = None
CONSOLE = []
LAST_TICK = 0


def measurement(index):
    t = index / 10
    throttle = 53 + 29 * math.sin(t / 9)
    voltage = 47.4 - throttle * 0.017
    current = 9 + throttle * 1.21 + math.sin(t * 3) * 0.4
    return dict(seq=index + 1, gateway_ms=t * 1000, time_s=t,
                thrust=0.8 + (throttle / 20) ** 1.7 + math.sin(t * 5) * 0.035,
                throttle=throttle, voltage=voltage, current=current,
                rpm=6000 + throttle * 180, power=voltage * current)


def tick():
    global LAST_TICK
    target = int((time.monotonic() - START) * 10) + 600
    if MODE in ('disconnected', 'stale'):
        LAST_TICK = target
        return
    for index in range(max(LAST_TICK, target - 600), target):
        sample = measurement(index)
        SAMPLES.append(sample)
        CONSOLE.append(dict(seq=index + 1, text=(f"Time: {sample['time_s']:.2f}\tThrust: {sample['thrust']:.2f}\tThrottle: {sample['throttle']:.1f}\tVBat: {sample['voltage']:.2f}\tI: {sample['current']:.2f}\tRPM: {sample['rpm']:.1f}\tTrim: 0\tTrainerSW: 1500\tnPoles: 20")))
        if RECORDING:
            RECORDING['data'].append(sample.copy())
            RECORDING['samples'] = len(RECORDING['data'])
    del SAMPLES[:-600]
    del CONSOLE[:-40]
    LAST_TICK = target


def csv_text(samples):
    header = 'gateway_time_s,nano_time_s,thrust,throttle_percent,voltage_V,current_A,rpm,power_W\n'
    return header + ''.join(','.join(str(value) if value is not None else '' for value in
                           [s['gateway_ms'] / 1000, s['time_s'], s['thrust'], s['throttle'], s['voltage'], s['current'], s['rpm'], s['power']]) + '\n' for s in samples)


def metadata(run):
    return {**{key: value for key, value in run.items() if key != 'data'}, 'bytes': len(csv_text(run['data']).encode())}


def seed():
    samples = [measurement(index) for index in range(600)]
    RUNS['run-demo-0001'] = dict(id='run-demo-0001', name='Demo · 120 mm EDF baseline', note='Simulated measurements for interface verification. Not a real test.',
                               status='complete', thrust_unit='kgf', source='legacy_debug', started_epoch_ms=1792771200000, started_gateway_ms=0, samples=len(samples), data=samples)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def send(self, data, code=200, content_type='application/json', filename=None):
        raw = json.dumps(data, allow_nan=False).encode() if content_type == 'application/json' else data.encode() if isinstance(data, str) else data
        self.send_response(code)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(raw)))
        self.send_header('Cache-Control', 'no-store')
        if filename:
            self.send_header('Content-Disposition', f'attachment; filename="{filename}"')
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        global MODE
        url = urlparse(self.path)
        params = parse_qs(url.query)
        if not url.path.startswith('/api/'):
            name = 'index.html' if url.path == '/' else url.path.lstrip('/')
            if name not in ('index.html', 'styles.css', 'app.js', 'model.js'):
                return self.send(dict(error='Not found'), 404)
            types = {'html': 'text/html', 'css': 'text/css', 'js': 'text/javascript'}
            return self.send((WEB / name).read_bytes(), content_type=types[name.rsplit('.', 1)[1]])
        with LOCK:
            tick()
            if url.path == '/api/demo':
                MODE = params.get('mode', [MODE])[0]
                return self.send(dict(mode=MODE))
            if url.path == '/api/status':
                active = RECORDING is not None
                return self.send(dict(demo=True, firmware='1.0.0-preview', uptime_ms=(time.monotonic() - START) * 1000 + 60000,
                                      mode=MODE, usb_connected=MODE != 'disconnected', usb_status='Simulated USB device', vid=0x1A86, pid=0x7523,
                                      sample_age_ms=12000 if MODE == 'stale' else 50, live=MODE not in ('disconnected', 'stale'),
                                      sample=SAMPLES[-1] if SAMPLES else None, last_error='', malformed=0, dropped=0,
                                      capabilities=CAPS if MODE == 'protocol_v1' else [], nano_config=CONFIG if MODE == 'protocol_v1' else {},
                                      settings=SETTINGS, storage=dict(ready=True, total=2031616, used=sum(metadata(r)['bytes'] for r in RUNS.values())),
                                      recording=dict(active=active, id=RECORDING['id'] if active else '', samples=RECORDING['samples'] if active else 0,
                                                     duration_s=time.monotonic() - RECORDING['started_monotonic'] if active else 0), command=COMMAND))
            if url.path == '/api/samples':
                after = int(params.get('after', ['0'])[0])
                selected = [s for s in SAMPLES if s['seq'] > after][:64]
                return self.send(dict(samples=selected, next_seq=selected[-1]['seq'] if selected else after, gap=bool(SAMPLES and after and after + 1 < SAMPLES[0]['seq'])))
            if url.path == '/api/console':
                after = int(params.get('after', ['0'])[0])
                return self.send(dict(lines=[line for line in CONSOLE if line['seq'] > after]))
            if url.path == '/api/runs':
                return self.send(dict(runs=[metadata(run) for run in RUNS.values()]))
            if url.path == '/api/download':
                run = RUNS.get(params.get('id', [''])[0])
                if not run:
                    return self.send(dict(error='Run not found'), 404)
                if run['status'] == 'recording':
                    return self.send(dict(error='Stop recording first'), 409)
                if params.get('format') == ['json']:
                    return self.send(metadata(run), filename=run['id'] + '.json')
                return self.send(csv_text(run['data']), content_type='text/csv', filename=run['id'] + '.csv')
            self.send(dict(error='Unknown endpoint'), 404)

    def do_POST(self):
        global RECORDING
        try:
            data = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))))
        except (ValueError, TypeError):
            return self.send(dict(error='Expected JSON'), 400)
        with LOCK:
            tick()
            if self.path == '/api/record':
                if data.get('action') == 'start':
                    if RECORDING or MODE in ('disconnected', 'stale'):
                        return self.send(dict(error='A live stream is required and only one run can record.'), 409)
                    key = f'run-preview-{int(time.time() * 1000)}'
                    RECORDING = dict(id=key, name=data.get('name', 'Preview run'), note=data.get('note', ''), status='recording', thrust_unit=SETTINGS['unit'],
                                     source=MODE, started_epoch_ms=data.get('epoch_ms', 0), started_gateway_ms=SAMPLES[-1]['gateway_ms'], samples=0, data=[], started_monotonic=time.monotonic())
                    RUNS[key] = RECORDING
                elif data.get('action') == 'stop':
                    if RECORDING:
                        RECORDING['status'] = 'complete'
                        RECORDING = None
                else:
                    return self.send(dict(error='Unknown recording action'), 400)
                return self.send(dict(ok=True))
            if self.path == '/api/delete':
                run = RUNS.get(data.get('id'))
                if not run:
                    return self.send(dict(error='Run not found'), 404)
                if run is RECORDING:
                    return self.send(dict(error='Stop recording first'), 409)
                del RUNS[data['id']]
                return self.send(dict(ok=True))
            if self.path == '/api/command':
                if MODE != 'protocol_v1' or data.get('command') not in CAPS or RECORDING:
                    return self.send(dict(error='Command unavailable in this mode'), 409)
                if data['command'] == 'set_config':
                    CONFIG.update(data.get('args', {})); CONFIG['persisted'] = False
                if data['command'] == 'save_config':
                    CONFIG['persisted'] = True
                COMMAND.update(state='success', message=f"Nano acknowledged {data['command']} (simulated)", request_id=0)
                return self.send(dict(request_id=1), 202)
            if self.path == '/api/settings':
                SETTINGS.update({key: value for key, value in data.items() if key != 'password'})
                return self.send(dict(ok=True, restarting=True, ip=SETTINGS['ip']))
            self.send(dict(error='Unknown endpoint'), 404)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    seed()
    print(f'Simulated dashboard preview: http://127.0.0.1:{args.port}', flush=True)
    ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
