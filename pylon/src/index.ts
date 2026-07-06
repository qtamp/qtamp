import {type Int, Pylon} from '@getcronit/pylon'

import {backendLink, type Snapshot} from './backendlink'

/**
 * qtamp-pylon — the GraphQL facade over a `qtamp --backend` control
 * channel (see ../PROTOCOL.md and ../docs/OKF-remote.md).
 *
 * `Query.player` mirrors the backend snapshot as typed objects and is
 * null while the backend is down; the flat mutations forward commands
 * and return the refreshed player; the subscriptions push JSON-string
 * documents (object-typed subscription fields do not stream through the
 * resolver wrapper — the ts6 pylon's documented constraint).
 *
 * Native/WASM qtamp heads do NOT need this schema: they speak the
 * control channel directly, which pylon.config.ts passes through under
 * the same paths. This schema serves web tooling, the music-bot driver,
 * and anything that wants typed access.
 */

class Transport {
  playing: boolean
  paused: boolean
  positionMs: number
  positionAtMs: number
  durationMs: number
  volume: Int
  pan: number

  constructor(t: Record<string, unknown>) {
    this.playing = Boolean(t.playing)
    this.paused = Boolean(t.paused)
    this.positionMs = Number(t.positionMs) || 0
    this.positionAtMs = Number(t.positionAtMs) || 0
    this.durationMs = Number(t.durationMs) || 0
    this.volume = (Number(t.volume) || 0) as Int
    this.pan = typeof t.pan === 'number' ? t.pan : 0.5
  }
}

class Track {
  title: string
  artist: string
  album: string
  filename: string
  displayTitle: string
  decoder: string
  bitrate: Int
  sampleRate: Int
  channels: Int

  constructor(t: Record<string, unknown>) {
    this.title = String(t.title ?? '')
    this.artist = String(t.artist ?? '')
    this.album = String(t.album ?? '')
    this.filename = String(t.filename ?? '')
    this.displayTitle = String(t.displayTitle ?? '')
    this.decoder = String(t.decoder ?? '')
    this.bitrate = (Number(t.bitrate) || 0) as Int
    this.sampleRate = (Number(t.sampleRate) || 0) as Int
    this.channels = (Number(t.channels) || 0) as Int
  }
}

class PlaylistRow {
  row: Int
  text: string
  durationMs: number

  constructor(row: number, r: {text?: string; durationMs?: number}) {
    this.row = row as Int
    this.text = String(r.text ?? '')
    this.durationMs = Number(r.durationMs) || 0
  }
}

class Playlist {
  revision: Int
  currentIndex: Int
  rowCount: Int

  constructor(private p: NonNullable<Snapshot['playlist']>) {
    this.revision = (Number(p.revision) || 0) as Int
    this.currentIndex = (Number(p.currentIndex ?? -1) ?? -1) as Int
    this.rowCount = (p.rows?.length ?? Number(p.count) ?? 0) as Int
  }

  /** A row window; the full list can be large, page it. */
  rows(offset: Int = 0 as Int, limit: Int = 500 as Int): PlaylistRow[] {
    const rows = this.p.rows ?? []
    const out: PlaylistRow[] = []
    const end = Math.min(rows.length, Number(offset) + Number(limit))
    for (let i = Math.max(0, Number(offset)); i < end; i++)
      out.push(new PlaylistRow(i, rows[i]))
    return out
  }
}

class Eq {
  on: boolean
  auto: boolean
  preamp: Int
  bands: Int[]

  constructor(e: Record<string, unknown>) {
    this.on = Boolean(e.on)
    this.auto = Boolean(e.auto)
    this.preamp = (Number(e.preamp) || 31) as Int
    this.bands = (Array.isArray(e.bands) ? e.bands : []).map(
      b => (Number(b) || 31) as Int
    )
  }
}

class Player {
  epoch: string
  revision: number
  serverNowMs: number

  constructor(private s: Snapshot) {
    this.epoch = String(s.epoch ?? '')
    this.revision = Number(s.revision) || 0
    this.serverNowMs = Number(s.serverNowMs) || 0
  }

  transport(): Transport {
    return new Transport(this.s.transport ?? {})
  }
  track(): Track | null {
    return this.s.track ? new Track(this.s.track) : null
  }
  playlist(): Playlist {
    return new Playlist(this.s.playlist ?? {rows: []})
  }
  eq(): Eq {
    return new Eq(this.s.eq ?? {})
  }
}

backendLink.start()

const currentPlayer = (): Player | null =>
  backendLink.connected && backendLink.snapshot
    ? new Player(backendLink.snapshot)
    : null

/** Forward a command and answer with the refreshed player state. */
async function run(op: string, args: Record<string, unknown> = {}) {
  await backendLink.cmd(op, args)
  return currentPlayer()
}

export default new Pylon({
  graphql: {
    Query: {
      /**
       * The shared player, mirrored from the backend snapshot. Null while
       * the backend is unreachable, so consumers can render an offline
       * state instead of failing.
       */
      player: currentPlayer
    },
    Mutation: {
      /** Start playback (a cold player plays the playlist's current row). */
      play: () => run('play'),
      /** Pause playback. */
      pause: () => run('pause'),
      /** Stop playback and rewind. */
      stop: () => run('stop'),
      /** Jump to the next playlist row. */
      next: () => run('next'),
      /** Jump to the previous playlist row. */
      prev: () => run('prev'),
      /** Seek to an absolute position in the current track. */
      seek: (ms: Int) => run('seek', {ms: Number(ms)}),
      /** Set the volume, 0..100. */
      setVolume: (v: Int) => run('setVolume', {v: Number(v)}),
      /** Set the balance on the 0..1 axis (0.5 = centre). */
      setPan: (v: number) => run('setPan', {v}),
      /** Toggle the equalizer. */
      setEqOn: (on: boolean) => run('setEqOn', {on}),
      /** Set one EQ band (0..9) on the Winamp 0..63 slider scale. */
      setEqBand: (band: Int, value: Int) =>
        run('setEqBand', {band: Number(band), value: Number(value)}),
      /**
       * Play a playlist row. `expectPlaylistRevision` guards against
       * acting on a stale row mapping; omit to force.
       */
      playRow: (row: Int, expectPlaylistRevision?: Int | null) =>
        run('playlistPlayRow', {
          row: Number(row),
          ...(expectPlaylistRevision != null
            ? {expectPlaylistRevision: Number(expectPlaylistRevision)}
            : {})
        }),
      /** Select a playlist row without playing it. */
      setCurrentRow: (row: Int, expectPlaylistRevision?: Int | null) =>
        run('playlistSetCurrentRow', {
          row: Number(row),
          ...(expectPlaylistRevision != null
            ? {expectPlaylistRevision: Number(expectPlaylistRevision)}
            : {})
        }),
      /**
       * Enqueue files by absolute path. The backend confines paths to its
       * music root — anything outside is rejected there.
       */
      playlistAdd: (paths: string[]) => run('playlistAddPaths', {paths}),
      /** Remove explicit playlist rows. */
      playlistRemove: (rows: Int[]) =>
        run('playlistRemoveRows', {rows: rows.map(Number)}),
      /** Clear the playlist. */
      playlistClear: () => run('playlistClear')
    },
    Subscription: {
      /**
       * Player state pushes as JSON documents WITHOUT playlist rows (those
       * travel on `playlistEvents`): `{connected, epoch, revision,
       * serverNowMs, transport, track, eq, playlist:{revision, count,
       * currentIndex}}`. First frame = current state; a 5s keepalive tick
       * repeats it while connected.
       */
      playerEvents: (): string =>
        backendLink.subscribePlayer() as unknown as string,
      /**
       * Full playlist documents `{connected, epoch, revision, playlist:
       * {revision, currentIndex, rows[...]}}`, pushed only when the
       * playlist changes (plus once on subscribe).
       */
      playlistEvents: (): string =>
        backendLink.subscribePlaylist() as unknown as string
    }
  }
})
