
/* ====================================================================== part 4: the picture (zoom, pan), the preview, the pipette */
function u32(b, o) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }
function u16(b, o) { return b[o] | (b[o + 1] << 8); }

/* ---------------------------------------------------------------- the JPEG inside a raw file
 * A CR2, a CR3 and a DNG all carry a full size JPEG preview next to the raw data. The picture on the page (and the small ones of
 * the list) need only that part of the file (a few MB instead of 25 - 80 MB). It is given an Exif block with the orientation, so
 * that portraits stand upright. The rules are the same as in the engine (src/fd_containers.h): the engine does the real work when
 * the photos are developed; this part only finds the JPEG for the screen. */
function be32(b, o) { return ((b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]) >>> 0; }
function withOrientation(jpg, ori) {
  var exif = new Uint8Array([0xFF, 0xE1, 0, 0x22, 0x45, 0x78, 0x69, 0x66, 0, 0,
    0x49, 0x49, 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, ori & 255, ori >> 8, 0, 0, 0, 0, 0, 0]);
  var out = new Uint8Array(2 + exif.length + jpg.length - 2);
  out.set(jpg.subarray(0, 2), 0); out.set(exif, 2); out.set(jpg.subarray(2), 2 + exif.length);
  return out.buffer;
}
async function readBytes(file, off, len) { return new Uint8Array(await file.slice(off, off + len).arrayBuffer()); }
async function isJpegAt(file, off, len) {
  if (len < 5 || off + len > file.size) return false;
  var h = await readBytes(file, off, 3);
  return h[0] === 0xFF && h[1] === 0xD8 && h[2] === 0xFF;
}
function noPreview() { var e = new Error('W tym pliku nie ma dużego podglądu JPG. Zapisz go z podglądem (w Lightroomie: DNG z „Pełnowymiarowy podgląd JPEG”).'); e.noPreview = true; return e; }

/* a DNG is a TIFF whose IFDs can be anywhere: the parser reads a window of the file and asks for a bigger one when it needs more */
function Need(upto) { this.upto = upto; }
function dngFind(buf, total) {
  function u8(o) { if (o < 0 || o >= total) throw new RangeError('bad'); if (o >= buf.length) throw new Need(o + 1); return buf[o]; }
  var be;
  if (u8(0) === 0x49 && u8(1) === 0x49 && u8(2) === 42 && u8(3) === 0) be = false;
  else if (u8(0) === 0x4D && u8(1) === 0x4D && u8(2) === 0 && u8(3) === 42) be = true;
  else return null;
  function r16(o) { var a = u8(o), c = u8(o + 1); return be ? (a << 8) | c : a | (c << 8); }
  function r32(o) { var a = r16(o), c = r16(o + 2); return (be ? (a * 65536) + c : (c * 65536) + a) >>> 0; }
  function num(e) { return r16(e + 2) === 3 ? r16(e + 8) : r32(e + 8); }
  var queue = [r32(4)], qi, cands = [], ori0 = 1;
  for (qi = 0; qi < queue.length; qi++) {
    var ifd = queue[qi], cnt, i, w = 0, h = 0, comp = 0, photo = 0, ori = 0, nstrips = 0, so = 0, sc = 0, jif = 0, jil = 0;
    if (ifd === 0 || ifd + 2 > total) continue;
    cnt = r16(ifd);
    if (cnt === 0 || cnt > 300 || ifd + 2 + 12 * cnt + 4 > total) continue;
    for (i = 0; i < cnt; i++) {
      var e = ifd + 2 + 12 * i, tag = r16(e), ty = r16(e + 2);
      if (ty !== 3 && ty !== 4 && ty !== 13) continue;
      if (tag === 0x100) w = num(e); else if (tag === 0x101) h = num(e); else if (tag === 0x103) comp = num(e);
      else if (tag === 0x106) photo = num(e); else if (tag === 0x112) ori = num(e);
      else if (tag === 0x111) { nstrips = r32(e + 4); so = num(e); } else if (tag === 0x117) sc = num(e);
      else if (tag === 0x201) jif = num(e); else if (tag === 0x202) jil = num(e);
      else if (tag === 0x14A) {
        var c = r32(e + 4), bytes = c * 4, v = bytes <= 4 ? e + 8 : r32(e + 8), k;
        if (c > 0x100000 || v + bytes > total) continue;
        for (k = 0; k < c && k < 16 && queue.length < 40; k++) queue.push(r32(v + 4 * k));
      }
    }
    if (qi === 0) ori0 = ori;
    var co = 0, cl = 0;
    if ((comp === 7 || comp === 6) && (photo === 6 || photo === 2) && nstrips === 1 && so && sc) { co = so; cl = sc; }
    else if (jif && jil) { co = jif; cl = jil; }
    if (cl && co + cl <= total) cands.push({ off: co, len: cl, ori: ori, score: (w || 1) * (h || 1) });
    if (qi === 0) { var nx = ifd + 2 + 12 * cnt; if (nx + 4 <= total && r32(nx)) queue.push(r32(nx)); }
  }
  cands.sort(function (a, b) { return b.score - a.score; });                  /* the biggest picture first (checked for the JPEG mark later) */
  cands.forEach(function (c) { c.ori = c.ori >= 1 && c.ori <= 8 ? c.ori : (ori0 >= 1 && ori0 <= 8 ? ori0 : 1); });
  return cands;
}
async function dngSource(file) {
  var size = Math.min(file.size, 1 << 20), r;
  for (;;) {
    var buf = await readBytes(file, 0, size);
    try { r = dngFind(buf, file.size); break; }
    catch (e) {
      if (e instanceof Need && size < file.size) { size = Math.min(file.size, Math.max(e.upto + 65536, size * 4)); continue; }
      throw new Error('Nie można odczytać tego pliku DNG.');
    }
  }
  if (!r) throw new Error('Nie można odczytać tego pliku DNG.');
  for (var c = 0; c < r.length; c++) if (await isJpegAt(file, r[c].off, r[c].len)) return withOrientation(await readBytes(file, r[c].off, r[c].len), r[c].ori);
  throw noPreview();
}

/* a CR3 is a file of boxes (ISO base media): the JPEG is the sample of the track that has a JPEG box in its description */
function boxes(b, s, e) {
  var out = [], p = s;
  while (p + 8 <= e) {
    var size = be32(b, p), type = String.fromCharCode(b[p + 4], b[p + 5], b[p + 6], b[p + 7]), hl = 8;
    if (size === 1) { if (p + 16 > e || be32(b, p + 8) !== 0) break; size = be32(b, p + 12); hl = 16; }
    else if (size === 0) size = e - p;
    if (size < hl || size > e - p) break;
    out.push({ type: type, s: p + hl, e: p + size });
    p += size;
  }
  return out;
}
function boxKid(b, x, type) { var l = boxes(b, x.s, x.e); for (var i = 0; i < l.length; i++) if (l[i].type === type) return l[i]; return null; }
function tiffOrientation(b, s, e) {
  if (e - s < 16) return 0;
  var be = b[s] === 0x4D;
  function r16(o) { return be ? (b[o] << 8) | b[o + 1] : b[o] | (b[o + 1] << 8); }
  function r32(o) { return be ? be32(b, o) : (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }
  var ifd = s + r32(s + 4), n, i;
  if (ifd + 2 > e) return 0;
  n = r16(ifd);
  for (i = 0; i < n && ifd + 2 + 12 * i + 12 <= e; i++) if (r16(ifd + 2 + 12 * i) === 0x112) return r16(ifd + 2 + 12 * i + 8);
  return 0;
}
var CR3_UUID_CANON = [0x85, 0xc0, 0xb6, 0x87, 0x82, 0x0f, 0x11, 0xe0, 0x81, 0x11, 0xf4, 0xce, 0x46, 0x2b, 0x6a, 0x48];
var CR3_UUID_PRVW = [0xea, 0xf4, 0x2b, 0x5e, 0x1c, 0x98, 0x4b, 0x88, 0xb9, 0xfb, 0xb7, 0xdc, 0x40, 0x6e, 0x4d, 0x16];
function sameBytes(b, o, a) { for (var i = 0; i < a.length; i++) if (b[o + i] !== a[i]) return false; return true; }
async function cr3Source(file) {
  var total = file.size, pos = 0, moov = null, prvw = null, guard = 0;
  while (pos + 8 <= total && guard++ < 64) {
    var h = await readBytes(file, pos, 16), size = be32(h, 0), type = String.fromCharCode(h[4], h[5], h[6], h[7]), hl = 8;
    if (size === 1) { if (be32(h, 8) !== 0) break; size = be32(h, 12); hl = 16; } else if (size === 0) size = total - pos;
    if (size < hl || size > total - pos) break;
    if (type === 'moov' && !moov && size < (16 << 20)) moov = { base: pos + hl, bytes: await readBytes(file, pos + hl, size - hl) };
    else if (type === 'uuid' && !prvw && size - hl >= 0x30) {
      var u = await readBytes(file, pos + hl, 0x30);
      if (sameBytes(u, 0, CR3_UUID_PRVW)) { var l = be32(u, 0x2c); if (hl + 0x30 + l <= size) prvw = { off: pos + hl + 0x30, len: l }; }
    }
    pos += size;
  }
  if (!moov) throw new Error('Nie można odczytać tego pliku CR3.');
  var b = moov.bytes, ori = 1, jpg = null, kids = boxes(b, 0, b.length), i, k;
  for (i = 0; i < kids.length; i++) {
    var x = kids[i];
    if (x.type === 'uuid' && x.e - x.s > 16 && sameBytes(b, x.s, CR3_UUID_CANON)) {
      var inner = boxes(b, x.s + 16, x.e);
      for (k = 0; k < inner.length; k++) if (inner[k].type === 'CMT1') { var o = tiffOrientation(b, inner[k].s, inner[k].e); if (o >= 1 && o <= 8) ori = o; }
    } else if (x.type === 'uuid' && !prvw && x.e - x.s >= 0x30 && sameBytes(b, x.s, CR3_UUID_PRVW)) {
      var pl = be32(b, x.s + 0x2c);
      if (x.s + 0x30 + pl <= x.e) prvw = { off: moov.base + x.s + 0x30, len: pl };
    } else if (x.type === 'trak' && !jpg) {
      var mdia = boxKid(b, x, 'mdia'), minf = mdia && boxKid(b, mdia, 'minf'), stbl = minf && boxKid(b, minf, 'stbl');
      var stsd = stbl && boxKid(b, stbl, 'stsd'), stsz = stbl && boxKid(b, stbl, 'stsz'), stco = stbl && boxKid(b, stbl, 'stco'), co64 = stbl && boxKid(b, stbl, 'co64');
      if (!stsd || !stsz) continue;
      var has = false;
      for (k = stsd.s; k + 4 <= stsd.e && k < stsd.s + 1024; k++) if (b[k] === 0x4A && b[k + 1] === 0x50 && b[k + 2] === 0x45 && b[k + 3] === 0x47) { has = true; break; }
      if (!has || stsz.e - stsz.s < 12 || be32(b, stsz.s + 8) === 0) continue;
      var ssz = be32(b, stsz.s + 4), off = -1;
      if (ssz === 0) { if (stsz.e - stsz.s < 16) continue; ssz = be32(b, stsz.s + 12); }
      if (stco && stco.e - stco.s >= 12 && be32(b, stco.s + 4) >= 1) off = be32(b, stco.s + 8);
      else if (co64 && co64.e - co64.s >= 16 && be32(b, co64.s + 4) >= 1 && be32(b, co64.s + 8) === 0) off = be32(b, co64.s + 12);
      if (off >= 0 && await isJpegAt(file, off, ssz)) jpg = { off: off, len: ssz };
    }
  }
  var pick = jpg || prvw;
  if (!pick || !(await isJpegAt(file, pick.off, pick.len))) throw noPreview();
  return withOrientation(await readBytes(file, pick.off, pick.len), ori);
}

async function jpegSource(file) {                      /* only the JPEG of the file (for the small pictures) */
  var head = new Uint8Array(await file.slice(0, 65536).arrayBuffer());
  if (head.length > 16 && head[0] === 0xFF && head[1] === 0xD8) return await file.arrayBuffer();
  if (head.length > 16 && head[4] === 0x66 && head[5] === 0x74 && head[6] === 0x79 && head[7] === 0x70) return await cr3Source(file);      /* 'ftyp' */
  if (head.length > 16 && head[0] === 0x49 && head[1] === 0x49 && head[2] === 42 && head[8] === 0x43 && head[9] === 0x52) {
    var ifd = u32(head, 4), n = u16(head, ifd), i, comp = 0, so = 0, sb = 0, ori = 1;
    for (i = 0; i < n && ifd + 2 + 12 * i + 12 <= head.length; i++) {
      var e = ifd + 2 + 12 * i, tag = u16(head, e);
      if (tag === 0x103) comp = u16(head, e + 8); else if (tag === 0x111) so = u32(head, e + 8); else if (tag === 0x117) sb = u32(head, e + 8);
      else if (tag === 0x112) ori = u16(head, e + 8);
    }
    if (comp === 6 && so && sb && so + sb <= file.size) {
      var jpg = new Uint8Array(await file.slice(so, so + sb).arrayBuffer());
      if (jpg[0] === 0xFF && jpg[1] === 0xD8) return withOrientation(jpg, ori);
    }
    return await file.arrayBuffer();
  }
  if (head.length > 16 && ((head[0] === 0x49 && head[1] === 0x49 && head[2] === 42 && head[3] === 0) || (head[0] === 0x4D && head[1] === 0x4D && head[2] === 0 && head[3] === 42))) return await dngSource(file);
  return await file.arrayBuffer();
}

/* what the engine gets for the picture on the screen: the whole file of a CR2 / CR3 / DNG (it develops the sensor data itself, and takes the
 * JPEG inside when it can not), otherwise the JPEG */
async function engineSource(file) {
  var head = new Uint8Array(await file.slice(0, 16).arrayBuffer());
  var tiff = head.length >= 16 && ((head[0] === 0x49 && head[1] === 0x49 && head[2] === 42 && head[3] === 0) || (head[0] === 0x4D && head[1] === 0x4D && head[2] === 0 && head[3] === 42));
  var cr3 = head.length >= 16 && head[4] === 0x66 && head[5] === 0x74 && head[6] === 0x79 && head[7] === 0x70;           /* 'ftyp' */
  if (tiff || cr3) return await file.arrayBuffer();
  return await jpegSource(file);
}
var BIG_SOURCE = 12 << 20;                              /* a bigger source (a RAW file) is read again from the disk when another engine needs it */
async function sourceCopy(pv) { return pv.buf ? pv.buf.slice(0) : await engineSource(pv.file.file); }

/* ---------------------------------------------------------------- the view: one transform (move + scale) of the picture
 * V.s is the scale in pixels of the FULL size photo (1 = 100 %). The picture on the screen is usually the engine's half size
 * version stretched to the full size by the browser; when zoomed in, the full size version is made as well (see below). */
var V = { fw: 0, fh: 0, s: 1, tx: 0, ty: 0, fit: true, key: '' };        /* key: the crop the picture on the screen was made with ('' = the whole frame) */
var stageEl, worldEl, imgEl;

function stageW() { return stageEl.clientWidth; }
function stageH() { return stageEl.clientHeight; }
function fitPad() { return CROP.on ? 2 * RING + 16 : 24; }           /* the crop tool leaves room round the picture: the zone in which a drag turns it */
function fitScale() { var pad = fitPad(); return V.fw ? Math.max(0.01, Math.min((stageW() - pad) / V.fw, (stageH() - pad) / V.fh)) : 1; }
function minScale() { return fitScale() * 0.6; }
/* What "100 %" is: one pixel of the FINISHED picture (its size is chosen under "Zapis") is one pixel of the screen. When the finished picture is as big as
 * the photo (or its crop) that is the same as a pixel of the photo. The picture on the screen is shown in pixels of the photo (V.s), so the zoom is V.s * outK(). */
function outK() {                                       /* how many pixels of the photo one pixel of the finished picture covers */
  var f = S.sel, fr = cropFrame(), r, d, w;
  if (!f || !fr) return 1;
  r = CROP.on && CROP.rect ? CROP.rect : cropRectPx(f, fr);
  w = r.x1 - r.x0;
  d = outDims(w, r.y1 - r.y0);
  return d.w > 0 && w > 0 ? w / d.w : 1;
}
function s100() { return clamp(1 / outK(), 0.02, maxScale()); }
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
  $('#zVal').textContent = Math.round(V.s * outK() * 100) + ' %';
  $('#zFit').setAttribute('aria-pressed', String(V.fit));
  $('#z100').setAttribute('aria-pressed', String(Math.abs(V.s - s100()) < 0.005));
  cropLayout();
  scheduleDetail();
}
function hudShow() {
  var h = $('#hud');
  h.textContent = Math.round(V.s * outK() * 100) + ' %  ·  ' + V.fw + ' × ' + V.fh;
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
function showGrain() {                                  /* "Pokaż ziarno jak w pliku": 1 pixel of the finished picture = 1 pixel of the screen, round the middle of the picture */
  if (!S.pv || !V.fw) { toast('Najpierw wybierz zdjęcie.'); return; }
  if (CROP.on) cropClose(true);
  if (S.pvView === 'before') pvShow('after');
  zoomCenter(s100());
}

/* show a picture URL: k = how many times bigger the real photo is than this picture (2 for the half size, 1 for the full one).
 * The natural size of the picture (the browser has applied the camera's orientation) decides the size on the screen. */
var showSeq = 0;
async function showImg(url, k, key) {
  var seq = ++showSeq, im = new Image();
  im.src = url;
  try { await im.decode(); } catch (e) { return; }
  if (seq !== showSeq) return;
  var fw = Math.round(im.naturalWidth * k), fh = Math.round(im.naturalHeight * k), changed = fw !== V.fw || fh !== V.fh;
  V.fw = fw; V.fh = fh; V.key = key || '';
  imgEl.style.width = fw + 'px'; imgEl.style.height = fh + 'px';
  imgEl.src = url; imgEl.classList.add('on');
  if (changed || V.fit) setFit(); else applyView();
}

/* ---------------------------------------------------------------- which picture is on the screen */
function curUrl() {
  var pv = S.pv;
  if (!pv) return null;
  if (S.pvView === 'before') return pv.beforeFull ? { u: pv.beforeFull, k: pv.beforeFullK, key: pv.beforeFullKey, full: true } : { u: pv.beforeUrl, k: pv.beforeK, key: pv.beforeKey };
  return pv.afterFull ? { u: pv.afterFull, k: pv.afterFullK, key: pv.afterFullKey, full: true } : { u: pv.afterUrl, k: pv.afterK, key: pv.afterKey };
}
function showCurrent() {
  var c = curUrl();
  if (!c || !c.u) return;
  $('#pvMsg').className = 'off';
  showImg(c.u, c.k, c.key);
  paintCap(c.full);
}
/* the line under the picture */
function paintCap(full) {
  var pv = S.pv;
  if (!pv || !pv.capBase) return;
  if (full === undefined) { var c = curUrl(); full = !!(c && c.full); }
  $('#pvCap').textContent = pv.capBase + (full ? ' · rozmiar gotowego zdjęcia: ziarno wygląda tak jak w pliku' : (pv.capNote || ''));
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
  cropLayout(); paintOutInfo(); paintCropSection();
}
function pvBusy(on) { stageEl.classList.toggle('busy', !!on); }

function clearPreview() {
  if (CROP.on) cropClose(false, true);
  S.sel = null; S.pv = null; S.pvSeq = (S.pvSeq || 0) + 1; showSeq++; detailSeq++;
  if (S.files.length) pvMessage('Wybierz zdjęcie z listy.'); else emptyMessage();
  renderList();
}

async function selectFile(f) {
  var seq, buf, copy;
  if (CROP.on) cropClose(true, true);                         /* the crop being drawn stays with the photo it was drawn on */
  S.sel = f; renderList();
  if (!f) return;
  if (isNarrow()) setPanel('l', false, true);                 /* a small screen: the list slides away and the picture shows */
  if (!S.preview) { S.waitSel = f; return; }               /* the engine is still starting */
  seq = S.pvSeq = (S.pvSeq || 0) + 1;
  S.pv = null; S.thumbs = {}; S.sheetSeq = (S.sheetSeq || 0) + 1; detailSeq++;
  pvMessage('Wczytuję zdjęcie…'); pvBusy(true);
  $('#btnSheet').disabled = true;
  paintOutInfo(); paintCropSection();
  try { buf = await engineSource(f.file); } catch (e) { pvMessage(e && e.message ? e.message : 'Nie można odczytać tego pliku.'); pvBusy(false); return; }
  if (seq !== S.pvSeq) return;
  copy = buf.byteLength <= BIG_SOURCE ? buf.slice(0) : null;
  var info = await S.preview.request({ type: 'source', buffer: buf }, [buf]);
  if (!info) { pvMessage('Za mało pamięci na podgląd tego zdjęcia.'); pvBusy(false); return; }
  if (seq !== S.pvSeq) return;
  S.pv = { file: f, buf: copy, info: info, beforeSig: '', beforeUrl: null, beforeBlob: null, afterUrl: null, afterBlob: null, afterFull: null, beforeFull: null, k: 2, beforeK: 2, afterK: 2,
           beforeKey: '', afterKey: '', beforeFullKey: '', afterFullKey: '', detailFor: null, frameW: 0, frameH: 0, gk: geoKey() };
  paintSource();
  /* "before": the engine with everything at 0 (and no grain): the picture as the camera made it (cropped like the "after", when the photo has a crop) */
  var ok = await runBefore(seq, S.pv);
  if (!ok) return;
  $('#btnSheet').disabled = S.running;
  V.fit = true;
  schedulePreview(true);
}

/* the "before" picture of the photo on the screen, for the source and the crop it has now. A small crop is made from the full size photo (previewSize),
 * which is known only once the size of the whole frame is: so the first run may have to be made twice. */
function beforeSigNow() { return val('usejpg') + ',' + val('rawref') + ',' + (S.hq ? 1 : 0) + '|' + geoKey() + '|' + previewSize(); }
async function runBefore(seq, pv, again) {
  var tries = 0, sig, size, key, res, old;
  for (;;) {
    sig = beforeSigNow(); size = previewSize(); key = geoKey();
    pv.beforeSig = sig;
    res = await S.preview.request({ type: 'run', params: pvPlain(size, PV_Q) });
    if (seq !== S.pvSeq || S.pv !== pv) return false;
    if (res.rc !== 0) { if (!again) { pvMessage('Podgląd niemożliwy: ' + res.text); pvBusy(false); S.pv = null; } return false; }
    if (res.picW && !pv.frameW) {                                                      /* (the engine reports the picture as it is turned: the whole frame is the other way round for a quarter turn) */
      var qt = (CROP.on ? CROP.turn : (pv.file.turn || 0)) & 1;
      pv.frameW = qt ? res.picH : res.picW; pv.frameH = qt ? res.picW : res.picH;
    }
    if (pv.beforeSig !== sig) return false;                                        /* (another run has been started meanwhile and keeps the sign) */
    if (previewSize() !== size && tries++ < 1) continue;
    break;
  }
  old = pv.beforeUrl;
  pv.k = res.outW ? res.srcW / res.outW : 2;
  pv.srcW = res.srcW; pv.srcH = res.srcH;
  pv.beforeK = pv.k; pv.beforeKey = key;
  pv.beforeBlob = new Blob([res.jpeg], { type: 'image/jpeg' });
  pv.beforeUrl = URL.createObjectURL(pv.beforeBlob);
  pv.beforeFull = null; pv.bc = null;
  if (old) setTimeout(function () { URL.revokeObjectURL(old); }, 2500);
  paintOutInfo(); paintCropSection();
  return true;
}

var PV_Q = 92;                                          /* the JPEG quality of the previews (the grain is not smoothed away) */
var pvTimer = null, pvPending = false, pvRunning = false;
function schedulePreview(now) {
  if (!S.pv) return;
  clearTimeout(pvTimer);
  pvTimer = setTimeout(function () { pvPending = true; if (!pvRunning) pumpPreview(); }, now ? 0 : 140);
  pvBusy(true);
}
function shownDims(pv) {                                 /* the size of the picture on the screen in photo pixels, as text */
  var fr = cropFrame(), r;
  if (!fr) return pv.srcW + ' × ' + pv.srcH;
  r = CROP.on ? { x0: 0, y0: 0, x1: fr.W, y1: fr.H } : cropRectPx(pv.file, fr);
  return (r.x1 - r.x0) + ' × ' + (r.y1 - r.y0);
}
async function pumpPreview() {
  pvRunning = true;
  while (pvPending) {
    pvPending = false;
    var seq = S.pvSeq, pv = S.pv;
    if (!pv) break;
    var key = geoKey(), size = previewSize();
    var res = await S.preview.request({ type: 'run', params: pvParams({ size: size, quality: PV_Q }) });
    if (seq !== S.pvSeq || S.pv !== pv) continue;
    if (res.rc !== 0) { pvMessage('Podgląd niemożliwy: ' + res.text); continue; }
    var old = pv.afterUrl, oldFull = pv.afterFull;
    pv.afterBlob = new Blob([res.jpeg], { type: 'image/jpeg' });
    pv.afterUrl = URL.createObjectURL(pv.afterBlob);
    pv.afterK = res.outW ? res.srcW / res.outW : 2; pv.afterKey = key;
    pv.afterFull = null; pv.detailFor = null;
    if (old) setTimeout(function () { URL.revokeObjectURL(old); }, 2500);
    if (oldFull) setTimeout(function () { URL.revokeObjectURL(oldFull); }, 2500);
    if (S.pvView !== 'before') showCurrent(); else $('#pvMsg').className = 'off';
    pv.capBase = S.films[S.film].full + ' · ' + shownDims(pv) + (res.raw ? ' · z RAW' : ' · z JPG');
    pv.capNote = size === 102 ? ' · podgląd pomniejszony; ziarno jak w pliku zobaczysz od 100 % (klawisz 1)' : ' · podgląd w pełnym rozmiarze';
    paintCap();
    drawHist(pv.afterBlob);
    scheduleDetail();
  }
  pvRunning = false;
  pvBusy(false);
}

/* ---------------------------------------------------------------- the details: the full size picture, when zoomed in */
var detailSeq = 0, detailTimer = null;
function needDetail() { return !!S.pv && V.fw > 0 && V.s * Math.max(1, outK()) > 0.55; }
function dropDetail() {                                 /* the full size pictures were made for other settings of the result: they are made again */
  var pv = S.pv;
  detailSeq++;
  if (!pv) return;
  pv.afterFull = null; pv.beforeFull = null;
  if (V.fw) showCurrent();
  scheduleDetail();
}
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
    await w.request({ type: 'source', buffer: await sourceCopy(pv) });
    pv.detailSrc = pv.file;
    if (seq !== detailSeq || S.pv !== pv) return;
  }
  var key = geoKey();
  /* the picture made here is the finished one: the size chosen for the result, the quality and the way of counting of the file (so the grain looks as it will in the file) */
  var p = view === 'before' ? pvPlain(101, S.quality) : pvParams({ size: 101, quality: S.quality });
  if (!CROP.on) mergeX(p.x, outX());
  var res = await w.request({ type: 'run', params: p }, null, function (pm) { if (seq === detailSeq) $('#hud').textContent = 'szczegóły ' + Math.round(pm / 10) + ' %'; });
  finishDetail(res, pv, seq, view, key);
}
function finishDetail(res, pv, seq, view, key) {
  $('#hud').classList.remove('on');
  if (seq !== detailSeq || S.pv !== pv || res.rc !== 0) return;
  var url = URL.createObjectURL(new Blob([res.jpeg], { type: 'image/jpeg' })), k = res.outW ? res.srcW / res.outW : 1;
  if (view === 'before') { pv.beforeFull = url; pv.beforeFullKey = key; pv.beforeFullK = k; } else { pv.afterFull = url; pv.afterFullKey = key; pv.afterFullK = k; }
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
    if (!V.fw || S.pipette || (e.target.closest && e.target.closest('#cropBar'))) return;
    var r = stageEl.getBoundingClientRect();
    if (V.fit || Math.abs(V.s - s100()) > 0.01) zoomAt(s100(), e.clientX - r.left, e.clientY - r.top); else setFit();
  });
  if (window.ResizeObserver) new ResizeObserver(function () { if (!V.fw) return; if (V.fit) setFit(); else applyView(); }).observe(stageEl);
  else window.addEventListener('resize', function () { if (V.fw) { if (V.fit) setFit(); else applyView(); } });
  $('#zFit').onclick = function () { setFit(); hudShow(); };
  $('#z100').onclick = function () { zoomCenter(s100()); };
  $('#zIn').onclick = function () { zoomStep(1.25); };
  $('#zOut').onclick = function () { zoomStep(0.8); };
  $('#pipCancel').onclick = stopPipette;
}
