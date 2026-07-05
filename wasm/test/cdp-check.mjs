// Real-Chromium smoke check for the WebAssembly player, driven over the
// DevTools Protocol. Unlike `--headless=new --screenshot --virtual-time-budget`,
// this runs in real wall-clock time so timer-driven code paths (the Maki
// timer chain, the audio pump) actually execute, and it captures every
// console message and uncaught exception, so LinkError / BigInt / abort
// surface exactly as in a user's browser.
//
// Usage: node cdp-check.mjs <url> [seconds] [screenshotPath]
// Requires a Chromium already listening on --remote-debugging-port=9222.

const url = process.argv[2] || 'https://qtamp.org/player/';
const seconds = Number(process.argv[3] || 18);
const shotPath = process.argv[4] || '/tmp/cdp-shot.png';

const FAIL_PATTERNS = [
  /LinkError/i, /Aborted/i, /BigInt/i, /function signature mismatch/i,
  /uncaught/i, /RuntimeError/i, /failed to instantiate/i,
  /Failed to create RHI/i, /Failed to initialize graphics/i,
];
const IGNORE = [
  /AudioContext was not allowed/i, /enumerateDevices/i, /video_capture/i,
  /GroupMarkerNotSet/i, /SkinRuntime: cannot open/i,
];

// CDP endpoint port — override via CDP_PORT (TS6 occupies 9222 on the
// dev box, so a co-hosted browser check must point elsewhere).
const cdpPort = process.env.CDP_PORT || '9222';
const cdpBase = `http://127.0.0.1:${cdpPort}`;
const list = await (await fetch(`${cdpBase}/json/list`)).json();
let page = list.find((t) => t.type === 'page');
if (!page) {
  const nt = await (await fetch(
    `${cdpBase}/json/new?` + encodeURIComponent(url))).json();
  page = nt;
}

const ws = new WebSocket(page.webSocketDebuggerUrl);
let id = 0;
const pending = new Map();
const send = (method, params = {}) =>
  new Promise((res) => { const m = ++id; pending.set(m, res);
    ws.send(JSON.stringify({ id: m, method, params })); });

const consoleLines = [];
const failures = [];
const record = (text) => {
  if (IGNORE.some((r) => r.test(text))) return;
  consoleLines.push(text);
  if (FAIL_PATTERNS.some((r) => r.test(text))) failures.push(text);
};

await new Promise((res) => (ws.onopen = res));
ws.onmessage = (ev) => {
  const msg = JSON.parse(ev.data);
  if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg.result); pending.delete(msg.id); return; }
  if (msg.method === 'Runtime.consoleAPICalled') {
    record('[console] ' + (msg.params.args || []).map((a) => a.value ?? a.description ?? '').join(' '));
  } else if (msg.method === 'Log.entryAdded') {
    record('[log:' + msg.params.entry.level + '] ' + msg.params.entry.text);
  } else if (msg.method === 'Runtime.exceptionThrown') {
    const e = msg.params.exceptionDetails;
    record('[exception] ' + (e.exception?.description || e.text));
  }
};

await send('Runtime.enable');
await send('Log.enable');
await send('Page.enable');
await send('Page.navigate', { url });

// Let the skin boot, then exercise the shade-mode toggle — the path
// that aborted under EMULATE_FUNCTION_POINTER_CASTS ("function
// signature mismatch" on the shade switch script).  Pass click coords
// as "x,y" in argv[5]; the WinampModernPP shade button sits near the
// top-right of the player.  A canvas double-click on the titlebar also
// triggers SWITCH;shade, so we double-click there as a coord-robust
// fallback and watch the console for an abort.
const clickArg = process.argv[5];
await new Promise((r) => setTimeout(r, Math.min(seconds, 10) * 1000));
const canvasBox = await send('Runtime.evaluate', { expression:
  '(() => { const c = document.querySelector("canvas");' +
  ' if (!c) return null; const r = c.getBoundingClientRect();' +
  ' return JSON.stringify([r.left, r.top, r.width, r.height]); })()',
  returnByValue: true });
let cx = 0, cy = 0;
if (clickArg && clickArg.includes(',')) {
  [cx, cy] = clickArg.split(',').map(Number);
} else if (canvasBox?.result?.value) {
  const [l, t, w] = JSON.parse(canvasBox.result.value);
  cx = l + w - 40; cy = t + 8;   // titlebar, near the shade/close cluster
}
if (cx || cy) {
  for (const type of ['mousePressed', 'mouseReleased',
                      'mousePressed', 'mouseReleased']) {
    await send('Input.dispatchMouseEvent', {
      type, x: cx, y: cy, button: 'left', clickCount: 2 });
    await new Promise((r) => setTimeout(r, 60));
  }
  record('[test] dispatched shade double-click at ' + cx + ',' + cy);
}

await new Promise((r) => setTimeout(r, Math.max(2, seconds - 10) * 1000));

const shot = await send('Page.captureScreenshot', { format: 'png' });
if (shot?.data) {
  const fs = await import('node:fs');
  fs.writeFileSync(shotPath, Buffer.from(shot.data, 'base64'));
}

console.log('=== console (' + consoleLines.length + ' lines) ===');
console.log(consoleLines.slice(-20).join('\n'));
console.log('=== screenshot: ' + shotPath + ' ===');
if (failures.length) {
  console.log('FAIL (' + failures.length + '):');
  console.log([...new Set(failures)].join('\n'));
  process.exit(1);
}
console.log('PASS: no fatal console errors in ' + seconds + 's of real-time run');
ws.close();
process.exit(0);
