'use strict';
(async function () {
  const FRAME_MS = 1000 / 50.08;
  const $ = (id) => document.getElementById(id);

  // ---------- load assets (inlined in dist build, fetched in dev) ----------
  const b64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
  async function asset(name, url) {
    if (window.EMBEDDED) return b64(window.EMBEDDED[name]);
    return new Uint8Array(await (await fetch(url)).arrayBuffer());
  }
  const [wasmBytes, romBytes, tapeBytes] = await Promise.all([
    asset('wasm', 'spectrum.wasm'),
    asset('rom', '../roms/48.rom'),
    asset('tape', '../tapes/CamelTrophy_EN.tzx'),
  ]);
  const { instance } = await WebAssembly.instantiate(wasmBytes, {});
  const W = instance.exports;
  const heap = () => new Uint8Array(W.memory.buffer);

  heap().set(romBytes, W.rom_ptr());
  W.init();

  // ---------- tape ----------
  let tapeData = tapeBytes;
  const PART_LABELS = { CAMEL1: '1 · Qualifying quiz', CAMEL2: '2 · Route selection', CAMEL3: '3 · Driving game' };
  let parts = [];
  function loadTape(bytes) {
    tapeData = bytes.slice(0, W.tape_capacity());
    heap().set(tapeData, W.tape_ptr());
    const n = W.tape_load(tapeData.length);
    parts = [];
    for (let i = 0; i < n; i++) {
      const off = W.tape_block_offset(i);
      const isTzx = tapeData[0] === 0x5a;
      // standard header block: TZX id 0x10 (data at +5) or TAP (data at +2)
      const data = isTzx ? (tapeData[off] === 0x10 ? off + 5 : -1) : off + 2;
      if (data >= 0 && tapeData[data] === 0 && tapeData[data + 1] <= 3) {
        const name = String.fromCharCode(...tapeData.subarray(data + 2, data + 12)).trim();
        parts.push({ block: i, name });
      }
    }
    const g = $('partsGroup');
    g.querySelectorAll('button').forEach((b) => b.remove());
    parts.forEach((p, k) => {
      const b = document.createElement('button');
      b.textContent = PART_LABELS[p.name] || (parts.length > 1 ? `Part ${k + 1}` : p.name || 'Program');
      b.title = `Reset and LOAD "${p.name}"`;
      b.onclick = () => { start(); loadPart(k); };
      g.appendChild(b);
    });
    return n;
  }
  loadTape(tapeBytes);

  // ---------- keyboard matrix ----------
  // [row, bit] per Spectrum key
  const K = {};
  [['CS', 'Z', 'X', 'C', 'V'], ['A', 'S', 'D', 'F', 'G'], ['Q', 'W', 'E', 'R', 'T'], ['1', '2', '3', '4', '5'],
   ['0', '9', '8', '7', '6'], ['P', 'O', 'I', 'U', 'Y'], ['EN', 'L', 'K', 'J', 'H'], ['SP', 'SS', 'M', 'N', 'B']]
    .forEach((r, ri) => r.forEach((k, bi) => { K[k] = [ri, bi]; }));

  const keyCount = new Map(); // spectrum key -> number of sources holding it
  function specKey(k, down) {
    const c = (keyCount.get(k) || 0) + (down ? 1 : -1);
    keyCount.set(k, Math.max(0, c));
    const [r, b] = K[k];
    const keys = new Uint8Array(W.memory.buffer, W.keys_ptr(), 8);
    if (c > 0) keys[r] &= ~(1 << b); else keys[r] |= 1 << b;
    const el = vkbEls[k];
    if (el) el.classList.toggle('down', c > 0);
  }
  function releaseAll() {
    for (const [k, c] of keyCount) if (c > 0) { keyCount.set(k, 1); specKey(k, false); }
    kemp = 0; W.set_kempston(0);
    for (const b in kempAt) delete kempAt[b];
  }

  // physical key -> list of spectrum keys
  const codeMap = {
    ShiftLeft: ['CS'], ShiftRight: ['CS'], ControlLeft: ['SS'], ControlRight: ['SS'], AltLeft: ['SS'],
    Enter: ['EN'], NumpadEnter: ['EN'], Space: ['SP'], Backspace: ['CS', '0'], Escape: ['CS', 'SP'],
  };
  for (const c of 'ABCDEFGHIJKLMNOPQRSTUVWXYZ') codeMap['Key' + c] = [c];
  for (const c of '0123456789') { codeMap['Digit' + c] = [c]; codeMap['Numpad' + c] = [c]; }
  // characters reached with Symbol Shift
  const symMap = {
    '"': 'P', ',': 'N', '.': 'M', ';': 'O', ':': 'Z', '?': 'C', '/': 'V', '*': 'B', '+': 'K', '-': 'J',
    '=': 'L', '<': 'R', '>': 'T', "'": '7', '!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '&': '6',
    '(': '8', ')': '9', '_': '0', '^': 'H', '£': 'X',
  };
  const joyMaps = {
    cursor: { ArrowLeft: ['5'], ArrowDown: ['6'], ArrowUp: ['7'], ArrowRight: ['8'], fire: ['0'] },
    sinclair: { ArrowLeft: ['6'], ArrowRight: ['7'], ArrowDown: ['8'], ArrowUp: ['9'], fire: ['0'] },
    qaop: { ArrowUp: ['Q'], ArrowDown: ['A'], ArrowLeft: ['O'], ArrowRight: ['P'], fire: ['M'] },
    edit: { ArrowLeft: ['CS', '5'], ArrowDown: ['CS', '6'], ArrowUp: ['CS', '7'], ArrowRight: ['CS', '8'], fire: ['SP'] },
  };
  const kempBits = { ArrowRight: 1, ArrowLeft: 2, ArrowDown: 4, ArrowUp: 8, fire: 16 };
  let kemp = 0;
  const kempAt = {}; // kempston bit -> frame it went down
  const held = new Map(); // event.code -> spectrum keys pressed for it

  function joyName(code) {
    if (code === 'Tab' || code === 'AltRight') return 'fire';
    return code.startsWith('Arrow') ? code : null;
  }

  function onKey(e, down) {
    if (e.metaKey) return;
    const joy = joyName(e.code);
    const mode = $('selJoy').value;
    if (joy) {
      e.preventDefault();
      if (mode === 'kempston') {
        const bit = kempBits[joy];
        if (down) {
          if (kempAt[bit] === undefined) kempAt[bit] = frame;
          kemp |= bit;
          W.set_kempston(kemp);
        } else if (kempAt[bit] !== undefined) {
          const at = kempAt[bit] + MIN_PRESS_FRAMES;
          delete kempAt[bit];
          if (frame < at) pendingUp.push({ at, keys: [], kemp: bit });
          else { kemp &= ~bit; W.set_kempston(kemp); }
        }
        return;
      }
      press(e.code, down, joyMaps[mode][joy]);
      return;
    }
    if (!down && held.has(e.code)) { e.preventDefault(); press(e.code, false); return; }
    if (down && symMap[e.key]) {
      e.preventDefault();
      // a symbol character: release Caps Shift, press Symbol Shift + key
      press(e.code, true, ['SS', symMap[e.key]], true);
      return;
    }
    const m = codeMap[e.code];
    if (!m) return;
    e.preventDefault();
    press(e.code, down, m);
  }
  function press(code, down, keys, suppressCaps) {
    if (down) {
      if (held.has(code)) return;
      if (suppressCaps && keyCount.get('CS') > 0) { keyCount.set('CS', 1); specKey('CS', false); }
      held.set(code, { keys, at: frame });
      keys.forEach((k) => specKey(k, true));
    } else {
      const h = held.get(code);
      if (!h) return;
      held.delete(code);
      // keep very short taps down for a few frames so the game can see them
      if (frame < h.at + MIN_PRESS_FRAMES) pendingUp.push({ at: h.at + MIN_PRESS_FRAMES, keys: h.keys });
      else h.keys.forEach((k) => specKey(k, false));
    }
  }
  const MIN_PRESS_FRAMES = 4;
  const pendingUp = [];
  window.addEventListener('keydown', (e) => { if (e.target.tagName === 'SELECT') return; start(); onKey(e, true); });
  window.addEventListener('keyup', (e) => onKey(e, false));
  window.addEventListener('blur', () => { held.clear(); releaseAll(); });

  // ---------- virtual keyboard ----------
  const vkbEls = {};
  const vkbLayout = [
    [['1', '!'], ['2', '@'], ['3', '#'], ['4', '$'], ['5', '%'], ['6', '&'], ['7', "'"], ['8', '('], ['9', ')'], ['0', '_']],
    [['Q', '<='], ['W', '<>'], ['E', '>='], ['R', '<'], ['T', '>'], ['Y', 'AND'], ['U', 'OR'], ['I', 'AT'], ['O', ';'], ['P', '"']],
    [['A', 'STOP'], ['S', 'NOT'], ['D', 'STEP'], ['F', 'TO'], ['G', 'THEN'], ['H', '↑'], ['J', '-'], ['K', '+'], ['L', '='], ['EN', '', 'ENTER']],
    [['CS', '', 'CAPS'], ['Z', ':'], ['X', '£'], ['C', '?'], ['V', '/'], ['B', '*'], ['N', ','], ['M', '.'], ['SS', '', 'SYM'], ['SP', '', 'SPACE']],
  ];
  const vkb = $('vkb');
  const latched = new Set();
  vkbLayout.forEach((r) => {
    const row = document.createElement('div');
    row.className = 'row';
    r.forEach(([k, sym, label]) => {
      const el = document.createElement('div');
      el.className = 'vk' + (label ? ' wide' : '');
      el.innerHTML = `<small>${sym}</small>${label || k}`;
      vkbEls[k] = el;
      const isShift = k === 'CS' || k === 'SS';
      el.addEventListener('pointerdown', (e) => {
        e.preventDefault(); start();
        if (isShift) {
          if (latched.has(k)) { latched.delete(k); el.classList.remove('latched'); specKey(k, false); }
          else { latched.add(k); el.classList.add('latched'); specKey(k, true); }
          return;
        }
        specKey(k, true);
        const at = frame;
        const up = () => {
          // shifts latch for one keypress and are released together with it
          const keys = [k, ...latched];
          for (const s of latched) vkbEls[s].classList.remove('latched');
          latched.clear();
          if (frame < at + MIN_PRESS_FRAMES) pendingUp.push({ at: at + MIN_PRESS_FRAMES, keys });
          else keys.forEach((s) => specKey(s, false));
          window.removeEventListener('pointerup', up); window.removeEventListener('pointercancel', up);
        };
        window.addEventListener('pointerup', up); window.addEventListener('pointercancel', up);
      });
      row.appendChild(el);
    });
    vkb.appendChild(row);
  });
  if (matchMedia('(pointer: coarse)').matches) vkb.hidden = false;
  $('btnKbd').onclick = () => { vkb.hidden = !vkb.hidden; };

  // ---------- auto-typing (LOAD "") ----------
  let frame = 0;
  const script = []; // {at, key, down}
  function typeKeys(seq, startAt) {
    let t = startAt;
    for (const combo of seq) {
      combo.forEach((k) => script.push({ at: t, key: k, down: true }));
      t += 4;
      combo.slice().reverse().forEach((k) => script.push({ at: t, key: k, down: false }));
      t += 4;
    }
  }
  function runScript() {
    while (script.length && script[0].at <= frame) {
      const s = script.shift();
      specKey(s.key, s.down);
    }
    for (let i = pendingUp.length - 1; i >= 0; i--) {
      const u = pendingUp[i];
      if (u.at > frame) continue;
      u.keys.forEach((k) => specKey(k, false));
      if (u.kemp && kempAt[u.kemp] === undefined) { kemp &= ~u.kemp; W.set_kempston(kemp); }
      pendingUp.splice(i, 1);
    }
  }
  function loadPart(k) {
    W.reset();
    held.clear(); keyCount.clear(); script.length = 0; pendingUp.length = 0; frame = 0;
    W.set_kempston(kemp = 0);
    W.tape_stop();
    W.tape_seek_block(parts.length ? parts[k].block : 0);
    // ROM is ready for input after ~90 frames
    typeKeys([['J'], ['SS', 'P'], ['SS', 'P'], ['EN']], 110);
  }

  // ---------- video ----------
  const canvas = $('screen');
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(320, 256);
  function draw() {
    img.data.set(new Uint8Array(W.memory.buffer, W.fb_ptr(), 320 * 256 * 4));
    ctx.putImageData(img, 0, 0);
  }

  // ---------- audio ----------
  let actx = null, node = null;
  const workletSrc = `
    class Beeper extends AudioWorkletProcessor {
      constructor() { super(); this.q = []; this.cur = null; this.pos = 0; this.size = 0;
        this.port.onmessage = (e) => { if (e.data === 'flush') { this.q = []; this.cur = null; this.size = 0; return; }
          if (this.size > 6000) return; this.q.push(e.data); this.size += e.data.length; }; }
      process(_, outputs) {
        const out = outputs[0][0];
        for (let i = 0; i < out.length; i++) {
          if (!this.cur || this.pos >= this.cur.length) {
            this.cur = this.q.shift() || null; this.pos = 0;
            if (!this.cur) { out.fill(0, i); break; }
            this.size -= this.cur.length;
          }
          out[i] = this.cur[this.pos++];
        }
        return true;
      }
    }
    registerProcessor('beeper', Beeper);`;
  async function startAudio() {
    try {
      actx = new AudioContext({ latencyHint: 'interactive' });
      const url = URL.createObjectURL(new Blob([workletSrc], { type: 'application/javascript' }));
      await actx.audioWorklet.addModule(url);
      node = new AudioWorkletNode(actx, 'beeper');
      node.connect(actx.destination);
      W.set_sample_rate(actx.sampleRate);
    } catch (err) {
      console.warn('audio unavailable', err);
      actx = null; node = null;
    }
  }
  function pushAudio(n) {
    if (!node || !n) return;
    node.port.postMessage(new Float32Array(W.memory.buffer, W.audio_ptr(), n).slice());
  }

  // ---------- main loop ----------
  let started = false, paused = false, last = 0, acc = 0;
  function start() {
    if (started) return;
    started = true;
    $('startOverlay').hidden = true;
    startAudio();
    loadPart(0);
  }
  $('startOverlay').onclick = start;

  function loop(now) {
    requestAnimationFrame(loop);
    if (!started || paused) { last = now; return; }
    const turbo = $('chkTurbo').checked && W.tape_is_playing();
    if (turbo) {
      // run flat out while the tape is loading
      const t0 = performance.now();
      W.set_muted(1);
      do { runScript(); W.run_frame(); frame++; } while (W.tape_is_playing() && performance.now() - t0 < 14);
      W.set_muted($('chkMute').checked ? 1 : 0);
      acc = 0;
    } else {
      acc += Math.min(now - last, 100);
      let n = 0;
      while (acc >= FRAME_MS && n < 5) {
        runScript();
        pushAudio(W.run_frame());
        frame++; acc -= FRAME_MS; n++;
      }
    }
    last = now;
    draw();
    updateTape();
  }

  // ---------- tape UI ----------
  let lastStatus = '';
  function updateTape() {
    const len = W.tape_length(), pos = W.tape_position(), playing = W.tape_is_playing();
    $('tapeBar').style.width = (len ? (100 * pos / len) : 0).toFixed(1) + '%';
    let blk = 0;
    for (let i = 0; i < W.tape_block_count(); i++) if (W.tape_block_start(i) <= pos) blk = i;
    let part = 0;
    parts.forEach((p, k) => { if (p.block <= blk) part = k; });
    const s = (playing ? '▶ ' : '■ ') + (parts.length ? (parts[part].name || `Part ${part + 1}`) : '') + ` ${Math.round(100 * pos / Math.max(1, len))}%`;
    if (s !== lastStatus) {
      $('tapeStatus').textContent = s;
      $('btnPlay').textContent = playing ? '■ Stop' : '▶ Play';
      lastStatus = s;
    }
  }
  $('btnPlay').onclick = () => { start(); if (W.tape_is_playing()) W.tape_stop(); else W.tape_play(); };

  // ---------- machine controls ----------
  $('btnReset').onclick = () => { start(); W.reset(); W.tape_stop(); script.length = 0; keyCount.clear(); };
  $('btnPause').onclick = () => {
    paused = !paused; $('btnPause').textContent = paused ? 'Resume' : 'Pause';
    if (node) node.port.postMessage('flush');
  };
  $('chkMute').onchange = () => W.set_muted($('chkMute').checked ? 1 : 0);
  $('selJoy').onchange = () => { releaseAll(); held.clear(); $('selJoy').blur(); };
  $('btnFull').onclick = () => {
    const el = document.querySelector('.screen-wrap');
    (el.requestFullscreen || el.webkitRequestFullscreen).call(el);
  };
  $('btnFile').onclick = () => $('fileTape').click();
  $('fileTape').onchange = async () => {
    const f = $('fileTape').files[0];
    if (!f) return;
    if (!loadTape(new Uint8Array(await f.arrayBuffer()))) { alert('Could not read that tape file.'); return; }
    started = false; start(); loadPart(0);
  };

  // ---------- snapshots ----------
  const STATE_KEY = 'camel-trophy-state';
  $('btnSave').onclick = () => {
    if (!started) return;
    W.state_save();
    const s = new Uint8Array(W.memory.buffer, W.state_ptr(), W.state_size());
    let bin = '';
    for (let i = 0; i < s.length; i += 8192) bin += String.fromCharCode(...s.subarray(i, i + 8192));
    try {
      localStorage.setItem(STATE_KEY, JSON.stringify({ state: btoa(bin), tape: W.tape_position() }));
      flash($('btnSave'), 'Saved');
    } catch (e) { alert('Could not save: ' + e.message); }
  };
  $('btnLoad').onclick = () => {
    let saved = null;
    try { saved = JSON.parse(localStorage.getItem(STATE_KEY)); } catch (e) { /* ignore */ }
    if (!saved) { flash($('btnLoad'), 'Nothing saved'); return; }
    start();
    script.length = 0;
    heap().set(b64(saved.state), W.state_ptr());
    if (W.state_load()) { W.tape_set_position(saved.tape || 0); flash($('btnLoad'), 'Restored'); }
  };
  function flash(btn, text) {
    const t = btn.textContent; btn.textContent = text;
    setTimeout(() => { btn.textContent = t; }, 1200);
  }

  draw();
  requestAnimationFrame(loop);
})();
