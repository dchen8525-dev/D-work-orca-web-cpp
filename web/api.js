// Cookie-authenticated JSON endpoints. Every call is same-origin; the session
// cookie is HttpOnly, so the browser attaches it and this module never sees it.

async function post(path, body) {
  return await fetch(path, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body)
  })
}

export const api = {
  async session() {
    try {
      const response = await fetch('/api/session', { cache: 'no-store' })
      if (!response.ok) return { authenticated: false }
      return await response.json()
    } catch {
      return { authenticated: false }
    }
  },

  async login(password) {
    const response = await post('/api/login', { password })
    return response.ok
  },

  async logout() {
    try {
      await post('/api/logout', {})
    } catch {
      // Signing out must work offline too: the local view is dropped regardless.
    }
  },

  /** Rotates the login password; every other session dies, this one is re-issued. */
  async changePassword(current, next) {
    const response = await post('/api/password', { current, new: next })
    const payload = await response.json().catch(() => ({}))
    return { ok: response.ok && payload.ok !== false, error: payload.error }
  }
}
