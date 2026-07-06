// End-to-end against the booted build artifact over real HTTP: mock
// backend + built pylon (`npm run build` first). Asserts the typed
// GraphQL schema, mutation forwarding, the subscription push, and the
// control-channel passthrough routes the qtamp heads use.
import {afterAll, beforeAll, expect, test} from 'vitest'

import {startMockBackend, type MockBackend} from './mock-backend'

let mock: MockBackend

const PORT = '18791'
const BASE = `http://127.0.0.1:${PORT}`

beforeAll(async () => {
  mock = await startMockBackend()
  process.env.PORT = PORT
  process.env.QTAMP_BACKEND_URL = `http://127.0.0.1:${mock.port}`
  await import('../.pylon/index.js')
  for (let i = 0; i < 50; i++) {
    try {
      const res = await fetch(
        BASE + '/graphql?query=' + encodeURIComponent('{player{revision}}')
      )
      if (res.ok) {
        const body = (await res.json()) as {data?: any}
        if (body.data && 'player' in body.data) return
        throw new Error(`port ${PORT} answers with a foreign schema`)
      }
    } catch (e) {
      if (e instanceof Error && e.message.includes('foreign')) throw e
    }
    await new Promise(r => setTimeout(r, 100))
  }
  throw new Error('pylon never came up on ' + BASE)
}, 20000)

afterAll(async () => {
  await mock?.close()
})

async function gql(query: string, variables?: Record<string, unknown>) {
  const res = await fetch(BASE + '/graphql', {
    method: 'POST',
    headers: {'content-type': 'application/json'},
    body: JSON.stringify({query, variables})
  })
  return res.json() as Promise<{data?: any; errors?: any[]}>
}

async function until(cond: () => Promise<boolean> | boolean, ms = 4000) {
  const t0 = Date.now()
  for (;;) {
    if (await cond()) return
    if (Date.now() - t0 > ms) throw new Error('condition never became true')
    await new Promise(r => setTimeout(r, 50))
  }
}

test('player query mirrors the backend snapshot', async () => {
  await until(async () => {
    const body = await gql('{player{epoch}}')
    return body.data?.player?.epoch === 'mock-epoch-1'
  })
  const body = await gql(
    '{player{revision transport{playing volume} playlist{rowCount currentIndex} eq{preamp}}}'
  )
  expect(body.errors).toBeUndefined()
  expect(body.data.player.transport.playing).toBe(false)
  expect(body.data.player.transport.volume).toBe(100)
  expect(body.data.player.playlist.rowCount).toBe(0)
  expect(body.data.player.eq.preamp).toBe(31)
})

test('mutations forward to the backend and return fresh state', async () => {
  const body = await gql(
    'mutation{playlistAdd(paths:["/music/a.mp3","/music/b.mp3"]){playlist{rowCount rows{row text}}}}'
  )
  expect(body.errors).toBeUndefined()
  expect(body.data.playlistAdd.playlist.rowCount).toBe(2)
  expect(mock.cmdLog.some(c => c.op === 'playlistAddPaths')).toBe(true)

  const play = await gql('mutation{play{transport{playing}}}')
  expect(play.data.play.transport.playing).toBe(true)

  // A backend rejection surfaces as a GraphQL error.
  const boom = await gql('mutation{playRow(row:0,expectPlaylistRevision:999){revision}}')
  expect(boom.errors?.length).toBeGreaterThan(0)
})

test('playerEvents subscription pushes on change', async () => {
  const controller = new AbortController()
  const res = await fetch(
    BASE +
      '/graphql?query=' +
      encodeURIComponent('subscription{playerEvents}'),
    {headers: {accept: 'text/event-stream'}, signal: controller.signal}
  )
  expect(res.status).toBe(200)
  const reader = res.body!.getReader()
  const decoder = new TextDecoder()
  let buffer = ''
  const nextDoc = async (): Promise<any> => {
    for (;;) {
      const idx = buffer.indexOf('\n\n')
      if (idx >= 0) {
        const frame = buffer.slice(0, idx)
        buffer = buffer.slice(idx + 2)
        const dataLine = frame
          .split('\n')
          .find(l => l.startsWith('data:'))
        if (!dataLine) continue
        const payload = JSON.parse(dataLine.slice(5).trim())
        const doc = payload?.data?.playerEvents
        if (doc) return JSON.parse(doc)
        continue
      }
      const {done, value} = await reader.read()
      if (done) throw new Error('stream ended')
      buffer += decoder.decode(value, {stream: true})
    }
  }

  const first = await nextDoc() // immediate current state
  expect(first.connected).toBe(true)

  mock.state.transport.volume = 55
  mock.bumpAndPush('transport')

  const t0 = Date.now()
  for (;;) {
    const doc = await nextDoc()
    if (doc.transport?.volume === 55) break
    if (Date.now() - t0 > 4000) throw new Error('push never arrived')
  }
  controller.abort()
})

test('control-channel passthrough serves the heads', async () => {
  // /state via the pylon = the backend's document.
  const state = (await (await fetch(BASE + '/state')).json()) as any
  expect(state.epoch).toBe('mock-epoch-1')

  // /cmd via the pylon lands in the backend's log.
  const before = mock.cmdLog.length
  const res = await fetch(BASE + '/cmd', {
    method: 'POST',
    headers: {'content-type': 'application/json'},
    body: JSON.stringify({op: 'pause', args: {}})
  })
  expect(((await res.json()) as any).ok).toBe(true)
  expect(mock.cmdLog.length).toBe(before + 1)

  // /events streams through: the first frame is the state snapshot.
  const controller = new AbortController()
  const ev = await fetch(BASE + '/events', {signal: controller.signal})
  expect(ev.status).toBe(200)
  const reader = ev.body!.getReader()
  const {value} = await reader.read()
  expect(new TextDecoder().decode(value)).toContain('event: state')
  controller.abort()
})
