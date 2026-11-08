// The board: projects → worktrees → tasks, with the live status stream layered
// over worktree.ps (mirrors Board.tsx).

import { el, render } from '../dom.js'
import { changePasswordSheet } from './auth.js'
import { createTaskSheet } from './create.js'
import {
  connectionLabel,
  connectionTone,
  firstLine,
  markForState,
  orcaNote,
  relativeTime,
  safeGet,
  safeRemove,
  safeSet,
  sessionIdFromPaneKey,
  stateLabel
} from '../util.js'

const FILTERS = [
  { key: 'all', label: '全部' },
  { key: 'working', label: '进行中' },
  { key: 'attention', label: '待确认' }
]

export function boardView({ hub, onOpenTask, onOpenAutomations, onSignOut }) {
  const state = {
    status: { connected: false, gatewayConnected: false, orcaState: null },
    projects: [],
    worktrees: [],
    projections: new Map(),
    error: null,
    initializing: true,
    filter: 'all',
    query: '',
    folded: new Set(),
    refreshing: false,
    notifyOn: safeGet('orca-notify') === '1',
    creatingIn: null,
    changingPassword: false
  }
  let destroyed = false
  let seenAttention = null
  let lastSignature = null
  // Status frames repaint the board often. Reusing the button nodes keeps
  // pointerdown/pointerup on the same element, so a tap cannot be swallowed
  // by a repaint that lands in between.
  const taskButtons = new Map()
  const projectHeads = new Map()
  const newButtons = new Map()

  // --- skeleton ---------------------------------------------------------
  const dot = el('span', { className: 'dot dot-red' })
  const kickerText = el('span', {}, 'orca · 正在连接服务器…')
  const today = el('span', { className: 'right' }, new Date().toLocaleDateString('zh-CN', { month: 'long', day: 'numeric' }))
  const dek = el('p', { className: 'dek' }, '0 个任务正在跑，0 个等你回话。')
  const orcaLine = el('p', { className: 'orca-note' })
  orcaLine.hidden = true

  const notifyButton = el('button', {}, state.notifyOn ? '提醒 ✓' : '提醒')
  notifyButton.setAttribute('aria-pressed', String(state.notifyOn))
  notifyButton.addEventListener('click', () => void toggleNotify())

  const search = el('input', {
    className: 'board-search',
    type: 'search',
    name: 'search',
    inputMode: 'search',
    placeholder: '搜标题、分支或项目名'
  })
  search.addEventListener('input', () => {
    state.query = search.value
    paintBoard()
  })
  const filterNav = el('nav', { className: 'filters' }, search)

  const boardMain = el('main', { className: 'board' })
  const root = el(
    'div',
    { className: 'app' },
    el(
      'header',
      { className: 'masthead' },
      el('div', { className: 'kicker' }, dot, kickerText, today),
      el(
        'div',
        { className: 'title-row' },
        el('h1', {}, '进行中的', el('em', {}, '工作')),
        el(
          'div',
          { className: 'mast-actions' },
          notifyButton,
          el('button', { onclick: onOpenAutomations }, '定时'),
          el('button', { onclick: openPassword }, '密码'),
          el('button', { onclick: () => void onSignOut() }, '退出')
        )
      ),
      dek,
      orcaLine
    ),
    el('div', { className: 'rule double mast-rule' }),
    filterNav,
    boardMain
  )

  for (const entry of FILTERS) {
    const button = el('button', { dataset: { filter: entry.key } }, entry.label, el('b', {}, '0'))
    button.setAttribute('aria-pressed', String(entry.key === state.filter))
    button.addEventListener('click', () => {
      state.filter = entry.key
      paintFilters()
      paintBoard()
    })
    filterNav.appendChild(button)
  }

  // --- data -------------------------------------------------------------
  const load = async (quiet = false) => {
    try {
      const [projectList, ps] = await Promise.all([
        hub.request('project.list', {}),
        hub.request('worktree.ps', {})
      ])
      if (destroyed) return
      state.projects = projectList.projects ?? []
      state.worktrees = ps.worktrees ?? []
      state.error = null
    } catch (failure) {
      if (!quiet && !destroyed) state.error = failure.message
    } finally {
      if (!destroyed) {
        state.initializing = false
        paintBoard()
      }
    }
  }

  const unsubscribe = hub.subscribe('agentSession.subscribeStatus', {}, (event) => {
    if (event.kind === 'reset' || event.kind === 'error') {
      // A reset means "refetch", not "nothing is running": keep the last known
      // statuses on screen until the fresh snapshot replaces them.
      void load(true)
      return
    }
    const frame = event.result
    if (!frame || frame.type === 'end') return
    if (frame.type === 'snapshot') {
      // An empty opening snapshot means "not seeded yet", not "no tasks".
      if (!frame.sessions?.length) return
      state.projections = new Map(frame.sessions.map((session) => [session.sessionId, session]))
    } else if (frame.type === 'status') {
      state.projections = new Map(state.projections)
      state.projections.set(frame.session.sessionId, frame.session)
    } else {
      return
    }
    paintBoard()
  })

  const tickTimer = setInterval(() => paintBoard(), 30_000)
  const pollTimer = setInterval(() => {
    if (state.status.gatewayConnected) void load(true)
  }, 15_000)

  // --- rendering --------------------------------------------------------
  function groups() {
    const projectByRepo = new Map()
    for (const project of state.projects) {
      for (const repoId of project.sourceRepoIds ?? []) projectByRepo.set(repoId, project.displayName)
    }
    const grouped = new Map()
    for (const worktree of state.worktrees) {
      const name = projectByRepo.get(worktree.repoId) ?? '其他'
      const branch = worktree.branch?.replace('refs/heads/', '') ?? ''
      const label = worktree.displayName ?? branch ?? worktree.path
      const tasks = []
      for (const agent of worktree.agents ?? []) {
        const sessionId = sessionIdFromPaneKey(agent.paneKey)
        if (!sessionId) continue
        const projection = state.projections.get(sessionId)
        const status = projection?.status ?? (agent.state === 'working' ? 'working' : 'idle')
        const title = projection?.conversationName ?? agent.conversationName ?? agent.prompt ?? '未命名任务'
        const detail = projection?.toolName
          ? `正在 ${projection.toolName}`
          : firstLine(agent.lastAssistantMessage ?? agent.prompt ?? '')
        tasks.push({
          key: agent.paneKey,
          sessionId,
          title,
          detail,
          state: status,
          updatedAt: projection?.updatedAt ?? agent.updatedAt,
          agent: agent.agentType,
          target: { sessionId, title, agent: agent.agentType }
        })
      }
      const rows = grouped.get(name) ?? []
      rows.push({ worktree, label, branch, tasks })
      grouped.set(name, rows)
    }
    return [...grouped].map(([name, rows]) => ({ name, worktrees: rows }))
  }

  function paintFilters() {
    for (const button of filterNav.querySelectorAll('button[data-filter]')) {
      const active = button.dataset.filter === state.filter
      button.className = active ? 'on' : ''
      button.setAttribute('aria-pressed', String(active))
    }
  }

  function paintHeader(allGroups) {
    const all = allGroups.flatMap((group) => group.worktrees.flatMap((row) => row.tasks))
    const working = all.filter((task) => task.state === 'working').length
    const attention = all.filter((task) => task.state === 'attention').length

    dot.className = `dot dot-${connectionTone(state.status)}`
    kickerText.textContent = `orca · ${connectionLabel(state.status)}`
    dek.textContent = `${working} 个任务正在跑，${attention} 个等你回话。`
    const note = state.status.gatewayConnected ? orcaNote(state.status.orcaState) : null
    orcaLine.hidden = !note
    orcaLine.textContent = note ? `家里 Orca ${note}，下面是最后已知状态` : ''

    for (const button of filterNav.querySelectorAll('button[data-filter]')) {
      const key = button.dataset.filter
      button.querySelector('b').textContent = String(
        key === 'all' ? all.length : all.filter((task) => task.state === key).length
      )
    }
  }

  function paintBoard() {
    if (destroyed) return
    const allGroups = groups()
    paintHeader(allGroups)
    paintFilters()

    // Project numbers stay tied to the unfiltered order, so they do not shuffle per filter.
    const indexed = allGroups.map((group, index) => ({ ...group, index }))
    const query = state.query.trim().toLowerCase()
    let visible
    if (query) {
      visible = indexed
        .map((group) => {
          if (group.name.toLowerCase().includes(query)) return group
          return {
            ...group,
            worktrees: group.worktrees
              .map((row) => ({
                ...row,
                tasks:
                  row.branch.toLowerCase().includes(query) || row.label.toLowerCase().includes(query)
                    ? row.tasks
                    : row.tasks.filter(
                        (task) =>
                          task.title.toLowerCase().includes(query) ||
                          task.detail.toLowerCase().includes(query)
                      )
              }))
              .filter((row) => row.tasks.length > 0)
          }
        })
        .filter((group) => group.worktrees.length > 0)
    } else if (state.filter === 'all') {
      visible = indexed
    } else {
      visible = indexed
        .map((group) => ({
          ...group,
          worktrees: group.worktrees
            .map((row) => ({ ...row, tasks: row.tasks.filter((task) => task.state === state.filter) }))
            .filter((row) => row.tasks.length > 0)
        }))
        .filter((group) => group.worktrees.length > 0)
    }

    // Status frames arrive on every tool event. Replacing the whole board that
    // often makes buttons un-clickable (pointerdown and pointerup land on two
    // different nodes), so repaint only when something visible actually changed.
    const signature = JSON.stringify({
      status: [
        state.status.connected,
        state.status.gatewayConnected,
        state.status.orcaState,
        state.status.message ?? null
      ],
      filter: state.filter,
      query: state.query,
      error: state.error,
      initializing: state.initializing,
      worktrees: state.worktrees.length,
      projects: state.projects.length,
      visible: visible.map((group) => [
        group.index,
        group.name,
        group.worktrees.map((row) => [
          row.label,
          row.branch,
          row.tasks.map((task) => [
            task.key,
            task.title,
            task.detail,
            task.state,
            task.agent,
            task.updatedAt ? relativeTime(task.updatedAt) : ''
          ])
        ])
      ])
    })
    if (signature === lastSignature) return
    lastSignature = signature

    const nodes = []
    if (!state.status.gatewayConnected) {
      nodes.push(el('p', { className: 'empty' }, '家里电脑的 gateway 未连接。Orca 是否还开着？'))
    }
    if (state.error) nodes.push(el('p', { className: 'error' }, state.error))
    if (state.initializing && state.worktrees.length === 0) {
      nodes.push(el('p', { className: 'empty' }, '载入中…'))
    }
    for (const group of visible) nodes.push(projectSection(group))
    if (state.status.gatewayConnected && state.worktrees.length > 0 && visible.length === 0) {
      nodes.push(
        el('p', { className: 'empty' },
          state.query.trim() ? `没有匹配「${state.query.trim()}」的任务。` : '这一栏暂时是空的。')
      )
    }
    if (!state.initializing && state.status.gatewayConnected && state.worktrees.length === 0) {
      nodes.push(el('p', { className: 'empty' }, '没有工作区'))
    }
    render(boardMain, nodes)
    maybeNotify()
  }

  function projectSection(group) {
    const folded = state.folded.has(group.name)
    const head = projectHead(group)
    const section = el('section', { className: 'project' }, head)
    if (folded) {
      const tasks = group.worktrees.flatMap((row) => row.tasks)
      const working = tasks.filter((task) => task.state === 'working').length
      const attention = tasks.filter((task) => task.state === 'attention').length
      const idle = tasks.length - working - attention
      const latest = tasks.reduce((max, task) => Math.max(max, task.updatedAt ?? 0), 0)
      const summary = el('p', { className: 'project-summary' })
      if (tasks.length === 0) {
        summary.appendChild(document.createTextNode('还没有任务'))
      } else {
        if (working > 0) summary.appendChild(summaryItem('sdot-green', `${working} 进行中`))
        if (attention > 0) summary.appendChild(summaryItem('sdot-amber', `${attention} 待确认`))
        if (idle > 0) summary.appendChild(summaryItem('sdot-grey', `${idle} 空闲`))
      }
      if (latest) summary.appendChild(el('span', { className: 's-latest' }, relativeTime(latest)))
      section.appendChild(summary)
      return section
    }

    for (const row of group.worktrees) {
      const newButton = newButtonFor(row.worktree)
      const line = el(
        'div',
        { className: 'worktree-line' },
        el('span', {}, '⑂'),
        el('span', { className: 'branch' }, row.branch || row.label),
        newButton
      )
      const wrapper = el('div', {}, line)
      if (row.tasks.length === 0) wrapper.appendChild(el('p', { className: 'worktree-no-tasks' }, '这里还没有任务。'))
      for (const task of row.tasks) wrapper.appendChild(taskButton(task))
      section.appendChild(wrapper)
    }
    return section
  }

  function projectHead(group) {
    let entry = projectHeads.get(group.name)
    if (!entry) {
      const no = el('span', { className: 'no' })
      const name = el('h2')
      const count = el('span', { className: 'count' })
      const twist = el('span', { className: 'twist', 'aria-hidden': 'true' }, '▾')
      const head = el('div', { className: 'project-head', role: 'button', tabIndex: 0 }, no, name, count, twist)
      const toggle = () => {
        if (state.folded.has(group.name)) state.folded.delete(group.name)
        else state.folded.add(group.name)
        paintBoard()
      }
      head.addEventListener('click', toggle)
      head.addEventListener('keydown', (event) => {
        if (event.key === 'Enter' || event.key === ' ') {
          event.preventDefault()
          toggle()
        }
      })
      entry = { head, no, name, count, twist }
      projectHeads.set(group.name, entry)
    }
    const folded = state.folded.has(group.name)
    entry.no.textContent = String(group.index + 1).padStart(2, '0')
    entry.name.textContent = group.name
    entry.count.textContent = `${group.worktrees.reduce((sum, row) => sum + row.tasks.length, 0)} 条`
    entry.head.className = folded ? 'project-head folded' : 'project-head'
    entry.head.setAttribute('aria-expanded', String(!folded))
    return entry.head
  }

  function newButtonFor(worktree) {
    let entry = newButtons.get(worktree.worktreeId)
    if (!entry) {
      const button = el('button', { className: 'new' }, '新建')
      button.addEventListener('click', () => openCreate(entry.worktree))
      entry = { button, worktree }
      newButtons.set(worktree.worktreeId, entry)
    }
    entry.worktree = worktree
    return entry.button
  }

  function taskButton(task) {
    let entry = taskButtons.get(task.key)
    if (!entry) {
      const button = el('button')
      button.addEventListener('click', () => onOpenTask(entry.task.target))
      entry = { button, task }
      taskButtons.set(task.key, entry)
    }
    entry.task = task
    const button = entry.button
    button.className = `task task-${task.state}`
    render(
      button,
      el(
        'span',
        { className: 'task-head' },
        el('span', { className: 'task-mark' }, markForState(task.state)),
        el('span', { className: 'task-body' }, el('span', { className: 'task-title' }, task.title))
      ),
      task.detail ? el('span', { className: 'task-detail' }, task.detail) : null,
      el(
        'span',
        { className: 'task-foot' },
        task.state === 'working' ? el('i', { className: 'spin', 'aria-hidden': 'true' }) : null,
        el('span', { className: 'state' }, stateLabel(task.state)),
        el('span', { className: 'sep' }, '/'),
        el('span', {}, task.agent ?? 'agent'),
        task.updatedAt ? el('span', { className: 'time' }, relativeTime(task.updatedAt)) : null
      )
    )
    return button
  }

  // --- notifications ----------------------------------------------------
  function maybeNotify() {
    const attention = groups()
      .flatMap((group) => group.worktrees.flatMap((row) => row.tasks))
      .filter((task) => task.state === 'attention')
      .map((task) => ({ key: task.key, title: task.title }))
    const next = new Set(attention.map((task) => task.key))
    const previous = seenAttention
    seenAttention = next
    if (!previous || !state.notifyOn || !document.hidden) return
    const fresh = attention.filter((task) => !previous.has(task.key))
    if (fresh.length === 0) return
    const body = fresh.length > 1 ? `${fresh[0].title} 等 ${fresh.length} 个任务` : fresh[0].title
    if (typeof Notification !== 'undefined' && Notification.permission === 'granted') {
      try {
        new Notification('任务在等你确认', { body, tag: 'orca-attention' })
      } catch {
        // Notification constructors throw on some WebViews; vibrating still matters.
      }
    }
    navigator.vibrate?.(40)
  }

  async function toggleNotify() {
    if (state.notifyOn) {
      safeRemove('orca-notify')
      state.notifyOn = false
      paintNotify()
      return
    }
    if (typeof Notification === 'undefined') {
      // No Notification API (old iOS Safari): vibration alone is still useful.
      state.notifyOn = true
      safeSet('orca-notify', '1')
      paintNotify()
      return
    }
    try {
      if ((await Notification.requestPermission()) === 'granted') {
        safeSet('orca-notify', '1')
        state.notifyOn = true
        paintNotify()
      }
    } catch {
      // Denied browsers reject here; staying off is the correct outcome.
    }
  }

  function paintNotify() {
    notifyButton.textContent = state.notifyOn ? '提醒 ✓' : '提醒'
    notifyButton.setAttribute('aria-pressed', String(state.notifyOn))
  }

  // --- sheets -----------------------------------------------------------
  function openCreate(worktree) {
    state.creatingIn = worktree
    const candidates = new Set(['claude', 'codex'])
    for (const row of state.worktrees) {
      for (const agent of row.agents ?? []) if (agent.agentType) candidates.add(agent.agentType)
    }
    root.appendChild(
      createTaskSheet({
        hub,
        worktree,
        candidates: [...candidates],
        onClose: () => {
          root.querySelector('.sheet')?.remove()
          state.creatingIn = null
        },
        onCreated: (task) => {
          root.querySelector('.sheet')?.remove()
          state.creatingIn = null
          void load(true)
          onOpenTask(task)
        }
      })
    )
  }

  function openPassword() {
    root.appendChild(
      changePasswordSheet({
        onClose: () => root.querySelector('.sheet')?.remove()
      })
    )
  }

  // --- pull to refresh --------------------------------------------------
  let pull = null
  boardMain.addEventListener('touchstart', (event) => {
    pull = boardMain.scrollTop <= 0 ? { startY: event.touches[0].clientY, armed: false } : null
  })
  boardMain.addEventListener('touchmove', (event) => {
    if (!pull || pull.armed || state.refreshing) return
    if (event.touches[0].clientY - pull.startY > 90) {
      pull.armed = true
      state.refreshing = true
      const pill = el('div', { className: 'pull-pill' }, '刷新中…')
      root.prepend(pill)
      load(true).finally(() => {
        pill.remove()
        state.refreshing = false
      })
    }
  })
  boardMain.addEventListener('touchend', () => {
    pull = null
  })

  paintBoard()

  return {
    element: root,
    setStatus(status) {
      const wasConnected = state.status.gatewayConnected
      state.status = status
      if (!status.gatewayConnected && wasConnected) state.projections = new Map()
      if (status.gatewayConnected && !wasConnected) void load()
      paintBoard()
    },
    destroy() {
      destroyed = true
      clearInterval(tickTimer)
      clearInterval(pollTimer)
      unsubscribe()
    }
  }
}

function summaryItem(dotClass, text) {
  return el(
    'span',
    { className: 's-item' },
    el('i', { className: `sdot ${dotClass}`, 'aria-hidden': 'true' }),
    text
  )
}
