/* engine_core.js - FILM LOOK DESKTOP: the JavaScript side of the engine (the WebAssembly module built from src/fd_wasm.c).
 * Plain JavaScript without modules, so that the very same text runs inside the page, inside a Web Worker and in the tests
 * under Node (tests/test_wasm.js). Own code of the film look project. */

/* CRC-32 of a byte array (the ZIP file needs it) */
var fdCrcTable = null;
function fdCrc32(u8) {
  if (!fdCrcTable) {
    fdCrcTable = new Int32Array(256);
    for (var n = 0; n < 256; n++) {
      var c = n;
      for (var k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
      fdCrcTable[n] = c;
    }
  }
  var crc = -1;
  for (var i = 0; i < u8.length; i++) crc = fdCrcTable[(crc ^ u8[i]) & 255] ^ (crc >>> 8);
  return (crc ^ -1) >>> 0;
}

/* wasmModule: a compiled WebAssembly.Module. Returns (a promise of) the engine:
 *   engine.films            the list of films: {i, name, full, maker, type, typeNo, kind, desc, iso, mono}
 *   engine.version          text
 *   engine.setSource(u8)    keeps a photo (the whole file) in the engine's memory, for several runs
 *   engine.run(params)      develops the kept photo: params = {film, size, quality, strength, grain, contrast, fade, warm,
 *                           shadow, high, sat, vig}; returns {rc, text, srcW, srcH, outW, outH, jpeg (Uint8Array)}
 *   engine.process(u8, params)   setSource + run + forget, for one photo at a time
 *   engine.onProgress       set to a function(permille) to get progress */
async function fdCreateEngine(wasmModule) {
  var engine = { onProgress: null };
  var inst = await WebAssembly.instantiate(wasmModule, { env: { progress: function (pm) { if (engine.onProgress) engine.onProgress(pm); } } });
  var x = inst.exports;
  var dec = new TextDecoder('utf-8');
  function str(p) {
    var m = new Uint8Array(x.memory.buffer), e = p >>> 0;
    while (m[e]) e++;
    return dec.decode(m.subarray(p >>> 0, e));
  }
  var src = null;

  engine.version = str(x.fd_version());
  engine.films = [];
  for (var i = 0, n = x.fd_film_count(); i < n; i++) {
    engine.films.push({
      i: i, name: str(x.fd_film_text(i, 0)), full: str(x.fd_film_text(i, 1)), maker: str(x.fd_film_text(i, 2)),
      type: str(x.fd_film_text(i, 3)), kind: str(x.fd_film_text(i, 4)), desc: str(x.fd_film_text(i, 5)),
      typeNo: x.fd_film_num(i, 0), iso: x.fd_film_num(i, 1), mono: x.fd_film_num(i, 2) === 1
    });
  }

  engine.forget = function () { x.fd_reset(); src = null; };

  engine.setSource = function (u8) {
    x.fd_reset();
    var ptr = x.fd_alloc(u8.length) >>> 0;
    if (!ptr) { src = null; return false; }
    new Uint8Array(x.memory.buffer, ptr, u8.length).set(u8);
    x.fd_heap_mark();
    src = { ptr: ptr, len: u8.length };
    return true;
  };

  engine.run = function (p) {
    if (!src) return { rc: -4, text: 'za mało pamięci na to zdjęcie' };
    var rc = x.fd_process(src.ptr, src.len, p.film | 0, p.size | 0, p.quality | 0, p.strength | 0, p.grain | 0, p.contrast | 0,
      p.fade | 0, p.warm | 0, p.shadow | 0, p.high | 0, p.sat | 0, p.vig | 0) | 0;
    var res = { rc: rc, text: str(x.fd_error_text(rc)) };
    if (rc === 0) {
      var ptr = x.fd_result_int(6) >>> 0, len = x.fd_result_int(5) >>> 0;
      res.srcW = x.fd_result_int(1) | 0; res.srcH = x.fd_result_int(2) | 0;
      res.outW = x.fd_result_int(3) | 0; res.outH = x.fd_result_int(4) | 0;
      res.jpeg = new Uint8Array(x.memory.buffer, ptr, len).slice();
    }
    x.fd_heap_release();
    return res;
  };

  engine.process = function (u8, p) {
    var res;
    if (!engine.setSource(u8)) return { rc: -4, text: 'za mało pamięci na to zdjęcie' };
    res = engine.run(p);
    engine.forget();
    return res;
  };
  return engine;
}

if (typeof module !== 'undefined' && module.exports) module.exports = { fdCreateEngine: fdCreateEngine, fdCrc32: fdCrc32 };
