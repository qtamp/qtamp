// One-origin test server for the WASM remote head: serves the built
// dist (index.html, qtamp.js/.wasm, qtloader.js) AND a scripted
// pylon/PROTOCOL.md mock backend (/state, /events SSE, /cmd) — so the
// head can be pointed at `?graphql=/` with no CORS in the picture.
// COOP/COEP headers ride on every response (Qt wasm's verified-serving
// setup).  Import { startMockRemoteServer } from tests, or run
// standalone: node mock-remote-server.mjs <distDir> [port].
import {createServer} from 'node:http'
import {readFile} from 'node:fs/promises'
import {extname, join, normalize} from 'node:path'

const MIME = {
  '.html': 'text/html', '.js': 'application/javascript',
  '.wasm': 'application/wasm', '.svg': 'image/svg+xml',
  '.png': 'image/png', '.json': 'application/json'
}

export function makeInitialState() {
  return {
    epoch: 'mock-epoch-1',
    revision: 1,
    serverNowMs: 1000,
    transport: {playing: false, paused: false, positionMs: 0,
                positionAtMs: 1000, durationMs: 0, volume: 100, pan: 0.5},
    track: {title: '', artist: '', album: '', filename: '',
            displayTitle: '', decoder: '', bitrate: 0, sampleRate: 0,
            channels: 0},
    playlist: {revision: 0, count: 0, currentIndex: -1, rows: []},
    eq: {on: false, auto: false, preamp: 31, bands: Array(10).fill(31)}
  }
}

export async function startMockRemoteServer(distDir, port = 0) {
  const state = makeInitialState()
  const cmdLog = []
  const sinks = new Set()
  const hits = {state: 0, events: 0, cmd: 0}

  const pushEvent = (name, payload) => {
    const frame = `event: ${name}\ndata: ${JSON.stringify(payload)}\n\n`
    for (const res of sinks) res.write(frame)
  }
  const bumpAndPush = section => {
    state.revision++
    if (section === 'playlist') state.playlist.revision++
    pushEvent(section, {
      epoch: state.epoch, revision: state.revision,
      serverNowMs: state.serverNowMs, [section]: state[section]
    })
  }

  const server = createServer(async (req, res) => {
    const url = new URL(req.url || '/', 'http://x')
    res.setHeader('cross-origin-opener-policy', 'same-origin')
    res.setHeader('cross-origin-embedder-policy', 'require-corp')

    if (req.method === 'GET' && url.pathname === '/state') {
      hits.state++
      state.playlist.count = state.playlist.rows.length
      res.writeHead(200, {'content-type': 'application/json'})
      res.end(JSON.stringify(state))
      return
    }
    if (req.method === 'GET' && url.pathname === '/events') {
      hits.events++
      res.writeHead(200, {
        'content-type': 'text/event-stream',
        'cache-control': 'no-store',
        connection: 'keep-alive'
      })
      state.playlist.count = state.playlist.rows.length
      res.write(`event: state\ndata: ${JSON.stringify(state)}\n\n`)
      sinks.add(res)
      res.on('close', () => sinks.delete(res))
      return
    }
    if (req.method === 'POST' && url.pathname === '/cmd') {
      hits.cmd++
      let body = ''
      req.on('data', c => (body += c))
      req.on('end', () => {
        let cmd = {}
        try { cmd = JSON.parse(body) } catch {}
        cmdLog.push({op: cmd.op, args: cmd.args ?? {}})
        switch (cmd.op) {
          case 'play':
            state.transport.playing = true
            state.transport.paused = false
            bumpAndPush('transport')
            break
          case 'pause':
            state.transport.paused = true
            bumpAndPush('transport')
            break
          case 'stop':
            state.transport.playing = false
            state.transport.paused = false
            bumpAndPush('transport')
            break
          case 'setVolume':
            state.transport.volume = Number(cmd.args?.v) || 0
            bumpAndPush('transport')
            break
          case 'playlistAddPaths':
            for (const p of cmd.args?.paths ?? [])
              state.playlist.rows.push({text: String(p), durationMs: 0})
            state.playlist.count = state.playlist.rows.length
            bumpAndPush('playlist')
            break
          default:
            break
        }
        res.writeHead(200, {'content-type': 'application/json'})
        res.end(JSON.stringify({ok: true, revision: state.revision}))
      })
      return
    }

    // Static dist serving.
    let file = normalize(url.pathname).replace(/^([/\\.])+/, '')
    if (file === '') file = 'index.html'
    try {
      const data = await readFile(join(distDir, file))
      res.writeHead(200, {
        'content-type': MIME[extname(file)] || 'application/octet-stream',
        'cache-control': 'no-cache'
      })
      res.end(data)
    } catch {
      res.writeHead(404).end('not found')
    }
  })

  await new Promise(resolve => server.listen(port, '127.0.0.1', resolve))
  return {
    server,
    port: server.address().port,
    state, cmdLog, hits, pushEvent, bumpAndPush,
    close: () => new Promise(resolve => {
      for (const res of sinks) res.destroy()
      server.close(() => resolve())
    })
  }
}

// Standalone: node mock-remote-server.mjs <distDir> [port]
if (import.meta.url === `file://${process.argv[1]}`) {
  const {port} = await startMockRemoteServer(
    process.argv[2] || '.', Number(process.argv[3] || 0))
  console.log(`mock remote server on http://127.0.0.1:${port}`)
}
