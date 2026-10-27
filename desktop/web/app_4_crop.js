/* ====================================================================== part 4b: the crop and the size of the result
 * The crop is a rectangle of the picture AS IT IS SHOWN (the camera's orientation applied), kept for every photo on its own:
 *   f.crop    = {x0, y0, x1, y1} in millionths of the picture's width / height (a rectangle the user drew)
 *   f.cropAr  = {w, h, auto: 1}  "this ratio, as big as it goes, in the middle" for photos that were not opened (the engine works the
 *               rectangle out from the real size of each photo, and turns the ratio round on a portrait photo)
 *   f.rot     = the straightening angle in thousandths of a degree (positive = the picture is turned clockwise): like the angle of the
 *               crop tool of Lightroom, the picture is turned under the crop, which is a rectangle of the TURNED picture and always lies inside it
 *   f.turn    = quarter turns clockwise (1..3) of the picture; the crop is then a rectangle of the picture as it is turned
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
var CROP = { on: false, rect: null, ar: 'orig', flip: false, drag: null, backup: null, custom: [4, 5], rot: 0, turn: 0, ruler: false, spin: null, rbase: null, fine: false };
var RING = 46;                                           /* the width (screen pixels) of the zone round the crop in which a drag turns the picture */
function copyRect(r) { return { x0: r.x0, y0: r.y0, x1: r.x1, y1: r.y1 }; }
var ROT_MAX = 45;                                        /* the angle goes from -45 to +45 degrees (more is a quarter turn) */
var OUT_MAX = 16384, OUT_MAX_PIX = 160e6;

function gcd(a, b) { while (b) { var t = a % b; a = b; b = t; } return a; }
function frac6(px, n) { return Math.round(px / n * 1e6); }              /* pixels -> millionths */
function px6(f, n) { return Math.floor(f * n / 1e6 + 0.5); }            /* millionths -> pixels, the way the engine does it */

/* ---------------------------------------------------------------- what the engine is told */
function geoKey() {                                     /* what the picture on the screen is cropped to ('' = the whole frame) */
  var f = S.sel, k = '';
  if (!f) return '';
  if (CROP.on) return CROP.turn ? 'T' + CROP.turn : '';       /* the tool is open: the whole frame (turned by quarter turns, but not straightened: that is done on the screen) */
  if (f.crop) k = 'r' + [f.crop.x0, f.crop.y0, f.crop.x1, f.crop.y1].join(',');
  else if (f.cropAr) k = 'a' + f.cropAr.w + ':' + f.cropAr.h;
  if (f.turn) k += 'T' + f.turn;
  if (f.rot) k += 'R' + f.rot;
  return k;
}
function hasGeo(f) { return !!(f && (f.crop || f.cropAr || f.rot || f.turn)); }
function geoX(f) {                                      /* the crop of photo f as the engine's extra parameters */
  var x = {};
  if (!f) return x;
  if (f.crop) { x.crop_x0 = f.crop.x0; x.crop_y0 = f.crop.y0; x.crop_x1 = f.crop.x1; x.crop_y1 = f.crop.y1; }
  else if (f.cropAr) { x.crop_ar_w = f.cropAr.w; x.crop_ar_h = f.cropAr.h; x.crop_ar_auto = f.cropAr.auto ? 1 : 0; }
  if (f.turn) x.turn = f.turn;
  if (f.rot) x.crop_rot = f.rot;
  return x;
}
function frameX() { return CROP.turn ? { turn: CROP.turn } : {}; }        /* the whole frame while the tool is open */
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
  mergeX(p.x, CROP.on ? frameX() : geoX(S.sel));
  p.x.hq = S.hq ? 1 : 0;                                /* the previews are counted the way the files are (4:4:4, every pixel exact): the colour grain is as strong as in the file */
  return p;
}
function pvPlain(size, quality) {
  var p = plainParams(size, quality);
  mergeX(p.x, CROP.on ? frameX() : geoX(S.sel));
  p.x.hq = S.hq ? 1 : 0;
  return p;
}
function previewSize() {                                /* the half size picture; a small crop is made from the full size, so that it keeps its detail */
  var pv = S.pv, f = S.sel, fr = cropFrame();
  if (CROP.on || !pv || !f || !f.crop || !fr) return 102;
  var long = Math.max((f.crop.x1 - f.crop.x0) / 1e6 * fr.W, (f.crop.y1 - f.crop.y0) / 1e6 * fr.H);
  return long >= 2800 ? 102 : 101;
}

/* ---------------------------------------------------------------- the rectangle in pixels, and the size that comes out */
function cropFrame() {                                 /* the picture as it is shown, turned by the quarter turns: pv.frameW x frameH is the photo as the camera shows it */
  var pv = S.pv, f = S.sel, t;
  if (!pv || !pv.frameW) return null;
  t = (CROP.on ? CROP.turn : (f && f.turn)) || 0;
  return t & 1 ? { W: pv.frameH, H: pv.frameW } : { W: pv.frameW, H: pv.frameH };
}

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
  refit();
}

/* ---------------------------------------------------------------- the turned picture: the crop has to lie inside it
 * The picture is turned by CROP.rot round its middle; the crop is a rectangle of the screen frame (axis aligned) and its four corners
 * must be inside the turned picture (the same rule as in the engine, fd_rot_fit in fd_rot.h). */
function turnTrig(md) { var t = md / 1000 * Math.PI / 180; return { c: Math.cos(t), s: Math.sin(t) }; }
function rectInQuad(R, fr, md, tol) {
  var T, i, x, y, u, v;
  if (!md) return R.x0 >= -tol && R.y0 >= -tol && R.x1 <= fr.W + tol && R.y1 <= fr.H + tol;
  T = turnTrig(md);
  for (i = 0; i < 4; i++) {
    x = (i & 1 ? R.x1 : R.x0) - fr.W / 2; y = (i & 2 ? R.y1 : R.y0) - fr.H / 2;
    u = T.c * x + T.s * y; v = -T.s * x + T.c * y;
    if (Math.abs(u) > fr.W / 2 + tol || Math.abs(v) > fr.H / 2 + tol) return false;
  }
  return true;
}
function quadScale(R, fr, md) {                         /* how far the rectangle can stay as it is (1) or must shrink round its middle (< 1); 0 = its middle is outside */
  var T = turnTrig(md), mx = (R.x0 + R.x1) / 2 - fr.W / 2, my = (R.y0 + R.y1) / 2 - fr.H / 2, hw = (R.x1 - R.x0) / 2, hh = (R.y1 - R.y0) / 2;
  var ax = T.c * mx + T.s * my, ay = -T.s * mx + T.c * my, best = 1, sx, sy, a, b, A, L;
  if (Math.abs(ax) > fr.W / 2 || Math.abs(ay) > fr.H / 2) return 0;
  for (sx = -1; sx <= 1; sx += 2) for (sy = -1; sy <= 1; sy += 2) for (a = 0; a < 2; a++) {
    b = a ? (-T.s * sx * hw + T.c * sy * hh) : (T.c * sx * hw + T.s * sy * hh);
    A = a ? ay : ax; L = a ? fr.H / 2 : fr.W / 2;
    if (b === 0) continue;
    best = Math.min(best, (L - (b > 0 ? A : -A)) / Math.abs(b));
  }
  return Math.max(0, best);
}
function refit() {                                      /* after the angle or the ratio changed: shrink the crop (round its middle) until it is inside the turned picture */
  var fr = cropFrame(), R = CROP.rect, rt = ratioNow(), cx, cy, s, w, h, sz, n, tries = 0;
  if (!fr || !R || !CROP.rot) return;
  if (rectInQuad(R, fr, CROP.rot, 0.5)) return;
  cx = (R.x0 + R.x1) / 2; cy = (R.y0 + R.y1) / 2;
  s = quadScale(R, fr, CROP.rot);
  if (s <= 0) {                                         /* the middle itself is outside: the crop goes to the middle of the picture first */
    cx = fr.W / 2; cy = fr.H / 2;
    R = { x0: cx - (R.x1 - R.x0) / 2, y0: cy - (R.y1 - R.y0) / 2, x1: cx + (R.x1 - R.x0) / 2, y1: cy + (R.y1 - R.y0) / 2 };
    s = quadScale(R, fr, CROP.rot);
  }
  w = (R.x1 - R.x0) * Math.min(1, s); h = (R.y1 - R.y0) * Math.min(1, s);
  for (;;) {
    sz = rt ? sizeFor(rt, w, Math.floor(fr.W), Math.floor(fr.H)) : { w: Math.max(1, Math.floor(w)), h: Math.max(1, Math.floor(h)) };
    n = { x0: Math.round(cx - sz.w / 2), y0: Math.round(cy - sz.h / 2) };
    n.x1 = n.x0 + sz.w; n.y1 = n.y0 + sz.h;
    if (rectInQuad(n, fr, CROP.rot, 1) || tries++ > 60 || (sz.w <= 2 && sz.h <= 2)) break;
    h = h * (w - 1) / Math.max(1, w); w = w - 1;
  }
  CROP.rect = n;
}
function setAngle(deg) {                                /* the angle in degrees; the crop is made again from the one it was when the turning started (so it grows back when the angle goes back, also after a gesture ended: until the crop itself is moved or its ratio changed) */
  var md = Math.round(clamp(Number(deg) || 0, -ROT_MAX, ROT_MAX) * 100) * 10;
  if (md === CROP.rot || !CROP.rect) return;
  if (!CROP.rbase) CROP.rbase = copyRect(CROP.rect);
  CROP.rot = md;
  CROP.rect = copyRect(CROP.rbase);
  refit(); cropLayout(); paintCropBar();
}
function angleEnd() { CROP.fine = false; CROP.spin = null; cropLayout(); }                /* CROP.rbase stays: the angle back to 0 brings the crop back (until the crop itself is changed by hand) */
function degText(md) { return String(Math.round(md / 10) / 100).replace('.', ','); }
function angleFromRuler(x0, y0, x1, y1) {              /* a line drawn along the horizon (or a vertical): the angle that makes it straight */
  var a = Math.atan2(y1 - y0, x1 - x0) * 180 / Math.PI, f = -a;
  f = (((f + 45) % 90) + 90) % 90 - 45;
  return f;
}
function toggleRuler(on) {                              /* "Wyprostuj": draw a line along the horizon (or a wall): the picture is turned so that the line is straight */
  CROP.ruler = on === undefined ? !CROP.ruler : !!on;
  $('#cropRulerPad').classList.toggle('hidden', !CROP.ruler || !CROP.on);
  $('#cropRulerLine').classList.add('hidden');
  paintCropBar();
}
function rulerDown(e) {
  var r = $('#stage').getBoundingClientRect();
  e.preventDefault(); e.stopPropagation();
  try { e.currentTarget.setPointerCapture(e.pointerId); } catch (x) {}
  CROP.rl = { id: e.pointerId, x0: e.clientX - r.left, y0: e.clientY - r.top, x1: e.clientX - r.left, y1: e.clientY - r.top };
}
function rulerMove(e) {
  var l = CROP.rl, r = $('#stage').getBoundingClientRect(), el = $('#cropRulerLine'), len;
  if (!l || l.id !== e.pointerId) return;
  l.x1 = e.clientX - r.left; l.y1 = e.clientY - r.top;
  len = Math.hypot(l.x1 - l.x0, l.y1 - l.y0);
  el.classList.remove('hidden');
  el.style.left = l.x0 + 'px'; el.style.top = l.y0 + 'px'; el.style.width = len + 'px';
  el.style.transform = 'rotate(' + Math.atan2(l.y1 - l.y0, l.x1 - l.x0) + 'rad)';
}
function rulerUp(e) {
  var l = CROP.rl;
  if (!l || l.id !== e.pointerId) return;
  CROP.rl = null;
  if (Math.hypot(l.x1 - l.x0, l.y1 - l.y0) >= 24) { setAngle(CROP.rot / 1000 + angleFromRuler(l.x0, l.y0, l.x1, l.y1)); }
  toggleRuler(false);
}
function ringDown(e) {                                  /* a drag in the zone round the crop turns the picture, like in Lightroom */
  var b = $('#cropRect').getBoundingClientRect(), cx = b.left + b.width / 2, cy = b.top + b.height / 2;
  if (!CROP.on) return;
  e.preventDefault(); e.stopPropagation();
  try { e.currentTarget.setPointerCapture(e.pointerId); } catch (x) {}
  CROP.spin = { id: e.pointerId, cx: cx, cy: cy, a0: Math.atan2(e.clientY - cy, e.clientX - cx), rot0: CROP.rot };
  cropLayout();
}
function ringMove(e) {
  var d = CROP.spin, a, da;
  if (!d || d.id !== e.pointerId) return;
  a = Math.atan2(e.clientY - d.cy, e.clientX - d.cx);
  da = (a - d.a0) * 180 / Math.PI;
  da = ((da + 540) % 360) - 180;                        /* the shortest way round */
  setAngle(d.rot0 / 1000 + da);
}
function ringUp(e) { if (CROP.spin && CROP.spin.id === e.pointerId) angleEnd(); }

function cropTurn(dir) {                                /* a quarter turn of the picture, clockwise (dir > 0) or the other way */
  var fr = cropFrame(), R = CROP.rect, W, H;
  if (!CROP.on || !fr || !R) return;
  W = fr.W; H = fr.H;
  CROP.rect = dir > 0 ? { x0: H - R.y1, y0: R.x0, x1: H - R.y0, y1: R.x1 } : { x0: R.y0, y0: W - R.x1, x1: R.y1, y1: W - R.x0 };
  CROP.rbase = null;
  CROP.turn = (CROP.turn + (dir > 0 ? 1 : 3)) & 3;
  if (CROP.ar !== 'orig' && CROP.ar !== 'free') CROP.flip = !CROP.flip;
  paintCropBar(); geoChanged(); cropLayout();
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
  CROP.rbase = null;
  CROP.drag = { kind: kind, id: e.pointerId, sx: e.clientX, sy: e.clientY, r0: { x0: CROP.rect.x0, y0: CROP.rect.y0, x1: CROP.rect.x1, y1: CROP.rect.y1 } };
}
function dragMove(e) {
  var d = CROP.drag, fr = cropFrame(), sc, dx, dy, r0, w, h, hp;
  if (!d || d.id !== e.pointerId || !fr) return;
  sc = frameScale();
  dx = (e.clientX - d.sx) / sc; dy = (e.clientY - d.sy) / sc; r0 = d.r0;
  w = r0.x1 - r0.x0; h = r0.y1 - r0.y0;
  var mv = function (tx, ty) {
    var r = { x0: clamp(Math.round(r0.x0 + dx * tx), 0, fr.W - w), y0: clamp(Math.round(r0.y0 + dy * ty), 0, fr.H - h) };
    r.x1 = r.x0 + w; r.y1 = r.y0 + h;
    return r;
  };
  if (d.kind === 'move') {
    CROP.rect = mv(1, 1);
    if (CROP.rot && !rectInQuad(CROP.rect, fr, CROP.rot, 0.5)) {         /* the picture is turned: slide along its edges (first sideways, then up or down) */
      var tx = latest(function (t) { return mv(t, 0); }), ty;
      ty = latest(function (t) { return mv(tx, t); });
      CROP.rect = mv(tx, ty);
    }
  } else {
    hp = handlePoint(d.kind, r0);
    var rt = ratioNow(), mk = function (t) { return dragRect(d.kind, r0, { x: hp.x + dx * t, y: hp.y + dy * t }, fr, rt); };
    CROP.rect = mk(1);
    if (CROP.rot && !rectInQuad(CROP.rect, fr, CROP.rot, 0.5)) CROP.rect = mk(latest(mk));        /* ... or stops at the edge of the turned picture */
  }
  cropLayout();
}
function latest(mk) {                                   /* the biggest t in 0..1 for which the rectangle mk(t) is still inside the turned picture (mk(0) is) */
  var fr = cropFrame(), lo = 0, hi = 1, i, m;
  for (i = 0; i < 22; i++) { m = (lo + hi) / 2; if (rectInQuad(mk(m), fr, CROP.rot, 0.5)) lo = m; else hi = m; }
  return lo;
}
function dragEnd(e) { if (CROP.drag && CROP.drag.id === e.pointerId) CROP.drag = null; }

/* ---------------------------------------------------------------- showing it */
function cropLayout() {
  var ui = $('#cropUi'), fr = cropFrame(), show = CROP.on && !!fr && !!V.fw && V.key === geoKey() && !!CROP.rect, r, s, el, w, h, t, g;
  if (!ui) return;
  ui.classList.toggle('hidden', !show);
  if (imgEl) imgEl.style.transform = show && CROP.rot ? 'rotate(' + (CROP.rot / 1000) + 'deg)' : '';        /* the picture is turned on the screen; the engine does it for the file */
  if (!show) return;
  s = V.s * V.fw / fr.W; r = CROP.rect; el = $('#cropRect'); g = $('#cropRing');
  el.style.left = (V.tx + r.x0 * s) + 'px'; el.style.top = (V.ty + r.y0 * s) + 'px';
  el.style.width = ((r.x1 - r.x0) * s) + 'px'; el.style.height = ((r.y1 - r.y0) * s) + 'px';
  g.style.left = (V.tx + r.x0 * s - RING) + 'px'; g.style.top = (V.ty + r.y0 * s - RING) + 'px';
  g.style.width = ((r.x1 - r.x0) * s + 2 * RING) + 'px'; g.style.height = ((r.y1 - r.y0) * s + 2 * RING) + 'px';
  el.classList.toggle('fine', !!CROP.spin || CROP.fine);
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
  $('#cropAng').value = CROP.rot / 1000; $('#cropAngN').value = String(Math.round(CROP.rot / 10) / 100);
  $('#cropRuler').setAttribute('aria-pressed', String(CROP.ruler)); $('#cropRuler').classList.toggle('on', CROP.ruler);
}
function paintCropSection() {                           /* the section "Kadr" in the right panel */
  var el = $('#cropSum'), f = S.sel, fr = cropFrame(), r, bt = $('#btnCropOpen'), bc = $('#btnCropClear'), nAll = 0, t;
  S.files.forEach(function (x) { if (hasGeo(x)) nAll++; });
  if (bt) bt.textContent = CROP.on ? 'Zamknij kadrowanie' : 'Kadruj i prostuj…';
  if (!el) return;
  if (!f || !fr) t = 'Wybierz zdjęcie, żeby je przyciąć.';
  else if (hasGeo(f)) {
    r = cropRectPx(f, fr);
    t = (f.crop || f.cropAr ? 'Kadr tego zdjęcia: ' + (r.x1 - r.x0) + ' × ' + (r.y1 - r.y0) + ' px' + (ratioText(r.x1 - r.x0, r.y1 - r.y0) ? ' (' + ratioText(r.x1 - r.x0, r.y1 - r.y0) + ')' : '') + (f.cropAr && !f.crop ? ' — wyśrodkowany, jeszcze nieoglądany' : '') : 'To zdjęcie nie jest przycięte')
      + (f.rot ? ', obrócone o ' + degText(f.rot) + '°' : '') + (f.turn ? ', ' + (f.turn === 1 ? 'obrócone o 90° w prawo' : (f.turn === 2 ? 'obrócone o 180°' : 'obrócone o 90° w lewo')) : '') + '.';
  } else t = 'To zdjęcie nie jest przycięte ani obrócone.';
  el.textContent = t + (nAll > 1 ? ' Zdjęć z kadrem: ' + nAll + '.' : '');
  if (bc) bc.disabled = !hasGeo(f) && !CROP.on;
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
  CROP.backup = { crop: f.crop, cropAr: f.cropAr, rot: f.rot || 0, turn: f.turn || 0 };
  CROP.turn = f.turn || 0; CROP.rot = f.rot || 0; CROP.rbase = null; CROP.ruler = false; CROP.fine = false; CROP.spin = null;
  CROP.rect = cropRectPx(f, fr);
  d = (f.crop || f.cropAr) ? detectAr(CROP.rect, fr) : null;
  if (d) { CROP.ar = d.id; CROP.flip = d.flip; }
  else if (!arObj(CROP.ar)) CROP.ar = 'orig';
  CROP.on = true;
  $('#stage').classList.add('cropping'); $('#cropBar').classList.remove('hidden');
  if (!f.crop && !f.cropAr && CROP.ar !== 'free') { if (CROP.ar !== 'orig') CROP.flip = fr.H > fr.W; fitRatio(true); }       /* a ratio chosen earlier is put on the new crop at once */
  refit();
  $('#cropRulerPad').classList.add('hidden'); $('#cropRulerLine').classList.add('hidden');
  paintCropBar(); paintCropSection();
  geoChanged();                                         /* the whole frame on the screen: made again when the picture on it is a cropped one */
  if (V.fit && V.fw) setFit();                          /* (room round the picture for the turning zone) */
  cropLayout();
}
function cropClose(apply, quiet) {
  var f = S.sel, fr = cropFrame(), R = CROP.rect, ar, rot = CROP.rot, turn = CROP.turn;
  if (!CROP.on) return;
  CROP.on = false; CROP.drag = null; CROP.spin = null; CROP.rbase = null; CROP.fine = false; CROP.ruler = false;
  $('#stage').classList.remove('cropping'); $('#cropBar').classList.add('hidden'); $('#cropUi').classList.add('hidden');
  $('#cropRulerPad').classList.add('hidden'); $('#cropRulerLine').classList.add('hidden');
  if (imgEl) imgEl.style.transform = '';
  if (f && fr && R) {
    if (apply) {
      if (R.x0 <= 0 && R.y0 <= 0 && R.x1 >= fr.W && R.y1 >= fr.H && !rot) { delete f.crop; delete f.cropAr; }
      else {
        ar = f.cropAr && !rot && turn === (f.turn || 0) ? autoRectPx(fr.W, fr.H, f.cropAr) : null;
        if (ar && ar.x0 === R.x0 && ar.y0 === R.y0 && ar.x1 === R.x1 && ar.y1 === R.y1) { delete f.crop; }       /* not changed: stays "automatic" */
        else { f.crop = { x0: frac6(R.x0, fr.W), y0: frac6(R.y0, fr.H), x1: frac6(R.x1, fr.W), y1: frac6(R.y1, fr.H) }; delete f.cropAr; }
      }
      if (rot) f.rot = rot; else delete f.rot;
      if (turn) f.turn = turn; else delete f.turn;
    } else {
      if (CROP.backup.crop) f.crop = CROP.backup.crop; else delete f.crop;
      if (CROP.backup.cropAr) f.cropAr = CROP.backup.cropAr; else delete f.cropAr;
    }
  }
  paintCropBar(); geoChanged(quiet);
  if (V.fit && V.fw) setFit();
  if (apply && f && JSON.stringify(CROP.backup) !== JSON.stringify({ crop: f.crop, cropAr: f.cropAr, rot: f.rot || 0, turn: f.turn || 0 })) staleResults();
  renderList();
}
function cropToggle() { if (CROP.on) cropClose(true); else cropOpen(); }
function cropRemove() {                                 /* "Usuń kadr" of the photo on the screen */
  var f = S.sel;
  if (!f) return;
  if (CROP.on) { CROP.rect = cropFrame() ? { x0: 0, y0: 0, x1: cropFrame().W, y1: cropFrame().H } : CROP.rect; CROP.rot = 0; CROP.rbase = null; fitRatio(); paintCropBar(); cropLayout(); return; }
  if (!hasGeo(f)) return;
  delete f.crop; delete f.cropAr; delete f.rot; delete f.turn;
  geoChanged(); renderList(); staleResults();
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
function cropAnyMod() { return hasGeo(S.sel); }

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
    CROP.rbase = null;
    paintCropBar(); fitRatio(false); cropLayout();
  };
  function setCustom() {
    var a = clamp(Math.round(Number($('#cropCa').value)) || 1, 1, 99), b = clamp(Math.round(Number($('#cropCb').value)) || 1, 1, 99);
    CROP.custom = [a, b]; S.ui.cropCustom = [a, b]; CROP.rbase = null; save(); paintCropBar(); fitRatio(false); cropLayout();
  }
  $('#cropCa').onchange = setCustom; $('#cropCb').onchange = setCustom;
  $('#cropFlip').onclick = function () { CROP.flip = !CROP.flip; CROP.rbase = null; fitRatio(true); cropLayout(); };
  $('#cropAll').onclick = cropAllPhotos;
  $('#cropReset').onclick = function () { var fr = cropFrame(); if (fr) { CROP.rot = 0; CROP.rbase = null; CROP.rect = { x0: 0, y0: 0, x1: fr.W, y1: fr.H }; fitRatio(true); paintCropBar(); cropLayout(); } };
  var ang = $('#cropAng'), angN = $('#cropAngN'), ring = $('#cropRing'), pad = $('#cropRulerPad');
  ang.oninput = function () { CROP.fine = true; setAngle(Number(ang.value)); };
  ang.onchange = angleEnd;
  ang.addEventListener('pointerup', angleEnd);
  ang.ondblclick = function () { setAngle(0); angleEnd(); };
  angN.onchange = function () { setAngle(Number(angN.value)); angleEnd(); paintCropBar(); };
  $('#cropRuler').onclick = function () { toggleRuler(); };
  $('#cropTurnL').onclick = function () { cropTurn(-1); };
  $('#cropTurnR').onclick = function () { cropTurn(1); };
  ring.addEventListener('pointerdown', ringDown); ring.addEventListener('pointermove', ringMove);
  ring.addEventListener('pointerup', ringUp); ring.addEventListener('pointercancel', ringUp);
  pad.addEventListener('pointerdown', rulerDown); pad.addEventListener('pointermove', rulerMove);
  pad.addEventListener('pointerup', rulerUp); pad.addEventListener('pointercancel', rulerUp);
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
  m.onchange = function () { S.out.mode = m.value; if (m.value !== 'full' && !(S.out.px >= 16)) S.out.px = 3000; paintOutControls(); paintCropBarInfo(); save(); staleResults(); dropDetail(); updateMods(); };
  n.onchange = function () { S.out.px = clamp(Math.round(Number(n.value)) || 3000, 16, OUT_MAX); paintOutControls(); paintCropBarInfo(); save(); staleResults(); dropDetail(); };
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
  row.innerHTML = '<button type="button" class="btn sm primary" id="btnCropOpen" title="Przytnij i wyprostuj zdjęcie: kadr z uchwytami, siatką, proporcjami i obrotem (klawisz C)">Kadruj i prostuj…</button><button type="button" class="btn sm" id="btnCropClear" title="Usuwa kadr i obrót tego zdjęcia">Usuń kadr i obrót</button>';
  p.className = 'hint'; p.id = 'cropSum';
  q.className = 'hint'; q.textContent = 'Kadr i obrót są osobne dla każdego zdjęcia. W narzędziu kadru: suwak „Kąt” prostuje zdjęcie (można też przeciągać myszą tuż przy ramce, na zewnątrz, albo użyć „Wyprostuj” i narysować linię wzdłuż horyzontu), a „⟲ 90°” i „⟳ 90°” obracają zdjęcie. Te same proporcje dla wszystkich zdjęć ustawisz przyciskiem „Dla wszystkich”. Rozmiar gotowych zdjęć wybierasz w dziale „Zapis”.';
  box.appendChild(row); box.appendChild(p); box.appendChild(q);
  row.querySelector('#btnCropOpen').onclick = cropToggle;
  row.querySelector('#btnCropClear').onclick = cropRemove;
  paintCropSection();
}
