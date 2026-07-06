// BackendLink vs the mock backend: snapshot on connect, event-driven
// cache updates and fan-out, command forwarding with post-write refetch,
// epoch-change resync, and rejection surfacing.
import {afterEach, expect, test} from 'vitest'

import {BackendLink} from './backendlink'
import {startMockBackend, type MockBackend} from './mock-backend'

let mock: MockBackend | null = null
let link: BackendLink | null = null

afterEach(async () => {
  link?.stop()
  link = null
  await mock?.close()
  mock = null
})

async function until(cond: () => boolean, ms = 3000) {
  const t0 = Date.now()
  while (!cond()) {
    if (Date.now() - t0 > ms) throw new Error('condition never became true')
    await new Promise(r => setTimeout(r, 20))
  }
}

async function connectedLink() {
  mock = await startMockBackend()
  link = new BackendLink(`http://127.0.0.1:${mock.port}`)
  link.start()
  await until(() => link!.connected && link!.snapshot != null)
  return {mock, link: link!}
}

test('connects and mirrors the snapshot', async () => {
  const {link} = await connectedLink()
  expect(link.snapshot?.epoch).toBe('mock-epoch-1')
  expect(link.snapshot?.transport).toBeTruthy()
  const doc = JSON.parse(link.playerDoc())
  expect(doc.connected).toBe(true)
  expect(doc.playlist.count).toBe(0)
})

test('events update the cache and fan out to subscribers', async () => {
  const {mock, link} = await connectedLink()

  const sub = link.subscribePlayer()
  const first = await sub.next() // immediate current-state frame
  expect(JSON.parse(first.value as string).connected).toBe(true)

  mock.state.transport.playing = true
  mock.bumpAndPush('transport')

  // The pushed transport event lands in the cache and on the sink.
  let doc: any
  const t0 = Date.now()
  for (;;) {
    const frame = await sub.next()
    doc = JSON.parse(frame.value as string)
    if (doc.transport?.playing === true) break
    if (Date.now() - t0 > 3000) throw new Error('never saw playing=true')
  }
  expect(link.snapshot?.transport?.playing).toBe(true)
  await sub.return?.()
})

test('playlist events publish full rows on playlistEvents', async () => {
  const {mock, link} = await connectedLink()
  const sub = link.subscribePlaylist()
  await sub.next() // initial

  mock.state.playlist.rows.push({text: 'Song A', durationMs: 1000})
  mock.state.playlist.count = 1
  mock.bumpAndPush('playlist')

  const t0 = Date.now()
  for (;;) {
    const frame = await sub.next()
    const doc = JSON.parse(frame.value as string)
    if (doc.playlist?.rows?.length === 1) {
      expect(doc.playlist.rows[0].text).toBe('Song A')
      break
    }
    if (Date.now() - t0 > 3000) throw new Error('never saw the new row')
  }
  await sub.return?.()
})

test('cmd forwards, refetches, and surfaces rejections', async () => {
  const {mock, link} = await connectedLink()

  await link.cmd('setVolume', {v: 40})
  expect(mock.cmdLog.at(-1)).toEqual({op: 'setVolume', args: {v: 40}})
  await until(() => Number(link.snapshot?.transport?.volume) === 40)

  await expect(link.cmd('boom')).rejects.toThrow('boom')
})

test('an epoch change forces a full resync', async () => {
  const {mock, link} = await connectedLink()

  // Simulate a backend restart: new epoch, restarted counters.
  mock.state.epoch = 'mock-epoch-2'
  mock.state.revision = 1
  mock.state.transport.volume = 77
  mock.pushEvent('transport', {
    epoch: 'mock-epoch-2',
    revision: 1,
    transport: mock.state.transport
  })

  await until(() => link.snapshot?.epoch === 'mock-epoch-2')
  expect(Number(link.snapshot?.transport?.volume)).toBe(77)
})
