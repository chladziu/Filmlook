'use strict';
/* ====================================================================== the page itself (part 1: the basics)
 * Plain JavaScript. The engine (the same C code as in the camera module) runs in WebAssembly inside Web Workers (one per
 * processor core, so that several photos are developed at once); if the browser does not allow workers it runs on the page
 * itself, one photo at a time. The script is written in several files (app_1 ... app_5) that are joined into one by build_web.py. */
var $ = function (s) { return document.querySelector(s); };
var ENGINE_SRC = document.getElementById('engine-src').textContent;
var STORE_KEY = 'filmlook-desktop-2';

function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }
function esc(s) { return String(s).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }

/* ---------------------------------------------------------------- the controls
 * One entry per slider. scope 'film': remembered for every film separately (the look of the film); scope 'scene': stays when
 * the film is changed (the correction of the picture: light, white balance, skin, colours). top: sent as a parameter of the engine's
 * nine, otherwise as an extra (see engine_core.js). scale: the number shown is value / scale (exposure is kept in hundredths of a stop). */
var CTRL = {}, CTRL_LIST = [];
function ctl(key, scope, top, name, min, max, def, o) {
  var c = { key: key, scope: scope, top: top, name: name, min: min, max: max, def: def, scale: 1, step: 1, hint: '' };
  if (o) for (var k in o) c[k] = o[k];
  CTRL[key] = c; CTRL_LIST.push(c);
  return c;
}
/* the source of the picture: the sensor data of a RAW file, or the JPEG that is inside it */
ctl('usejpg', 'scene', false, 'Źródło: JPG z pliku', 0, 1, 0, { hint: 'wyłączone: zdjęcie jest wywoływane z surowych danych RAW; włączone: z JPG zapisanego w pliku' });
ctl('rawref', 'scene', false, 'Wzorzec tonalny z JPG aparatu', 0, 1, 1, { hint: 'dopasowuje ton RAW do JPG, który aparat zapisał w pliku' });
var SRC_KEYS = ['usejpg', 'rawref'];
/* film and grain */
ctl('strength', 'film', true, 'Efekt', 0, 100, 100, { hint: 'ile filmu jest w zdjęciu: 0 = oryginał, 100 = pełny film' });
ctl('grain', 'film', true, 'Ziarno', 0, 200, 100, { hint: 'ilość ziarna: 100 = tak jak w filmie' });
ctl('gsize', 'film', false, 'Wielkość ziarna', -50, 50, 0, { hint: 'drobniejsze — grubsze ziarno niż w filmie' });
ctl('grough', 'film', false, 'Szorstkość ziarna', -50, 50, 0, { hint: 'gładsze — bardziej nierówne ziarno niż w filmie' });
ctl('gcolor', 'film', false, 'Kolor ziarna', 0, 100, 100, { hint: 'ziarno kolorowe, jak w filmie barwnym: 100 = pełne, 0 = szare ziarno (czarno-białe filmy mają zawsze szare)' });
ctl('vig', 'film', true, 'Winieta', 0, 100, 0, { hint: 'ciemniejsze rogi zdjęcia' });
ctl('fade', 'film', true, 'Matowość', 0, 100, 0, { hint: 'podniesione czernie, „wyblakły” wygląd' });
/* light */
ctl('expo', 'scene', false, 'Ekspozycja', -300, 300, 0, { scale: 100, step: 5, hint: 'jaśniej / ciemniej, w stopniach przysłony (EV)' });
ctl('contrast', 'film', true, 'Kontrast', -50, 50, 0, { hint: 'mniej — więcej kontrastu' });
ctl('hi_t', 'scene', false, 'Światła', -100, 100, 0, { hint: 'jasne partie zdjęcia: przyciemnij (−) lub rozjaśnij (+)' });
ctl('sh_t', 'scene', false, 'Cienie', -100, 100, 0, { hint: 'ciemne partie zdjęcia: przyciemnij (−) lub rozjaśnij (+)' });
ctl('wh_t', 'scene', false, 'Biel', -100, 100, 0, { hint: 'punkt bieli: najjaśniejsze miejsca' });
ctl('bl_t', 'scene', false, 'Czerń', -100, 100, 0, { hint: 'punkt czerni: najciemniejsze miejsca' });
/* white balance */
ctl('wb_temp', 'scene', false, 'Temperatura', -100, 100, 0, { hint: 'chłodniej (niebiesko) — cieplej (bursztynowo)' });
ctl('wb_tint', 'scene', false, 'Odcień', -100, 100, 0, { hint: 'bardziej zielono — bardziej purpurowo' });
/* colour */
ctl('sat', 'film', true, 'Nasycenie', -100, 100, 0, { hint: 'mniej — więcej koloru' });
ctl('warm', 'film', true, 'Ciepło', -100, 100, 0, { hint: 'chłodniej — cieplej na całym zdjęciu (jak filtr)' });
ctl('shadow', 'film', true, 'Kolor cieni', -100, 100, 0, { hint: 'kolor cieni: niebieskozielony — bursztynowy' });
ctl('high', 'film', true, 'Kolor świateł', -100, 100, 0, { hint: 'kolor świateł: niebieski — bursztynowy' });
/* skin */
ctl('skin_hue', 'scene', false, 'Odcień skóry', -100, 100, 0, { hint: 'bardziej czerwona (−) — bardziej żółta (+)' });
ctl('skin_sat', 'scene', false, 'Nasycenie skóry', -100, 100, 0, { hint: 'bledsza — bardziej zarumieniona' });
ctl('skin_lum', 'scene', false, 'Jasność skóry', -100, 100, 0, { hint: 'ciemniejsza — jaśniejsza' });
/* colour mixer: hue, saturation, luminance of 8 colours */
var BANDS = [
  ['red', 'Czerwony', 0], ['orange', 'Pomarańczowy', 30], ['yellow', 'Żółty', 58], ['green', 'Zielony', 125],
  ['aqua', 'Turkusowy', 180], ['blue', 'Niebieski', 225], ['purple', 'Fioletowy', 275], ['magenta', 'Purpurowy', 320]
];
BANDS.forEach(function (b) { ctl('hue_' + b[0], 'scene', false, b[1], -100, 100, 0, { hint: 'przesuwa ten kolor w stronę sąsiednich barw' }); });
BANDS.forEach(function (b) { ctl('sat_' + b[0], 'scene', false, b[1], -100, 100, 0, { hint: 'mniej — więcej koloru tylko w tym kolorze' }); });
BANDS.forEach(function (b) { ctl('lum_' + b[0], 'scene', false, b[1], -100, 100, 0, { hint: 'ciemniej — jaśniej tylko ten kolor' }); });
/* toning: three wheels (shadows, midtones, highlights) and the colour filter of black and white films */
ctl('ton_mode', 'film', false, 'Tryb tonowania', 0, 1, 0);
ctl('cg_sh_h', 'film', false, 'Cienie: odcień', 0, 359, 30, { hint: 'kolor cieni (kąt na kole barw)' });
ctl('cg_sh_s', 'film', false, 'Cienie: nasycenie', 0, 100, 0, { hint: 'jak mocno cienie są zabarwione' });
ctl('cg_mid_h', 'film', false, 'Półtony: odcień', 0, 359, 40, { hint: 'kolor półtonów (kąt na kole barw)' });
ctl('cg_mid_s', 'film', false, 'Półtony: nasycenie', 0, 100, 0, { hint: 'jak mocno półtony są zabarwione' });
ctl('cg_hi_h', 'film', false, 'Światła: odcień', 0, 359, 45, { hint: 'kolor świateł (kąt na kole barw)' });
ctl('cg_hi_s', 'film', false, 'Światła: nasycenie', 0, 100, 0, { hint: 'jak mocno światła są zabarwione' });
ctl('filter', 'film', false, 'Filtr barwny', 0, 5, 0);
ctl('filter_amt', 'film', false, 'Siła filtra', 0, 100, 100, { hint: 'jak mocno filtr działa' });

var RUN_NOTE = 'Wynik: nowe pliki JPG, oryginały zostają nietknięte.';
var TOP_KEYS = ['strength', 'grain', 'contrast', 'fade', 'warm', 'shadow', 'high', 'sat', 'vig'];

/* what the toning buttons set (hue in degrees, saturation 0..100 of shadows / midtones / highlights) */
var TONES = [
  { id: 'film', name: 'Z filmu', mode: 0, v: [0, 0, 0, 0, 0, 0] },
  { id: 'none', name: 'Neutralne', mode: 1, v: [0, 0, 0, 0, 0, 0] },
  { id: 'sepia', name: 'Sepia', mode: 1, v: [30, 25, 0, 0, 45, 20], dot: '#a9824f' },
  { id: 'sel', name: 'Selen', mode: 1, v: [280, 22, 0, 0, 220, 8], dot: '#7e6a98' },
  { id: 'cyan', name: 'Cyjanotypia', mode: 1, v: [215, 45, 0, 0, 200, 25], dot: '#4a79a8' },
  { id: 'gold', name: 'Złoto', mode: 1, v: [0, 0, 45, 40, 0, 0], dot: '#a8913f' },
  { id: 'split', name: 'Zimne cienie, ciepłe światła', mode: 1, v: [215, 30, 0, 0, 40, 30], dot: '#8a9aa8' }
];
var FILTERS = [
  { n: 0, name: 'Brak', dot: null }, { n: 1, name: 'Żółty', dot: '#e8d34a' }, { n: 2, name: 'Pomarańczowy', dot: '#e8872e' },
  { n: 3, name: 'Czerwony', dot: '#d33a2c' }, { n: 4, name: 'Zielony', dot: '#4fa64a' }, { n: 5, name: 'Niebieski', dot: '#3f6fd0' }
];

/* white balance as in a camera: the kind of light the photo was taken in. t = temperature, i = tint of the sliders (what is needed to
 * make the colours neutral again: under a bulb the photo is orange, so "Żarówka" cools it). The values come from the colour
 * temperatures (daylight 5500 K = no change) and from the range of the sliders (±100 = ±0.8 EV of red and blue). */
var WB_PRESETS = [
  { id: 'asis', name: 'Jak w aparacie', t: 0, i: 0, tip: 'bez korekty (światło dzienne, ok. 5500 K)' },
  { id: 'cloud', name: 'Pochmurno', t: 14, i: 0, tip: 'pochmurne niebo, ok. 6500 K: zdjęcie robi się cieplejsze' },
  { id: 'shade', name: 'Cień', t: 25, i: 2, tip: 'cień w słoneczny dzień, ok. 7500 K: zdjęcie robi się cieplejsze' },
  { id: 'tung', name: 'Żarówka', t: -85, i: 0, tip: 'żarówka, ok. 2800 K: zdjęcie robi się chłodniejsze (mniej pomarańczowe)' },
  { id: 'fluo', name: 'Jarzeniówka', t: -35, i: 25, tip: 'świetlówka, ok. 4000 K: chłodniej i mniej zielono' },
  { id: 'flash', name: 'Lampa błyskowa', t: 5, i: 0, tip: 'lampa błyskowa, ok. 5600 K: odrobinę cieplej' }
];

/* ---------------------------------------------------------------- state */
var S = {
  films: [], film: 0, looks: {}, scene: {}, out: { mode: 'full', px: 3000 }, quality: 98, hq: true, pair: true,
  ui: { lw: 300, rw: 340, lc: null, rc: null, open: { film: 1, light: 1 }, theme: '', hsl: 0 },
  files: [], nextId: 1, sel: null, running: false, cancel: false,
  module: null, pool: [], preview: null, detail: null, pv: null, outDir: null, inline: false, pvView: 'after'
};
var memo = {
  get: function (k) { try { return JSON.parse(localStorage.getItem(k)); } catch (e) { return null; } },
  set: function (k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* no storage: fine */ } }
};

function toast(msg, ms) {
  var t = $('#toast'); t.textContent = msg; t.style.display = 'block';
  clearTimeout(toast.tm); toast.tm = setTimeout(function () { t.style.display = 'none'; }, ms || 4000);
}
function fmtMB(n) { return (n / 1048576).toFixed(n < 10485760 ? 1 : 0) + ' MB'; }
function slug(s) { return s.replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, ''); }
function plural(n, one, few, many) { return n === 1 ? one : (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 10 || n % 100 >= 20) ? few : many); }

/* the values of the controls: scene ones in S.scene, film ones in S.looks[film] */
function store(c) {
  if (c.scope === 'scene') return S.scene;
  var v = S.looks[S.film];
  if (!v) { v = {}; S.looks[S.film] = v; }
  return v;
}
function val(key) { var c = CTRL[key], v = store(c)[key]; return typeof v === 'number' ? v : c.def; }
function isMono() { return !!(S.films[S.film] && S.films[S.film].mono); }
function modKey(key) { return val(key) !== CTRL[key].def; }
function anyMod(keys) { return keys.some(modKey); }

/* the parameters of one development: the film, the size, the quality and every control */
function paramsFor(film, size, quality, looks, scene) {
  var p = { film: film, size: size, quality: quality, x: { hq: 0 } }, lv = looks[film] || {};        /* (the preview: the quicker way; the final pictures ask for hq, see runAll) */
  CTRL_LIST.forEach(function (c) {
    var src = c.scope === 'scene' ? scene : lv, v = typeof src[c.key] === 'number' ? src[c.key] : c.def;
    if (c.top) p[c.key] = v; else p.x[c.key] = v;
  });
  crvParams(scene, !!(S.films[film] && S.films[film].mono), p.x);
  return p;
}
function params(extra) {                              /* size 0 = the photo as it is (a crop and a size of the result are added by the caller, see app_4_crop.js) */
  var p = paramsFor(S.film, 0, S.quality, S.looks, S.scene), k;
  for (k in (extra || {})) p[k] = extra[k];
  return p;
}
/* the picture as the program starts from it: the engine with everything at 0 (only the choice of the source stays) */
function plainParams(size, quality) {
  var p = { film: 0, size: size, quality: quality, x: { usejpg: val('usejpg'), rawref: val('rawref'), hq: 0 }, strength: 0, grain: 0, contrast: 0, fade: 0, warm: 0, shadow: 0, high: 0, sat: 0, vig: 0 };
  return p;
}

function save() {
  var u = S.ui;
  memo.set(STORE_KEY, { film: S.film, looks: S.looks, scene: S.scene, out: S.out, quality: S.quality, hq: S.hq, pair: S.pair, ui: u });
}

/* ---------------------------------------------------------------- workers */
function makeHandler(post) {
  var eng = null, curId = 0;
  return async function (e) {
    var m = e.data, res;
    try {
      if (m.type === 'init') {
        eng = await fdCreateEngine(m.module);
        eng.onProgress = function (pm) { post({ type: 'progress', id: curId, pm: pm }); };
        post({ type: 'ready' });
      } else if (m.type === 'source') {
        curId = m.id;
        post({ type: 'source', id: m.id, ok: eng.setSource(new Uint8Array(m.buffer)) });
      } else if (m.type === 'run' || m.type === 'process') {
        curId = m.id;
        res = m.type === 'run' ? eng.run(m.params) : eng.process(new Uint8Array(m.buffer), m.params);
        if (res.jpeg && m.crc) res.crc = fdCrc32(res.jpeg);
        post({ type: 'done', id: m.id, res: res }, res.jpeg ? [res.jpeg.buffer] : []);
      }
    } catch (err) {
      post({ type: 'done', id: m.id, res: { rc: -999, text: 'błąd silnika: ' + err } });
    }
  };
}

function Client(w) {                                  /* a worker (real or inline) with promises on top */
  var me = this;
  me.w = w; me.id = 0; me.pending = {}; me.readyCb = null;
  w.onmessage = function (e) {
    var m = e.data, p;
    if (m.type === 'ready') { if (me.readyCb) me.readyCb(true); return; }
    p = me.pending[m.id];
    if (!p) return;
    if (m.type === 'progress') { if (p.onp) p.onp(m.pm); return; }
    delete me.pending[m.id];
    p.res(m.type === 'source' ? m.ok : m.res);
  };
}
Client.prototype.request = function (msg, transfer, onp) {
  var me = this;
  return new Promise(function (res) {
    var id = ++me.id;
    me.pending[id] = { res: res, onp: onp };
    msg.id = id;
    me.w.postMessage(msg, transfer || []);
  });
};
Client.prototype.init = function (module) {
  var me = this;
  return new Promise(function (res) {
    var done = false;
    me.readyCb = function (ok) { if (!done) { done = true; res(ok); } };
    me.w.onerror = function () { if (!done) { done = true; res(false); } };
    setTimeout(function () { if (!done) { done = true; res(false); } }, 5000);
    me.w.postMessage({ type: 'init', module: module });
  });
};

function InlineWorker() {                             /* no Web Workers: the same handler on the page itself */
  var me = this;
  var handler = makeHandler(function (m) { setTimeout(function () { if (me.onmessage) me.onmessage({ data: m }); }, 0); });
  me.postMessage = function (m) { setTimeout(function () { handler({ data: m }); }, 0); };
}

function newWorker() {
  var src = ENGINE_SRC + '\n' + makeHandler.toString() + '\nself.onmessage = makeHandler(function (m, t) { self.postMessage(m, t || []); });';
  var url = URL.createObjectURL(new Blob([src], { type: 'text/javascript' }));
  return new Worker(url);
}

async function startWorkers(module, n) {
  var list = [], i, c;
  try {
    for (i = 0; i < n; i++) { c = new Client(newWorker()); if (await c.init(module)) list.push(c); else { try { c.w.terminate(); } catch (e) {} break; } }
  } catch (e) { list = []; }
  if (!list.length) {                                  /* the browser does not run workers here: one engine on the page */
    c = new Client(new InlineWorker());
    await c.init(module);
    list = [c]; S.inline = true;
  }
  return list;
}
