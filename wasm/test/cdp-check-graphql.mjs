// Real-Chromium end-to-end check for the Wasabi 2 GraphQL wasm head.
// Boots the CANONICAL qtwasabi-pylon (mock player mode) serving both
// GraphQL and the dist (PYLON_STATIC_DIR), loads the head, and asserts
// the GraphQL data path from inside a real browser:
//   1. the head POSTs the player query and opens the EventSource
//      subscription (GET /graphql?query=subscription...) — proven from
//      the pylon's request log,
//   2. a GraphQL mutation pushes a typed event without breaking the head,
//   3. the pledit variant boots at its native 436x164,
//   4. no fatal console errors.
//
// Usage: node cdp-check-graphql.mjs <distDir> [bootSeconds]
// Requires a Chromium on --remote-debugging-port=$CDP_PORT (default 9222).
import {spawn} from 'node:child_process'
import {mkdtempSync} from 'node:fs'
import {tmpdir} from 'node:os'
import {join} from 'node:path'
import {fileURLToPath} from 'node:url'

const distDir = process.argv[2]
const bootSeconds = Number(process.argv[3] || 15)
if (!distDir) {
  console.error('usage: node cdp-check-graphql.mjs <distDir> [bootSeconds]')
  process.exit(2)
}

const PYLON_DIR = join(
  fileURLToPath(new URL('.', import.meta.url)),
  '../../deps/qtWasabi/api/pylon'
)
const PORT = 18000 + Math.floor(Math.random() * 2000)
const BASE = `http://127.0.0.1:${PORT}`

// ── boot the canonical pylon (mock player) ──────────────────────────
const pylonLog = []
const pylon = spawn(
  process.execPath,
  ['--enable-source-maps', '.pylon/index.js'],
  {
    cwd: PYLON_DIR,
    env: {
      ...process.env,
      PORT: String(PORT),
      PYLON_STATIC_DIR: distDir,
      PYLON_DISABLE_TELEMETRY: 'true'
    }
  }
)
pylon.stdout.on('data', d => pylonLog.push(d.toString()))
pylon.stderr.on('data', d => pylonLog.push(d.toString()))

const sleep = ms => new Promise(r => setTimeout(r, ms))
for (let i = 0; i < 60; i++) {
  try {
    const res = await fetch(`${BASE}/graphql`, {
      method: 'POST',
      headers: {'content-type': 'application/json'},
      body: '{"query":"{apiInfo{schemaVersion}}"}'
    })
    if (res.ok) break
  } catch {}
  await sleep(250)
}
console.log(`pylon on ${BASE} (mock player, serving ${distDir})`)

const FAIL_PATTERNS = [
  /LinkError/i, /Aborted/i, /BigInt/i, /function signature mismatch/i,
  /uncaught/i, /RuntimeError/i, /failed to instantiate/i,
  /Failed to create RHI/i, /Failed to initialize graphics/i,
  /layout load failed/i, /Application exit/i,
]
const IGNORE = [
  /AudioContext was not allowed/i, /enumerateDevices/i, /video_capture/i,
  /GroupMarkerNotSet/i, /SkinRuntime: cannot open/i, /favicon/i,
  /Failed to open audio device/i, /Failed to load resource/i,
]

const cdpPort = process.env.CDP_PORT || '9222'
const cdpBase = `http://127.0.0.1:${cdpPort}`

const failures = []
const consoleLines = []
const record = text => {
  if (IGNORE.some(r => r.test(text))) return
  consoleLines.push(text)
  if (FAIL_PATTERNS.some(r => r.test(text))) failures.push(text)
}

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

// ── 1) the player head over GraphQL ─────────────────────────────────
{
  const page = await openPage(`${BASE}/?window=player&graphql=/graphql`)
  await sleep(bootSeconds * 1000)

  const log = pylonLog.join('')
  check(consoleLines.some(l => /remote head connected/.test(l)),
        'console shows "remote head connected"')
  check(/POST \/graphql/.test(log),
        'pylon saw the player query (POST /graphql)')
  check(/GET \/graphql/.test(log),
        'pylon saw the EventSource subscription (GET /graphql?query=subscription)')

  // 2) GraphQL mutation → typed event → head must survive the push.
  await fetch(`${BASE}/graphql`, {
    method: 'POST',
    headers: {'content-type': 'application/json'},
    body: JSON.stringify({
      query: 'mutation{playlistAdd(paths:["/music/demo.mp3"]){ok}}'
    })
  })
  await fetch(`${BASE}/graphql`, {
    method: 'POST',
    headers: {'content-type': 'application/json'},
    body: JSON.stringify({query: 'mutation{play{ok}}'})
  })
  await sleep(2000)

  const shot = await page.send('Page.captureScreenshot', {format: 'png'})
  if (shot?.data) {
    const fs = await import('node:fs')
    fs.writeFileSync('/tmp/cdp-graphql-player.png',
                     Buffer.from(shot.data, 'base64'))
    console.log('  shot /tmp/cdp-graphql-player.png')
  }
  await page.close()
}

// ── 3) the pledit head ──────────────────────────────────────────────
{
  const page = await openPage(`${BASE}/?window=pledit&graphql=/graphql`)
  await sleep(bootSeconds * 1000)
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
    fs.writeFileSync('/tmp/cdp-graphql-pledit.png',
                     Buffer.from(shot.data, 'base64'))
    console.log('  shot /tmp/cdp-graphql-pledit.png')
  }
  await page.close()
}

check(failures.length === 0, 'no fatal console errors')
if (failures.length) console.log([...new Set(failures)].join('\n'))
console.log('=== last console lines ===')
console.log(consoleLines.slice(-12).join('\n'))

pylon.kill()
console.log(ok ? 'PASS' : 'FAIL')
process.exit(ok ? 0 : 1)
