import {type Plugin, type PylonConfig} from '@getcronit/pylon'
import {serve} from '@hono/node-server'

/**
 * The app owns serving: a 'last'-strategy plugin starts listening after
 * the GraphQL handler and every route is mounted (the ts6-bridge pylon's
 * pattern). PORT env, default 8789 (the bot container's qtamp-pylon).
 */
const servePylon = (): Plugin => ({
  name: 'serve',
  strategy: 'last',
  setup: app => {
    const raw = process.env.PORT
    const envPort = raw && raw.trim() !== '' ? Number(raw) : NaN
    const port = Number.isFinite(envPort) ? envPort : 8789
    serve({fetch: app.fetch, port}, info =>
      console.log(`qtamp pylon running at http://localhost:${info.port}`)
    )
  }
})

/**
 * Control-channel passthrough: qtamp heads (native --connect, the WASM
 * frontend) speak the PROTOCOL.md routes directly, so the pylon exposes
 * them 1:1 against the backend. GraphQL stays the typed facade for web
 * tooling; the passthrough is the head transport. Streaming responses
 * (the SSE /events) pass through as-is — the fetch Response IS the Hono
 * response.
 */
const backendProxy = (): Plugin => ({
  name: 'backend-proxy',
  setup: app => {
    const base = process.env.QTAMP_BACKEND_URL ?? 'http://127.0.0.1:18800'
    app.get('/state', () => fetch(`${base}/state`))
    app.get('/events', () =>
      fetch(`${base}/events`, {headers: {accept: 'text/event-stream'}})
    )
    app.get('/art/current', c => {
      const inm = c.req.header('if-none-match')
      return fetch(`${base}/art/current`, {
        headers: inm ? {'if-none-match': inm} : {}
      })
    })
    app.post('/cmd', async c => {
      const body = await c.req.text()
      if (body.length > 64 * 1024) return c.text('payload too large', 413)
      return fetch(`${base}/cmd`, {
        method: 'POST',
        headers: {'content-type': 'application/json'},
        body
      })
    })
  }
})

/**
 * /graphql CORS: Yoga's default reflects any Origin. Same policy as the
 * ts6 pylon: only allowlisted origins (env QTAMP_CORS_ORIGINS) plus local
 * dev hosts get credentialed CORS; everything else has the headers
 * stripped. Non-browser callers (no Origin) pass untouched.
 */
const ALLOWED_ORIGINS = new Set(
  (process.env.QTAMP_CORS_ORIGINS || '')
    .split(',')
    .map(s => s.trim())
    .filter(Boolean)
)
const isDevOrigin = (o: string) =>
  /^https?:\/\/(localhost|127\.0\.0\.1)(:\d+)?$/.test(o)

const corsGuard = (): Plugin => ({
  name: 'cors-guard',
  setup: app => {
    app.use('/graphql', async (c, next) => {
      const origin = c.req.header('origin')
      const allowed =
        !!origin && (ALLOWED_ORIGINS.has(origin) || isDevOrigin(origin))
      if (c.req.method === 'OPTIONS') {
        if (!allowed) return c.body(null, 204, {vary: 'Origin'})
        return c.body(null, 204, {
          'access-control-allow-origin': origin,
          'access-control-allow-credentials': 'true',
          'access-control-allow-methods': 'GET, POST, OPTIONS',
          'access-control-allow-headers':
            c.req.header('access-control-request-headers') || 'content-type',
          'access-control-max-age': '86400',
          vary: 'Origin'
        })
      }
      await next()
      if (!origin) return
      c.res.headers.delete('access-control-allow-origin')
      c.res.headers.delete('access-control-allow-credentials')
      if (allowed) {
        c.res.headers.set('access-control-allow-origin', origin)
        c.res.headers.set('access-control-allow-credentials', 'true')
        if (!/\bOrigin\b/i.test(c.res.headers.get('vary') || '')) {
          c.res.headers.append('vary', 'Origin')
        }
      }
    })
  }
})

export default {
  graphiql: process.env.NODE_ENV !== 'production',
  landingPage: false,
  plugins: [backendProxy(), corsGuard(), servePylon()]
} satisfies PylonConfig
