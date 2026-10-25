// The demo page: esp32sim's WebAssembly worker booting the firmware builds listed in
// sim/wasm/fw/demos.json, with only the screen and the keys. The worker protocol is esp32sim's
// (web/emu.js is the full page's client); the developer view is that full page.
(() => {
  const FW = 'sim/wasm/fw/';
  const KINDS = { rom: 0, bootloader: 1, ptable: 2, app: 3, sd: 8 };
  const PINS = { left: 0, right: 7, power: 3 };
  const $ = (s) => document.querySelector(s);
  const canvas = $('canvas.emu'), overlay = $('.emu-overlay'), statusLine = $('.status'), choices = $('.choices');
  const startButton = $('.splash .start'), splashText = $('.splash-text'), splashNote = $('.splash-note'), devlink = $('.devlink');
  const ctx = canvas.getContext('2d');
  const q = new URLSearchParams(location.search);
  const say = (s) => { statusLine.textContent = s; };
  const mb = (n) => (n / 1048576).toFixed(n < 10485760 ? 1 : 0) + ' MB';

  let demos = [], current = null, worker = null, ready = null, started = false, booting = 0, firstFrame = false;
  const pending = new Map();
  const ask = (key, msg, transfer) => new Promise((res) => { pending.set(key, res); worker.postMessage(msg, transfer || []); });
  const send = (o) => { if (started) worker.postMessage({ op: 'text', data: typeof o === 'string' ? o : JSON.stringify(o) }); };

  function drawFrame(buf) {
    const dv = new DataView(buf), w = dv.getUint16(1, true), h = dv.getUint16(3, true);
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    const im = ctx.createImageData(w, h), d = im.data;
    for (let i = 0; i < w * h; i++) {
      const p = dv.getUint16(5 + i * 2, true);
      d[i * 4] = (p >> 11) * 255 / 31; d[i * 4 + 1] = ((p >> 5) & 63) * 255 / 63; d[i * 4 + 2] = (p & 31) * 255 / 31; d[i * 4 + 3] = 255;
    }
    ctx.putImageData(im, 0, 0);
    if (!firstFrame) { firstFrame = true; overlay.hidden = true; }
  }

  function startWorker() {
    worker = new Worker('sim/wasm/worker.js', { type: 'module' });
    ready = new Promise((resolve, reject) => {
      worker.onmessage = (ev) => {
        const m = ev.data;
        if (m.bin !== undefined) { try { if (new Uint8Array(m.bin)[0] === 1) drawFrame(m.bin); } finally { if (m.ack) worker.postMessage({ op: 'frame-ack' }); } return; }
        if (m.text !== undefined) {
          let t; try { t = JSON.parse(m.text); } catch { return; }
          if (t.t === 'stat' && started) say(`Running ${current.title}` + (t.speed == null ? '' : t.speed < 0.95 ? ` · ${Math.floor(t.speed * 100)}% of real time` : ' · real time'));
          return;
        }
        if (m.log !== undefined) { console.log(m.log); return; }
        if (m.ready) resolve();
        if (m.created !== undefined) { const r = pending.get('created'); pending.delete('created'); r && r(m.created); }
        if (m.loaded !== undefined) { const r = pending.get('load' + m.loaded); pending.delete('load' + m.loaded); r && r(m.ok); }
        if (m.started !== undefined) { started = m.started; if (!started) say('Boot failed: ' + (m.error || 'see the browser console')); }
        if (m.stopped !== undefined) { started = false; say('The emulator stopped (code ' + m.stopped + ')'); }
      };
      worker.onerror = (e) => reject(new Error(e.message || 'worker failed'));
    });
    return fetchBytes('sim/wasm/esp32sim.wasm').then((buf) => { worker.postMessage({ op: 'init', wasm: buf, frameAck: true }, [buf]); return ready; });
  }

  // Fetched files, kept for the next boot: the worker takes a copy each time.
  const cache = new Map();
  let progress = { done: 0, total: 0 };
  async function fetchBytes(url) {
    if (cache.has(url)) return cache.get(url).slice(0);
    const r = await fetch(url);
    if (!r.ok) throw new Error(`${url}: ${r.status}`);
    const len = +r.headers.get('content-length') || 0, parts = [], reader = r.body.getReader();
    let got = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      parts.push(value); got += value.length; progress.done += value.length;
      if (!len) progress.total += value.length;
      splashNote.textContent = `Fetching ${mb(progress.done)} of ${mb(Math.max(progress.done, progress.total))}…`;
    }
    let buf = await new Blob(parts).arrayBuffer();
    const h = new Uint8Array(buf, 0, Math.min(2, buf.byteLength));
    if (url.endsWith('.gz') && h[0] === 0x1f && h[1] === 0x8b) buf = await new Response(new Blob([buf]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
    cache.set(url, buf);
    return buf.slice(0);
  }

  async function boot(demo) {
    const token = ++booting;
    current = demo; started = false; firstFrame = false;
    devlink.href = demo.custom ? 'sim/run.html?wasm' : `sim/run.html?wasm&fw=${encodeURIComponent(demo.fw)}`;
    overlay.hidden = false; startButton.hidden = true;
    splashText.textContent = `Starting ${demo.title}…`;
    try {
      const man = await (await fetch(FW + (demo.base || demo.fw) + '.json', { cache: 'no-cache' })).json();
      let urls = [...Object.entries(man.files || {}).map(([k, u]) => [k, u]), ...Object.entries(man.flash_at || {}).map(([at, u]) => ['flash', u, parseInt(at, 16)])];
      // a custom build replaces the app, or with a merged image the bootloader and the partition table too
      if (demo.custom) urls = urls.filter(([k]) => k !== 'app' && !(demo.custom.merged && (k === 'bootloader' || k === 'ptable')));
      progress = { done: 0, total: (worker ? 0 : demo.wasm_bytes || 0) + urls.reduce((n, [, u]) => n + (cache.has(FW + u) ? 0 : (demo.bytes?.[u] || 0)), 0) };
      say(`Fetching ${demo.title}…`);
      if (!worker) await startWorker();
      const files = [];
      for (const [kind, u, at] of urls) files.push([kind, await fetchBytes(FW + u), at]);
      if (demo.custom) files.splice(1, 0, demo.custom.merged ? ['flash', demo.custom.bytes.slice(0), 0] : ['app', demo.custom.bytes.slice(0)]);
      if (token !== booting) return;
      splashNote.textContent = 'Booting…'; say(`Booting ${demo.title}…`);
      if (!await ask('created', { op: 'create', board: man.board, flash_mb: man.flash_mb || 16, psram_mb: man.psram_mb || 8, jit: true, experiments: [] })) throw new Error('could not create the emulator');
      for (const [kind, data, at] of files) {
        const ok = at !== undefined ? await ask('loadat' + at, { op: 'load', at, data }, [data]) : await ask('load' + KINDS[kind], { op: 'load', kind: KINDS[kind], data }, [data]);
        if (!ok) throw new Error('could not load ' + kind);
      }
      if (token !== booting) return;
      worker.postMessage({ op: 'start', appDirect: false });
    } catch (e) {
      if (token !== booting) return;
      say('Could not start: ' + e.message); splashText.textContent = 'The demo could not start.'; splashNote.textContent = e.message;
      startButton.hidden = false; startButton.textContent = 'Try again';
    }
  }

  // ---- the firmware choices
  fetch(FW + 'demos.json', { cache: 'no-cache' }).then((r) => r.json()).then((list) => {
    demos = list;
    const wanted = demos.find((d) => d.fw === q.get('fw')) || demos[0];
    for (const d of demos) {
      const label = document.createElement('label');
      label.innerHTML = `<input type="radio" name="fw"><span></span><small></small>`;
      label.querySelector('span').textContent = d.title; label.querySelector('small').textContent = d.note || '';
      const input = label.querySelector('input'); input.value = d.fw; input.checked = d === wanted;
      input.onchange = () => {
        const u = new URL(location.href); u.searchParams.set('fw', d.fw); history.replaceState(null, '', u);
        devlink.href = `sim/run.html?wasm&fw=${encodeURIComponent(d.fw)}`;
        if (current) { window.nexlibrRefresh?.(); boot(d); } else current = d;
      };
      choices.appendChild(label);
    }
    $('.firmware').hidden = demos.length < 2;
    current = null;
    const total = (wanted.wasm_bytes || 0) + Object.values(wanted.bytes || {}).reduce((a, b) => a + b, 0);
    $('.dl-size').textContent = mb(total);
    devlink.href = `sim/run.html?wasm&fw=${encodeURIComponent(wanted.fw)}`;
    startButton.onclick = () => boot(demos.find((d) => d.fw === choices.querySelector('input:checked').value));
  }).catch((e) => say('No demo firmware on this site: ' + e.message));

  // ---- your own build: an app image, or a merged image (the partition table's AA 50 at 0x8000)
  function inspect(buf) {
    const b = new Uint8Array(buf);
    if (b.length < 0x20 || b[0] !== 0xe9) throw new Error('this is not an ESP32 firmware image (they start with byte E9)');
    const chip = b[12] | b[13] << 8;   // esp_image_header_t.chip_id; the ESP32-S3 is 9
    if (chip !== 9) throw new Error(`this image is for another chip (id ${chip}); the X4 Pro is an ESP32-S3`);
    return b.length > 0x8002 && b[0x8000] === 0xaa && b[0x8001] === 0x50;
  }
  async function useFile(file) {
    if (!file || !demos.length) return;
    let custom;
    try { const bytes = await file.arrayBuffer(); custom = { bytes, merged: inspect(bytes) }; }
    catch (e) { say(`${file.name}: ${e.message}`); return; }
    const demo = { fw: 'custom', base: demos[0].fw, title: file.name, custom };
    choices.querySelector('label.yours')?.remove();
    const label = document.createElement('label'); label.className = 'yours';
    label.innerHTML = `<input type="radio" name="fw" value="custom"><span></span><small></small>`;
    label.querySelector('span').textContent = file.name;
    label.querySelector('small').textContent = `your build, ${mb(file.size)}` + (custom.merged ? ', merged image' : '');
    const input = label.querySelector('input'); input.onchange = () => { window.nexlibrRefresh?.(); boot(demo); };
    choices.appendChild(label); $('.firmware').hidden = false; input.checked = true;
    window.nexlibrRefresh?.(); boot(demo);
  }
  $('.custom input[type=file]').addEventListener('change', (e) => { useFile(e.target.files[0]); e.target.value = ''; });
  addEventListener('dragover', (e) => { if (e.dataTransfer?.types.includes('Files')) e.preventDefault(); });
  addEventListener('drop', (e) => { const f = e.dataTransfer?.files[0]; if (f) { e.preventDefault(); useFile(f); } });

  // ---- input: the touch panel, the side keys, the Home pad
  installTouchInput(canvas, (d) => send(d), { intervalMs: 8 });
  const hold = (el, msg) => {
    let downAt = 0, down = false;
    const press = (v) => {
      if (v === down) return; down = v; el.classList.toggle('down', v);
      if (v) { downAt = performance.now(); send(msg(1)); } else setTimeout(() => send(msg(0)), Math.max(0, 60 - (performance.now() - downAt)));
    };
    el.addEventListener('pointerdown', (e) => { e.preventDefault(); press(true); });
    for (const ev of ['pointerup', 'pointerleave', 'pointercancel']) el.addEventListener(ev, () => press(false));
    el.addEventListener('click', (e) => e.preventDefault());
    return press;
  };
  const keys = {};
  document.querySelectorAll('.key[data-key="left"], .key[data-key="right"], .key[data-key="power"]').forEach((el) => { const pin = PINS[el.dataset.key]; keys[el.dataset.key] = hold(el, (v) => ({ t: 'btn', pin, v })); });
  // Reset restarts the chip: here, a fresh boot of the same firmware (and a fresh card)
  $('.key-reset').addEventListener('click', () => { if (current && started) { window.nexlibrRefresh?.(); boot(current); } });
  keys.home = hold($('.homepad'), (v) => ({ t: 'boardkey', name: 'home', v }));
  const KEYMAP = { ArrowLeft: 'left', ArrowRight: 'right', h: 'home', H: 'home', Home: 'home', p: 'power', P: 'power' };
  addEventListener('keydown', (e) => { const k = KEYMAP[e.key]; if (!k || e.repeat || e.metaKey || e.ctrlKey || e.altKey || e.target.closest?.('input, textarea')) return; e.preventDefault(); keys[k](true); });
  addEventListener('keyup', (e) => { const k = KEYMAP[e.key]; if (k) keys[k](false); });
})();
