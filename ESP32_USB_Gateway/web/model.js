(function (root) {
  'use strict';
  const metrics = {
    thrust: { label: 'Thrust', unit: 'kgf', color: '#ff8958', digits: 2 },
    rpm: { label: 'RPM', unit: 'rpm', color: '#8bbaff', digits: 0 },
    voltage: { label: 'Voltage', unit: 'V', color: '#c4a0ff', digits: 2 },
    current: { label: 'Current', unit: 'A', color: '#79d6ae', digits: 2 },
    power: { label: 'Power', unit: 'W', color: '#79d5e2', digits: 1 },
    throttle: { label: 'Throttle', unit: '%', color: '#e2c37a', digits: 1 }
  };
  const finite = value => typeof value === 'number' && Number.isFinite(value);
  const format = (value, digits = 2) => finite(value) ? value.toLocaleString('en-US', { minimumFractionDigits: digits, maximumFractionDigits: digits }) : '—';
  const duration = seconds => {
    if (!finite(seconds) || seconds < 0) return '—';
    const minutes = Math.floor(seconds / 60);
    return `${String(minutes).padStart(2, '0')}:${(seconds % 60).toFixed(1).padStart(4, '0')}`;
  };
  const bytes = value => !finite(value) ? '—' : value < 1024 ? `${value} B` : value < 1048576 ? `${(value / 1024).toFixed(1)} KB` : `${(value / 1048576).toFixed(2)} MB`;
  const escape = value => String(value ?? '').replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]));
  function parseCsv(text) {
    const samples = [];
    let headers = null, offset = 0, lastTime = -1, skipped = 0;
    for (const line of text.replace(/^\uFEFF/, '').split(/\r?\n/)) {
      if (!line.trim()) continue;
      const cells = line.split(',').map(value => value.trim());
      if (cells.some(value => /^(Time|gateway_time_s|nano_time_s)$/i.test(value))) {
        headers = cells.map(value => value.toLowerCase());
        continue;
      }
      if (!headers || cells.length !== headers.length) { skipped++; continue; }
      const row = Object.fromEntries(headers.map((key, index) => [key, cells[index] === '' ? null : Number(cells[index])]));
      const first = (...keys) => { for (const key of keys) if (finite(row[key])) return row[key]; return null; };
      const time = first('gateway_time_s', 'time');
      const thrust = first('thrust');
      if (!finite(time) || !finite(thrust)) { skipped++; continue; }
      if (time + offset < lastTime) offset = lastTime + 0.1 - time;
      const timeline = time + offset;
      lastTime = timeline;
      const voltage = first('voltage_v', 'vbat');
      const current = first('current_a', 'current');
      samples.push({ seq: samples.length + 1, gateway_ms: timeline * 1000,
        time_s: first('nano_time_s', 'time'), thrust,
        throttle: first('throttle_percent', 'throttle'), voltage, current,
        rpm: first('rpm'), power: first('power_w') ?? (finite(voltage) && finite(current) ? voltage * current : null) });
    }
    return { samples, skipped };
  }
  function csv(samples) {
    return 'gateway_time_s,nano_time_s,thrust,throttle_percent,voltage_V,current_A,rpm,power_W\n' + samples.map(sample =>
      [sample.gateway_ms / 1000, sample.time_s, sample.thrust, sample.throttle, sample.voltage, sample.current, sample.rpm, sample.power]
        .map(value => finite(value) ? String(value) : '').join(',')).join('\n') + '\n';
  }
  function windowed(samples, seconds) {
    if (!samples.length || !seconds) return samples;
    const cutoff = samples[samples.length - 1].gateway_ms - seconds * 1000;
    return samples.filter(sample => sample.gateway_ms >= cutoff);
  }
  const api = { metrics, finite, format, duration, bytes, escape, parseCsv, csv, windowed };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  root.RigModel = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
