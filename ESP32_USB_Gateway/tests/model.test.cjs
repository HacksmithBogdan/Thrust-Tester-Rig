const test = require('node:test');
const assert = require('node:assert/strict');
const model = require('../web/model.js');

test('imports original logs with repeated headers and restart timestamps', () => {
  const input = 'Time, Thrust, Throttle, Vbat, Current, RPM\n1.0,2.3,50,48,100,16000\nTime, Thrust, Throttle, Vbat, Current, RPM\n0.1,3.4,55,47,110,17000\n';
  const result = model.parseCsv(input);
  assert.equal(result.samples.length, 2);
  assert.equal(result.samples[0].power, 4800);
  assert.equal(result.samples[1].time_s, 0.1);
  assert.ok(result.samples[1].gateway_ms > result.samples[0].gateway_ms);
});
test('round-trips gateway CSV while preserving missing sensor values', () => {
  const sample = { gateway_ms: 1500, time_s: 1.2, thrust: -2, throttle: null, voltage: 48.25, current: 100, rpm: null, power: 4825 };
  const result = model.parseCsv(model.csv([sample]));
  assert.equal(result.samples[0].thrust, -2);
  assert.equal(result.samples[0].rpm, null);
  assert.equal(result.samples[0].throttle, null);
  assert.equal(result.samples[0].gateway_ms, 1500);
});
test('rejects invalid and unrecognized rows rather than fabricating zero readings', () => {
  const result = model.parseCsv('Time,Thrust,Throttle,Vbat,Current,RPM\n0,nan,0,0,0,0\nfoo\n1,2,,48,,\n');
  assert.equal(result.samples.length, 1);
  assert.equal(result.skipped, 2);
  assert.equal(result.samples[0].current, null);
  assert.equal(result.samples[0].power, null);
  assert.equal(model.format(null), '—');
});
test('handles BOM, CRLF and whitespace from SD exports', () => {
  const result = model.parseCsv('\uFEFFTime, Thrust, Throttle, Vbat, Current, RPM\r\n 0.1, 1.2, 40, 24, 12, 5000\r\n');
  assert.equal(result.samples.length, 1);
  assert.equal(result.samples[0].voltage, 24);
});
test('escapes imported run names and notes for HTML', () => {
  assert.equal(model.escape('<img src=x onerror="x">&'), '&lt;img src=x onerror=&quot;x&quot;&gt;&amp;');
});
test('bounds chart history by gateway time and supports complete-run view', () => {
  const samples = [{ gateway_ms: 0 }, { gateway_ms: 5000 }, { gateway_ms: 10000 }];
  assert.equal(model.windowed(samples, 6).length, 2);
  assert.equal(model.windowed(samples, 0).length, 3);
});
