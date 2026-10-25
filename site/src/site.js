// Light through the leaves, the e-ink refresh between pages, and the device's keys.
(() => {
  const still = matchMedia('(prefers-reduced-motion: reduce)').matches;

  // ---- dappled light: gradient noise on a tiny canvas, stretched (and so smoothed) by the browser.
  // Gray 128 is neutral under soft-light; brighter patches lighten, darker ones shade.
  const P = new Uint8Array(512);
  { const p = [...Array(256).keys()]; for (let i = 255; i > 0; i--) { const j = (Math.random() * (i + 1)) | 0; [p[i], p[j]] = [p[j], p[i]]; } for (let i = 0; i < 512; i++) P[i] = p[i & 255]; }
  const fade = (t) => t * t * t * (t * (t * 6 - 15) + 10);
  const grad = (h, x, y, z) => { const u = h < 8 ? x : y, v = h < 4 ? y : h === 12 || h === 14 ? x : z; return ((h & 1) ? -u : u) + ((h & 2) ? -v : v); };
  function noise(x, y, z) {
    const X = Math.floor(x) & 255, Y = Math.floor(y) & 255, Z = Math.floor(z) & 255;
    x -= Math.floor(x); y -= Math.floor(y); z -= Math.floor(z);
    const u = fade(x), v = fade(y), w = fade(z);
    const A = P[X] + Y, AA = P[A] + Z, AB = P[A + 1] + Z, B = P[X + 1] + Y, BA = P[B] + Z, BB = P[B + 1] + Z;
    const l = (a, b, t) => a + t * (b - a);
    return l(l(l(grad(P[AA] & 15, x, y, z), grad(P[BA] & 15, x - 1, y, z), u), l(grad(P[AB] & 15, x, y - 1, z), grad(P[BB] & 15, x - 1, y - 1, z), u), v),
             l(l(grad(P[AA + 1] & 15, x, y, z - 1), grad(P[BA + 1] & 15, x - 1, y, z - 1), u), l(grad(P[AB + 1] & 15, x, y - 1, z - 1), grad(P[BB + 1] & 15, x - 1, y - 1, z - 1), u), v), w);
  }
  const under = document.querySelector('.backdrop canvas.light'), over = document.querySelector('canvas.light-over');
  let ctx, img, W = 0, H = 0;
  function size() {
    W = 64; H = Math.max(16, Math.round(W * innerHeight / innerWidth));
    for (const c of [under, over]) { c.width = W; c.height = H; }
    ctx = under.getContext('2d'); img = ctx.createImageData(W, H);
  }
  function paint(ms) {
    const t = ms / 1000, d = img.data;
    // the light drifts slowly one way while the leaves sway a little against it
    const sx = t * .018, sy = t * .007, z = t * .045, sway = Math.sin(t * .23) * .25;
    for (let y = 0, i = 0; y < H; y++) {
      for (let x = 0; x < W; x++, i += 4) {
        const nx = x * .085 + sx + sway * .3, ny = y * .085 + sy;
        let n = noise(nx, ny, z) * .75 + noise(nx * 2.3 + 5, ny * 2.3 + sway, z * 1.7) * .32;
        n = n * 1.6 + .02;                                         // about -1..1
        const lit = n > 0 ? Math.min(1, n) ** 1.4 : -Math.min(1, -n) * .55;
        const g = 128 + lit * 112;
        d[i] = g + 6; d[i + 1] = g + 2; d[i + 2] = g - 10; d[i + 3] = 255;   // sunlight is warm
      }
    }
    ctx.putImageData(img, 0, 0);
    over.getContext('2d').drawImage(under, 0, 0);
  }
  size(); addEventListener('resize', size);
  if (still) paint(40000);
  else {
    let last = 0;
    const tick = (ms) => { if (ms - last > 45 && !document.hidden) { last = ms; paint(ms + 40000); } requestAnimationFrame(tick); };
    requestAnimationFrame(tick);
  }

  // ---- the screen: a refresh flash on page changes, page turns on the side keys
  const screen = document.querySelector('.screen');
  const flash = document.createElement('div'); flash.className = 'flash'; screen.appendChild(flash);
  const refresh = () => new Promise((done) => {
    if (still) return done();
    flash.classList.remove('go'); void flash.offsetWidth; flash.classList.add('go');
    setTimeout(done, 200);
  });
  window.nexlibrRefresh = refresh;
  if (document.body.classList.contains('demo')) return;   // there the keys belong to the emulator (demo.js)

  document.addEventListener('click', (e) => {
    const a = e.target.closest('a[href]');
    if (!a || !screen.contains(a) && !a.classList.contains('homepad') || e.defaultPrevented || e.button || e.metaKey || e.ctrlKey || e.shiftKey || a.target || a.hasAttribute('download')) return;
    const url = new URL(a.href, location.href);
    if (url.origin !== location.origin) return;
    e.preventDefault();
    if (url.pathname === location.pathname && url.hash) { refresh().then(() => { location.hash = url.hash; }); return; }
    refresh().then(() => { location.href = url.href; });
  });

  const content = document.querySelector('.content'), progress = document.querySelector('.pagebar .progress');
  const turn = (dir) => {
    if (!content) return;
    const step = content.clientHeight * .92;
    refresh(); content.scrollTop += dir * step;
  };
  const press = (key) => {
    if (key === 'left') turn(-1); else if (key === 'right') turn(1);
    else if (key === 'power') refresh();
    else if (key === 'reset') refresh().then(() => location.reload());
  };
  document.querySelectorAll('.key').forEach((k) => k.addEventListener('click', () => press(k.dataset.key)));
  addEventListener('keydown', (e) => {
    if (e.altKey || e.metaKey || e.ctrlKey) return;
    if (e.key === 'ArrowRight' || e.key === 'PageDown' || e.key === ' ' && !e.shiftKey) { e.preventDefault(); turn(1); }
    else if (e.key === 'ArrowLeft' || e.key === 'PageUp' || e.key === ' ' && e.shiftKey) { e.preventDefault(); turn(-1); }
    else if (e.key === 'ArrowDown') { e.preventDefault(); content && (content.scrollTop += 60); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); content && (content.scrollTop -= 60); }
    else if ((e.key === 'h' || e.key === 'Escape') && !document.body.classList.contains('home-page')) { refresh().then(() => { location.href = 'index.html'; }); }
  });
  if (content) {
    content.focus({ preventScroll: true });
    const show = () => { const max = content.scrollHeight - content.clientHeight; if (progress) progress.textContent = max > 0 ? Math.round(100 * content.scrollTop / max) + '%' : ''; };
    content.addEventListener('scroll', show, { passive: true }); show();
  }
})();
