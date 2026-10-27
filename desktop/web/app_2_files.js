
/* ====================================================================== part 2: the photos and the films */
var PHOTO_RE = /\.(jpe?g|cr2|cr3|dng)$/i;
function isPhoto(name) { return PHOTO_RE.test(name); }

function entryFiles(entry, out, path) {                /* drag and drop of a folder: walk through it */
  return new Promise(function (resolve) {
    if (entry.isFile) {
      entry.file(function (f) { f._path = path + f.name; if (isPhoto(f.name)) out.push(f); resolve(); }, function () { resolve(); });
    } else if (entry.isDirectory) {
      if (/^FILMLOOK$/i.test(entry.name)) { resolve(); return; }       /* the folder with finished pictures */
      var reader = entry.createReader(), all = [];
      (function more() {
        reader.readEntries(function (ents) {
          if (!ents.length) {
            var chain = Promise.resolve();
            all.forEach(function (e) { chain = chain.then(function () { return entryFiles(e, out, path + entry.name + '/'); }); });
            chain.then(resolve);
          } else { all = all.concat(Array.prototype.slice.call(ents)); more(); }
        }, function () { resolve(); });
      })();
    } else resolve();
  });
}

async function addFromDrop(dt) {
  var out = [], items = dt.items, i, ents = [];
  if (items && items.length && items[0].webkitGetAsEntry) {
    for (i = 0; i < items.length; i++) { var en = items[i].webkitGetAsEntry(); if (en) ents.push(en); }
    for (i = 0; i < ents.length; i++) await entryFiles(ents[i], out, '');
  } else {
    for (i = 0; i < dt.files.length; i++) if (isPhoto(dt.files[i].name)) out.push(dt.files[i]);
  }
  addFiles(out);
}

function addFiles(files) {
  var have = {}, added = 0;
  S.files.forEach(function (f) { have[f.key] = 1; });
  files.forEach(function (file) {
    var path = file._path || file.webkitRelativePath || file.name, key = path + '|' + file.size + '|' + file.lastModified;
    if (/(^|\/)FILMLOOK\//i.test(path)) return;
    if (have[key]) return;
    have[key] = 1;
    S.files.push({ id: S.nextId++, key: key, file: file, path: path, name: file.name, size: file.size, status: 'wait', pct: 0, checked: true, thumb: null, developed: false });
    added++;
  });
  applyPairs();
  if (added && !S.sel) selectFile(firstUsable());
  renderList();
  queueThumbs();
  if (!added && !files.length) toast('Nie znalazłem tu zdjęć JPG, CR2, CR3 ani DNG.');
  else if (!added && files.length) toast('Te zdjęcia są już na liście.');
}

function baseKey(f) { return f.path.replace(/\.[^.\/]+$/, '').toLowerCase(); }
function applyPairs() {                                /* RAW+JPG: when a CR2 / CR3 / DNG of the same name is there, the JPG waits out */
  var cr2 = {};
  S.files.forEach(function (f) { if (/\.(cr2|cr3|dng)$/i.test(f.name)) cr2[baseKey(f)] = 1; });
  S.files.forEach(function (f) {
    var skip = S.pair && /\.jpe?g$/i.test(f.name) && cr2[baseKey(f)];
    if (skip) { f.status = 'skipped'; f.checked = false; }
    else if (f.status === 'skipped') { f.status = 'wait'; f.checked = true; }
  });
}
function firstUsable() { for (var i = 0; i < S.files.length; i++) if (S.files[i].status !== 'skipped') return S.files[i]; return null; }
function usable() { return S.files.filter(function (f) { return f.status !== 'skipped'; }); }
function marked() { return S.files.filter(function (f) { return f.checked && f.status !== 'skipped'; }); }

/* ---------------------------------------------------------------- removing photos from the list */
function removeFiles(list) {
  var ids = {}, idx, next = null, wasSel = false;
  if (S.running) { toast('Poczekaj, aż skończy się wywoływanie.'); return; }
  list.forEach(function (f) { ids[f.id] = 1; });
  if (S.sel && ids[S.sel.id]) {
    wasSel = true;
    idx = S.files.indexOf(S.sel);
    for (var i = idx + 1; i < S.files.length && !next; i++) if (!ids[S.files[i].id] && S.files[i].status !== 'skipped') next = S.files[i];
    for (var j = idx - 1; j >= 0 && !next; j--) if (!ids[S.files[j].id] && S.files[j].status !== 'skipped') next = S.files[j];
  }
  list.forEach(function (f) { if (f.thumb) URL.revokeObjectURL(f.thumb); });
  S.files = S.files.filter(function (f) { return !ids[f.id]; });
  thumbQueue = thumbQueue.filter(function (f) { return !ids[f.id]; });
  applyPairs();
  if (wasSel) { if (CROP.on) cropClose(false, true); S.sel = null; if (next) selectFile(next); else clearPreview(); }
  renderList();
}

/* ---------------------------------------------------------------- the list on the page */
function itemMeta(f) {
  if (f.status === 'skipped') return { t: 'pominięte — jest plik RAW o tej samej nazwie', c: '' };
  if (f.status === 'running') return { t: 'wywołuję… ' + Math.round(f.pct) + ' %', c: '' };
  if (f.status === 'error') return { t: 'błąd: ' + f.err, c: 'err' };
  if (f.status === 'done') return { t: '✓ gotowe · ' + f.outW + ' × ' + f.outH + ' · ' + fmtMB(f.blob.size) + (f.raw ? ' · z RAW' : ''), c: 'ok' };
  return { t: fmtMB(f.size) + (/\.(cr2|cr3|dng)$/i.test(f.name) ? ' · ' + f.name.slice(-3).toUpperCase() : ' · JPG') + (f.crop || f.cropAr ? ' · z kadrem' : ''), c: '' };
}

function renderItem(f, li) {
  var m = itemMeta(f), act, ck = li.querySelector('.ck');
  li.className = (S.sel === f ? 'sel ' : '') + (f.status === 'skipped' ? 'skip' : '');
  li.querySelector('.nm').textContent = f.path;
  li.querySelector('.nm').title = f.path;
  var me = li.querySelector('.meta'); me.textContent = m.t; me.className = 'meta ' + m.c;
  ck.checked = !!f.checked; ck.disabled = f.status === 'skipped' || (S.running && f.status !== 'done');
  li.querySelector('.bar').style.width = f.status === 'running' ? f.pct + '%' : '0';
  li.querySelector('.x').disabled = S.running;
  var th = li.querySelector('.th');
  if (f.thumb && !th.firstChild) { th.innerHTML = '<img alt="" src="' + f.thumb + '">'; }
  act = li.querySelector('.meta');
  if (f.status === 'done') {
    var b = document.createElement('button'); b.className = 'dl'; b.type = 'button'; b.textContent = ' pobierz';
    b.onclick = function (ev) { ev.stopPropagation(); download(f.blob, f.outName); };
    me.appendChild(b);
  }
}

function renderList() {
  var ul = $('#list'), have = {}, u, done, mk;
  S.files.forEach(function (f) {
    var li = ul.querySelector('[data-id="' + f.id + '"]');
    if (!li) {
      li = document.createElement('li'); li.setAttribute('data-id', f.id);
      li.innerHTML = '<input type="checkbox" class="ck" aria-label="Wywołaj to zdjęcie"><span class="th"></span><span class="tx"><span class="nm"></span><span class="meta"></span></span><button type="button" class="x" title="Usuń to zdjęcie z listy (plik na dysku zostaje)" aria-label="Usuń z listy">×</button><span class="bar"></span>';
      li.onclick = function (ev) {
        if (ev.target.classList.contains('ck') || ev.target.classList.contains('x') || ev.target.classList.contains('dl')) return;
        if (f.status !== 'skipped') selectFile(f);
      };
      li.querySelector('.ck').onchange = function (ev) { f.checked = ev.target.checked; renderList(); };
      li.querySelector('.x').onclick = function (ev) { ev.stopPropagation(); removeFiles([f]); };
      ul.appendChild(li);
    }
    have[f.id] = 1;
    renderItem(f, li);
  });
  Array.prototype.slice.call(ul.children).forEach(function (li) { if (!have[li.getAttribute('data-id')]) ul.removeChild(li); });
  u = usable(); mk = marked(); done = S.files.filter(function (f) { return f.status === 'done'; }).length;
  $('#cnt').textContent = S.files.length ? '· ' + S.files.length + ' · zaznaczone ' + mk.length : '';
  $('#listEmpty').classList.toggle('hidden', !!S.files.length);
  $('#lact').classList.toggle('hidden', !S.files.length);
  $('#delSel').disabled = !mk.length || S.running;
  $('#btnClear').disabled = !S.files.length || S.running;
  var run = $('#btnRun');
  run.disabled = !mk.length || S.running || !S.pool.length;
  run.textContent = mk.length ? 'Wywołaj zaznaczone: ' + mk.length + ' ' + plural(mk.length, 'zdjęcie', 'zdjęcia', 'zdjęć') : (S.files.length ? 'Zaznacz zdjęcia do wywołania' : 'Wywołaj zaznaczone');
  $('#btnZip').disabled = !done || S.running;
  $('#btnSheet').disabled = !S.pv || S.running;
  $('#btnFolder').classList.toggle('hidden', !window.showDirectoryPicker);
  paintCropSection();                                  /* (the crop button waits while photos are being developed) */
}

function setChecked(pred) {
  S.files.forEach(function (f) { if (f.status !== 'skipped' && !(S.running && f.status !== 'done')) f.checked = !!pred(f); });
  renderList();
}

/* small pictures for the list, one at a time in the background (decoding a big JPEG takes a moment) */
var thumbQueue = [], thumbBusy = false;
function queueThumbs() {
  S.files.forEach(function (f) { if (!f.thumb && !f.thumbTried && thumbQueue.indexOf(f) < 0) thumbQueue.push(f); });
  if (!thumbBusy) pumpThumbs();
}
async function pumpThumbs() {
  thumbBusy = true;
  while (thumbQueue.length) {
    var f = thumbQueue.shift();
    if (S.files.indexOf(f) < 0) continue;
    f.thumbTried = true;
    try {
      var buf = await jpegSource(f.file), blob = new Blob([buf], { type: 'image/jpeg' });
      var bmp = await createImageBitmap(blob, { resizeWidth: 120, resizeQuality: 'low' });
      var c = document.createElement('canvas'); c.width = bmp.width; c.height = bmp.height;
      c.getContext('2d').drawImage(bmp, 0, 0);
      if (bmp.close) bmp.close();
      var out = await new Promise(function (r) { c.toBlob(r, 'image/jpeg', 0.8); });
      if (out && S.files.indexOf(f) >= 0) { f.thumb = URL.createObjectURL(out); var li = $('#list [data-id="' + f.id + '"]'); if (li) renderItem(f, li); }
    } catch (e) { /* no small picture: the row stays without one */ }
    await new Promise(function (r) { setTimeout(r, 15); });            /* let the page breathe between two photos */
  }
  thumbBusy = false;
}

/* ---------------------------------------------------------------- films */
function renderTabs() {
  var names = [], tabs = $('#tabs');
  S.films.forEach(function (f) { if (names.indexOf(f.type) < 0) names.push(f.type); });
  tabs.innerHTML = '';
  names.forEach(function (n) {
    var b = document.createElement('button'); b.type = 'button'; b.className = 'tab'; b.setAttribute('role', 'tab');
    b.textContent = n + ' (' + S.films.filter(function (f) { return f.type === n; }).length + ')';
    b.setAttribute('aria-selected', String(S.films[S.film].type === n));
    b.onclick = function () { var f = S.films.filter(function (x) { return x.type === n; })[0]; chooseFilm(f.i); };
    tabs.appendChild(b);
  });
}

function renderFilms() {
  var box = $('#films'), cur = S.films[S.film];
  box.innerHTML = '';
  S.films.filter(function (f) { return f.type === cur.type; }).forEach(function (f) {
    var b = document.createElement('button'); b.type = 'button'; b.className = 'fcard'; b.setAttribute('data-i', f.i);
    b.setAttribute('aria-pressed', String(f.i === S.film));
    b.innerHTML = '<span class="ph"></span><span class="tx"><span class="mk"></span><span class="fn"></span><span class="fi"></span></span>';
    b.querySelector('.mk').textContent = f.maker;
    b.querySelector('.fn').textContent = f.name;
    b.querySelector('.fi').textContent = (f.iso ? 'ISO ' + f.iso + ' · ' : '') + f.type.toLowerCase();
    b.querySelector('.tx').style.display = 'flex'; b.querySelector('.tx').style.flexDirection = 'column';
    b.onclick = function () { chooseFilm(f.i); };
    box.appendChild(b);
    if (S.thumbs && S.thumbs[f.i]) setThumb(f.i, S.thumbs[f.i]);
  });
  var d = $('#fdesc'); d.innerHTML = '<b></b> <span class="muted"></span><br><span></span>';
  d.querySelector('b').textContent = cur.full;
  d.querySelector('.muted').textContent = '— ' + cur.kind;
  d.lastChild.textContent = cur.desc;
}

function setThumb(i, url) {
  var b = $('#films .fcard[data-i="' + i + '"]');
  if (!b) return;
  b.classList.add('has-th');
  b.querySelector('.ph').innerHTML = '<img alt="" src="' + url + '">';
}

function chooseFilm(i) {
  S.film = i;
  renderTabs(); renderFilms();
  renderAdjust();
  save(); schedulePreview(); staleResults();
}

function staleResults() {                              /* the finished pictures are of the old settings: say it */
  if (S.running) return;
  var any = S.files.some(function (f) { return f.status === 'done'; });
  $('#runNote').textContent = any ? 'Ustawienia się zmieniły — gotowe zdjęcia są jeszcze z poprzednich. Zaznacz je ponownie i kliknij „Wywołaj”, żeby je odświeżyć.' : RUN_NOTE;
}

/* every film on the chosen photo, as small pictures in the film cards */
async function makeSheet() {
  var pv = S.pv, seq, n = S.films.length, next = 0;
  if (!pv || S.running) return;
  seq = S.sheetSeq = (S.sheetSeq || 0) + 1;
  S.thumbs = {};
  $('#btnSheet').disabled = true; $('#btnSheet').textContent = 'Liczę filmy…';
  var lanes = S.pool.slice(0, pv.buf ? S.pool.length : 3);                        /* (a RAW photo is decoded by every engine that takes part: not too many) */
  await Promise.all(lanes.map(async function (w) { return w.request({ type: 'source', buffer: await sourceCopy(pv) }); }));
  async function lane(w) {
    while (seq === S.sheetSeq) {
      var i = next++;
      if (i >= n) return;
      var p = paramsFor(i, 102, 70, S.looks, S.scene);
      if (!CROP.on) mergeX(p.x, geoX(pv.file));                                     /* the cards show the crop, too */
      var res = await w.request({ type: 'run', params: p });
      if (seq !== S.sheetSeq) return;
      if (res.rc === 0) {
        var blob = new Blob([res.jpeg], { type: 'image/jpeg' }), url = await thumbUrl(blob);
        if (seq !== S.sheetSeq) return;
        S.thumbs[i] = url; setThumb(i, url);
      }
    }
  }
  await Promise.all(lanes.map(lane));
  if (seq === S.sheetSeq) { $('#btnSheet').textContent = 'Pokaż filmy na moim zdjęciu'; $('#btnSheet').disabled = !S.pv || S.running; }
  if (S.inline && S.sel) { var again = S.thumbs; await selectFile(S.sel); S.thumbs = again; }      /* (one engine only: it has been given other photos) */
}
async function thumbUrl(blob) {                        /* a small copy of the picture for the card (the big one would be decoded for nothing) */
  try {
    var bmp = await createImageBitmap(blob, { resizeWidth: 360, resizeQuality: 'medium' });
    var c = document.createElement('canvas'); c.width = bmp.width; c.height = bmp.height;
    c.getContext('2d').drawImage(bmp, 0, 0);
    var out = await new Promise(function (r) { c.toBlob(r, 'image/jpeg', 0.82); });
    if (bmp.close) bmp.close();
    if (out) return URL.createObjectURL(out);
  } catch (e) { /* the browser cannot: use the big one */ }
  return URL.createObjectURL(blob);
}

/* ---------------------------------------------------------------- the settings of the camera (ML/SETTINGS/FILMLOOK.TXT) */
function loadCard(text) {
  var lines = text.split(/\r?\n/), n = 0, film = null, keys = ['strength', 'grain', 'contrast', 'hal', 'fade', 'warm', 'shadow', 'high', 'sat', 'vig'];
  lines.forEach(function (ln) {
    var m;
    if ((m = /^film=(.*)$/.exec(ln))) { S.films.forEach(function (f) { if (f.full === m[1].trim()) film = f.i; }); }
    else if ((m = /^mod=([^;]+);(.*)$/.exec(ln))) {
      var f = S.films.filter(function (x) { return x.full === m[1].trim(); })[0], nums = m[2].split(',').map(Number);
      if (!f || nums.length < 10 || nums.some(isNaN)) return;
      var v = S.looks[f.i] || {};
      keys.forEach(function (k, j) { var c = CTRL[k]; if (c) v[k] = clamp(nums[j], c.min, c.max); });
      S.looks[f.i] = v; n++;
    }
  });
  if (film === null && !n) { toast('To nie wygląda na plik FILMLOOK.TXT z karty.'); return; }
  if (film !== null) S.film = film;
  chooseFilm(S.film);
  toast('Wczytano ustawienia z karty' + (n ? ' (zmienione filmy: ' + n + ')' : '') + '.');
}
