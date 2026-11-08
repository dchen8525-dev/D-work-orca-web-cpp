// Entry point: session check, login, the hub socket, and route switching
// (mirrors App.tsx). No build step, no framework.

import { api } from './api.js'
import { HubClient } from './hub.js'
import { loginView } from './views/auth.js'
import { automationsView } from './views/automations.js'
import { boardView } from './views/board.js'
import { taskView } from './views/task.js'

const root = document.getElementById('root')

let hub = null
let current = null // { view, destroy }
let route = routeFromPath(location.pathname)
let status = { connected: false, gatewayConnected: false, orcaState: null }

function pathFor(next) {
  if (next.kind === 'task') return `/task/${encodeURIComponent(next.sessionId)}`
  return next.kind === 'automations' ? '/automations' : '/'
}

function routeFromPath(path) {
  const match = /^\/task\/([^/]+)/.exec(path)
  if (match) {
    return { kind: 'task', sessionId: decodeURIComponent(match[1]), title: '任务' }
  }
  return path === '/automations' ? { kind: 'automations' } : { kind: 'board' }
}

function teardown() {
  if (!current) return
  current.view.destroy()
  current = null
}

function mount(view) {
  teardown()
  current = { view }
  root.replaceChildren(view.element)
  view.setStatus(status)
}

function renderRoute() {
  if (!hub) return
  document.title = 'orca 工作台'
  if (route.kind === 'task') {
    mount(
      taskView({
        hub,
        sessionId: route.sessionId,
        title: route.title,
        agent: route.agent,
        onBack: goBack
      })
    )
    return
  }
  if (route.kind === 'automations') {
    mount(
      automationsView({
        hub,
        onBack: goBack,
        onOpenTask: (sessionId, title) => navigate({ kind: 'task', sessionId, title })
      })
    )
    return
  }
  mount(
    boardView({
      hub,
      onOpenTask: (task) => navigate({ kind: 'task', ...task }),
      onOpenAutomations: () => navigate({ kind: 'automations' }),
      onSignOut: () => void signOut()
    })
  )
}

function navigate(next) {
  history.pushState({ orcaWeb: true }, '', pathFor(next))
  route = next
  renderRoute()
}

// history.back() leaves the site if this page was opened directly on a deep link,
// so plant a board entry first in that case.
function goBack() {
  if (history.state?.orcaWeb) {
    history.back()
    return
  }
  route = { kind: 'board' }
  history.replaceState({ orcaWeb: true }, '', pathFor(route))
  renderRoute()
}

function handleStatus(next) {
  status = next
  if (next.authExpired) {
    // The cookie is gone; keeping the socket closed is the only sensible state.
    teardown()
    hub?.close()
    hub = null
    showLogin()
    return
  }
  current?.view.setStatus(next)
}

async function signOut() {
  teardown()
  hub?.close()
  hub = null
  await api.logout()
  showLogin()
}

function showLogin() {
  teardown()
  document.title = 'orca 工作台'
  root.replaceChildren(loginView({ onSuccess: () => void startMain() }))
}

function startMain() {
  teardown()
  route = routeFromPath(location.pathname)
  status = { connected: false, gatewayConnected: false, orcaState: null }
  hub = new HubClient()
  hub.onStatus(handleStatus)
  hub.connect()
  renderRoute()
}

window.addEventListener('popstate', () => {
  if (!hub) return
  route = routeFromPath(location.pathname)
  renderRoute()
})

async function boot() {
  const session = await api.session()
  if (session.authenticated) startMain()
  else showLogin()
}

void boot()
