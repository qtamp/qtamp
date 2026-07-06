/**
 * Mock backend — a node reference implementation of ../PROTOCOL.md for
 * the pylon tests (and `node --loader`-free dev without a compiled
 * qtamp). Serves /state, /events (SSE with the initial `state` frame),
 * /cmd (records ops, mutates the scripted state, pushes events). The
 * REAL implementation is `qtamp --backend`; keep the two in sync via
 * the shared protocol document.
 */
import {createServer, type Server, type ServerResponse} from 'node:http'

export type MockBackend = {
  server: Server
  port: number
  state: any
  cmdLog: {op: string; args: any}[]
  pushEvent: (name: string, payload: any) => void
  bumpAndPush: (section: 'transport' | 'track' | 'playlist' | 'eq') => void
  close: () => Promise<void>
}

export function makeInitialState() {
  return {
    epoch: 'mock-epoch-1',
    revision: 1,
    serverNowMs: 1000,
    transport: {
      playing: false,
      paused: false,
      positionMs: 0,
      positionAtMs: 1000,
      durationMs: 0,
      volume: 100,
      pan: 0.5
    },
    track: {
      title: '',
      artist: '',
      album: '',
      filename: '',
      displayTitle: '',
      decoder: '',
      bitrate: 0,
      sampleRate: 0,
      channels: 0
    },
    playlist: {revision: 0, count: 0, currentIndex: -1, rows: []},
    eq: {on: false, auto: false, preamp: 31, bands: Array(10).fill(31)}
  }
}

export async function startMockBackend(port = 0): Promise<MockBackend> {
  const state = makeInitialState()
  const cmdLog: {op: string; args: any}[] = []
  const sinks = new Set<ServerResponse>()

  const pushEvent = (name: string, payload: any) => {
    const frame = `event: ${name}\ndata: ${JSON.stringify(payload)}\n\n`
    for (const res of sinks) res.write(frame)
  }
  const bumpAndPush = (section: 'transport' | 'track' | 'playlist' | 'eq') => {
    state.revision++
    if (section === 'playlist') state.playlist.revision++
    pushEvent(section, {
      epoch: state.epoch,
      revision: state.revision,
      serverNowMs: state.serverNowMs,
      [section]: state[section]
    })
  }

  const server = createServer((req, res) => {
    const url = new URL(req.url || '/', 'http://x')
    if (req.method === 'GET' && url.pathname === '/state') {
      state.playlist.count = state.playlist.rows.length
      res.writeHead(200, {'content-type': 'application/json'})
      res.end(JSON.stringify(state))
      return
    }
    if (req.method === 'GET' && url.pathname === '/events') {
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
      let body = ''
      req.on('data', c => (body += c))
      req.on('end', () => {
        let cmd: any
        try {
          cmd = JSON.parse(body)
        } catch {
          res.writeHead(400).end(JSON.stringify({ok: false, error: 'bad json'}))
          return
        }
        cmdLog.push({op: cmd.op, args: cmd.args ?? {}})
        // A scripted subset of the real semantics, enough for the tests.
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
            for (const p of cmd.args?.paths ?? []) {
              state.playlist.rows.push({text: String(p), durationMs: 0})
            }
            state.playlist.count = state.playlist.rows.length
            bumpAndPush('playlist')
            break
          case 'playlistPlayRow': {
            const expect = cmd.args?.expectPlaylistRevision
            if (expect != null && Number(expect) !== state.playlist.revision) {
              res
                .writeHead(400, {'content-type': 'application/json'})
                .end(JSON.stringify({ok: false, error: 'playlistRevision mismatch'}))
              return
            }
            state.playlist.currentIndex = Number(cmd.args?.row) || 0
            state.transport.playing = true
            bumpAndPush('playlist')
            bumpAndPush('transport')
            break
          }
          case 'boom': // test hook: an op the backend rejects
            res
              .writeHead(400, {'content-type': 'application/json'})
              .end(JSON.stringify({ok: false, error: 'boom'}))
            return
          default:
            break
        }
        res
          .writeHead(200, {'content-type': 'application/json'})
          .end(JSON.stringify({ok: true, revision: state.revision}))
      })
      return
    }
    res.writeHead(404).end('not found')
  })

  await new Promise<void>(resolve =>
    server.listen(port, '127.0.0.1', resolve)
  )
  const addr = server.address()
  const realPort = typeof addr === 'object' && addr ? addr.port : port

  return {
    server,
    port: realPort,
    state,
    cmdLog,
    pushEvent,
    bumpAndPush,
    close: () =>
      new Promise<void>(resolve => {
        for (const res of sinks) res.destroy()
        server.close(() => resolve())
      })
  }
}
