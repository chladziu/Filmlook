/* ====================================================================== part 4b: the crop and the size of the result
 * The crop is a rectangle of the picture AS IT IS SHOWN (the camera's orientation applied), kept for every photo on its own:
 *   f.crop    = {x0, y0, x1, y1} in millionths of the picture's width / height (a rectangle the user drew)
 *   f.cropAr  = {w, h, auto: 1}  "this ratio, as big as it goes, in the middle" for photos that were not opened (the engine works the
 *               rectangle out from the real size of each photo, and turns the ratio round on a portrait photo)
 * The size of the result is one setting for all photos (S.out): the photo's own size, or the long side / the width / the height
 * in pixels (smaller or bigger than the photo).
 * While the crop tool is open the picture on the screen is the whole frame (uncropped); when it is closed the previews are made
 * from the cropped picture (so that the vignette and the grain belong to the result, as they will in the file). */

var AR_LIST = [
  { id: 'orig', name: 'Oryginał' },
  { id: 'free', name: 'Dowolne' },
  { id: '1:1', name: '1 : 1  (kwadrat)', a: 1, b: 1 },
  { id: '5:4', name: '5 : 4  (4 × 5)', a: 5, b: 4 },
  { id: '7:5', name: '7 : 5  (5 × 7)', a: 7, b: 5 },
  { id: '4:3', name: '4 : 3', a: 4, b: 3 },
  { id: '3:2', name: '3 : 2  (2 × 3, małpa)', a: 3, b: 2 },
  { id: '16:10', name: '16 : 10', a: 16, b: 10 },
  { id: '16:9', name: '16 : 9', a: 16, b: 9 },
  { id: '2:1', name: '2 : 1', a: 2, b: 1 },
  { id: '3:1', name: '3 : 1  (panorama)', a: 3, b: 1 },
  { id: 'custom', name: 'Własne…' }
];
AR_LIST[6].name = '3 : 2  (2 × 3, jak w aparacie)';
var CROP = { on: false, rect: null, ar: 'orig', flip: false, drag: null, backup: null, custom: [4, 5] };
var OUT_MAX = 16384, OUT_MAX_PIX = 160e6;

function gcd(a, b) { while (b) { var t = a % b; a = b; b = t; } return a; }
function frac6(px, n) { return Math.round(px / n * 1e6); }              /* pixels -> millionths */
function px6(f, n) { return Math.floor(f * n / 1e6 + 0.5); }            /* millionths -> pixels, the way the engine does it */

/* ---------------------------------------------------------------- what the engine is told */
function geoKey() {                                     /* what the picture on the screen is cropped to ('' = the whole frame) */
  var f = S.sel;
  if (!f || CROP.on) return '';
  if (f.crop) return 'r' + [f.crop.x0, f.crop.y0, f.crop.x1, f.crop.y1].join(',');
  if (f.cropAr) return 'a' + f.cropAr.w + ':' + f.cropAr.h;
  return '';
}
function geoX(f) {                                      /* the crop of photo f as the engine's extra parameters */
  var x = {};
  if (!f) return x;
  if (f.crop) { x.crop_x0 = f.crop.x0; x.crop_y0 = f.crop.y0; x.crop_x1 = f.crop.x1; x.crop_y1 = f.crop.y1; }
  else if (f.cropAr) { x.crop_ar_w = f.cropAr.w; x.crop_ar_h = f.cropAr.h; x.crop_ar_auto = f.cropAr.auto ? 1 : 0; }
  return x;
}
function outX() {                                       /* the size of the result as the engine's extra parameters */
  var o = S.out;
  if (o.mode === 'long') return { out_long: o.px };
  if (o.mode === 'w') return { out_w: o.px };
  if (o.mode === 'h') return { out_h: o.px };
  return {};
}
function outSuffix() {
  var o = S.out;
  return o.mode === 'long' ? '_' + o.px + 'px' : (o.mode === 'w' ? '_w' + o.px : (o.mode === 'h' ? '_h' + o.px : ''));
}
function mergeX(dst, src) { for (var k in src) dst[k] = src[k]; return dst; }
function pvParams(extra) {                              /* the settings on the screen for the picture on the screen (with the crop, when it is closed) */
  var p = params(extra);
  if (!CROP.on) mergeX(p.x, geoX(S.sel));
  return p;
}
function pvPlain(size, quality) {
  var p = plainParams(size, quality);
  if (!CROP.on) mergeX(p.x, geoX(S.sel));
  return p;
}
function previewSize() {                                /* the half size picture; a small crop is made from the full size, so that it keeps its detail */
  var pv = S.pv, f = S.sel;
  if (CROP.on || !pv || !f || !f.crop || !pv.frameW) return 102;
  var long = Math.max((f.crop.x1 - f.crop.x0) / 1e6 * pv.frameW, (f.crop.y1 - f.crop.y0) / 1e6 * pv.frameH);
  return long >= 2800 ? 102 : 101;
}

/* ---------------------------------------------------------------- the rectangle in pixels, and the size that comes out */
function cropFrame() { var pv = S.pv; return pv && pv.frameW ? { W: pv.frameW, H: pv.frameH } : null; }

function autoRectPx(W, H, ar) {                         /* the engine's rule for "the same ratio on every photo" (fd_geo_make in fd_core.h) */
  var a = ar.w, b = ar.h, g, k, w, h, t, x0, y0;
  if (ar.auto && a !== b && W !== H && ((W < H) !== (a < b))) { t = a; a = b; b = t; }
  g = gcd(a, b); a /= g; b /= g;
  k = Math.floor(Math.min(W / a, H / b));
  if (k >= 1) { w = k * a; h = k * b; }
  else if (W * b >= H * a) { h = H; w = Math.floor(h * a / b); }
  else { w = W; h = Math.floor(w * b / a); }
  w = Math.max(1, w); h = Math.max(1, h);
  x0 = Math.floor((W - w) / 2); y0 = Math.floor((H - h) / 2);
  return { x0: x0, y0: y0, x1: x0 + w, y1: y0 + h };
}
function cropRectPx(f, fr) {                            /* the crop of photo f in pixels of the whole picture (the whole frame when there is none) */
  var x0, y0, x1, y1;
  if (f && f.crop) {
    x0 = px6(f.crop.x0, fr.W); x1 = px6(f.crop.x1, fr.W); y0 = px6(f.crop.y0, fr.H); y1 = px6(f.crop.y1, fr.H);
    x0 = Math.min(x0, fr.W - 1); y0 = Math.min(y0, fr.H - 1); x1 = Math.max(x1, x0 + 1); y1 = Math.max(y1, y0 + 1);
    return { x0: x0, y0: y0, x1: x1, y1: y1 };
  }
  if (f && f.cropAr) return autoRectPx(fr.W, fr.H, f.cropAr);
  return { x0: 0, y0: 0, x1: fr.W, y1: fr.H };
}
function outDims(cw, ch) {                              /* the size of the result for a crop of cw x ch (the same rounding as in the engine) */
  var o = S.out, w = cw, h = ch;
  if (o.mode === 'long') { if (cw >= ch) { w = o.px; h = Math.round(ch * w / cw); } else { h = o.px; w = Math.round(cw * h / ch); } }
  else if (o.mode === 'w') { w = o.px; h = Math.round(ch * w / cw); }
  else if (o.mode === 'h') { h = o.px; w = Math.round(cw * h / ch); }
  return { w: Math.max(1, w), h: Math.max(1, h) };
}
function ratioText(w, h) {                              /* "3 : 2" when it is a simple ratio, else nothing */
  var g = gcd(w, h), a = w / g, b = h / g;
  return (a <= 24 && b <= 24) ? a + ' : ' + b : '';
}

/* the line under "Rozmiar zapisu": what comes out for the photo on the screen */
function paintOutInfo() {
  var el = $('#outInfo'), pv = S.pv, f = S.sel, fr = cropFrame(), r, d, mp, text = '', bad = false;
  if (!el) return;
  if (!pv || !f || !fr) { el.textContent = S.out.mode === 'full' ? 'Rozmiar zdjęcia (po kadrowaniu, jeśli jest kadr).' : 'Zdjęcie może być też powiększone ponad swój rozmiar.'; el.classList.remove('bad'); return; }
  r = cropRectPx(f, fr);
  d = outDims(r.x1 - r.x0, r.y1 - r.y0);
  mp = d.w * d.h / 1e6;
  text = 'To zdjęcie: ' + d.w + ' × ' + d.h + ' px (' + mp.toFixed(mp < 10 ? 1 : 0).replace('.', ',') + ' Mpix)';
  if (S.out.mode !== 'full' && Math.max(d.w, d.h) > Math.max(r.x1 - r.x0, r.y1 - r.y0)) text += ' — powiększone ' + (Math.max(d.w, d.h) / Math.max(r.x1 - r.x0, r.y1 - r.y0)).toFixed(2).replace('.', ',') + '×';
  if (d.w > OUT_MAX || d.h > OUT_MAX || d.w * d.h > OUT_MAX_PIX) { text = 'Za duży rozmiar: ' + d.w + ' × ' + d.h + ' px. Najwyżej ' + OUT_MAX + ' px w dłuższym boku i 160 Mpix.'; bad = true; }
  el.textContent = text; el.classList.toggle('bad', bad);
}
function paintOutControls() {
  var m = $('#outMode'), n = $('#outPx');
  if (!m) return;
  m.value = S.out.mode; n.value = S.out.px; n.disabled = S.out.mode === 'full';
  paintOutInfo();
}

/* ---------------------------------------------------------------- the ratio of the crop being drawn */
function arObj(id) { return AR_LIST.filter(function (x) { return x.id === id; })[0]; }
function ratioNow() {                                   /* null = free, else {a, b}: whole numbers, the crop has exactly this shape */
  var fr = cropFrame(), ar = arObj(CROP.ar), a, b, g, t;
  if (!fr || !ar || ar.id === 'free') return null;
  if (ar.id === 'orig') { a = fr.W; b = fr.H; }
  else {
    if (ar.id === 'custom') { a = CROP.custom[0]; b = CROP.custom[1]; } else { a = ar.a; b = ar.b; }
    if (CROP.flip) { t = a; a = b; b = t; }
  }
  g = gcd(a, b);
  return { a: a / g, b: b / g };
}
function sizeFor(rt, w, maxW, maxH) {                   /* a crop of that ratio, about w wide, within the limits */
  var k, ww, hh;
  if (rt.a <= 64 && rt.b <= 64) {                       /* a simple ratio: whole multiples of it, so the shape is exact */
    k = Math.max(1, Math.round(w / rt.a));
    k = Math.max(1, Math.min(k, Math.floor(maxW / rt.a), Math.floor(maxH / rt.b)));
    return { w: k * rt.a, h: k * rt.b };
  }
  ww = Math.max(1, Math.min(Math.round(w), maxW)); hh = Math.round(ww * rt.b / rt.a);
  if (hh > maxH) { hh = maxH; ww = Math.max(1, Math.round(hh * rt.a / rt.b)); }
  return { w: ww, h: Math.max(1, hh) };
}
function placeAround(cx, cy, s, fr) {                   /* a rectangle of size s with its middle at (cx, cy), moved inside the frame */
  var x0 = clamp(Math.round(cx - s.w / 2), 0, fr.W - s.w), y0 = clamp(Math.round(cy - s.h / 2), 0, fr.H - s.h);
  return { x0: x0, y0: y0, x1: x0 + s.w, y1: y0 + s.h };
}
function fitRatio(inFrame) {                            /* the crop gets the chosen ratio: the biggest one inside the crop (or inside the frame), round its middle */
  var fr = cropFrame(), rt = ratioNow(), R = CROP.rect, cx, cy, mw, mh, w;
  if (!fr || !rt || !R) return;
  cx = (R.x0 + R.x1) / 2; cy = (R.y0 + R.y1) / 2;
  if (inFrame) { mw = 2 * Math.min(cx, fr.W - cx); mh = 2 * Math.min(cy, fr.H - cy); }
  else { mw = R.x1 - R.x0; mh = R.y1 - R.y0; }
  w = Math.min(mw, mh * rt.a / rt.b);
  CROP.rect = placeAround(cx, cy, sizeFor(rt, w, Math.floor(inFrame ? Math.min(mw, fr.W) : mw), Math.floor(inFrame ? Math.min(mh, fr.H) : mh)), fr);
}

/* ---------------------------------------------------------------- dragging */
function dragRect(kind, r0, P, fr, rt) {                /* the new rectangle when the handle `kind` (nw n ne e se s sw w, or move) is at P (pixels of the frame) */
  var W = fr.W, H = fr.H, MIN = Math.max(8, Math.round(Math.min(W, H) * 0.01));
  var west = kind.indexOf('w') >= 0, east = kind.indexOf('e') >= 0, north = kind.indexOf('n') >= 0, south = kind.indexOf('s') >= 0;
  var n = { x0: r0.x0, y0: r0.y0, x1: r0.x1, y1: r0.y1 }, ax, ay, maxW, maxH, w, s, cx, cy;
  if (!rt) {                                            /* free: every side on its own */
    if (west) n.x0 = clamp(Math.round(P.x), 0, r0.x1 - MIN);
    if (east) n.x1 = clamp(Math.round(P.x), r0.x0 + MIN, W);
    if (north) n.y0 = clamp(Math.round(P.y), 0, r0.y1 - MIN);
    if (south) n.y1 = clamp(Math.round(P.y), r0.y0 + MIN, H);
    return n;
  }
  ax = west ? r0.x1 : r0.x0; ay = north ? r0.y1 : r0.y0;           /* the sides that stay where they are */
  if ((west || east) && (north || south)) {            /* a corner: the opposite corner stays */
    maxW = east ? W - ax : ax; maxH = south ? H - ay : ay;
    w = Math.max(east ? P.x - ax : ax - P.x, (south ? P.y - ay : ay - P.y) * rt.a / rt.b);
    s = sizeFor(rt, clamp(w, MIN, Math.max(MIN, maxW)), Math.floor(maxW), Math.floor(maxH));
    if (east) { n.x0 = ax; n.x1 = ax + s.w; } else { n.x1 = ax; n.x0 = ax - s.w; }
    if (south) { n.y0 = ay; n.y1 = ay + s.h; } else { n.y1 = ay; n.y0 = ay - s.h; }
  } else if (west || east) {                           /* a side edge: the width follows the pointer, the height the ratio, round the middle */
    cy = (r0.y0 + r0.y1) / 2;
    maxW = east ? W - ax : ax; maxH = 2 * Math.min(cy, H - cy);
    w = clamp(east ? P.x - ax : ax - P.x, MIN, Math.max(MIN, Math.min(maxW, maxH * rt.a / rt.b)));
    s = sizeFor(rt, w, Math.floor(maxW), Math.floor(maxH));
    if (east) { n.x0 = ax; n.x1 = ax + s.w; } else { n.x1 = ax; n.x0 = ax - s.w; }
    n.y0 = clamp(Math.round(cy - s.h / 2), 0, H - s.h); n.y1 = n.y0 + s.h;
  } else {                                             /* the top or the bottom edge */
    cx = (r0.x0 + r0.x1) / 2;
    maxH = south ? H - ay : ay; maxW = 2 * Math.min(cx, W - cx);
    var h = clamp(south ? P.y - ay : ay - P.y, MIN, Math.max(MIN, Math.min(maxH, maxW * rt.b / rt.a)));
    s = sizeFor(rt, h * rt.a / rt.b, Math.floor(maxW), Math.floor(maxH));
    if (south) { n.y0 = ay; n.y1 = ay + s.h; } else { n.y1 = ay; n.y0 = ay - s.h; }
    n.x0 = clamp(Math.round(cx - s.w / 2), 0, W - s.w); n.x1 = n.x0 + s.w;
  }
  return n;
}
function handlePoint(kind, r) {                         /* where the handle is on the rectangle */
  var x = kind.indexOf('w') >= 0 ? r.x0 : (kind.indexOf('e') >= 0 ? r.x1 : (r.x0 + r.x1) / 2);
  var y = kind.indexOf('n') >= 0 ? r.y0 : (kind.indexOf('s') >= 0 ? r.y1 : (r.y0 + r.y1) / 2);
  return { x: x, y: y };
}
function frameScale() {                                 /* screen pixels per pixel of the frame */
  var fr = cropFrame();
  return fr && V.fw ? V.s * V.fw / fr.W : 1;
}
function dragStart(e, kind) {
  var fr = cropFrame();
  if (!CROP.on || !fr) return;
  e.preventDefault(); e.stopPropagation();
  try { e.currentTarget.setPointerCapture(e.pointerId); } catch (x) {}
  CROP.drag = { kind: kind, id: e.pointerId, sx: e.clientX, sy: e.clientY, r0: { x0: CROP.rect.x0, y0: CROP.rect.y0, x1: CROP.rect.x1, y1: CROP.rect.y1 } };
}
function dragMove(e) {
  var d = CROP.drag, fr = cropFrame(), sc, dx, dy, r0, w, h, hp;
  if (!d || d.id !== e.pointerId || !fr) return;
  sc = frameScale();
  dx = (e.clientX - d.sx) / sc; dy = (e.clientY - d.sy) / sc; r0 = d.r0;
  if (d.kind === 'move') {
    w = r0.x1 - r0.x0; h = r0.y1 - r0.y0;
    CROP.rect = { x0: clamp(Math.round(r0.x0 + dx), 0, fr.W - w), y0: clamp(Math.round(r0.y0 + dy), 0, fr.H - h) };
    CROP.rect.x1 = CROP.rect.x0 + w; CROP.rect.y1 = CROP.rect.y0 + h;
  } else {
    hp = handlePoint(d.kind, r0);
    CROP.rect = dragRect(d.kind, r0, { x: hp.x + dx, y: hp.y + dy }, fr, ratioNow());
  }
  cropLayout();
}
function dragEnd(e) { if (CROP.drag && CROP.drag.id === e.pointerId) CROP.drag = null; }

/* ---------------------------------------------------------------- showing it */
function cropLayout() {
  var ui = $('#cropUi'), fr = cropFrame(), show = CROP.on && !!fr && !!V.fw && V.key === '' && !!CROP.rect, r, s, el, w, h, t;
  if (!ui) return;
  ui.classList.toggle('hidden', !show);
  if (!show) return;
  s = V.s * V.fw / fr.W; r = CROP.rect; el = $('#cropRect');
  el.style.left = (V.tx + r.x0 * s) + 'px'; el.style.top = (V.ty + r.y0 * s) + 'px';
  el.style.width = ((r.x1 - r.x0) * s) + 'px'; el.style.height = ((r.y1 - r.y0) * s) + 'px';
  w = r.x1 - r.x0; h = r.y1 - r.y0; t = ratioText(w, h);
  $('#cropSize').textContent = w + ' × ' + h + (t ? '  ·  ' + t : '');
  paintCropBarInfo();
}
function paintCropBarInfo() {
  var fr = cropFrame(), r = CROP.rect, d, el = $('#cropInfo');
  if (!el || !fr || !r) return;
  d = outDims(r.x1 - r.x0, r.y1 - r.y0);
  el.textContent = S.out.mode === 'full' ? 'wynik: ' + (r.x1 - r.x0) + ' × ' + (r.y1 - r.y0) + ' px' : 'wynik: ' + d.w + ' × ' + d.h + ' px';
}
function paintCropBar() {
  var sel = $('#cropAr'), ar = arObj(CROP.ar);
  if (!sel) return;
  sel.value = CROP.ar;
  $('#cropCustom').classList.toggle('hidden', CROP.ar !== 'custom');
  $('#cropCa').value = CROP.custom[0]; $('#cropCb').value = CROP.custom[1];
  $('#cropFlip').disabled = !ar || ar.id === 'free' || ar.id === 'orig';
  $('#cropAll').disabled = !ar || ar.id === 'free' || ar.id === 'orig';
  $('#btnCrop').setAttribute('aria-pressed', String(CROP.on));
}
function paintCropSection() {                           /* the section "Kadr" in the right panel */
  var el = $('#cropSum'), f = S.sel, fr = cropFrame(), r, bt = $('#btnCropOpen'), bc = $('#btnCropClear'), nAll = 0, t;
  S.files.forEach(function (x) { if (x.crop || x.cropAr) nAll++; });
  if (bt) bt.textContent = CROP.on ? 'Zamknij kadrowanie' : 'Kadruj zdjęcie…';
  if (!el) return;
  if (!f || !fr) t = 'Wybierz zdjęcie, żeby je przyciąć.';
  else if (f.crop || f.cropAr) {
    r = cropRectPx(f, fr);
    t = 'Kadr tego zdjęcia: ' + (r.x1 - r.x0) + ' × ' + (r.y1 - r.y0) + ' px' + (ratioText(r.x1 - r.x0, r.y1 - r.y0) ? ' (' + ratioText(r.x1 - r.x0, r.y1 - r.y0) + ')' : '') + (f.cropAr && !f.crop ? ' — wyśrodkowany, jeszcze nieoglądany' : '') + '.';
  } else t = 'To zdjęcie nie jest przycięte.';
  el.textContent = t + (nAll > 1 ? ' Zdjęć z kadrem: ' + nAll + '.' : '');
  if (bc) bc.disabled = !(f && (f.crop || f.cropAr)) && !CROP.on;
  $('#btnCrop').disabled = !f || !fr || S.running;
  if (bt) bt.disabled = !f || !fr || S.running;
  updateMods();
}

/* ---------------------------------------------------------------- open, close */
function detectAr(R, fr) {                              /* which ratio of the list is this rectangle (so that the list shows it when the tool is opened on a cropped photo) */
  var w = R.x1 - R.x0, h = R.y1 - R.y0, g = gcd(w, h), a = w / g, b = h / g, i, e;
  if (R.x0 <= 0 && R.y0 <= 0 && R.x1 >= fr.W && R.y1 >= fr.H) return null;                 /* the whole frame: the ratio stays as it was */
  if (w * fr.H === h * fr.W) return { id: 'orig', flip: false };
  for (i = 0; i < AR_LIST.length; i++) {
    e = AR_LIST[i];
    if (!e.a) continue;
    if (e.a === a && e.b === b) return { id: e.id, flip: false };                           /* (the ratios of the list are all "long : short") */
    if (e.a === b && e.b === a && e.a !== e.b) return { id: e.id, flip: true };
  }
  return { id: 'free', flip: false };
}
function cropOpen() {
  var f = S.sel, fr = cropFrame(), d;
  if (!f || !fr || CROP.on || S.running) return;
  if (S.pipette) stopPipette();
  CROP.backup = { crop: f.crop, cropAr: f.cropAr };
  CROP.rect = cropRectPx(f, fr);
  d = (f.crop || f.cropAr) ? detectAr(CROP.rect, fr) : null;
  if (d) { CROP.ar = d.id; CROP.flip = d.flip; }
  else if (!arObj(CROP.ar)) CROP.ar = 'orig';
  CROP.on = true;
  $('#stage').classList.add('cropping'); $('#cropBar').classList.remove('hidden');
  if (!f.crop && !f.cropAr && CROP.ar !== 'free') { if (CROP.ar !== 'orig') CROP.flip = fr.H > fr.W; fitRatio(true); }       /* a ratio chosen earlier is put on the new crop at once */
  paintCropBar(); paintCropSection();
  geoChanged();                                         /* the whole frame on the screen: made again when the picture on it is a cropped one */
  cropLayout();
}
function cropClose(apply, quiet) {
  var f = S.sel, fr = cropFrame(), R = CROP.rect, ar;
  if (!CROP.on) return;
  CROP.on = false; CROP.drag = null;
  $('#stage').classList.remove('cropping'); $('#cropBar').classList.add('hidden'); $('#cropUi').classList.add('hidden');
  if (f && fr && R) {
    if (apply) {
      if (R.x0 <= 0 && R.y0 <= 0 && R.x1 >= fr.W && R.y1 >= fr.H) { delete f.crop; delete f.cropAr; }
      else {
        ar = f.cropAr ? autoRectPx(fr.W, fr.H, f.cropAr) : null;
        if (ar && ar.x0 === R.x0 && ar.y0 === R.y0 && ar.x1 === R.x1 && ar.y1 === R.y1) { delete f.crop; }       /* not changed: stays "automatic" */
        else { f.crop = { x0: frac6(R.x0, fr.W), y0: frac6(R.y0, fr.H), x1: frac6(R.x1, fr.W), y1: frac6(R.y1, fr.H) }; delete f.cropAr; }
      }
    } else {
      if (CROP.backup.crop) f.crop = CROP.backup.crop; else delete f.crop;
      if (CROP.backup.cropAr) f.cropAr = CROP.backup.cropAr; else delete f.cropAr;
    }
  }
  paintCropBar(); geoChanged(quiet);
  if (apply && f && JSON.stringify(CROP.backup) !== JSON.stringify({ crop: f.crop, cropAr: f.cropAr })) staleResults();
  renderList();
}
function cropToggle() { if (CROP.on) cropClose(true); else cropOpen(); }
function cropRemove() {                                 /* "Usuń kadr" of the photo on the screen */
  var f = S.sel;
  if (!f) return;
  if (CROP.on) { CROP.rect = cropFrame() ? { x0: 0, y0: 0, x1: cropFrame().W, y1: cropFrame().H } : CROP.rect; fitRatio(); cropLayout(); return; }
  delete f.crop; delete f.cropAr;
  geoChanged(); renderList();
}

/* the picture on the screen is of another crop now: the pictures made for the old one are no good */
function geoChanged(quiet) {
  var pv = S.pv, k;
  paintOutInfo(); paintCropSection(); paintCropBarInfo();
  if (!pv) return;
  k = geoKey();
  if (pv.gk === k) return;                              /* the same picture as before (a crop that was only looked at) */
  pv.gk = k;
  pv.beforeFull = null; pv.afterFull = null; pv.bc = null;
  if (!quiet) { refreshBefore(); schedulePreview(true); }
  detailSeq++;
}
function cropAnyMod() { var f = S.sel; return !!(f && (f.crop || f.cropAr)); }

/* ---------------------------------------------------------------- the same ratio for all photos */
function cropAllPhotos() {
  var rt = ratioNow(), ar = arObj(CROP.ar), n = 0, kept = 0, a, b;
  if (!rt || !ar || ar.id === 'free' || ar.id === 'orig') { toast('Wybierz najpierw proporcje, na przykład 3 : 2.'); return; }
  a = Math.max(rt.a, rt.b); b = Math.min(rt.a, rt.b);          /* long : short; the engine turns it round for portrait photos */
  S.files.forEach(function (f) {
    if (f === S.sel || f.status === 'skipped') return;
    if (f.crop) { kept++; return; }
    f.cropAr = { w: a, h: b, auto: 1 }; n++;
  });
  renderList(); paintCropSection();
  toast('Proporcje ' + a + ' : ' + b + ' ustawione dla ' + n + ' ' + plural(n, 'zdjęcia', 'zdjęć', 'zdjęć') + ' (wyśrodkowane; pionowe zdjęcia dostają ' + b + ' : ' + a + ').' + (kept ? ' ' + kept + ' z własnym kadrem zostawiłem bez zmian.' : '') + ' To zdjęcie dostanie kadr, który tu ustawiasz.', 7000);
}

/* ---------------------------------------------------------------- wiring */
function initCrop() {
  var sel = $('#cropAr'), rect = $('#cropRect');
  AR_LIST.forEach(function (a) { var o = document.createElement('option'); o.value = a.id; o.textContent = a.name; sel.appendChild(o); });
  sel.onchange = function () {
    var fr = cropFrame();
    CROP.ar = sel.value; S.ui.cropAr = CROP.ar; save();
    if (fr && CROP.ar !== 'free' && CROP.ar !== 'orig') CROP.flip = fr.H > fr.W;
    paintCropBar(); fitRatio(false); cropLayout();
  };
  function setCustom() {
    var a = clamp(Math.round(Number($('#cropCa').value)) || 1, 1, 99), b = clamp(Math.round(Number($('#cropCb').value)) || 1, 1, 99);
    CROP.custom = [a, b]; S.ui.cropCustom = [a, b]; save(); paintCropBar(); fitRatio(false); cropLayout();
  }
  $('#cropCa').onchange = setCustom; $('#cropCb').onchange = setCustom;
  $('#cropFlip').onclick = function () { CROP.flip = !CROP.flip; fitRatio(true); cropLayout(); };
  $('#cropAll').onclick = cropAllPhotos;
  $('#cropReset').onclick = function () { var fr = cropFrame(); if (fr) { CROP.rect = { x0: 0, y0: 0, x1: fr.W, y1: fr.H }; fitRatio(true); cropLayout(); } };
  $('#cropCancel').onclick = function () { cropClose(false); };
  $('#cropDone').onclick = function () { cropClose(true); };
  $('#btnCrop').onclick = cropToggle;
  rect.addEventListener('pointerdown', function (e) { if (e.target === rect) dragStart(e, 'move'); });
  Array.prototype.forEach.call(rect.querySelectorAll('.ch'), function (h) {
    h.addEventListener('pointerdown', function (e) { dragStart(e, h.getAttribute('data-h')); });
  });
  ['pointermove'].forEach(function (t) { rect.addEventListener(t, dragMove); });
  ['pointerup', 'pointercancel'].forEach(function (t) { rect.addEventListener(t, dragEnd); });
  rect.addEventListener('dblclick', function (e) { e.stopPropagation(); cropClose(true); });
  $('#cropBar').addEventListener('pointerdown', function (e) { e.stopPropagation(); });

  /* the size of the result */
  var m = $('#outMode'), n = $('#outPx');
  m.onchange = function () { S.out.mode = m.value; if (m.value !== 'full' && !(S.out.px >= 16)) S.out.px = 3000; paintOutControls(); paintCropBarInfo(); save(); staleResults(); };
  n.onchange = function () { S.out.px = clamp(Math.round(Number(n.value)) || 3000, 16, OUT_MAX); paintOutControls(); paintCropBarInfo(); save(); staleResults(); };
  n.oninput = function () { var v = Math.round(Number(n.value)); if (v >= 16 && v <= OUT_MAX) { S.out.px = v; paintOutInfo(); paintCropBarInfo(); } };
}
function cropRestoreUi() {                              /* the ratio chosen last time and the size of the result (called by boot, once the saved settings are read) */
  if (S.ui.cropAr && arObj(S.ui.cropAr)) CROP.ar = S.ui.cropAr;
  if (S.ui.cropCustom && S.ui.cropCustom.length === 2) CROP.custom = S.ui.cropCustom.map(function (v) { return clamp(Math.round(Number(v)) || 1, 1, 99); });
  paintCropBar(); paintOutControls();
}

/* the section "Kadr" of the right panel */
function bodyCrop(box) {
  var row = document.createElement('div'), p = document.createElement('p'), q = document.createElement('p');
  row.className = 'row-btns';
  row.innerHTML = '<button type="button" class="btn sm primary" id="btnCropOpen" title="Przytnij zdjęcie: kadr z uchwytami, siatką i proporcjami (klawisz C)">Kadruj zdjęcie…</button><button type="button" class="btn sm" id="btnCropClear" title="Usuwa kadr tego zdjęcia">Usuń kadr</button>';
  p.className = 'hint'; p.id = 'cropSum';
  q.className = 'hint'; q.textContent = 'Kadr jest osobny dla każdego zdjęcia. Te same proporcje dla wszystkich zdjęć ustawisz w narzędziu kadru przyciskiem „Dla wszystkich”. Rozmiar gotowych zdjęć wybierasz na dole, w „Rozmiar zapisu”.';
  box.appendChild(row); box.appendChild(p); box.appendChild(q);
  row.querySelector('#btnCropOpen').onclick = cropToggle;
  row.querySelector('#btnCropClear').onclick = cropRemove;
  paintCropSection();
}
