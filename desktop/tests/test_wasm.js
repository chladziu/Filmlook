// test_wasm.js - runs the browser engine (the .wasm file) under Node on one photo and writes the JPEG:
//   node test_wasm.js fd.wasm photo.CR2 out.jpg film(0-based) size(0,1,2,101,102) quality [efekt ziarno kontrast matowosc cieplo cienie swiatla nasycenie winieta]
// The test script (run_tests.sh) compares the file with the one of the command line version and of the module's hostdev.
'use strict';
const fs = require('fs');
const path = require('path');
const core = require(path.join(__dirname, '..', 'web', 'engine_core.js'));

(async () => {
  const a = process.argv.slice(2);
  if (a.length < 6) { console.error('usage: test_wasm.js fd.wasm photo out.jpg film size quality [9 sliders]'); process.exit(2); }
  const mod = await WebAssembly.compile(fs.readFileSync(a[0]));
  const eng = await core.fdCreateEngine(mod);
  const s = a.slice(6).map(Number);
  const p = { film: +a[3], size: +a[4], quality: +a[5],
    strength: s.length > 0 ? s[0] : 100, grain: s.length > 1 ? s[1] : 100, contrast: s[2] || 0, fade: s[3] || 0,
    warm: s[4] || 0, shadow: s[5] || 0, high: s[6] || 0, sat: s[7] || 0, vig: s[8] || 0 };
  const t0 = Date.now();
  const res = eng.process(new Uint8Array(fs.readFileSync(a[1])), p);
  if (res.rc !== 0) { console.error('ERROR', res.rc, res.text); process.exit(1); }
  fs.writeFileSync(a[2], res.jpeg);
  console.error(`${path.basename(a[1])}: ${res.srcW}x${res.srcH} -> ${res.outW}x${res.outH}  ${res.jpeg.length} bytes  ${Date.now() - t0} ms  (${eng.films.length} films, engine ${eng.version})`);
})();
