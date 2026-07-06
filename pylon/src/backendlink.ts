/**
 * BackendLink — the pylon's client of the `qtamp --backend` control
 * channel (../PROTOCOL.md): holds the /events SSE stream open forever
 * (reconnect with a fixed 1s retry — the backend is loopback), keeps a
 * merged snapshot cache, forwards commands, and fans out change pushes
 * to the GraphQL subscriptions.
 *
 * The pylon never spawns the backend: both run under the container's
 * supervisor, and `Query.player` is simply null while the backend is
 * down (the ts6 pylon's bridgeLink pattern).
 */
import {PushQueue} from './pushqueue'

export type Snapshot = {
  epoch?: string
  revision?: number
  serverNowMs?: number
  transport?: Record<string, unknown>
  track?: Record<string, unknown>
  playlist?: {
    revision?: number
    count?: number
    currentIndex?: number
    rows?: {text?: string; durationMs?: number}[]
  }
  eq?: Record<string, unknown>
}

const SECTION_EVENTS = ['transport', 'track', 'playlist', 'eq'] as const

export class BackendLink {
  readonly base: string
  snapshot: Snapshot | null = null
  connected = false

  private playerSinks = new Set<PushQueue<string>>()
  private playlistSinks = new Set<PushQueue<string>>()
  private started = false
  private stopped = false

  constructor(base?: string) {
    this.base =
      base ?? process.env.QTAMP_BACKEND_URL ?? 'http://127.0.0.1:18800'
  }

  start() {
    if (this.started) return
    this.started = true
    void this.loop()
    // Keepalive tick: keeps subscription connections warm through proxy
    // idle timeouts and doubles as a position beacon for pollers.
    setInterval(() => {
      if (this.connected) this.publishPlayer()
    }, 5000).unref?.()
  }

  /** Test hook: stop reconnecting (vitest teardown). */
  stop() {
    this.stopped = true
  }

  private async loop() {
    for (;;) {
      if (this.stopped) return
      try {
        await this.refetchState()
        await this.consumeEvents() // returns when the stream drops
      } catch {
        // backend down or unreachable; fall through to retry
      }
      this.setConnected(false)
      await new Promise(r => setTimeout(r, 1000))
    }
  }

  private setConnected(up: boolean) {
    if (this.connected === up) return
    this.connected = up
    // Connectivity edges are player-visible state: push them.
    this.publishPlayer()
  }

  private async refetchState() {
    const res = await fetch(`${this.base}/state`)
    if (!res.ok) throw new Error(`state: HTTP ${res.status}`)
    this.snapshot = (await res.json()) as Snapshot
    this.setConnected(true)
    this.publishPlayer()
    this.publishPlaylist()
  }

  private async consumeEvents() {
    const res = await fetch(`${this.base}/events`, {
      headers: {accept: 'text/event-stream'}
    })
    if (!res.ok || !res.body) throw new Error('events unavailable')
    this.setConnected(true)

    // Minimal incremental SSE parse (the C++ twin is src/ssereader.cpp;
    // this covers the same subset: event/data fields, comments, blank-line
    // dispatch, LF/CRLF).
    const reader = res.body.getReader()
    const decoder = new TextDecoder()
    let buf = ''
    let event = ''
    let data: string[] = []
    for (;;) {
      const {done, value} = await reader.read()
      if (done) return
      buf += decoder.decode(value, {stream: true})
      let nl: number
      while ((nl = buf.indexOf('\n')) >= 0) {
        let line = buf.slice(0, nl)
        buf = buf.slice(nl + 1)
        if (line.endsWith('\r')) line = line.slice(0, -1)
        if (line === '') {
          if (data.length) this.onEvent(event || 'message', data.join('\n'))
          event = ''
          data = []
          continue
        }
        if (line.startsWith(':')) continue
        const colon = line.indexOf(':')
        const field = colon < 0 ? line : line.slice(0, colon)
        let val = colon < 0 ? '' : line.slice(colon + 1)
        if (val.startsWith(' ')) val = val.slice(1)
        if (field === 'event') event = val
        else if (field === 'data') data.push(val)
      }
    }
  }

  private onEvent(name: string, dataStr: string) {
    let payload: any
    try {
      payload = JSON.parse(dataStr)
    } catch {
      return
    }
    if (name === 'ping') return
    if (name === 'state') {
      this.snapshot = payload as Snapshot
      this.publishPlayer()
      this.publishPlaylist()
      return
    }
    if (!(SECTION_EVENTS as readonly string[]).includes(name)) return
    if (!this.snapshot) {
      void this.refetchState().catch(() => {})
      return
    }
    // Same semantics as the C++ applyEvent: epoch change or a revision
    // gap means we missed something — re-snapshot; stale events drop.
    if (
      payload.epoch &&
      this.snapshot.epoch &&
      payload.epoch !== this.snapshot.epoch
    ) {
      void this.refetchState().catch(() => {})
      return
    }
    const rev = Number(payload.revision) || 0
    const have = Number(this.snapshot.revision) || 0
    if (rev && rev <= have) return
    const gap = rev && have && rev > have + 1
    if (payload[name] !== undefined) {
      this.snapshot = {
        ...this.snapshot,
        [name]: payload[name],
        revision: rev || this.snapshot.revision,
        serverNowMs: payload.serverNowMs ?? this.snapshot.serverNowMs
      }
    }
    if (gap) {
      void this.refetchState().catch(() => {})
      return
    }
    if (name === 'playlist') this.publishPlaylist()
    this.publishPlayer()
  }

  /**
   * Forward a command; on acceptance refresh the snapshot so mutations
   * can return the post-write state. Backend rejections (stale playlist
   * revision, gated path) surface as errors to the GraphQL caller.
   */
  async cmd(op: string, args: Record<string, unknown> = {}) {
    const res = await fetch(`${this.base}/cmd`, {
      method: 'POST',
      headers: {'content-type': 'application/json'},
      body: JSON.stringify({op, args})
    })
    const body = (await res.json().catch(() => ({ok: false}))) as {
      ok?: boolean
      error?: string
    }
    if (!res.ok || !body.ok) {
      throw new Error(body.error || `command ${op} failed`)
    }
    await this.refetchState().catch(() => {})
    return body
  }

  /**
   * The playerEvents document: the snapshot WITHOUT playlist rows (kept
   * small — rows travel on playlistEvents only when they change), plus
   * the link's own connectivity flag.
   */
  playerDoc(): string {
    const s = this.snapshot
    if (!s) return JSON.stringify({connected: false})
    const {playlist, ...rest} = s
    return JSON.stringify({
      connected: this.connected,
      ...rest,
      playlist: playlist
        ? {
            revision: playlist.revision,
            count: playlist.count ?? playlist.rows?.length ?? 0,
            currentIndex: playlist.currentIndex
          }
        : undefined
    })
  }

  playlistDoc(): string {
    const s = this.snapshot
    return JSON.stringify({
      connected: this.connected,
      epoch: s?.epoch,
      revision: s?.revision,
      playlist: s?.playlist ?? null
    })
  }

  subscribePlayer(): AsyncIterableIterator<string> {
    const q = new PushQueue<string>(() => this.playerSinks.delete(q))
    this.playerSinks.add(q)
    q.push(this.playerDoc()) // current state immediately on subscribe
    return q
  }

  subscribePlaylist(): AsyncIterableIterator<string> {
    const q = new PushQueue<string>(() => this.playlistSinks.delete(q))
    this.playlistSinks.add(q)
    q.push(this.playlistDoc())
    return q
  }

  private publishPlayer() {
    if (this.playerSinks.size === 0) return
    const doc = this.playerDoc()
    for (const q of this.playerSinks) q.push(doc)
  }

  private publishPlaylist() {
    if (this.playlistSinks.size === 0) return
    const doc = this.playlistDoc()
    for (const q of this.playlistSinks) q.push(doc)
  }
}

export const backendLink = new BackendLink()
