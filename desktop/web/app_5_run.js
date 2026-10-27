
/* ====================================================================== part 5: developing, saving, the layout of the page, start */
function outName(f, p, used) {
  var base = f.name.replace(/\.[^.]+$/, ''), name = base + '_' + slug(S.films[p.film].full) + SIZE_SUFFIX[p.size], k = 1, cand = name + '.jpg';
  while (used[cand.toLowerCase()]) { k++; cand = name + '_' + k + '.jpg'; }
  used[cand.toLowerCase()] = 1;
  return cand;
}

function download(blob, name) {
  var a = document.createElement('a'), url = URL.createObjectURL(blob);
  a.href = url; a.download = name; document.body.appendChild(a); a.click();
  setTimeout(function () { document.body.removeChild(a); URL.revokeObjectURL(url); }, 4000);
}

async function writeToFolder(f) {
  if (!S.outDir || !f.blob) return true;
  try {
    var fh = await S.outDir.getFileHandle(f.outName, { create: true }), w = await fh.createWritable();
    await w.write(f.blob); await w.close();
    return true;
  } catch (e) { toast('Nie udało się zapisać w folderze: ' + e.message); S.outDir = null; $('#folderInfo').textContent = ''; return false; }
}

/* develops the ticked photos with the settings on the screen */
async function runAll() {
  var todo = marked(), p = params(), used = {}, next = 0, finished = 0, t0 = Date.now();
  if (!todo.length || S.running) return;
  S.running = true; S.cancel = false; S.sheetSeq = (S.sheetSeq || 0) + 1; detailSeq++;
  todo.forEach(function (f) { f.status = 'wait'; f.blob = null; f.pct = 0; f.outName = outName(f, p, used); });
  $('#btnStop').classList.remove('hidden'); $('#btnRun').disabled = true; $('#btnZip').disabled = true;
  $('#runNote').textContent = 'Wynik: nowe pliki JPG, oryginały zostają nietknięte.';
  renderList();
  function updateBar() {
    var part = 0; todo.forEach(function (f) { part += f.status === 'done' || f.status === 'error' ? 1 : (f.status === 'running' ? f.pct / 100 : 0); });
    $('#overallBar').style.width = (100 * part / todo.length) + '%';
    $('#runInfo').textContent = finished + ' z ' + todo.length + ' gotowe';
  }
  updateBar();
  async function lane(w) {
    while (!S.cancel) {
      var k = next++;
      if (k >= todo.length) return;
      var f = todo[k], buf;
      f.status = 'running'; f.pct = 0; renderList();
      try { buf = await f.file.arrayBuffer(); } catch (e) { f.status = 'error'; f.err = 'nie można odczytać pliku'; finished++; updateBar(); renderList(); continue; }
      var res = await w.request({ type: 'process', buffer: buf, params: p, crc: true }, [buf], function (pm) {
        f.pct = pm / 10; var li = $('#list [data-id="' + f.id + '"]'); if (li) renderItem(f, li); updateBar();
      });
      if (res.rc === 0) {
        f.status = 'done'; f.blob = new Blob([res.jpeg], { type: 'image/jpeg' }); f.crc = res.crc; f.outW = res.outW; f.outH = res.outH;
        f.checked = false; f.developed = true;                         /* done: it leaves the "to develop" set */
        await writeToFolder(f);
      } else { f.status = 'error'; f.err = res.text; }
      finished++; updateBar(); renderList();
    }
  }
  await Promise.all(S.pool.map(lane));
  S.running = false; $('#btnStop').classList.add('hidden');
  todo.forEach(function (f) { if (f.status === 'wait') f.status = 'wait'; });
  var ok = todo.filter(function (f) { return f.status === 'done'; }).length, bad = todo.filter(function (f) { return f.status === 'error'; }).length;
  $('#runInfo').textContent = (S.cancel ? 'Zatrzymano. ' : 'Gotowe. ') + ok + ' ' + plural(ok, 'zdjęcie', 'zdjęcia', 'zdjęć') + ' w ' + Math.round((Date.now() - t0) / 1000) + ' s' + (bad ? ', z błędem: ' + bad : '') + '.';
  if (!S.cancel) $('#overallBar').style.width = '100%';
  renderList();
  if (S.inline && S.sel) selectFile(S.sel);              /* (one engine only: the batch has replaced the preview's photo) */
  if (ok && !S.outDir) $('#runNote').textContent = 'Kliknij „Pobierz ZIP” (albo „pobierz” przy pojedynczym zdjęciu). Wywołane zdjęcia są odznaczone — nowe zdjęcia dodane do listy zostaną wywołane osobno.';
  else if (ok) $('#runNote').textContent = 'Zdjęcia są zapisane w wybranym folderze. Wywołane zdjęcia są odznaczone.';
}

/* ---------------------------------------------------------------- ZIP (no compression: JPEGs do not shrink) */
function dosTime(d) { return ((d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1)) & 0xFFFF; }
function dosDate(d) { return (((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate()) & 0xFFFF; }
function buildZip(entries) {                           /* entries: [{name, blob, crc}] -> Blob */
  var parts = [], central = [], offset = 0, now = new Date(), enc = new TextEncoder();
  entries.forEach(function (e) {
    var name = enc.encode(e.name), h = new DataView(new ArrayBuffer(30)), c = new DataView(new ArrayBuffer(46));
    h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x0800, true); h.setUint16(8, 0, true);
    h.setUint16(10, dosTime(now), true); h.setUint16(12, dosDate(now), true); h.setUint32(14, e.crc, true);
    h.setUint32(18, e.blob.size, true); h.setUint32(22, e.blob.size, true); h.setUint16(26, name.length, true); h.setUint16(28, 0, true);
    parts.push(h.buffer, name, e.blob);
    c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true); c.setUint16(10, 0, true);
    c.setUint16(12, dosTime(now), true); c.setUint16(14, dosDate(now), true); c.setUint32(16, e.crc, true);
    c.setUint32(20, e.blob.size, true); c.setUint32(24, e.blob.size, true); c.setUint16(28, name.length, true);
    c.setUint32(42, offset, true);
    central.push(c.buffer, name);
    offset += 30 + name.length + e.blob.size;
  });
  var csize = 0; central.forEach(function (b) { csize += b.byteLength !== undefined ? b.byteLength : b.size; });
  var end = new DataView(new ArrayBuffer(22));
  end.setUint32(0, 0x06054b50, true); end.setUint16(8, entries.length, true); end.setUint16(10, entries.length, true);
  end.setUint32(12, csize, true); end.setUint32(16, offset, true);
  return new Blob(parts.concat(central, [end.buffer]), { type: 'application/zip' });
}

function zipAll() {
  var done = S.files.filter(function (f) { return f.status === 'done'; }), total = 0, d = new Date();
  if (!done.length) return;
  done.forEach(function (f) { total += f.blob.size; });
  if (total > 3.8e9 || done.length > 60000) { toast('To za dużo na jeden ZIP (ponad 3,8 GB). Użyj „Zapisz w folderze…” albo pobieraj pojedynczo.', 8000); return; }
  var pad = function (n) { return (n < 10 ? '0' : '') + n; };
  download(buildZip(done.map(function (f) { return { name: f.outName, blob: f.blob, crc: f.crc }; })),
    'FILMLOOK_' + d.getFullYear() + pad(d.getMonth() + 1) + pad(d.getDate()) + '-' + pad(d.getHours()) + pad(d.getMinutes()) + '.zip');
}

async function pickFolder() {
  try {
    S.outDir = await window.showDirectoryPicker({ mode: 'readwrite' });
  } catch (e) { return; }
  $('#folderInfo').textContent = 'zapisuję do folderu: ' + S.outDir.name;
  var n = 0;
  for (var i = 0; i < S.files.length; i++) if (S.files[i].status === 'done') { if (await writeToFolder(S.files[i])) n++; }
  toast(n ? 'Zapisano ' + n + ' zdjęć w folderze ' + S.outDir.name + '.' : 'Nowe zdjęcia będą zapisywane w folderze ' + S.outDir.name + '.');
}

/* ---------------------------------------------------------------- the panels: hide, show, drag the edge */
function isNarrow() { return window.matchMedia('(max-width: 899px)').matches; }
function applyWidths() { var c = $('#cols'); c.style.setProperty('--lw', S.ui.lw + 'px'); c.style.setProperty('--rw', S.ui.rw + 'px'); }
function setPanel(side, open, noSave) {
  var cls = side === 'l' ? 'lc' : 'rc';
  $('#app').classList.toggle(cls, !open);
  S.ui[cls] = !open;
  $(side === 'l' ? '#btnL' : '#btnR').setAttribute('aria-pressed', String(open));
  if (open && isNarrow()) setPanel(side === 'l' ? 'r' : 'l', false, true);       /* on a small screen one panel at a time */
  if (!noSave) save();
}
function togglePanel(side) { setPanel(side, $('#app').classList.contains(side === 'l' ? 'lc' : 'rc')); }

function initSplit(id, side) {
  var el = $(id), btn = el.querySelector('.sbtn'), key = side + 'w', def = side === 'l' ? 300 : 340, drag = null;
  function maxW() { return Math.max(240, Math.min(620, Math.round(window.innerWidth * 0.45))); }
  function collapsed() { return $('#app').classList.contains(side + 'c'); }
  el.addEventListener('pointerdown', function (e) {
    if (e.target.closest('.sbtn')) return;
    if (collapsed()) { setPanel(side, true); return; }
    try { el.setPointerCapture(e.pointerId); } catch (x) {}
    drag = { x: e.clientX, w: S.ui[key] }; el.classList.add('drag');
  });
  el.addEventListener('pointermove', function (e) {
    if (!drag) return;
    var dx = e.clientX - drag.x, w = side === 'l' ? drag.w + dx : drag.w - dx;
    S.ui[key] = clamp(Math.round(w), 200, maxW()); applyWidths();
  });
  function end() { if (drag) { drag = null; el.classList.remove('drag'); save(); } }
  el.addEventListener('pointerup', end); el.addEventListener('pointercancel', end);
  el.addEventListener('dblclick', function (e) { if (e.target.closest('.sbtn')) return; S.ui[key] = def; applyWidths(); save(); });
  el.addEventListener('keydown', function (e) {
    var step = e.shiftKey ? 48 : 16;
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); togglePanel(side); }
    else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
      e.preventDefault();
      var grow = (side === 'l') === (e.key === 'ArrowRight');
      S.ui[key] = clamp(S.ui[key] + (grow ? step : -step), 200, maxW()); applyWidths(); save();
    }
  });
  btn.onclick = function () { togglePanel(side); };
}

function setTheme(t) {
  S.ui.theme = t;
  if (t) document.documentElement.setAttribute('data-theme', t); else document.documentElement.removeAttribute('data-theme');
}

/* ---------------------------------------------------------------- wiring */
function wire() {
  var all = $('#dropall'), depth = 0;
  function pick(inp) { return function (e) { var l = Array.prototype.slice.call(e.target.files); addFiles(l.filter(function (f) { return isPhoto(f.name); })); e.target.value = ''; }; }
  $('#btnFiles').onclick = function () { $('#inFiles').click(); };
  $('#btnDir').onclick = function () { $('#inDir').click(); };
  $('#pvMsg').addEventListener('click', function (e) {
    if (e.target.closest('#btnFiles2')) $('#inFiles').click();
    else if (e.target.closest('#btnDir2')) $('#inDir').click();
  });
  $('#inFiles').onchange = pick(); $('#inDir').onchange = pick();
  document.addEventListener('dragenter', function (e) { if (e.dataTransfer && Array.prototype.indexOf.call(e.dataTransfer.types || [], 'Files') >= 0) { depth++; all.classList.add('on'); e.preventDefault(); } });
  document.addEventListener('dragover', function (e) { e.preventDefault(); });
  document.addEventListener('dragleave', function () { depth = Math.max(0, depth - 1); if (!depth) all.classList.remove('on'); });
  document.addEventListener('drop', function (e) { e.preventDefault(); depth = 0; all.classList.remove('on'); if (e.dataTransfer) addFromDrop(e.dataTransfer); });

  $('#selAll').onclick = function () { setChecked(function () { return true; }); };
  $('#selNone').onclick = function () { setChecked(function () { return false; }); };
  $('#selNew').onclick = function () { setChecked(function (f) { return !f.developed; }); };
  $('#delSel').onclick = function () { removeFiles(marked()); };
  $('#btnClear').onclick = function () { removeFiles(S.files.slice()); $('#overallBar').style.width = '0'; $('#runInfo').textContent = ''; };
  $('#chkPair').onchange = function (e) { S.pair = e.target.checked; applyPairs(); if (S.sel && S.sel.status === 'skipped') selectFile(firstUsable()); renderList(); save(); };
  $('#btnSheet').onclick = makeSheet;
  $('#btnCard').onclick = function () { $('#inCfg').click(); };
  $('#inCfg').onchange = function (e) { var f = e.target.files[0]; if (f) f.text().then(loadCard); e.target.value = ''; };

  Array.prototype.forEach.call(document.querySelectorAll('#sizes button'), function (b) {
    b.onclick = function () { S.size = Number(b.getAttribute('data-size')); markSizes(); save(); staleResults(); };
  });
  var qr = $('#qr'), qn = $('#qn');
  function setQ(v) { v = clamp(Math.round(Number(v)), 70, 100); if (isNaN(v)) return; S.quality = v; qr.value = v; qn.value = v; save(); staleResults(); }
  qr.oninput = function () { setQ(qr.value); }; qn.onchange = function () { setQ(qn.value); };
  $('#btnRun').onclick = runAll;
  $('#btnStop').onclick = function () { S.cancel = true; $('#btnStop').classList.add('hidden'); };
  $('#btnZip').onclick = zipAll;
  $('#btnFolder').onclick = pickFolder;

  $('#pvBefore').onclick = function () { pvShow('before'); };
  $('#pvAfter').onclick = function () { pvShow('after'); };
  $('#btnL').onclick = function () { togglePanel('l'); };
  $('#btnR').onclick = function () { togglePanel('r'); };
  $('#scrim').onclick = function () { setPanel('l', false, true); setPanel('r', false); };
  $('#btnAbout').onclick = function () { var d = $('#about'); if (d.showModal) d.showModal(); else d.setAttribute('open', ''); };
  $('#btnTheme').onclick = function () {
    setTheme(S.ui.theme === 'light' ? 'dark' : 'light'); save();         /* the darkroom (dark) is the default, a light page is one click away */
  };
  initSplit('#splitL', 'l'); initSplit('#splitR', 'r');
  initStage();

  document.addEventListener('keydown', function (e) {
    var t = e.target, typing = t && (t.tagName === 'INPUT' && t.type !== 'range' && t.type !== 'checkbox' || t.tagName === 'TEXTAREA' || t.tagName === 'SELECT');
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    if (e.key === 'Escape') { if (S.pipette) stopPipette(); return; }
    if (typing || (t && t.closest && t.closest('dialog'))) return;
    var k = e.key;
    if (k === '[') togglePanel('l');
    else if (k === ']') togglePanel('r');
    else if (k === 'f' || k === 'F') setFit();
    else if (k === '1') zoomCenter(1);
    else if (k === '+' || k === '=') zoomStep(1.25);
    else if (k === '-' || k === '_') zoomStep(0.8);
    else if (k === '\\') { if (!e.repeat && S.pvView !== 'before') { S.holdPrev = S.pvView; pvShow('before'); } }
    else if ((k === 'ArrowLeft' || k === 'ArrowRight') && !(t && t.type === 'range')) {
      var u = usable(), i = u.indexOf(S.sel);
      if (u.length) { e.preventDefault(); selectFile(u[clamp(i + (k === 'ArrowRight' ? 1 : -1), 0, u.length - 1)]); }
    }
    else if ((k === 'Delete' || k === 'Backspace') && S.sel && !(t && t.type === 'range')) { e.preventDefault(); removeFiles([S.sel]); }
  });
  document.addEventListener('keyup', function (e) { if (e.key === '\\' && S.holdPrev) { pvShow(S.holdPrev); S.holdPrev = null; } });
  window.addEventListener('blur', function () { if (S.holdPrev) { pvShow(S.holdPrev); S.holdPrev = null; } });
}
function markSizes() {
  Array.prototype.forEach.call(document.querySelectorAll('#sizes button'), function (b) { b.setAttribute('aria-checked', String(Number(b.getAttribute('data-size')) === S.size)); });
}

async function boot() {
  var saved = memo.get(STORE_KEY), old = null, bin, bytes, i, eng;
  wire();
  if (!saved) { old = memo.get('filmlook-desktop-1'); if (old) saved = { film: old.film, looks: old.vals || {}, scene: {}, size: old.size, quality: old.quality, pair: old.pair }; }
  /* the layout first (before the engine is compiled), so that the panels are where they should be from the first picture on */
  if (saved && saved.ui) { for (var k in saved.ui) S.ui[k] = saved.ui[k]; }
  if (S.ui.lc === null || S.ui.rc === null) { S.ui.lc = isNarrow(); S.ui.rc = isNarrow(); }
  S.ui.lw = clamp(S.ui.lw | 0 || 300, 200, 620); S.ui.rw = clamp(S.ui.rw | 0 || 340, 200, 620);
  if (!S.ui.open) S.ui.open = {};
  setTheme(S.ui.theme || '');
  applyWidths();
  setPanel('l', !S.ui.lc, true); setPanel('r', !S.ui.rc, true);
  setTimeout(function () { $('#app').classList.remove('boot'); }, 120);       /* from now on the panels may slide */
  if (!window.WebAssembly) { $('#chipEngine').textContent = 'ta przeglądarka jest za stara (brak WebAssembly)'; $('#chipEngine').className = 'chip bad'; return; }
  try {
    bin = atob(document.getElementById('wasm-b64').textContent.trim());
    bytes = new Uint8Array(bin.length);
    for (i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    S.module = await WebAssembly.compile(bytes);
    eng = await fdCreateEngine(S.module);
  } catch (e) { $('#chipEngine').textContent = 'nie udało się uruchomić silnika: ' + e; $('#chipEngine').className = 'chip bad'; return; }
  S.films = eng.films;
  $('#verTxt').textContent = eng.version;
  if (saved) {
    S.looks = saved.looks || {}; S.scene = saved.scene || {};
    S.film = clamp(saved.film | 0, 0, S.films.length - 1);
    S.size = clamp(saved.size | 0, 0, 2); S.quality = clamp(saved.quality | 0 || 95, 70, 100);
    if (saved.pair === false) S.pair = false;
  }
  $('#chkPair').checked = S.pair;
  $('#qr').value = S.quality; $('#qn').value = S.quality;
  markSizes();
  chooseFilm(S.film);
  emptyMessage();
  var n = Math.max(1, Math.min(4, (navigator.hardwareConcurrency || 2) - 1));
  S.pool = await startWorkers(S.module, n + 1);          /* n for the batch + 1 for the preview (a 2nd one for the details is made when needed) */
  if (S.pool.length > 1) S.preview = S.pool.pop(); else S.preview = S.pool[0];
  $('#chipEngine').textContent = 'silnik gotowy · ' + S.films.length + ' filmów' + (S.inline ? ' · tryb pojedynczy' : ' · równolegle: ' + S.pool.length);
  $('#chipEngine').className = 'chip ok';
  renderList();
  window.FD = { S: S, V: V, CTRL: CTRL, val: val, setVal: setVal };                /* for the tests */
  if (S.waitSel) { var w = S.waitSel; S.waitSel = null; selectFile(w); }
}
boot();
