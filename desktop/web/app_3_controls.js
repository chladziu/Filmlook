
/* ====================================================================== part 3: the settings panel (right) */
var ROWS = {};                                        /* key -> {row, range, num} of the sliders that are on the screen now */

function fmtVal(c, v) { return c.scale === 1 ? String(v) : (v > 0 ? '+' : '') + (v / c.scale).toFixed(2); }
function paintRange(c, r, v) {
  var pv = (v - c.min) / (c.max - c.min) * 100, a = 0, b = pv;
  if (c.min < 0 && c.max > 0) { var z = (0 - c.min) / (c.max - c.min) * 100; a = Math.min(z, pv); b = Math.max(z, pv); }
  r.style.setProperty('--a', a + '%'); r.style.setProperty('--b', b + '%');
}

function syncRow(key) {
  var R = ROWS[key], c = CTRL[key], v = val(key);
  if (!R) return;
  R.range.value = v; R.num.value = c.scale === 1 ? v : (v / c.scale).toFixed(2);
  R.row.classList.toggle('mod', v !== c.def);
  paintRange(c, R.range, v);
}

function setVal(key, v, quiet) {
  var c = CTRL[key];
  v = Math.round(Number(v));
  if (isNaN(v)) return;
  store(c)[key] = clamp(v, c.min, c.max);
  syncRow(key);
  if (!quiet) afterChange(key);
}
function afterChange(key) {
  if (key && SRC_KEYS.indexOf(key) >= 0) { paintSource(); refreshBefore(); }
  if (key && /^(cg_|ton_mode)/.test(key)) paintWheels();
  if (!key || /^(ton_mode|cg_|filter|wb_)/.test(key)) refreshChips();
  updateMods();
  save(); schedulePreview(); staleResults();
}

/* a slider row: name, slider, number. Double click on the name or the slider brings the value back. */
function makeRow(key, o) {
  o = o || {};
  var c = CTRL[key], row = document.createElement('div'), id = 'sl-' + key;
  row.className = 'srow' + (o.cls ? ' ' + o.cls : ''); row.title = c.hint || '';
  row.innerHTML = '<label for="' + id + '"></label><input type="range" id="' + id + '"><input type="number" aria-label="' + esc(c.name) + ', wartość">';
  var lab = row.querySelector('label'), r = row.querySelector('input[type=range]'), n = row.querySelector('input[type=number]');
  if (o.dot) lab.innerHTML = '<i style="background:' + o.dot + '"></i><span></span>', lab.querySelector('span').textContent = o.label || c.name, lab.className = 'bl';
  else lab.textContent = o.label || c.name;
  r.min = c.min; r.max = c.max; r.step = c.step;
  n.min = c.min / c.scale; n.max = c.max / c.scale; n.step = c.step / c.scale;
  if (o.trk) r.style.setProperty('--trk', o.trk);
  if (o.hue) r.classList.add('hue');
  r.oninput = function () { setVal(key, r.value); };
  n.onchange = function () { setVal(key, parseFloat(n.value) * c.scale); };
  lab.ondblclick = r.ondblclick = function () { setVal(key, c.def); };
  ROWS[key] = { row: row, range: r, num: n };
  syncRow(key);
  return row;
}

function hsl(h, s, l) { return 'hsl(' + h + ',' + s + '%,' + l + '%)'; }
function bandTrack(kind, h) {
  if (kind === 'hue') return 'linear-gradient(90deg,' + hsl(h - 38, 85, 50) + ',' + hsl(h, 85, 50) + ',' + hsl(h + 38, 85, 50) + ')';
  if (kind === 'sat') return 'linear-gradient(90deg,' + hsl(h, 8, 50) + ',' + hsl(h, 92, 50) + ')';
  return 'linear-gradient(90deg,' + hsl(h, 70, 16) + ',' + hsl(h, 82, 55) + ',' + hsl(h, 92, 90) + ')';
}

/* ---------------------------------------------------------------- the sections */
function toneKeys() { return ['ton_mode', 'cg_sh_s', 'cg_mid_s', 'cg_hi_s']; }
function mixerKeys(mono) {
  var k = [];
  BANDS.forEach(function (b) { if (!mono) { k.push('hue_' + b[0]); k.push('sat_' + b[0]); } k.push('lum_' + b[0]); });
  return k;
}
function sectionList() {
  var mono = isMono(), L = [];
  L.push({ id: 'crop', title: 'Kadr', keys: [], body: bodyCrop, reset: cropRemove, mod: cropAnyMod });
  L.push({ id: 'source', title: 'Źródło zdjęcia (RAW)', keys: SRC_KEYS, body: bodySource });
  var fk = mono ? ['strength', 'grain', 'gsize', 'grough', 'vig', 'fade'] : ['strength', 'grain', 'gsize', 'grough', 'gcolor', 'vig', 'fade'];
  L.push({ id: 'film', title: 'Film i ziarno', keys: fk, body: bodyFilm(fk) });
  L.push({ id: 'light', title: 'Światło', keys: ['expo', 'contrast', 'hi_t', 'sh_t', 'wh_t', 'bl_t'], body: bodyRows(['expo', 'contrast', 'hi_t', 'sh_t', 'wh_t', 'bl_t']) });
  L.push({ id: 'curves', title: 'Krzywe', keys: [], body: bodyCurves, reset: crvReset, mod: crvAnyMod });
  if (mono) {
    L.push({ id: 'filter', title: 'Filtr barwny', keys: ['filter', 'filter_amt'], body: bodyFilter });
    L.push({ id: 'tone', title: 'Tonowanie', keys: toneKeys(), body: bodyTone });
  }
  L.push({ id: 'wb', title: 'Balans bieli', keys: ['wb_temp', 'wb_tint'], body: bodyWB });
  L.push({ id: 'color', title: mono ? 'Ciepło i odcień' : 'Kolor', keys: mono ? ['warm', 'shadow', 'high'] : ['sat', 'warm', 'shadow', 'high'],
           body: bodyRows(mono ? ['warm', 'shadow', 'high'] : ['sat', 'warm', 'shadow', 'high']) });
  L.push({ id: 'skin', title: 'Tony skóry', keys: mono ? ['skin_lum'] : ['skin_hue', 'skin_sat', 'skin_lum'], body: bodySkin });
  L.push({ id: 'hsl', title: mono ? 'Mieszalnik czerni i bieli' : 'Mikser kolorów', keys: mixerKeys(mono), body: bodyMixer });
  if (!mono) L.push({ id: 'tone', title: 'Kolorystyka (tonowanie)', keys: toneKeys(), body: bodyTone });
  return L;
}

/* where the picture comes from: the sensor data (RAW) or the JPEG of the camera that is in the file */
function srcState() {                                   /* what is known about the photo on the screen */
  var pv = S.pv, f = pv && pv.file, info = pv && pv.info, name = f ? f.name : '';
  if (!pv) return { kind: 'none', text: 'Wybierz zdjęcie — tu zobaczysz, z czego program je wywołuje.' };
  if (info && info.raw) return { kind: 'raw', text: 'Ten plik ma surowe dane matrycy (RAW, ' + info.rawW + ' × ' + info.rawH + ').' + (info.fit ? '' : ' Brak w nim dużego JPG, więc wzorzec tonalny jest ogólny (średni dla tego aparatu).') };
  if (info && info.rawErrText) return { kind: 'nojpg', text: 'Surowych danych tego pliku nie udało się odczytać (' + info.rawErrText + '), więc pracuję na JPG z pliku.' };
  if (/\.cr3$/i.test(name)) return { kind: 'nojpg', text: 'Surowych danych tego pliku CR3 program nie czyta (aparat spoza listy albo nowszy sposób zapisu RAW) — pracuję na JPG z pliku. Możesz zamienić plik na DNG (darmowy Adobe DNG Converter) i wczytać DNG.' };
  if (/\.(cr2|dng)$/i.test(name)) return { kind: 'nojpg', text: 'Program czyta RAW z plików Canon CR2 (5D Mark II), Canon CR3 (EOS R, RP, R5, R6 i inne) oraz z plików DNG (z matrycą Bayera lub liniowe, bez kompresji albo z kompresją bezstratną). Ten plik jest innego rodzaju — pracuję na JPG z pliku.' };
  return { kind: 'nojpg', text: 'To zwykły JPG — nie ma w nim surowych danych, więc nie ma z czego robić RAW.' };
}
function bodySource(box) {
  var st = srcState(), p = document.createElement('p'), seg = document.createElement('div'), lab = document.createElement('label'), h2 = document.createElement('p');
  p.className = 'hint'; p.id = 'srcInfo';
  seg.className = 'seg wide'; seg.id = 'srcSeg'; seg.setAttribute('role', 'radiogroup'); seg.setAttribute('aria-label', 'Źródło zdjęcia');
  [['0', 'RAW'], ['1', 'JPG z pliku']].forEach(function (o) {
    var b = document.createElement('button'); b.type = 'button'; b.setAttribute('role', 'radio'); b.setAttribute('data-v', o[0]); b.textContent = o[1];
    b.title = o[0] === '0' ? 'Wywołuj z surowych danych matrycy: najwyższa jakość, pełny zakres jasności' : 'Wywołuj z JPG, który aparat zapisał w pliku (niższa jakość, jak dotąd)';
    b.onclick = function () { setVal('usejpg', Number(o[0])); };
    seg.appendChild(b);
  });
  lab.className = 'chk small'; lab.id = 'srcRefLab';
  lab.innerHTML = '<input type="checkbox" id="chkRef"> <span>Wzorzec tonalny z JPG aparatu</span>';
  lab.title = CTRL.rawref.hint;
  lab.querySelector('input').onchange = function (e) { setVal('rawref', e.target.checked ? 1 : 0); };
  h2.className = 'hint'; h2.id = 'srcRefHint';
  h2.textContent = 'Włączone: jasność i kontrast zdjęcia są dopasowane do JPG, który aparat zapisał w tym samym pliku (taki, jaki widać na ekranie aparatu) — filmy nakładają się na ten wygląd. Wyłączone: czysty RAW z neutralnym, standardowym tonem, bez wzorca z aparatu.';
  box.appendChild(p); box.appendChild(seg); box.appendChild(lab); box.appendChild(h2);
  paintSource();
}
function paintSource() {
  var st = srcState(), p = $('#srcInfo'), seg = $('#srcSeg'), chk = $('#chkRef'), lab = $('#srcRefLab');
  if (!p || !seg) return;
  var usejpg = val('usejpg') === 1, canRaw = st.kind === 'raw';
  p.textContent = st.text + (canRaw ? (usejpg ? ' Wybrano JPG z pliku.' : ' Zdjęcie jest wywoływane z RAW.') : '');
  Array.prototype.forEach.call(seg.children, function (b) {
    var on = (b.getAttribute('data-v') === '1') === usejpg;
    b.setAttribute('aria-checked', String(on)); b.setAttribute('aria-pressed', String(on));
    b.disabled = st.kind === 'nojpg';
  });
  chk.checked = val('rawref') === 1;
  chk.disabled = !(canRaw && !usejpg);
  lab.classList.toggle('dim', chk.disabled);
}
/* "before": the starting picture is made again when the source or the crop on the screen is changed */
async function refreshBefore() {
  var pv = S.pv;
  if (!pv || !S.preview || !pv.beforeUrl) return;
  if (pv.beforeSig === beforeSigNow()) return;
  if (await runBefore(S.pvSeq, pv, true) && S.pv === pv) {
    if (S.pvView === 'before') showCurrent();
    scheduleDetail();
  }
}

function bodyFilm(keys) {
  return function (box) {
    var h = document.createElement('div'), p = document.createElement('p');
    keys.forEach(function (k) { box.appendChild(makeRow(k)); });
    h.className = 'row-btns';
    h.innerHTML = '<button type="button" class="btn sm" id="btnGrain" title="Powiększa zdjęcie tak, że jeden piksel gotowego zdjęcia to jeden piksel ekranu (klawisz 1). Dokładnie tak ziarno będzie wyglądać w pliku.">Pokaż ziarno jak w pliku (1:1)</button>';
    p.className = 'hint'; p.textContent = 'Ziarno ocenia się w powiększeniu: przy widoku całego zdjęcia jest zbyt drobne, żeby je dobrze zobaczyć. Widok 1:1 jest liczony tak samo jak gotowy plik (ten sam rozmiar, ta sama jakość).';
    box.appendChild(h); box.appendChild(p);
    h.querySelector('#btnGrain').onclick = showGrain;
  };
}
function bodyRows(keys) {
  return function (box) { keys.forEach(function (k) { box.appendChild(makeRow(k)); }); };
}

function bodyWB(box) {
  var h = document.createElement('div');
  h.className = 'row-btns';
  h.innerHTML = '<button type="button" class="btn sm" id="btnPip" title="Kliknij w zdjęciu miejsce, które powinno być szare lub białe">Kroplomierz</button><button type="button" class="btn sm" id="btnAutoWB" title="Ustawia balans bieli tak, żeby średni kolor zdjęcia był neutralny">Automatycznie</button>';
  box.appendChild(h);
  h.querySelector('#btnPip').onclick = startPipette;
  h.querySelector('#btnAutoWB').onclick = autoWB;
  var lg = document.createElement('p'), ch = document.createElement('div');
  lg.className = 'hint'; lg.textContent = 'Rodzaj światła, w którym zrobiono zdjęcie — jak w menu aparatu:';
  ch.className = 'chips'; ch.id = 'wbChips';
  WB_PRESETS.forEach(function (w) {
    var b = document.createElement('button'); b.type = 'button'; b.className = 'pill'; b.setAttribute('data-id', w.id); b.title = w.tip;
    b.textContent = w.name;
    b.onclick = function () { setVal('wb_temp', w.t, true); setVal('wb_tint', w.i, true); afterChange('wb_temp'); };
    ch.appendChild(b);
  });
  box.appendChild(lg); box.appendChild(ch);
  box.appendChild(makeRow('wb_temp', { trk: 'linear-gradient(90deg,#4f8dff,#d8d8d8,#ffb347)' }));
  box.appendChild(makeRow('wb_tint', { trk: 'linear-gradient(90deg,#3fbf5c,#d8d8d8,#d65fd0)' }));
  var n = document.createElement('p'); n.className = 'hint'; n.textContent = 'Poprawia kolor całego zdjęcia przed nałożeniem filmu. Przy pliku RAW balans bieli działa na surowych danych (tak jak w Lightroomie); przy zwykłym JPG jest łagodniejszy.';
  box.appendChild(n);
  refreshChips();
}

function bodySkin(box) {
  var mono = isMono(), n = document.createElement('p');
  n.className = 'hint';
  n.textContent = mono ? 'Jak jasna ma być skóra na czarno-białym zdjęciu. Zmienia tylko barwy skóry, resztę zostawia.' : 'Zmienia tylko barwy skóry (pomarańczowe, z domieszką czerwieni i żółci). Reszta zdjęcia zostaje.';
  box.appendChild(n);
  if (!mono) { box.appendChild(makeRow('skin_hue', { trk: 'linear-gradient(90deg,#d9534f,#d9926b,#d9c24f)' })); box.appendChild(makeRow('skin_sat', { trk: bandTrack('sat', 24) })); }
  box.appendChild(makeRow('skin_lum', { trk: bandTrack('lum', 24) }));
}

function bodyMixer(box) {
  var mono = isMono(), p = document.createElement('p');
  if (mono) {
    p.className = 'hint'; p.textContent = 'Jak jasny ma być każdy kolor w czerni i bieli — tak jak filtry w ciemni, ale osobno dla każdej barwy.';
    box.appendChild(p);
    BANDS.forEach(function (b) { box.appendChild(makeRow('lum_' + b[0], { dot: hsl(b[2], 80, 50), trk: bandTrack('lum', b[2]), cls: 'bandrow' })); });
    return;
  }
  var tabs = document.createElement('div'), names = ['Barwa', 'Nasycenie', 'Luminancja'], kinds = ['hue', 'sat', 'lum'], wrap = document.createElement('div');
  tabs.className = 'tabs2'; tabs.setAttribute('role', 'tablist');
  names.forEach(function (nm, i) {
    var b = document.createElement('button'); b.type = 'button'; b.textContent = nm; b.setAttribute('role', 'tab');
    b.setAttribute('aria-selected', String(S.ui.hsl === i));
    b.className = BANDS.some(function (bd) { return modKey(kinds[i] + '_' + bd[0]); }) ? 'mod' : '';
    b.onclick = function () { S.ui.hsl = i; save(); renderAdjust(); };
    tabs.appendChild(b);
  });
  box.appendChild(tabs);
  p.className = 'hint';
  p.textContent = ['Przesuwa wybrany kolor w stronę sąsiednich barw (np. zieleń bardziej żółta albo bardziej turkusowa).', 'Zmienia, jak intensywny jest wybrany kolor — reszta zdjęcia zostaje.', 'Zmienia, jak jasny jest wybrany kolor — np. ciemniejsze niebo przez ściemnienie niebieskiego.'][S.ui.hsl];
  box.appendChild(p);
  BANDS.forEach(function (b) {
    var kind = kinds[S.ui.hsl];
    box.appendChild(makeRow(kind + '_' + b[0], { dot: hsl(b[2], 80, 50), trk: bandTrack(kind, b[2]), cls: 'bandrow' }));
  });
}

function bodyFilter(box) {
  var p = document.createElement('p'), chips = document.createElement('div');
  p.className = 'hint'; p.textContent = 'Jak filtr na obiektywie aparatu czarno-białego: czerwony ciemni niebo i rozjaśnia skórę, zielony rozjaśnia zieleń, niebieski rozjaśnia niebo.';
  box.appendChild(p);
  chips.className = 'chips'; chips.id = 'filterChips';
  FILTERS.forEach(function (f) {
    var b = document.createElement('button'); b.type = 'button'; b.className = 'pill'; b.setAttribute('data-n', f.n);
    b.innerHTML = (f.dot ? '<i style="background:' + f.dot + '"></i>' : '') + esc(f.name);
    b.onclick = function () { setVal('filter', f.n); };
    chips.appendChild(b);
  });
  box.appendChild(chips);
  box.appendChild(makeRow('filter_amt'));
}

function bodyTone(box) {
  var p = document.createElement('p'), chips = document.createElement('div');
  p.className = 'hint';
  p.textContent = isMono() ? 'Kolor, w jakim wydrukowano by czarno-białe zdjęcie: sepia, selen, cyjanotypia… Osobno dla cieni, półtonów i świateł.' : 'Zabarwia cienie, półtony i światła — jak tonowanie w ciemni albo „color grading”. „Z filmu” zostawia kolory filmu.';
  box.appendChild(p);
  chips.className = 'chips'; chips.id = 'toneChips';
  TONES.forEach(function (t) {
    var b = document.createElement('button'); b.type = 'button'; b.className = 'pill'; b.setAttribute('data-id', t.id);
    b.innerHTML = (t.dot ? '<i style="background:' + t.dot + '"></i>' : '') + esc(t.name);
    b.onclick = function () { applyTone(t); };
    chips.appendChild(b);
  });
  box.appendChild(chips);
  [['sh', 'Cienie'], ['mid', 'Półtony'], ['hi', 'Światła']].forEach(function (w) {
    var d = document.createElement('div'), body = document.createElement('div'), sw = document.createElement('span');
    d.className = 'wheel'; d.setAttribute('data-w', w[0]); sw.className = 'sw';
    body.appendChild(makeRow('cg_' + w[0] + '_h', { label: w[1] + ': barwa', hue: true }));
    body.appendChild(makeRow('cg_' + w[0] + '_s', { label: w[1] + ': siła' }));
    d.appendChild(sw); d.appendChild(body);
    box.appendChild(d);
  });
  paintWheels(); refreshChips();
}

function applyTone(t) {
  var keys = ['cg_sh_h', 'cg_sh_s', 'cg_mid_h', 'cg_mid_s', 'cg_hi_h', 'cg_hi_s'];
  setVal('ton_mode', t.mode, true);
  if (t.id === 'film') { keys.forEach(function (k, i) { if (i % 2) setVal(k, 0, true); }); }
  else keys.forEach(function (k, i) { var v = t.v[i]; if (i % 2 === 0 && v === 0 && t.v[i + 1] === 0) return; setVal(k, v, true); });
  afterChange('ton_mode');
}

function paintWheels() {
  ['sh', 'mid', 'hi'].forEach(function (w) {
    var d = document.querySelector('.wheel[data-w="' + w + '"]');
    if (!d) return;
    var h = val('cg_' + w + '_h'), s = val('cg_' + w + '_s'), sw = d.querySelector('.sw');
    sw.style.background = s > 0 ? hsl(h, 25 + Math.round(s * 0.6), 50) : 'var(--track)';
    var R = ROWS['cg_' + w + '_s'];
    if (R) R.range.style.setProperty('--trk', 'linear-gradient(90deg,' + hsl(h, 6, 50) + ',' + hsl(h, 90, 52) + ')');
  });
}
function refreshChips() {
  var tc = $('#toneChips'), fc = $('#filterChips'), wc = $('#wbChips');
  if (wc) {
    var t = val('wb_temp'), ti = val('wb_tint');
    Array.prototype.forEach.call(wc.children, function (b) {
      var w = WB_PRESETS.filter(function (x) { return x.id === b.getAttribute('data-id'); })[0];
      b.setAttribute('aria-pressed', String(w.t === t && w.i === ti));
    });
  }
  if (tc) {
    var cur = null;
    if (val('ton_mode') === 0) cur = 'film';
    else TONES.forEach(function (t) {
      if (t.mode !== 1 || cur) return;
      var ks = ['cg_sh_h', 'cg_sh_s', 'cg_mid_h', 'cg_mid_s', 'cg_hi_h', 'cg_hi_s'], ok = true;
      ks.forEach(function (k, i) { if (i % 2 === 0 && val(ks[i + 1]) === 0 && t.v[i + 1] === 0) return; if (val(k) !== t.v[i]) ok = false; });
      if (ok) cur = t.id;
    });
    Array.prototype.forEach.call(tc.children, function (b) { b.setAttribute('aria-pressed', String(b.getAttribute('data-id') === cur)); });
  }
  if (fc) Array.prototype.forEach.call(fc.children, function (b) { b.setAttribute('aria-pressed', String(Number(b.getAttribute('data-n')) === val('filter'))); });
}

/* ---------------------------------------------------------------- building the panel */
/* the groups of the right panel: one icon of the strip at the edge each; the sections of a group are under it */
var GROUPS = [
  { id: 'film', title: 'Film i ziarno' }, { id: 'light', title: 'Światło' }, { id: 'color', title: 'Kolor' },
  { id: 'crop', title: 'Kadr i obrót' }, { id: 'source', title: 'Źródło zdjęcia' }, { id: 'export', title: 'Zapis zdjęć' }
];
var SEC_GROUP = { film: 'film', light: 'light', curves: 'light', wb: 'color', color: 'color', skin: 'color', hsl: 'color', tone: 'color', filter: 'color', crop: 'crop', source: 'source' };
function groupOk(id) { return GROUPS.some(function (g) { return g.id === id; }); }
function setGroup(id, noSave) {
  var g = groupOk(id) ? id : 'film', title = GROUPS.filter(function (x) { return x.id === g; })[0].title;
  S.ui.group = g;
  Array.prototype.forEach.call(document.querySelectorAll('#adj .grp'), function (d) { d.classList.toggle('hidden', d.getAttribute('data-g') !== g); });
  $('#adj').classList.toggle('hidden', g === 'export'); $('#exp').classList.toggle('hidden', g !== 'export');
  Array.prototype.forEach.call(document.querySelectorAll('#rail .rb'), function (b) { var on = b.getAttribute('data-g') === g; b.setAttribute('aria-selected', String(on)); b.setAttribute('aria-pressed', String(on)); });
  $('#rtitle').textContent = title;
  $('#rsFilm').classList.toggle('hidden', g !== 'film');
  $('#rsScene').classList.toggle('hidden', g !== 'light' && g !== 'color');
  if (g === 'export') paintExportInfo();
  if (!noSave) save();
}
function initRail() {
  Array.prototype.forEach.call(document.querySelectorAll('#rail .rb'), function (b) { b.onclick = function () { setGroup(b.getAttribute('data-g')); }; });
  $('#rsFilm').onclick = function () { delete S.looks[S.film]; afterReset(); };
  $('#rsScene').onclick = function () {                                    /* (the choice of the source is not a correction: it stays) */
    var keep = {}; SRC_KEYS.forEach(function (k) { if (typeof S.scene[k] === 'number') keep[k] = S.scene[k]; });
    S.scene = keep; afterReset();
  };
}
function openExport() { setPanel('r', true); setGroup('export'); }

function sectionOpen(id) { var o = S.ui.open[id]; return o === undefined ? (id === 'film' || id === 'light' || id === 'source' || id === 'crop' || id === 'wb' || id === 'filter') : !!o; }

function renderAdjust() {
  var box = $('#adj'), secs = sectionList(), gEls = {};
  ROWS = {};
  box.innerHTML = '';
  GROUPS.forEach(function (g) {
    var d;
    if (g.id === 'export') return;
    d = document.createElement('div'); d.className = 'grp'; d.setAttribute('data-g', g.id);
    box.appendChild(d); gEls[g.id] = d;
  });
  secs.forEach(function (s) {
    var el = document.createElement('div'), sh = document.createElement('div'), body = document.createElement('div');
    el.className = 'sec' + (sectionOpen(s.id) ? ' open' : ''); el.setAttribute('data-id', s.id);
    sh.className = 'sh';
    sh.innerHTML = '<button type="button" class="tgl" aria-expanded="' + sectionOpen(s.id) + '"><span class="chev"></span><span class="st"></span></button><span class="dot" aria-hidden="true"></span><button type="button" class="rs" title="Przywróć ten dział do wartości domyślnych" aria-label="Przywróć ten dział">↺</button>';
    sh.querySelector('.st').textContent = s.title;
    body.className = 'sb';
    sh.querySelector('.tgl').onclick = function () {
      var open = !el.classList.contains('open');
      el.classList.toggle('open', open); sh.querySelector('.tgl').setAttribute('aria-expanded', String(open));
      S.ui.open[s.id] = open ? 1 : 0; save();
    };
    sh.querySelector('.rs').onclick = function (ev) {
      ev.stopPropagation();
      s.keys.forEach(function (k) { var c = CTRL[k]; store(c)[k] = c.def; });
      if (s.reset) s.reset();
      if (s.id === 'tone') { setVal('ton_mode', 0, true); }
      if (s.id === 'hsl' && isMono()) mixerKeys(false).forEach(function (k) { if (/^lum_/.test(k)) store(CTRL[k])[k] = 0; });
      afterReset();
    };
    el.appendChild(sh); el.appendChild(body);
    gEls[SEC_GROUP[s.id] || 'film'].appendChild(el);
    s.body(body);
    el.__keys = s.keys; el.__mod = s.mod;
  });
  Object.keys(gEls).forEach(function (g) { gEls[g].classList.toggle('single', gEls[g].children.length === 1); });          /* one section in a group: the group's own title is enough */
  setGroup(S.ui.group, true);
  updateMods();
}
function afterReset() { renderAdjust(); save(); refreshBefore(); schedulePreview(); staleResults(); }

function groupMods() {                                  /* which groups have something changed (the dot on the icon) */
  var m = {};
  sectionList().forEach(function (s) { if (anyMod(s.keys || []) || (s.mod && s.mod())) m[SEC_GROUP[s.id] || 'film'] = true; });
  if (S.out && S.out.mode !== 'full') m.export = true;
  return m;
}
function updateMods() {
  var gm = groupMods();
  Array.prototype.forEach.call(document.querySelectorAll('#adj .sec'), function (el) {
    el.classList.toggle('mod', anyMod(el.__keys || []) || !!(el.__mod && el.__mod()));
  });
  Array.prototype.forEach.call(document.querySelectorAll('#rail .rb'), function (b) { b.classList.toggle('mod', !!gm[b.getAttribute('data-g')]); });
  var rf = $('#rsFilm'), rs = $('#rsScene');
  if (rf) rf.disabled = !CTRL_LIST.some(function (c) { return c.scope === 'film' && modKey(c.key); });
  if (rs) rs.disabled = !CTRL_LIST.some(function (c) { return c.scope === 'scene' && SRC_KEYS.indexOf(c.key) < 0 && modKey(c.key); }) && !crvAnyMod();
  if (S.ui.hsl !== undefined) {
    var tabs = document.querySelectorAll('#adj .sec[data-id="hsl"] .tabs2 button'), kinds = ['hue', 'sat', 'lum'];
    Array.prototype.forEach.call(tabs, function (b, i) { b.classList.toggle('mod', BANDS.some(function (bd) { return modKey(kinds[i] + '_' + bd[0]); })); });
  }
}
