// Browser side of the hub link: cookie-authenticated WebSocket with reconnect
// and resubscribe (mirrors apps/web/src/hub.ts and src/client/hub-client.ts).

const REQUEST_TIMEOUT_MS = 30_000
const PING_MS = 25_000
const SILENCE_MS = 75_000

export class HubClient {
  constructor() {
    this.socket = null
    this.status = { connected: false, gatewayConnected: false, orcaState: null }
    this.statusListeners = new Set()
    this.pending = new Map()
    this.subscriptions = new Map()
    this.nextId = 1
    this.closed = false
    this.attempt = 0
    this.pingTimer = null
    this.lastFrameAt = 0
  }

  connect() {
    if (!this.closed && this.socket) {
      // Already connecting or connected; a second open() would race the first.
      return
    }
    this.closed = false
    this.attempt = 0
    void this.open()
  }

  close() {
    this.closed = true
    this.stopPing()
    // Callers awaiting a request must not hang until their own timeout.
    this.rejectPending(new Error('Connection closed'))
    this.socket?.close(1000, 'closing')
    this.socket = null
  }

  onStatus(listener) {
    this.statusListeners.add(listener)
    listener(this.status)
    return () => this.statusListeners.delete(listener)
  }

  request(method, params, timeoutMs = REQUEST_TIMEOUT_MS) {
    const socket = this.socket
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      return Promise.reject(new Error('Not connected'))
    }
    const id = `r${this.nextId++}`
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id)
        reject(new Error(`${method} timed out`))
      }, timeoutMs)
      this.pending.set(id, { resolve, reject, timer })
      socket.send(JSON.stringify({ type: 'request', id, method, params, timeoutMs }))
    })
  }

  /** Returns an unsubscribe function. Survives socket reconnects. */
  subscribe(method, params, onEvent) {
    const subscription = { id: `s${this.nextId++}`, method, params, onEvent }
    this.subscriptions.set(subscription.id, subscription)
    this.sendSubscription(subscription)
    return () => {
      this.subscriptions.delete(subscription.id)
      if (this.socket?.readyState === WebSocket.OPEN) {
        this.socket.send(JSON.stringify({ type: 'unsubscribe', id: subscription.id }))
      }
    }
  }

  async open() {
    if (this.closed) return
    const scheme = location.protocol === 'https:' ? 'wss' : 'ws'
    let socket
    try {
      socket = new WebSocket(`${scheme}://${location.host}/client`)
    } catch {
      // A constructor throw (illegal URL on some mobile WebViews) must still retry.
      this.setStatus({ ...this.status, connected: false, gatewayConnected: false })
      setTimeout(() => void this.open(), backoffMs(this.attempt++))
      return
    }
    this.socket = socket
    // A socket that never said welcome was rejected at the upgrade; retrying
    // cannot fix that, only a fresh login can.
    let admitted = false

    socket.addEventListener('message', (event) => {
      if (this.socket !== socket) return
      this.lastFrameAt = Date.now()
      if (typeof event.data !== 'string') return
      let frame
      try {
        frame = JSON.parse(event.data)
      } catch {
        return
      }
      if (frame?.type === 'welcome') admitted = true
      this.handleFrame(frame)
    })

    socket.addEventListener('close', () => {
      if (this.socket !== socket) {
        // Superseded by a newer socket; its handlers own the state from here.
        return
      }
      this.socket = null
      this.stopPing()
      this.setStatus({ ...this.status, connected: false, gatewayConnected: false })
      this.rejectPending(new Error('Connection closed'))
      if (this.closed) return
      if (admitted) {
        setTimeout(() => void this.open(), backoffMs(this.attempt++))
        return
      }
      void this.reopenOrSignOut()
    })

    socket.addEventListener('open', () => {
      if (this.socket !== socket || this.closed) return
      this.attempt = 0
      this.lastFrameAt = Date.now()
      this.setStatus({ ...this.status, connected: true })
      this.startPing()
      // The hub forgot our streams with the old socket; re-register them.
      for (const subscription of this.subscriptions.values()) {
        this.sendSubscription(subscription)
        subscription.onEvent({ kind: 'reset' })
      }
    })
  }

  /** Distinguishes a dropped network from a cookie the server no longer honours. */
  async reopenOrSignOut() {
    let authenticated = true
    try {
      const response = await fetch('/api/session', { cache: 'no-store' })
      authenticated = response.ok ? Boolean((await response.json()).authenticated) : false
    } catch {
      // Server unreachable is not proof of a bad cookie.
    }
    if (this.socket) return
    if (!authenticated) {
      this.closed = true
      this.setStatus({
        connected: false,
        gatewayConnected: false,
        orcaState: null,
        authExpired: true
      })
      return
    }
    setTimeout(() => void this.open(), backoffMs(this.attempt++))
  }

  handleFrame(frame) {
    if (!frame || typeof frame !== 'object') return
    switch (frame.type) {
      case 'welcome':
      case 'gateway-state': {
        const gatewayConnected =
          frame.type === 'welcome' ? Boolean(frame.gatewayConnected) : Boolean(frame.connected)
        this.setStatus({
          connected: this.status.connected,
          gatewayConnected,
          orcaState: frame.orcaState ?? null,
          ...(typeof frame.message === 'string' ? { message: frame.message } : {})
        })
        return
      }
      case 'request-result': {
        const id = String(frame.id)
        const entry = this.pending.get(id)
        if (!entry) return
        this.pending.delete(id)
        clearTimeout(entry.timer)
        if (frame.ok) {
          entry.resolve(frame.result)
        } else {
          const error = frame.error ?? {}
          entry.reject(new Error(`${error.code ?? 'error'}: ${error.message ?? 'request failed'}`))
        }
        return
      }
      case 'stream-frame':
      case 'stream-error':
      case 'stream-reset': {
        const subscription = this.subscriptions.get(String(frame.id))
        if (!subscription) return
        if (frame.type === 'stream-frame') {
          subscription.onEvent({ kind: 'frame', result: frame.result })
        } else if (frame.type === 'stream-error') {
          const error = frame.error ?? {}
          subscription.onEvent({
            kind: 'error',
            code: error.code ?? 'error',
            message: error.message ?? 'stream failed'
          })
        } else {
          subscription.onEvent({ kind: 'reset' })
        }
        return
      }
      case 'pong':
        return
    }
  }

  sendSubscription(subscription) {
    if (this.socket?.readyState !== WebSocket.OPEN) return
    this.socket.send(
      JSON.stringify({
        type: 'subscribe',
        id: subscription.id,
        method: subscription.method,
        params: subscription.params
      })
    )
  }

  rejectPending(error) {
    for (const [id, entry] of this.pending) {
      clearTimeout(entry.timer)
      entry.reject(error)
      this.pending.delete(id)
    }
  }

  setStatus(status) {
    this.status = status
    for (const listener of [...this.statusListeners]) listener(status)
  }

  startPing() {
    this.stopPing()
    this.pingTimer = setInterval(() => {
      if (Date.now() - this.lastFrameAt > SILENCE_MS) {
        this.socket?.close(4004, 'silent')
        return
      }
      if (this.socket?.readyState === WebSocket.OPEN) {
        this.socket.send(JSON.stringify({ type: 'ping' }))
      }
    }, PING_MS)
  }

  stopPing() {
    if (this.pingTimer) {
      clearInterval(this.pingTimer)
      this.pingTimer = null
    }
  }
}

function backoffMs(attempt) {
  return Math.round(Math.min(15_000, 500 * 2 ** Math.min(attempt, 6)) * (0.8 + Math.random() * 0.4))
}
