
/* ====================================================================== part 4: the picture (zoom, pan), the preview, the pipette */
function u32(b, o) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }
function u16(b, o) { return b[o] | (b[o + 1] << 8); }

/* a CR2 carries its full size JPEG; the preview only needs that part of the file (a few MB instead of 25 - 30 MB).
 * It is given the orientation of the CR2 so that portraits stand upright. */
async function previewSource(file) {
  var head = new Uint8Array(await file.slice(0, 65536).arrayBuffer());
  if (head.length > 16 && head[0] === 0x49 && head[1] === 0x49 && head[2] === 42 && head[8] === 0x43 && head[9] === 0x52) {
    var ifd = u32(head, 4), n = u16(head, ifd), i, comp = 0, so = 0, sb = 0, ori = 1;
    for (i = 0; i < n && ifd + 2 + 12 * i + 12 <= head.length; i++) {
      var e = ifd + 2 + 12 * i, tag = u16(head, e);
      if (tag === 0x103) comp = u16(head, e + 8); else if (tag === 0x111) so = u32(head, e + 8); else if (tag === 0x117) sb = u32(head, e + 8);
      else if (tag === 0x112) ori = u16(head, e + 8);
    }
    if (comp === 6 && so && sb && so + sb <= file.size) {
      var jpg = new Uint8Array(await file.slice(so, so + sb).arrayBuffer());
      if (jpg[0] === 0xFF && jpg[1] === 0xD8) {
        /* SOI + an Exif block with the orientation only + the rest of the JPEG */
        var exif = new Uint8Array([0xFF, 0xE1, 0, 0x22, 0x45, 0x78, 0x69, 0x66, 0, 0,
          0x49, 0x49, 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, ori & 255, ori >> 8, 0, 0, 0, 0, 0, 0]);
        var out = new Uint8Array(2 + exif.length + jpg.length - 2);
        out.set(jpg.subarray(0, 2), 0); out.set(exif, 2); out.set(jpg.subarray(2), 2 + exif.length);
        return out.buffer;
      }
    }
  }
  return await file.arrayBuffer();
}

/* ---------------------------------------------------------------- the view: one transform (move + scale) of the picture
 * V.s is the scale in pixels of the FULL size photo (1 = 100 %). The picture on the screen is usually the engine's half size
 * version stretched to the full size by the browser; when zoomed in, the full size version is made as well (see below). */
var V = { fw: 0, fh: 0, s: 1, tx: 0, ty: 0, fit: true };
var stageEl, worldEl, imgEl;

function stageW() { return stageEl.clientWidth; }
function stageH() { return stageEl.clientHeight; }
function fitScale() { return V.fw ? Math.max(0.01, Math.min((stageW() - 24) / V.fw, (stageH() - 24) / V.fh)) : 1; }
function minScale() { return fitScale() * 0.6; }
function maxScale() { return 4; }

function clampPan() {
  var w = V.fw * V.s, h = V.fh * V.s, sw = stageW(), sh = stageH();
  V.tx = w <= sw ? (sw - w) / 2 : clamp(V.tx, sw - w - 80, 80);
  V.ty = h <= sh ? (sh - h) / 2 : clamp(V.ty, sh - h - 80, 80);
}
function applyView() {
  if (!V.fw) return;
  clampPan();
  worldEl.style.transform = 'translate(' + V.tx + 'px,' + V.ty + 'px) scale(' + V.s + ')';
  var shown = V.s * (imgEl.naturalWidth ? V.fw / imgEl.naturalWidth : 1);
  imgEl.classList.toggle('px', shown >= 3);
  $('#zVal').textContent = Math.round(V.s * 100) + ' %';
  $('#zFit').setAttribute('aria-pressed', String(V.fit));
  $('#z100').setAttribute('aria-pressed', String(Math.abs(V.s - 1) < 0.005));
  scheduleDetail();
}
function hudShow() {
  var h = $('#hud');
  h.textContent = Math.round(V.s * 100) + ' %  ·  ' + V.fw + ' × ' + V.fh;
  h.classList.add('on'); clearTimeout(hudShow.tm); hudShow.tm = setTimeout(function () { h.classList.remove('on'); }, 1400);
}
function setFit() {
  V.fit = true; V.s = fitScale(); V.tx = (stageW() - V.fw * V.s) / 2; V.ty = (stageH() - V.fh * V.s) / 2;
  applyView();
}
function zoomAt(ns, cx, cy) {
  if (!V.fw) return;
  ns = clamp(ns, minScale(), maxScale());
  var k = ns / V.s;
  V.tx = cx - (cx - V.tx) * k; V.ty = cy - (cy - V.ty) * k; V.s = ns;
  V.fit = Math.abs(ns - fitScale()) < 0.002;
  applyView();
  hudShow();
}
function zoomCenter(ns) { zoomAt(ns, stageW() / 2, stageH() / 2); }
function zoomStep(f) { zoomCenter(V.s * f); }

/* show a picture URL: k = how many times bigger the real photo is than this picture (2 for the half size, 1 for the full one).
 * The natural size of the picture (the browser has applied the camera's orientation) decides the size on the screen. */
var showSeq = 0;
async function showImg(url, k) {
  var seq = ++showSeq, im = new Image();
  im.src = url;
  try { await im.decode(); } catch (e) { return; }
  if (seq !== showSeq) return;
  var fw = Math.round(im.naturalWidth * k), fh = Math.round(im.naturalHeight * k), changed = fw !== V.fw || fh !== V.fh;
  V.fw = fw; V.fh = fh;
  imgEl.style.width = fw + 'px'; imgEl.style.height = fh + 'px';
  imgEl.src = url; imgEl.classList.add('on');
  if (changed || V.fit) setFit(); else applyView();
}

/* ---------------------------------------------------------------- which picture is on the screen */
function curUrl() {
  var pv = S.pv;
  if (!pv) return null;
  if (S.pvView === 'before') return pv.beforeFull ? { u: pv.beforeFull, k: 1 } : { u: pv.beforeUrl, k: pv.k };
  return pv.afterFull ? { u: pv.afterFull, k: 1 } : { u: pv.afterUrl, k: pv.k };
}
function showCurrent() {
  var c = curUrl();
  if (!c || !c.u) return;
  $('#pvMsg').className = 'off';
  showImg(c.u, c.k);
}
function pvShow(which) {
  $('#pvBefore').setAttribute('aria-pressed', String(which === 'before'));
  $('#pvAfter').setAttribute('aria-pressed', String(which === 'after'));
  S.pvView = which;
  showCurrent();
  scheduleDetail();
}
function pvMessage(t) {
  var m = $('#pvMsg'); m.className = 'on small-msg'; m.innerHTML = '<div><p class="big"></p></div>'; m.querySelector('.big').textContent = t;
  imgEl.classList.remove('on');
}
function emptyMessage() {
  var m = $('#pvMsg'); m.className = 'on'; m.innerHTML = EMPTY_MSG;
  imgEl.classList.remove('on'); V.fw = 0; $('#zVal').textContent = '—'; $('#hist').classList.remove('on');
  $('#pvCap').textContent = 'Wybierz zdjęcie, a tutaj zobaczysz, jak będzie wyglądało.';
}
function pvBusy(on) { stageEl.classList.toggle('busy', !!on); }

function clearPreview() {
  S.sel = null; S.pv = null; S.pvSeq = (S.pvSeq || 0) + 1; showSeq++; detailSeq++;
  if (S.files.length) pvMessage('Wybierz zdjęcie z listy.'); else emptyMessage();
  renderList();
}

async function selectFile(f) {
  var seq, buf, copy;
  S.sel = f; renderList();
  if (!f) return;
  if (isNarrow()) setPanel('l', false, true);                 /* a small screen: the list slides away and the picture shows */
  if (!S.preview) { S.waitSel = f; return; }               /* the engine is still starting */
  seq = S.pvSeq = (S.pvSeq || 0) + 1;
  S.pv = null; S.thumbs = {}; S.sheetSeq = (S.sheetSeq || 0) + 1; detailSeq++;
  pvMessage('Wczytuję zdjęcie…'); pvBusy(true);
  $('#btnSheet').disabled = true;
  try { buf = await previewSource(f.file); } catch (e) { pvMessage('Nie można odczytać tego pliku.'); pvBusy(false); return; }
  if (seq !== S.pvSeq) return;
  copy = buf.slice(0);
  if (!(await S.preview.request({ type: 'source', buffer: buf }, [buf]))) { pvMessage('Za mało pamięci na podgląd tego zdjęcia.'); pvBusy(false); return; }
  if (seq !== S.pvSeq) return;
  S.pv = { file: f, buf: copy, beforeUrl: null, beforeBlob: null, afterUrl: null, afterBlob: null, afterFull: null, beforeFull: null, k: 2, detailFor: null };
  /* "before": the engine with everything at 0 (and no grain): the picture as the camera made it */
  var res = await S.preview.request({ type: 'run', params: plainParams(102, 88) });
  if (seq !== S.pvSeq) return;
  if (res.rc !== 0) { pvMessage('Podgląd niemożliwy: ' + res.text); pvBusy(false); S.pv = null; return; }
  S.pv.k = res.outW ? res.srcW / res.outW : 2;
  S.pv.srcW = res.srcW; S.pv.srcH = res.srcH;
  S.pv.beforeBlob = new Blob([res.jpeg], { type: 'image/jpeg' });
  S.pv.beforeUrl = URL.createObjectURL(S.pv.beforeBlob);
  $('#btnSheet').disabled = S.running;
  V.fit = true;
  schedulePreview(true);
}

var pvTimer = null, pvPending = false, pvRunning = false;
function schedulePreview(now) {
  if (!S.pv) return;
  clearTimeout(pvTimer);
  pvTimer = setTimeout(function () { pvPending = true; if (!pvRunning) pumpPreview(); }, now ? 0 : 140);
  pvBusy(true);
}
async function pumpPreview() {
  pvRunning = true;
  while (pvPending) {
    pvPending = false;
    var seq = S.pvSeq, pv = S.pv;
    if (!pv) break;
    var res = await S.preview.request({ type: 'run', params: params({ size: 102, quality: 88 }) });
    if (seq !== S.pvSeq || S.pv !== pv) continue;
    if (res.rc !== 0) { pvMessage('Podgląd niemożliwy: ' + res.text); continue; }
    var old = pv.afterUrl, oldFull = pv.afterFull;
    pv.afterBlob = new Blob([res.jpeg], { type: 'image/jpeg' });
    pv.afterUrl = URL.createObjectURL(pv.afterBlob);
    pv.afterFull = null; pv.detailFor = null;
    if (old) setTimeout(function () { URL.revokeObjectURL(old); }, 2500);
    if (oldFull) setTimeout(function () { URL.revokeObjectURL(oldFull); }, 2500);
    if (S.pvView !== 'before') showCurrent(); else $('#pvMsg').className = 'off';
    $('#pvCap').textContent = S.films[S.film].full + ' · ' + pv.srcW + ' × ' + pv.srcH + ' · podgląd w połowie rozmiaru (po powiększeniu: w pełnym), ziarno w gotowym zdjęciu wygląda nieco inaczej';
    drawHist(pv.afterBlob);
    scheduleDetail();
  }
  pvRunning = false;
  pvBusy(false);
}

/* ---------------------------------------------------------------- the details: the full size picture, when zoomed in */
var detailSeq = 0, detailTimer = null;
function needDetail() { return !!S.pv && V.fw > 0 && V.s > 0.55; }
function scheduleDetail() {
  clearTimeout(detailTimer);
  var pv = S.pv;
  if (!needDetail()) return;
  var want = S.pvView === 'before' ? !pv.beforeFull : !pv.afterFull;
  if (!want) return;
  detailTimer = setTimeout(runDetail, 320);
}
async function getDetailWorker() {
  if (S.inline) return S.preview;
  if (!S.detail) {
    var c = new Client(newWorker());
    if (!(await c.init(S.module))) return S.preview;
    S.detail = c;
  }
  return S.detail;
}
async function runDetail() {
  var pv = S.pv, seq = ++detailSeq, view = S.pvView;
  if (!pv || !needDetail() || S.running) return;
  var w = await getDetailWorker();
  if (seq !== detailSeq || S.pv !== pv) return;
  $('#hud').textContent = 'szczegóły…'; $('#hud').classList.add('on');
  if (w !== S.preview && pv.detailSrc !== pv.file) {
    await w.request({ type: 'source', buffer: pv.buf.slice(0) });
    pv.detailSrc = pv.file;
    if (seq !== detailSeq || S.pv !== pv) return;
  }
  var p = view === 'before' ? plainParams(101, 90) : params({ size: 101, quality: 90 });
  var res = await w.request({ type: 'run', params: p }, null, function (pm) { if (seq === detailSeq) $('#hud').textContent = 'szczegóły ' + Math.round(pm / 10) + ' %'; });
  finishDetail(res, pv, seq, view);
}
function finishDetail(res, pv, seq, view) {
  $('#hud').classList.remove('on');
  if (seq !== detailSeq || S.pv !== pv || res.rc !== 0) return;
  var url = URL.createObjectURL(new Blob([res.jpeg], { type: 'image/jpeg' }));
  if (view === 'before') pv.beforeFull = url; else pv.afterFull = url;
  if (S.pvView === view) showCurrent();
}

/* ---------------------------------------------------------------- the histogram of the picture on the screen */
var histBusy = false;
async function drawHist(blob) {
  if (histBusy) return;
  histBusy = true;
  try {
    var bmp = await createImageBitmap(blob, { resizeWidth: 200, resizeQuality: 'low' });
    var c = document.createElement('canvas'); c.width = bmp.width; c.height = bmp.height;
    var g = c.getContext('2d', { willReadFrequently: true }); g.drawImage(bmp, 0, 0);
    var d = g.getImageData(0, 0, c.width, c.height).data, h = [new Uint32Array(256), new Uint32Array(256), new Uint32Array(256)], i, m = 1;
    if (bmp.close) bmp.close();
    for (i = 0; i < d.length; i += 4) { h[0][d[i]]++; h[1][d[i + 1]]++; h[2][d[i + 2]]++; }
    for (i = 1; i < 255; i++) { m = Math.max(m, h[0][i], h[1][i], h[2][i]); }
    var cv = $('#hist'), x = cv.getContext('2d'), W = cv.width, H = cv.height;
    x.clearRect(0, 0, W, H);
    x.globalCompositeOperation = 'lighter';
    ['#ff4a3a', '#3adf5a', '#3a7bff'].forEach(function (col, ch) {
      x.fillStyle = col; x.globalAlpha = 0.75; x.beginPath(); x.moveTo(0, H);
      for (var j = 0; j < 256; j++) x.lineTo(j, H - Math.min(1, h[ch][j] / m) * (H - 2));
      x.lineTo(255, H); x.closePath(); x.fill();
    });
    x.globalAlpha = 1; x.globalCompositeOperation = 'source-over';
    cv.classList.add('on');
  } catch (e) { /* no histogram: fine */ }
  histBusy = false;
}

/* ---------------------------------------------------------------- white balance: the pipette and "automatic" */
function lin8(v) { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
function wbFromRGB(r, g, b) {                           /* 0..255 averages of what should be neutral -> temperature, tint (the engine's scale) */
  var a = Math.log2(Math.max(lin8(r), 1e-4)), gg = Math.log2(Math.max(lin8(g), 1e-4)), c = Math.log2(Math.max(lin8(b), 1e-4));
  return { t: clamp(Math.round(62.5 * (c - a)), -100, 100), i: clamp(Math.round(200 * (gg - (a + c) / 2)), -100, 100) };       /* the engine: 100 = 0.8 stop on red and blue each, tint 100 = 0.5 stop on green */
}
async function beforeCanvas() {
  var pv = S.pv;
  if (!pv || !pv.beforeBlob) return null;
  if (!pv.bc) {
    var bmp = await createImageBitmap(pv.beforeBlob);
    var c = document.createElement('canvas'); c.width = bmp.width; c.height = bmp.height;
    c.getContext('2d', { willReadFrequently: true }).drawImage(bmp, 0, 0);
    if (bmp.close) bmp.close();
    pv.bc = c;
  }
  return pv.bc;
}
function startPipette() {
  if (!S.pv) { toast('Najpierw wybierz zdjęcie.'); return; }
  S.pipette = true; stageEl.classList.add('pip'); $('#pipNote').classList.remove('hidden');
  if (isNarrow()) setPanel('r', false);
}
function stopPipette() { S.pipette = false; stageEl.classList.remove('pip'); $('#pipNote').classList.add('hidden'); }
async function pipetteAt(cx, cy) {
  var c = await beforeCanvas();
  if (!c) return;
  var ix = ((cx - V.tx) / V.s) / V.fw * c.width, iy = ((cy - V.ty) / V.s) / V.fh * c.height, r = 3;
  if (ix < 0 || iy < 0 || ix >= c.width || iy >= c.height) return;
  var x0 = clamp(Math.round(ix) - r, 0, c.width - 1), y0 = clamp(Math.round(iy) - r, 0, c.height - 1);
  var w = Math.min(2 * r + 1, c.width - x0), h = Math.min(2 * r + 1, c.height - y0);
  var d = c.getContext('2d').getImageData(x0, y0, w, h).data, n = 0, R = 0, G = 0, B = 0, i;
  for (i = 0; i < d.length; i += 4) { R += lin8(d[i]); G += lin8(d[i + 1]); B += lin8(d[i + 2]); n++; }
  /* the average is taken in linear light, then turned back into 0..255 for wbFromRGB (which works in linear light again) */
  var back = function (l) { return 255 * (l <= 0.0031308 ? 12.92 * l : 1.055 * Math.pow(l, 1 / 2.4) - 0.055); };
  var wb = wbFromRGB(back(R / n), back(G / n), back(B / n));
  setVal('wb_temp', wb.t, true); setVal('wb_tint', wb.i, true); afterChange();
  stopPipette();
  toast('Balans bieli ustawiony: temperatura ' + wb.t + ', odcień ' + wb.i + '.');
}
async function autoWB() {
  var c = await beforeCanvas();
  if (!c) { toast('Najpierw wybierz zdjęcie.'); return; }
  var s = document.createElement('canvas'); s.width = 48; s.height = Math.max(1, Math.round(48 * c.height / c.width));
  var g = s.getContext('2d', { willReadFrequently: true }); g.drawImage(c, 0, 0, s.width, s.height);
  var d = g.getImageData(0, 0, s.width, s.height).data, n = 0, R = 0, G = 0, B = 0, i;
  for (i = 0; i < d.length; i += 4) { R += lin8(d[i]); G += lin8(d[i + 1]); B += lin8(d[i + 2]); n++; }
  var back = function (l) { return 255 * (l <= 0.0031308 ? 12.92 * l : 1.055 * Math.pow(l, 1 / 2.4) - 0.055); };
  var wb = wbFromRGB(back(R / n), back(G / n), back(B / n));
  setVal('wb_temp', clamp(wb.t, -80, 80), true); setVal('wb_tint', clamp(wb.i, -80, 80), true); afterChange();
  toast('Balans bieli ustawiony automatycznie: temperatura ' + val('wb_temp') + ', odcień ' + val('wb_tint') + '.');
}

/* ---------------------------------------------------------------- mouse, touch and keyboard on the picture */
var ptrs = {}, pinch = null, EMPTY_MSG = '';
function initStage() {
  stageEl = $('#stage'); worldEl = $('#world'); imgEl = $('#pvImg');
  EMPTY_MSG = $('#pvMsg').innerHTML;
  stageEl.addEventListener('wheel', function (e) {
    if (!V.fw) return;
    e.preventDefault();
    var r = stageEl.getBoundingClientRect(), dy = e.deltaMode === 1 ? e.deltaY * 16 : e.deltaY;
    zoomAt(V.s * Math.exp(-dy * (e.ctrlKey ? 0.01 : 0.0018)), e.clientX - r.left, e.clientY - r.top);
  }, { passive: false });
  stageEl.addEventListener('pointerdown', function (e) {
    if (e.target.closest('#pvMsg button') || e.target.closest('.pipnote')) return;
    if (!V.fw) return;
    try { stageEl.setPointerCapture(e.pointerId); } catch (x) {}
    ptrs[e.pointerId] = { x: e.clientX, y: e.clientY, sx: e.clientX, sy: e.clientY, moved: false };
    var ids = Object.keys(ptrs);
    if (ids.length === 2) {
      var a = ptrs[ids[0]], b = ptrs[ids[1]];
      pinch = { d: Math.hypot(a.x - b.x, a.y - b.y), s: V.s };
    }
  });
  stageEl.addEventListener('pointermove', function (e) {
    var p = ptrs[e.pointerId];
    if (!p) return;
    var r = stageEl.getBoundingClientRect(), ids = Object.keys(ptrs);
    if (ids.length === 2 && pinch) {
      p.x = e.clientX; p.y = e.clientY;
      var a = ptrs[ids[0]], b = ptrs[ids[1]];
      zoomAt(pinch.s * Math.hypot(a.x - b.x, a.y - b.y) / Math.max(1, pinch.d), (a.x + b.x) / 2 - r.left, (a.y + b.y) / 2 - r.top);
      p.moved = true;
      return;
    }
    if (!p.moved && Math.hypot(e.clientX - p.sx, e.clientY - p.sy) < 4) return;
    p.moved = true;
    stageEl.classList.add('panning');
    V.tx += e.clientX - p.x; V.ty += e.clientY - p.y; V.fit = false;
    p.x = e.clientX; p.y = e.clientY;
    applyView();
  });
  function up(e) {
    var p = ptrs[e.pointerId];
    if (!p) return;
    delete ptrs[e.pointerId];
    if (!Object.keys(ptrs).length) { pinch = null; stageEl.classList.remove('panning'); }
    if (e.type === 'pointerup' && !p.moved && S.pipette) {
      var r = stageEl.getBoundingClientRect();
      pipetteAt(e.clientX - r.left, e.clientY - r.top);
    }
  }
  stageEl.addEventListener('pointerup', up);
  stageEl.addEventListener('pointercancel', up);
  stageEl.addEventListener('dblclick', function (e) {
    if (!V.fw || S.pipette) return;
    var r = stageEl.getBoundingClientRect();
    if (V.fit || Math.abs(V.s - 1) > 0.01) zoomAt(1, e.clientX - r.left, e.clientY - r.top); else setFit();
  });
  if (window.ResizeObserver) new ResizeObserver(function () { if (!V.fw) return; if (V.fit) setFit(); else applyView(); }).observe(stageEl);
  else window.addEventListener('resize', function () { if (V.fw) { if (V.fit) setFit(); else applyView(); } });
  $('#zFit').onclick = function () { setFit(); hudShow(); };
  $('#z100').onclick = function () { zoomCenter(1); };
  $('#zIn').onclick = function () { zoomStep(1.25); };
  $('#zOut').onclick = function () { zoomStep(0.8); };
  $('#pipCancel').onclick = stopPipette;
}
