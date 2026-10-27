/* ====================================================================== part 3b: the tone curves (RGB, red, green, blue)
 * A curve is a list of points [input, output], both 0..255, input growing. The straight line [[0,0],[255,255]] is "no curve".
 * The curves belong to the picture (S.scene.crv = {m, r, g, b}), not to the film; the engine applies them after the film's own
 * curves. Master first, then the channel curves. A black and white film only has the master curve. */
var CRV_KEYS = ['m', 'r', 'g', 'b'], CRV_NAMES = ['RGB', 'R', 'G', 'B'], CRV_MAX = 12;
var CRV_COLORS = ['', '#e5534b', '#43b05c', '#4f8dff'];
var CRV_PRESETS = [
  { id: 'line', name: 'Prosta', pts: [[0, 0], [255, 255]] },
  { id: 'soft', name: 'Łagodny kontrast', pts: [[0, 0], [64, 52], [192, 204], [255, 255]] },
  { id: 'hard', name: 'Mocny kontrast', pts: [[0, 0], [64, 40], [192, 220], [255, 255]] },
  { id: 'fade', name: 'Matowe czernie', pts: [[0, 28], [255, 255]] },
  { id: 'bright', name: 'Jaśniej', pts: [[0, 0], [128, 150], [255, 255]] },
  { id: 'dark', name: 'Ciemniej', pts: [[0, 0], [128, 108], [255, 255]] }
];
var CRV_DEFAULT = [[0, 0], [255, 255]];
var crvSel = -1, crvDrag = -1;                                      /* the selected point of the curve on the screen */

function crvChannel() { return isMono() ? 0 : clamp(S.ui.crv || 0, 0, 3); }
function crvIsDefault(pts) { return pts.length === 2 && pts[0][0] === 0 && pts[0][1] === 0 && pts[1][0] === 255 && pts[1][1] === 255; }
function crvGet(k) {
  var c = S.scene.crv, p = c && c[CRV_KEYS[k]];
  return p && p.length >= 2 ? p : CRV_DEFAULT.map(function (q) { return q.slice(); });
}
function crvSet(k, pts) {
  var c = S.scene.crv || (S.scene.crv = {});
  if (crvIsDefault(pts)) delete c[CRV_KEYS[k]]; else c[CRV_KEYS[k]] = pts;
  if (!Object.keys(c).length) delete S.scene.crv;
}
function crvAnyMod() { var c = S.scene.crv; return !!c && Object.keys(c).some(function (k) { return c[k] && c[k].length >= 2 && !crvIsDefault(c[k]); }); }
function crvReset() { delete S.scene.crv; crvSel = -1; }

/* the curve the engine draws through the points (monotone cubic Hermite, like lb_crv_init in lookbuild.h): 256 outputs */
function crvTable(pts) {
  var n = pts.length, m = [], d = [], j, t = [], x;
  for (j = 0; j + 1 < n; j++) { var dx = pts[j + 1][0] - pts[j][0]; m[j] = dx > 0 ? (pts[j + 1][1] - pts[j][1]) / dx : 0; }
  d[0] = m[0]; d[n - 1] = m[n - 2];
  for (j = 1; j + 1 < n; j++) d[j] = m[j - 1] * m[j] <= 0 ? 0 : 2 * m[j - 1] * m[j] / (m[j - 1] + m[j]);
  for (x = 0; x < 256; x++) {
    if (x <= pts[0][0]) { t.push(pts[0][1]); continue; }
    if (x >= pts[n - 1][0]) { t.push(pts[n - 1][1]); continue; }
    j = 0; while (j + 2 < n && x >= pts[j + 1][0]) j++;
    var h = pts[j + 1][0] - pts[j][0], u = (x - pts[j][0]) / h, u2 = u * u, u3 = u2 * u;
    t.push(clamp((2 * u3 - 3 * u2 + 1) * pts[j][1] + (u3 - 2 * u2 + u) * h * d[j] + (-2 * u3 + 3 * u2) * pts[j + 1][1] + (u3 - u2) * h * d[j + 1], 0, 255));
  }
  return t;
}

/* the extras sent to the engine: crvK_n, crvK_xJ, crvK_yJ (a black and white film: the master curve only) */
function crvParams(scene, mono, x) {
  var c = scene.crv;
  if (!c) return;
  CRV_KEYS.forEach(function (key, k) {
    var pts = c[key];
    if (!pts || pts.length < 2 || (mono && k > 0)) return;
    x['crv' + k + '_n'] = Math.min(pts.length, CRV_MAX);
    pts.slice(0, CRV_MAX).forEach(function (p, j) { x['crv' + k + '_x' + j] = p[0]; x['crv' + k + '_y' + j] = p[1]; });
  });
}

function bodyCurves(box) {
  var mono = isMono(), tabs = document.createElement('div'), wrap = document.createElement('div'), cv = document.createElement('canvas'),
      info = document.createElement('div'), chips = document.createElement('div'), p = document.createElement('p'), bar = document.createElement('div');
  var ch = crvChannel(), W = 0;
  if (!mono) {
    tabs.className = 'tabs2'; tabs.setAttribute('role', 'tablist');
    CRV_NAMES.forEach(function (nm, i) {
      var b = document.createElement('button'); b.type = 'button'; b.textContent = nm; b.setAttribute('role', 'tab');
      b.setAttribute('aria-selected', String(ch === i));
      b.className = (S.scene.crv && S.scene.crv[CRV_KEYS[i]] && !crvIsDefault(S.scene.crv[CRV_KEYS[i]])) ? 'mod' : '';
      if (i > 0) b.style.setProperty('--tc', CRV_COLORS[i]);
      b.onclick = function () { S.ui.crv = i; crvSel = -1; save(); renderAdjust(); };
      tabs.appendChild(b);
    });
    box.appendChild(tabs);
  }
  p.className = 'hint';
  p.textContent = 'Kliknij na wykresie, żeby dodać punkt, i przeciągnij go. Dwukrotne kliknięcie na punkcie (albo klawisz Delete) usuwa go. Poziomo: jasność wejściowa, pionowo: wynikowa. Strzałki przesuwają zaznaczony punkt.';
  wrap.className = 'crvwrap'; cv.className = 'crvcv'; cv.tabIndex = 0; cv.setAttribute('role', 'img');
  cv.setAttribute('aria-label', 'Wykres krzywej ' + CRV_NAMES[ch]);
  info.className = 'crvinfo small muted'; info.textContent = ' ';
  wrap.appendChild(cv); wrap.appendChild(info);
  box.appendChild(p); box.appendChild(wrap);

  /* the colours come from the page's theme */
  function css(name) { return getComputedStyle(cv).getPropertyValue(name).trim() || '#888'; }
  function size() {
    var w = Math.max(120, Math.round(cv.clientWidth)), dpr = window.devicePixelRatio || 1;
    if (w !== W || cv.width !== Math.round(w * dpr)) { W = w; cv.width = Math.round(w * dpr); cv.height = Math.round(w * dpr); }
    return dpr;
  }
  function px(x) { return 6 + (x / 255) * (W - 12); }
  function py(y) { return W - 6 - (y / 255) * (W - 12); }
  function draw() {
    var dpr = size(), g = cv.getContext('2d'), pts = crvGet(ch), i, k;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, W, W);
    g.fillStyle = css('--panel2'); g.fillRect(0, 0, W, W);
    g.strokeStyle = css('--line2'); g.lineWidth = 1;
    for (i = 0; i <= 4; i++) {
      g.beginPath(); g.moveTo(px(i * 63.75) + .5, py(0)); g.lineTo(px(i * 63.75) + .5, py(255)); g.stroke();
      g.beginPath(); g.moveTo(px(0), py(i * 63.75) + .5); g.lineTo(px(255), py(i * 63.75) + .5); g.stroke();
    }
    g.strokeStyle = css('--ink3'); g.setLineDash([3, 3]);
    g.beginPath(); g.moveTo(px(0), py(0)); g.lineTo(px(255), py(255)); g.stroke(); g.setLineDash([]);
    /* the curves of the other channels, faint */
    for (k = 0; k < (mono ? 1 : 4); k++) {
      if (k === ch || !crvGet(k) || crvIsDefault(crvGet(k))) continue;
      g.globalAlpha = .35; g.strokeStyle = k === 0 ? css('--ink') : CRV_COLORS[k]; g.lineWidth = 1.5; drawLine(g, crvTable(crvGet(k)));
    }
    g.globalAlpha = 1;
    g.strokeStyle = ch === 0 ? css('--ink') : CRV_COLORS[ch]; g.lineWidth = 2; drawLine(g, crvTable(pts));
    for (i = 0; i < pts.length; i++) {
      g.beginPath(); g.arc(px(pts[i][0]), py(pts[i][1]), i === crvSel ? 5.5 : 4.5, 0, 6.2832);
      g.fillStyle = i === crvSel ? (ch === 0 ? css('--accent') : CRV_COLORS[ch]) : css('--panel'); g.fill();
      g.lineWidth = 1.8; g.strokeStyle = ch === 0 ? css('--ink') : CRV_COLORS[ch]; g.stroke();
    }
  }
  function drawLine(g, t) {
    var x; g.beginPath();
    for (x = 0; x < 256; x++) { if (x === 0) g.moveTo(px(x), py(t[x])); else g.lineTo(px(x), py(t[x])); }
    g.stroke();
  }
  function say(i) { var pts = crvGet(ch); info.textContent = i >= 0 && pts[i] ? 'wejście ' + pts[i][0] + ' → wyjście ' + pts[i][1] : ' '; }
  function changed() { crvSet(ch, crvGet(ch)); draw(); updateMods(); save(); schedulePreview(); staleResults(); refreshCrvTab(); }
  function refreshCrvTab() {
    Array.prototype.forEach.call(tabs.children || [], function (b, i) {
      b.classList.toggle('mod', !!(S.scene.crv && S.scene.crv[CRV_KEYS[i]] && !crvIsDefault(S.scene.crv[CRV_KEYS[i]])));
    });
  }
  function keep(pts) { var c = S.scene.crv || (S.scene.crv = {}); c[CRV_KEYS[ch]] = pts; }     /* a working copy, kept even while it is still a line */
  function toXY(e) {
    var r = cv.getBoundingClientRect(), s = W / r.width;
    return [clamp(Math.round(((e.clientX - r.left) * s - 6) / (W - 12) * 255), 0, 255), clamp(Math.round((W - 6 - (e.clientY - r.top) * s) / (W - 12) * 255), 0, 255), (e.clientX - r.left) * s, (e.clientY - r.top) * s];
  }
  function nearest(e) {
    var pts = crvGet(ch), q = toXY(e), best = -1, bd = 12 * 12, i;
    for (i = 0; i < pts.length; i++) { var dx = px(pts[i][0]) - q[2], dy = py(pts[i][1]) - q[3], d = dx * dx + dy * dy; if (d < bd) { bd = d; best = i; } }
    return best;
  }
  cv.addEventListener('pointerdown', function (e) {
    if (e.button === 2) return;
    var pts = crvGet(ch), i = nearest(e), q = toXY(e);
    e.preventDefault(); cv.focus();
    if (i < 0) {
      if (pts.length >= CRV_MAX) { toast('Krzywa może mieć najwyżej ' + CRV_MAX + ' punktów.'); return; }
      if (pts.some(function (a) { return Math.abs(a[0] - q[0]) < 3; })) return;
      pts.push([q[0], q[1]]); pts.sort(function (a, b) { return a[0] - b[0]; });
      i = pts.findIndex(function (a) { return a[0] === q[0] && a[1] === q[1]; });
      keep(pts);
    }
    crvSel = i; crvDrag = i; cv.setPointerCapture(e.pointerId); say(i); draw(); changed();
  });
  cv.addEventListener('pointermove', function (e) {
    var pts = crvGet(ch), q = toXY(e);
    if (crvDrag < 0) { say(nearest(e)); cv.style.cursor = nearest(e) >= 0 ? 'grab' : 'crosshair'; return; }
    var lo = crvDrag > 0 ? pts[crvDrag - 1][0] + 1 : 0, hi = crvDrag < pts.length - 1 ? pts[crvDrag + 1][0] - 1 : 255;
    pts[crvDrag] = [clamp(q[0], lo, hi), q[1]];
    keep(pts); say(crvDrag); changed();
  });
  function endDrag() { crvDrag = -1; }
  cv.addEventListener('pointerup', endDrag); cv.addEventListener('pointercancel', endDrag);
  cv.addEventListener('pointerleave', function () { if (crvDrag < 0) say(crvSel); });
  function removeAt(i) {
    var pts = crvGet(ch);
    if (i <= 0 || i >= pts.length - 1) return;                      /* the two ends stay */
    pts.splice(i, 1); keep(pts); crvSel = -1; say(-1); changed();
  }
  cv.addEventListener('dblclick', function (e) { removeAt(nearest(e)); });
  cv.addEventListener('contextmenu', function (e) { e.preventDefault(); removeAt(nearest(e)); });
  cv.addEventListener('keydown', function (e) {
    var pts = crvGet(ch), k = e.key, step = e.shiftKey ? 10 : 1;
    if (k === 'Tab') return;
    if (k === 'Delete' || k === 'Backspace') { e.preventDefault(); e.stopPropagation(); removeAt(crvSel); return; }
    if (crvSel < 0 || !pts[crvSel]) { if (k.indexOf('Arrow') === 0) { e.preventDefault(); e.stopPropagation(); crvSel = 0; draw(); say(0); } return; }
    var pt = pts[crvSel], lo = crvSel > 0 ? pts[crvSel - 1][0] + 1 : 0, hi = crvSel < pts.length - 1 ? pts[crvSel + 1][0] - 1 : 255;
    if (k === 'ArrowLeft') pt[0] = clamp(pt[0] - step, lo, hi);
    else if (k === 'ArrowRight') pt[0] = clamp(pt[0] + step, lo, hi);
    else if (k === 'ArrowUp') pt[1] = clamp(pt[1] + step, 0, 255);
    else if (k === 'ArrowDown') pt[1] = clamp(pt[1] - step, 0, 255);
    else if (k === '.' || k === 'PageDown') crvSel = Math.min(pts.length - 1, crvSel + 1);
    else if (k === ',' || k === 'PageUp') crvSel = Math.max(0, crvSel - 1);
    else return;
    e.preventDefault(); e.stopPropagation(); keep(pts); say(crvSel); changed();
  });

  bar.className = 'row-btns';
  bar.innerHTML = '<button type="button" class="btn sm" id="crvReset" title="Prosta linia w tym kanale">Zeruj tę krzywą</button>';
  bar.querySelector('#crvReset').onclick = function () { crvSet(ch, CRV_DEFAULT.map(function (q) { return q.slice(); })); crvSel = -1; renderAdjust(); save(); schedulePreview(); staleResults(); };
  if (ch === 0) {
    chips.className = 'chips'; chips.id = 'crvChips';
    CRV_PRESETS.forEach(function (pr) {
      var b = document.createElement('button'); b.type = 'button'; b.className = 'pill'; b.textContent = pr.name; b.setAttribute('data-id', pr.id);
      b.setAttribute('aria-pressed', String(JSON.stringify(crvGet(0)) === JSON.stringify(pr.pts)));
      b.onclick = function () { crvSet(0, pr.pts.map(function (q) { return q.slice(); })); crvSel = -1; renderAdjust(); save(); schedulePreview(); staleResults(); };
      chips.appendChild(b);
    });
    box.appendChild(chips);
  }
  box.appendChild(bar);
  draw();
  if (window.ResizeObserver) { var ro = new ResizeObserver(function () { if (cv.isConnected) draw(); }); ro.observe(cv); }
}
