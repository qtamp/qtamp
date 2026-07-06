// Real-Chromium end-to-end check for the QTAMP_WASM_REMOTE head.
// Boots the one-origin mock server (dist + PROTOCOL.md routes), loads
// the head with ?window=player&graphql=/, and asserts the full network
// loop from inside a real browser:
//   1. the head fetches /state and subscribes to /events (EventSource),
//   2. a server-side mutation pushes an SSE event without breaking it,
//   3. a pushed revision GAP forces the head to refetch /state — the
//      observable proof that events are delivered AND applied,
//   4. no fatal console errors; screenshots for the eyeball check, and
//      the ?window=pledit variant boots at its native 436x164.
//
// Usage: node cdp-check-remote.mjs <distDir> [bootSeconds]
// Requires a Chromium listening on --remote-debugging-port=$CDP_PORT
// (default 9222; the dev box runs TS6 on 9222 — use another port).
import {startMockRemoteServer} from './mock-remote-server.mjs'

const distDir = process.argv[2]
const bootSeconds = Number(process.argv[3] || 15)
if (!distDir) {
  console.error('usage: node cdp-check-remote.mjs <distDir> [bootSeconds]')
  process.exit(2)
}

const FAIL_PATTERNS = [
  /LinkError/i, /Aborted/i, /BigInt/i, /function signature mismatch/i,
  /uncaught/i, /RuntimeError/i, /failed to instantiate/i,
  /Failed to create RHI/i, /Failed to initialize graphics/i,
  /layout load failed/i, /Application exit/i,
]
const IGNORE = [
  /AudioContext was not allowed/i, /enumerateDevices/i, /video_capture/i,
  /GroupMarkerNotSet/i, /SkinRuntime: cannot open/i, /favicon/i,
  /Failed to open audio device/i,
]

const mock = await startMockRemoteServer(distDir)
const base = `http://127.0.0.1:${mock.port}`
console.log(`mock server on ${base}`)

const cdpPort = process.env.CDP_PORT || '9222'
const cdpBase = `http://127.0.0.1:${cdpPort}`

const failures = []
const consoleLines = []
const record = text => {
  if (IGNORE.some(r => r.test(text))) return
  consoleLines.push(text)
  if (FAIL_PATTERNS.some(r => r.test(text))) failures.push(text)
}
const sleep = ms => new Promise(r => setTimeout(r, ms))

async function openPage(url) {
  const t = await (await fetch(
    `${cdpBase}/json/new?` + encodeURIComponent(url), {method: 'PUT'}
  )).json()
  const ws = new WebSocket(t.webSocketDebuggerUrl)
  let id = 0
  const pending = new Map()
  const send = (method, params = {}) =>
    new Promise(res => {
      const m = ++id
      pending.set(m, res)
      ws.send(JSON.stringify({id: m, method, params}))
    })
  await new Promise(res => (ws.onopen = res))
  ws.onmessage = ev => {
    const msg = JSON.parse(ev.data)
    if (msg.id && pending.has(msg.id)) {
      pending.get(msg.id)(msg.result)
      pending.delete(msg.id)
      return
    }
    if (msg.method === 'Runtime.consoleAPICalled') {
      record('[console] ' + (msg.params.args || [])
        .map(a => a.value ?? a.description ?? '').join(' '))
    } else if (msg.method === 'Log.entryAdded') {
      record('[log:' + msg.params.entry.level + '] ' + msg.params.entry.text)
    } else if (msg.method === 'Runtime.exceptionThrown') {
      const e = msg.params.exceptionDetails
      record('[exception] ' + (e.exception?.description || e.text))
    }
  }
  await send('Runtime.enable')
  await send('Log.enable')
  await send('Page.enable')
  const close = () => {
    ws.close()
    return fetch(`${cdpBase}/json/close/${t.id}`).catch(() => {})
  }
  return {send, close}
}

let ok = true
const check = (cond, label) => {
  console.log(`  ${cond ? 'ok  ' : 'FAIL'} ${label}`)
  if (!cond) ok = false
}

// ── 1) the player head ─────────────────────────────────────────────
{
  const page = await openPage(`${base}/?window=player&graphql=/`)
  await sleep(bootSeconds * 1000)

  check(mock.hits.state >= 1, `head fetched /state (${mock.hits.state}x)`)
  check(mock.hits.events >= 1,
        `head subscribed /events via EventSource (${mock.hits.events}x)`)
  check(consoleLines.some(l => /remote head connected/.test(l)),
        'console shows "remote head connected"')

  // 2) server-side mutation → SSE push must not break the head.
  await fetch(`${base}/cmd`, {
    method: 'POST',
    body: JSON.stringify({op: 'playlistAddPaths',
                          args: {paths: ['/music/demo.mp3']}})
  })
  await fetch(`${base}/cmd`, {method: 'POST',
                              body: JSON.stringify({op: 'play', args: {}})})
  await sleep(1500)

  // 3) revision GAP → the head must resync by refetching /state.
  const statesBefore = mock.hits.state
  mock.state.transport.volume = 55
  mock.pushEvent('transport', {
    epoch: mock.state.epoch,
    revision: mock.state.revision + 5, // a gap the head cannot apply
    serverNowMs: mock.state.serverNowMs,
    transport: mock.state.transport
  })
  mock.state.revision += 5
  let resynced = false
  for (let i = 0; i < 40 && !resynced; i++) {
    await sleep(250)
    resynced = mock.hits.state > statesBefore
  }
  check(resynced,
        'revision gap forced a /state resync (events delivered AND applied)')

  const shot = await page.send('Page.captureScreenshot', {format: 'png'})
  if (shot?.data) {
    const fs = await import('node:fs')
    fs.writeFileSync('/tmp/cdp-remote-player.png',
                     Buffer.from(shot.data, 'base64'))
    console.log('  shot /tmp/cdp-remote-player.png')
  }
  await page.close()
}

// ── 4) the pledit head (container root through query params) ───────
{
  const page = await openPage(`${base}/?window=pledit&graphql=/`)
  await sleep(bootSeconds * 1000)
  // Qt 6.8 renders into a shadow root (qt-shadow-container), so the
  // canvas is invisible to a plain document.querySelector.
  const box = await page.send('Runtime.evaluate', {
    expression:
      '(() => { const find = root => {' +
      '  const c = root.querySelector("canvas"); if (c) return c;' +
      '  for (const el of root.querySelectorAll("*"))' +
      '    if (el.shadowRoot) { const r = find(el.shadowRoot); if (r) return r; }' +
      '  return null; };' +
      ' const c = find(document);' +
      ' if (!c) return ""; const r = c.getBoundingClientRect();' +
      ' return Math.round(r.width) + "x" + Math.round(r.height); })()',
    returnByValue: true
  })
  check(box?.result?.value === '436x164',
        `pledit head canvas is 436x164 (got "${box?.result?.value}")`)
  const shot = await page.send('Page.captureScreenshot', {format: 'png'})
  if (shot?.data) {
    const fs = await import('node:fs')
    fs.writeFileSync('/tmp/cdp-remote-pledit.png',
                     Buffer.from(shot.data, 'base64'))
    console.log('  shot /tmp/cdp-remote-pledit.png')
  }
  await page.close()
}

check(failures.length === 0, 'no fatal console errors')
if (failures.length) console.log([...new Set(failures)].join('\n'))
console.log('=== last console lines ===')
console.log(consoleLines.slice(-15).join('\n'))

await mock.close()
console.log(ok ? 'PASS' : 'FAIL')
process.exit(ok ? 0 : 1)
