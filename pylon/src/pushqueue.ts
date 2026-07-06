/**
 * Newest-wins async push queue backing the GraphQL subscriptions: pushes
 * buffer up to a small backlog, a slow consumer only ever misses
 * intermediate states (every payload here is a full snapshot document, so
 * dropped backlog self-heals). Lifted from the ts6-bridge pylon's
 * screencast (screen.ts).
 */
export class PushQueue<T> implements AsyncIterableIterator<T> {
  private buffer: T[] = []
  private wake: (() => void) | null = null
  private closed = false

  constructor(private onClose: () => void) {}

  push(value: T) {
    if (this.closed) return
    this.buffer.push(value)
    if (this.buffer.length > 3) this.buffer.splice(0, this.buffer.length - 3)
    this.wake?.()
  }

  async next(): Promise<IteratorResult<T>> {
    for (;;) {
      if (this.buffer.length > 0)
        return {done: false, value: this.buffer.shift() as T}
      if (this.closed) return {done: true, value: undefined as never}
      await new Promise<void>(resolve => (this.wake = resolve))
      this.wake = null
    }
  }

  async return(): Promise<IteratorResult<T>> {
    this.close()
    return {done: true, value: undefined as never}
  }

  close() {
    if (this.closed) return
    this.closed = true
    this.wake?.()
    this.onClose()
  }

  [Symbol.asyncIterator]() {
    return this
  }
}
