/* Entire dashboard is served by the gateway. No CDN or internet dependencies. */
(() => {
  'use strict';
  const M = window.RigModel, $ = selector => document.querySelector(selector);
  const state = { status: null, samples: [], after: 0, consoleAfter: 0, consoleLines: [], metric: 'thrust', saved: null, page: 'live', initialized: false, runs: [], gatewayUptime: 0, lastError: '', configText: '', currentCommand: '', toastTimer: null };
  async function api(path, data) {
    const response = await fetch(path, data === undefined ? { cache: 'no-store', signal: AbortSignal.timeout(6000) } : {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(data), signal: AbortSignal.timeout(6000)
    });
    const body = await response.json();
    if (!response.ok) throw new Error(body.error || `Request failed (${response.status})`);
    return body;
  }
  function toast(message, error = false) {
    $('#toast').textContent = message;
    $('#toast').classList.toggle('error', error);
    $('#toast').hidden = false;
    clearTimeout(state.toastTimer);
    state.toastTimer = setTimeout(() => { $('#toast').hidden = true; }, error ? 6500 : 4000);
  }
  function setPage(page) {
    if (!['live', 'runs', 'calibration', 'device'].includes(page)) page = 'live';
    const changed = state.page !== page;
    state.page = page;
    document.querySelectorAll('.page').forEach(item => item.classList.toggle('active', item.id === `page-${page}`));
    document.querySelectorAll('.nav-item').forEach(item => item.classList.toggle('active', item.dataset.page === page));
    history.replaceState(null, '', `#${page}`);
    if (changed) window.scrollTo(0, 0);
    if (page === 'runs') refreshRuns().catch(error => toast(error.message, true));
    if (page === 'live') drawChart();
  }
  document.querySelectorAll('[data-page]').forEach(button => button.addEventListener('click', () => setPage(button.dataset.page)));
  window.addEventListener('hashchange', () => setPage(location.hash.slice(1)));
  document.querySelectorAll('[data-close]').forEach(button => button.addEventListener('click', () => button.closest('dialog').close()));
  let confirmCallback = null;
  function confirmAction(title, text, callback) {
    $('#confirmTitle').textContent = title;
    $('#confirmText').textContent = text;
    confirmCallback = callback;
    $('#confirmDialog').showModal();
  }
  $('#confirmAction').addEventListener('click', async () => {
    $('#confirmDialog').close();
    try { await confirmCallback?.(); } catch (error) { toast(error.message, true); }
  });
  function renderStatus(status) {
    state.status = status;
    $('#demoBadge').hidden = !status.demo;
    const pill = $('#connectionPill');
    pill.classList.toggle('live', status.live);
    pill.innerHTML = `<i></i>${status.live ? 'USB streaming' : status.usb_connected ? 'USB · waiting for data' : 'USB disconnected'}`;
    const rate = M.finite(status.sample_rate_hz) ? ` · ${status.sample_rate_hz.toFixed(1)} samples/s` : '';
    $('#streamStatus').innerHTML = `<i class="signal-dot" style="background:${status.live ? 'var(--green)' : '#697583'}"></i>${status.live ? (status.mode === 'protocol_v1' ? 'Live · protocol v1' : 'Live · legacy stream') + rate : 'Waiting for measurements'}`;
    const settings = status.settings;
    $('#networkName').textContent = settings.ssid;
    $('#networkAddress').textContent = settings.ip;
    $('#thrustUnit').textContent = settings.unit;
    document.querySelectorAll('.unit-label').forEach(item => { item.textContent = settings.unit; });
    const isSaved = Boolean(state.saved);
    if (isSaved) $('#streamStatus').innerHTML = '<i class="signal-dot" style="background:#8bbaff"></i> Saved trace · historical measurements';
    const sample = isSaved ? state.saved.samples.at(-1) : status.sample;
    const unit = isSaved ? state.saved.unit || settings.unit : settings.unit;
    $('#thrustUnit').textContent = unit;
    for (const [key, metric] of Object.entries(M.metrics)) $('#value-' + key).textContent = M.format(sample?.[key], metric.digits);
    $('.metrics').classList.toggle('stale', !isSaved && !status.live);
    $('#calLive').textContent = `${M.format(status.sample?.thrust)} ${settings.unit}`;
    $('#throttleHint').textContent = M.finite(sample?.throttle) && (sample.throttle < 0 || sample.throttle > 100) ? 'Check PWM' : '';
    $('#sampleClock').textContent = M.duration(sample?.time_s);
    const samples = isSaved ? state.saved.samples : state.samples;
    const peak = samples.reduce((value, item) => M.finite(item.thrust) ? Math.max(value, item.thrust) : value, -Infinity);
    $('#peakThrust').textContent = `Peak ${M.format(peak)}`;
    const recording = status.recording;
    $('#recordStatus').textContent = recording.active ? `● REC ${M.duration(recording.duration_s)} / ${recording.samples} SAMPLES` : 'NO ACTIVE RECORDING';
    $('#recordStatus').classList.toggle('recording', recording.active);
    const recordButton = $('#recordButton');
    recordButton.innerHTML = `<span class="record-icon"></span>${recording.active ? 'Stop recording' : 'Start recording'}`;
    recordButton.classList.toggle('danger', recording.active);
    recordButton.disabled = !recording.active && (!status.live || !status.storage.ready);
    $('#storagePercent').textContent = status.storage.ready ? `${Math.round(status.storage.used / status.storage.total * 100)}%` : 'OFFLINE';
    $('#storageBar').style.width = status.storage.ready ? `${Math.min(100, status.storage.used / status.storage.total * 100)}%` : '0%';
    $('#storageText').textContent = `${M.bytes(status.storage.used)} / ${M.bytes(status.storage.total)}`;
    $('#diagUsb').textContent = status.usb_status;
    $('#diagVid').textContent = status.vid ? `${status.vid.toString(16).padStart(4, '0')} : ${status.pid.toString(16).padStart(4, '0')}` : '—';
    $('#diagMode').textContent = status.mode.replaceAll('_', ' ');
    $('#diagAge').textContent = status.sample_age_ms < 0 ? 'No sample received' : `${(status.sample_age_ms / 1000).toFixed(1)} s ago`;
    $('#diagErrors').textContent = `${status.malformed} / ${status.dropped}`;
    $('#diagRate').textContent = M.finite(status.sample_rate_hz) ? `${status.sample_rate_hz.toFixed(1)} samples/s` : '—';
    $('#diagGap').textContent = M.finite(status.max_sample_gap_ms) ? `${status.max_sample_gap_ms.toFixed(0)} ms` : '—';
    $('#diagNanoGaps').textContent = status.nano_sequence_gaps ?? 0;
    $('#diagSd').textContent = status.nano_config?.sd_ok ? status.nano_config.sd_file : 'Unavailable';
    $('#diagUptime').textContent = M.duration(status.uptime_ms / 1000);
    $('#diagStorage').textContent = status.storage.ready ? `${M.bytes(status.storage.used)} used` : 'Unavailable';
    if (!state.initialized) {
      for (const name of ['ssid', 'ip', 'baud', 'unit']) $('#settingsForm').elements[name].value = settings[name];
      state.initialized = true;
    }
    const notice = $('#connectionNotice');
    let noticeText = '';
    if (!status.usb_connected) noticeText = status.usb_status + '. Open Device & connection for hookup checks.';
    else if (!status.live) noticeText = 'USB is connected, but no fresh measurements are arriving. Check Nano serial speed in Device & connection (115200 for the updated Nano), and check the load-cell connection.';
    else if (status.mode !== 'protocol_v1') noticeText = 'Legacy Nano stream connected. Monitoring and gateway recording are ready. Update the Nano firmware to enable calibration commands.';
    notice.hidden = !noticeText;
    notice.textContent = noticeText;
    const compatible = status.mode === 'protocol_v1';
    $('#calibrationNotice').textContent = !compatible ? 'Nano update required: the existing firmware only sends readings. Calibration controls enable when the Nano advertises support for USB commands.' : recording.active ? 'Stop recording before changing calibration or saving settings.' : 'USB command protocol connected. Wait for each acknowledgement before continuing. EEPROM is written only when you choose Save.';
    $('#calibrationNotice').classList.toggle('good', compatible && !recording.active);
    document.querySelectorAll('[data-capability]').forEach(button => {
      button.disabled = !status.usb_connected || !status.capabilities.includes(button.dataset.capability) || recording.active || status.command.state === 'pending';
    });
    const configText = JSON.stringify(status.nano_config);
    if (configText !== state.configText && status.nano_config?.calibration_factor !== undefined) {
      for (const name of ['calibration_factor', 'poles', 'shunt_ohms', 'divider_ratio']) {
        if (M.finite(status.nano_config[name])) $('#nanoConfigForm').elements[name].value = status.nano_config[name];
      }
      state.configText = configText;
      $('#nanoConfigState').textContent = status.nano_config.persisted ? 'SAVED TO EEPROM' : 'READ FROM NANO';
      $('#nanoConfigState').classList.add('good');
    }
    $('#commandResult').textContent = status.command.message || 'No command sent.';
    if (state.currentCommand === 'pending' && status.command.state !== 'pending') toast(status.command.message, status.command.state !== 'success');
    state.currentCommand = status.command.state;
    if (status.last_error && status.last_error !== state.lastError) toast(status.last_error, true);
    state.lastError = status.last_error;
    $('#returnLive').hidden = !isSaved;
    $('#chartTitle').textContent = isSaved ? state.saved.name : 'Live measurements';
    $('#traceSource').textContent = isSaved ? 'SAVED TRACE' : 'USB SERIAL';
  }
  async function poll() {
    try {
      let [status, data] = await Promise.all([api('/api/status'), api(`/api/samples?after=${state.after}`)]);
      if (status.uptime_ms < state.gatewayUptime) {
        state.after = 0; state.consoleAfter = 0; state.samples = []; state.consoleLines = []; state.configText = '';
        data = await api('/api/samples?after=0');
      }
      state.gatewayUptime = status.uptime_ms;
      state.after = data.next_seq;
      state.samples.push(...data.samples);
      if (state.samples.length > 60000) state.samples.splice(0, state.samples.length - 60000);
      if (data.gap) toast('Some live history was missed. Gateway recordings are independent of the browser.');
      renderStatus(status);
      if (state.page === 'live') drawChart();
      if (state.page === 'device') {
        const serial = await api(`/api/console?after=${state.consoleAfter}`);
        for (const line of serial.lines) { state.consoleAfter = line.seq; state.consoleLines.push(line.text); }
        state.consoleLines = state.consoleLines.slice(-100);
        $('#serialConsole').textContent = state.consoleLines.join('\n') || 'Waiting for USB serial input…';
        $('#serialConsole').scrollTop = $('#serialConsole').scrollHeight;
      }
    } catch (error) {
      $('#connectionPill').innerHTML = '<i></i> Gateway unavailable';
      $('#connectionPill').classList.remove('live');
      $('.metrics').classList.add('stale');
      $('#connectionNotice').hidden = false;
      $('#connectionNotice').textContent = 'Cannot reach the gateway. Check that your phone is still connected to the rig’s Wi-Fi network.';
      $('#recordButton').disabled = true;
      document.querySelectorAll('[data-capability]').forEach(button => { button.disabled = true; });
    }
    // Acquisition/recording are independent. Fetch batches more often for a
    // responsive display; never start overlapping polling loops.
    setTimeout(poll, document.hidden ? 1000 : 100);
  }
  $('#recordButton').addEventListener('click', () => {
    if (state.status?.recording.active) {
      confirmAction('Finish this recording?', 'The completed run will remain in the test library for viewing and download.', async () => {
        await api('/api/record', { action: 'stop' }); toast('Run saved to gateway.'); await refreshRuns();
      });
    } else $('#recordDialog').showModal();
  });
  $('#recordForm').addEventListener('submit', async event => {
    event.preventDefault();
    const form = event.currentTarget;
    const button = form.querySelector('[type=submit]'); button.disabled = true;
    try {
      await api('/api/record', { action: 'start', name: form.elements.name.value.trim(), note: form.elements.note.value.trim(), epoch_ms: Date.now() });
      state.saved = null; state.samples = []; $('#chartWindow').value = '120';
      $('#recordDialog').close(); toast('Recording on gateway. Your phone can disconnect.'); await refreshRuns();
    } catch (error) { toast(error.message, true); } finally { button.disabled = false; }
  });
  async function refreshRuns() {
    const data = await api('/api/runs');
    state.runs = data.runs.sort((a, b) => (b.started_epoch_ms || b.started_gateway_ms) - (a.started_epoch_ms || a.started_gateway_ms));
    $('#runCount').textContent = state.runs.length;
    const list = $('#runList'); list.replaceChildren();
    if (!state.runs.length) {
      list.innerHTML = '<div class="empty-state"><span class="small-icon">▤</span><h2>A clean slate for the next experiment.</h2><p>Start a recording from the live dashboard, or open a CSV from an earlier test.</p><button class="button" data-empty-import>Open local CSV</button></div>';
      list.querySelector('[data-empty-import]').onclick = () => $('#importFile').click();
    }
    for (const run of state.runs) {
      const card = document.createElement('article'); card.className = 'panel run-card';
      const date = run.started_epoch_ms ? new Date(run.started_epoch_ms).toLocaleString() : 'Clock not provided';
      const active = run.status === 'recording';
      const completed = run.status === 'complete';
      card.innerHTML = `<div class="run-symbol">▤</div><div class="run-details"><h3>${M.escape(run.name)}</h3><div class="run-meta"><span>${M.escape(date)}</span><span>${M.escape(run.samples ?? '—')} samples</span><span>${M.bytes(run.bytes)}</span><span>${M.escape(run.thrust_unit)}</span></div>${run.note ? `<p>${M.escape(run.note)}</p>` : ''}</div><span class="badge ${completed ? 'good' : 'warn'}">${M.escape(run.status.toUpperCase().replaceAll('_', ' '))}</span><div class="run-actions"><button class="button" data-open ${active ? 'disabled' : ''}>View trace</button><a class="button ${active ? 'disabled-link' : ''}" ${active ? 'aria-disabled="true"' : `href="/api/download?id=${encodeURIComponent(run.id)}" download`}>↓ CSV</a><a class="button subtle" ${active ? 'aria-disabled="true"' : `href="/api/download?id=${encodeURIComponent(run.id)}&format=json" download`}>Metadata</a><button class="button subtle" data-delete ${active ? 'disabled' : ''} aria-label="Delete ${M.escape(run.name)}">×</button></div>`;
      card.querySelector('[data-open]').onclick = () => openRun(run).catch(error => toast(error.message, true));
      card.querySelector('[data-delete]').onclick = () => confirmAction('Delete this saved run?', `“${run.name}” will be permanently removed from the gateway. Download its CSV and metadata first if you need to keep it.`, async () => {
        await api('/api/delete', { id: run.id }); await refreshRuns(); toast('Run deleted.');
      });
      list.append(card);
    }
  }
  $('#refreshRuns').onclick = () => refreshRuns().catch(error => toast(error.message, true));
  async function openRun(run) {
    const response = await fetch(`/api/download?id=${encodeURIComponent(run.id)}`);
    if (!response.ok) throw new Error('Could not open this run. Stop recording before opening it.');
    const parsed = M.parseCsv(await response.text());
    if (!parsed.samples.length) throw new Error('This file contains no recognized measurements.');
    state.saved = { name: run.name, samples: parsed.samples, unit: run.thrust_unit };
    $('#chartWindow').value = '0'; setPage('live'); if (state.status) renderStatus(state.status); drawChart();
    if (parsed.skipped) toast(`Opened trace; skipped ${parsed.skipped} unrecognized rows.`);
  }
  $('#returnLive').onclick = () => { state.saved = null; $('#chartWindow').value = '120'; if (state.status) renderStatus(state.status); drawChart(); };
  $('#importButton').onclick = () => $('#importFile').click();
  $('#importFile').onchange = async event => {
    const file = event.target.files[0]; if (!file) return;
    try {
      if (file.size > 20 * 1024 * 1024) throw new Error('Choose a file smaller than 20 MB.');
      const parsed = M.parseCsv(await file.text());
      if (!parsed.samples.length) throw new Error('No recognized data. Open a six-column original datalog or gateway CSV.');
      state.saved = { name: file.name, samples: parsed.samples, unit: state.status?.settings.unit || 'kgf' };
      $('#chartWindow').value = '0'; setPage('live'); if (state.status) renderStatus(state.status); drawChart();
      toast(`Opened ${parsed.samples.length.toLocaleString()} samples${parsed.skipped ? `; skipped ${parsed.skipped} rows` : ''}. File stays on your phone.`);
    } catch (error) { toast(error.message, true); } finally { event.target.value = ''; }
  };
  function downloadText(text, name, type) {
    const url = URL.createObjectURL(new Blob([text], { type }));
    const link = document.createElement('a'); link.href = url; link.download = name; document.body.append(link); link.click(); link.remove(); setTimeout(() => URL.revokeObjectURL(url), 3000);
  }
  $('#exportTrace').onclick = () => {
    const samples = state.saved ? state.saved.samples : state.samples;
    if (!samples.length) return toast('No measurements to export yet.', true);
    downloadText(M.csv(samples), `${(state.saved?.name || 'live-trace').replace(/[^a-zA-Z0-9_-]/g, '_')}.csv`, 'text/csv');
    if (!state.saved) toast('Exported browser history. Record a run to preserve the full test on the gateway.');
  };
  async function command(name, args = {}) {
    await api('/api/command', { command: name, args });
    state.currentCommand = 'pending'; $('#commandResult').textContent = 'Command sent. Waiting for Nano acknowledgement…';
  }
  $('#tareButton').onclick = () => confirmAction('Tare the load cell?', 'Remove all external test force first. The Nano will establish a new zero.', () => command('tare'));
  $('#calibrateButton').onclick = () => {
    const load = Number($('#knownLoad').value);
    if (!Number.isFinite(load) || load <= 0) return toast('Enter a positive known load.', true);
    confirmAction('Calibrate against this load?', `With the known ${load} ${state.status?.settings.unit} load applied and steady, calculate a new scale factor. Save to EEPROM separately after verification.`, () => command('calibrate', { known_load: load }));
  };
  $('#readConfig').onclick = () => command('get_config').catch(error => toast(error.message, true));
  $('#saveNanoConfig').onclick = () => confirmAction('Save calibration to EEPROM?', 'The current Nano settings will become the settings used after its next restart.', () => command('save_config'));
  $('#nanoConfigForm').onsubmit = event => {
    event.preventDefault(); const values = Object.fromEntries(new FormData(event.currentTarget));
    const args = Object.fromEntries(Object.entries(values).map(([key, value]) => [key, Number(value)]));
    confirmAction('Apply measurement settings?', 'These values change the Nano’s calculations immediately. Check the resulting readings before saving to EEPROM.', () => command('set_config', args));
  };
  $('#settingsForm').onsubmit = event => {
    event.preventDefault(); const settings = Object.fromEntries(new FormData(event.currentTarget)); settings.baud = Number(settings.baud);
    confirmAction('Restart with these settings?', `The gateway will restart. Reconnect to “${settings.ssid}” and open http://${settings.ip}. The serial speed must match the Nano.`, async () => {
      const result = await api('/api/settings', settings); toast(`Settings saved. Reconnect at http://${result.ip}.`);
    });
  };
  $('#clearConsole').onclick = () => { state.consoleLines = []; $('#serialConsole').textContent = ''; };
  document.querySelectorAll('[data-metric]').forEach(button => button.onclick = () => {
    state.metric = button.dataset.metric;
    document.querySelectorAll('[data-metric]').forEach(item => item.classList.toggle('selected', item === button)); drawChart();
  });
  $('#chartWindow').onchange = drawChart;
  let plotted = [];
  function drawChart() {
    if (state.page !== 'live') return;
    const canvas = $('#chart'), rect = canvas.getBoundingClientRect();
    if (!rect.width || !rect.height) return;
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(rect.width * ratio); canvas.height = Math.round(rect.height * ratio);
    const ctx = canvas.getContext('2d'); ctx.scale(ratio, ratio);
    const width = rect.width, height = rect.height, pad = { left: 48, right: 16, top: 21, bottom: 31 };
    const samples = M.windowed(state.saved ? state.saved.samples : state.samples, Number($('#chartWindow').value));
    const values = samples.filter(sample => M.finite(sample[state.metric]));
    $('#chartEmpty').hidden = values.length > 0;
    $('#traceInfo').textContent = `${samples.length.toLocaleString()} SAMPLES${state.saved ? '' : ' / BROWSER HISTORY'}`;
    $('#exportTrace').disabled = !(state.saved ? state.saved.samples.length : state.samples.length);
    const config = M.metrics[state.metric];
    const unit = state.metric === 'thrust' ? state.saved?.unit || state.status?.settings.unit || 'kgf' : config.unit;
    ctx.font = '9px ui-monospace, monospace'; ctx.strokeStyle = '#2a3441'; ctx.lineWidth = 1;
    let min = 0, max = values.length ? -Infinity : 10;
    for (const sample of values) { min = Math.min(min, sample[state.metric]); max = Math.max(max, sample[state.metric]); }
    const margin = Math.max((max - min) * 0.12, Math.abs(max) * 0.03, 0.1); max += margin; if (min < 0) min -= margin;
    for (let i = 0; i <= 4; i++) {
      const y = pad.top + (height - pad.top - pad.bottom) * i / 4;
      ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(width - pad.right, y); ctx.stroke();
      ctx.fillStyle = '#6e7c90'; ctx.textAlign = 'right';
      const number = max - (max - min) * i / 4;
      ctx.fillText(Math.abs(number) >= 10000 ? `${(number / 1000).toFixed(0)}k` : number.toFixed(max - min > 100 ? 0 : 1), pad.left - 10, y + 3);
    }
    ctx.fillStyle = config.color; ctx.textAlign = 'left'; ctx.fillText(unit.toUpperCase(), pad.left, 9);
    plotted = [];
    if (!values.length) return;
    const firstTime = samples[0].gateway_ms;
    const span = Math.max(samples.at(-1).gateway_ms - firstTime, 1000);
    const x = sample => pad.left + (sample.gateway_ms - firstTime) / span * (width - pad.left - pad.right);
    const y = sample => pad.top + (max - sample[state.metric]) / (max - min) * (height - pad.top - pad.bottom);
    // Bound drawing work for long imported files while preserving min/max in each bin.
    const stride = Math.max(1, Math.floor(values.length / Math.max(width * 2, 1)));
    const render = [];
    for (let i = 0; i < values.length; i += stride) {
      const bin = values.slice(i, i + stride);
      let low = bin[0], high = bin[0];
      for (const sample of bin) { if (sample[state.metric] < low[state.metric]) low = sample; if (sample[state.metric] > high[state.metric]) high = sample; }
      if (low.gateway_ms <= high.gateway_ms) render.push(low, high); else render.push(high, low);
    }
    if (render.at(-1) !== values.at(-1)) render.push(values.at(-1));
    ctx.beginPath();
    let previous = null;
    for (const sample of render) {
      const px = x(sample), py = y(sample);
      if (!previous || sample.gateway_ms - previous.gateway_ms > 3000) ctx.moveTo(px, py); else ctx.lineTo(px, py);
      plotted.push({ x: px, y: py, sample }); previous = sample;
    }
    ctx.strokeStyle = config.color; ctx.lineWidth = 1.8; ctx.lineJoin = 'round'; ctx.stroke();
    const last = plotted.at(-1); ctx.fillStyle = config.color; ctx.beginPath(); ctx.arc(last.x, last.y, 3, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = '#6e7c90'; ctx.textAlign = 'center';
    for (let i = 0; i <= 4; i++) { const seconds = span / 1000 * i / 4; ctx.fillText(seconds >= 60 ? `${Math.floor(seconds / 60)}m ${Math.floor(seconds % 60)}s` : `${seconds.toFixed(span <= 5000 ? 2 : 0)}s`, pad.left + (width - pad.left - pad.right) * i / 4, height - 9); }
  }
  $('#chart').addEventListener('pointermove', event => {
    if (!plotted.length) return;
    const rect = event.currentTarget.getBoundingClientRect(), pointer = event.clientX - rect.left;
    const point = plotted.reduce((best, item) => Math.abs(item.x - pointer) < Math.abs(best.x - pointer) ? item : best);
    const config = M.metrics[state.metric], unit = state.metric === 'thrust' ? state.saved?.unit || state.status?.settings.unit : config.unit;
    $('#chartTooltip').textContent = `${M.finite(point.sample.time_s) ? point.sample.time_s.toFixed(3) + ' s' : '—'}\n${M.format(point.sample[state.metric], config.digits)} ${unit}`;
    $('#chartTooltip').style.left = `${Math.min(Math.max(0, point.x + 8), rect.width - 135)}px`;
    $('#chartTooltip').style.top = `${Math.max(15, point.y - 55)}px`; $('#chartTooltip').hidden = false;
  });
  $('#chart').addEventListener('pointerleave', () => { $('#chartTooltip').hidden = true; });
  new ResizeObserver(() => drawChart()).observe($('#chart').parentElement);
  setPage(location.hash.slice(1));
  refreshRuns().catch(() => {});
  poll();
})();
