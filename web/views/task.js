// One task: history tail, live refetch, replies, approvals and questions
// (mirrors TaskView.tsx). Everything is rebuilt from state with DOM nodes.

import { el, render } from '../dom.js'
import { mutationEnvelope } from '../envelope.js'
import { markdown } from './markdown.js'
import { relativeTime, shortId, shortLabel, stateLabel, toneForState } from '../util.js'

const PERMISSION_MODES = [
  { value: 'ask', label: '每次都问', description: '编辑和命令都先找你要确认' },
  { value: 'accept-edits', label: '自动接受编辑', description: '文件编辑自动批，其余还要问' },
  { value: 'auto', label: '替我审批', description: '审批请求由系统替你回应' },
  { value: 'full-access', label: '全部放行', description: '所有编辑和命令都不再询问' },
  { value: 'own', label: 'agent 自己的设置', description: '沿用 agent 配置里的权限' }
]

export function taskView({ hub, sessionId, title, agent, onBack }) {
  const state = {
    status: { connected: false, gatewayConnected: false, orcaState: null },
    page: null,
    error: null,
    text: '',
    busy: false,
    loadingOlder: false,
    options: null,
    optionsOpen: false,
    optionBusyKey: null,
    optionError: null,
    liveTitle: null
  }
  let destroyed = false
  let debounce = null
  let olderCount = 0
  let messagesDirty = false
  // Details stay open across repaints; typed question answers survive too.
  const expanded = new Set()
  const questionState = new Map()
  const originalTitle = document.title
  // Nodes are reused by item id: a live stream refetches every 350 ms, and
  // replacing whole subtrees would swallow taps and lose scroll anchors.
  const itemCache = new Map()
  const stepCache = new Map()

  // The status lines outlive repaints, so reparenting keeps them clickable.
  const offlineLine = el('p', { className: 'empty' }, '连接已断开，等待恢复…')
  offlineLine.hidden = true
  const olderButton = el('button', { className: 'load-older' }, '载入更早的消息')
  olderButton.hidden = true
  olderButton.addEventListener('click', () => void loadOlder())
  const thinkingLine = el(
    'p',
    { className: 'thinking' },
    el('i', { className: 'spin', 'aria-hidden': 'true' }),
    el('span', { className: 'activity' }),
    el('span', { className: 'think-dots', 'aria-hidden': 'true' }, el('i'), el('i'), el('i'))
  )
  thinkingLine.hidden = true
  const noMessagesLine = el('p', { className: 'empty' }, '还没有消息')
  noMessagesLine.hidden = true
  const loadingLine = el('p', { className: 'empty' }, '载入中…')
  loadingLine.hidden = true
  const errorLine = el('p', { className: 'error' })
  errorLine.hidden = true

  // --- skeleton ---------------------------------------------------------
  const stopButton = el('button', { className: 'stop', onclick: () => void stop() }, '停止')
  const topnav = el(
    'div',
    { className: 'topnav' },
    el('button', { className: 'back', onclick: onBack }, '← 返回'),
    el('span', { className: 'where' }, `${agent ?? 'agent'} / ${shortId(sessionId)}`),
    stopButton
  )
  const titleNode = el('h1')
  const statePill = el('span', { className: 'pill' })
  const modelButton = el('button', { className: 'pill', onclick: () => openOptions() })
  const permissionButton = el('button', { className: 'pill', onclick: () => openOptions() })
  const sepOptions = el('span', { className: 'sep' }, '·')
  const sepUpdated = el('span', { className: 'sep' }, '·')
  const updatedSpan = el('span')
  const sepOffline = el('span', { className: 'sep' }, '·')
  const offlineSpan = el('span', {}, '连接已断开')
  const head = el(
    'div',
    { className: 'article-head' },
    el('div', { className: 'eyebrow' }, `${agent ?? 'agent'} 的工作记录`),
    titleNode,
    el(
      'div',
      { className: 'byline' },
      statePill,
      sepOptions,
      modelButton,
      permissionButton,
      sepUpdated,
      updatedSpan,
      sepOffline,
      offlineSpan
    )
  )
  const messages = el('div', { className: 'messages' })
  const reply = el('textarea', {
    name: 'reply',
    rows: 1,
    placeholder: '写一条回复…'
  })
  reply.addEventListener('input', () => {
    state.text = reply.value
    paintComposer()
  })
  reply.addEventListener('keydown', (event) => {
    if (event.key === 'Enter' && (event.metaKey || event.ctrlKey)) {
      event.preventDefault()
      void send()
    }
  })
  const sendButton = el('button', { onclick: () => void send() }, '发送 →')
  const composer = el('div', { className: 'composer' }, reply, sendButton)
  const root = el(
    'div',
    { className: 'app task-view' },
    topnav,
    el('div', { className: 'rule' }),
    head,
    el('div', { className: 'rule' }),
    messages,
    composer
  )

  // --- data -------------------------------------------------------------
  async function loadOptions() {
    try {
      const options = await hub.request('agentSession.options', { sessionId })
      if (!destroyed) {
        state.options = options
        paintHead()
        paintOptions()
      }
    } catch {
      if (!destroyed) state.options = null
    }
  }

  async function load() {
    try {
      const history = await hub.request('agentSession.history', {
        sessionId,
        direction: 'tail',
        limit: Math.min(200, 40 + olderCount)
      })
      if (destroyed) return
      if (history.page) {
        state.page = history.page
      } else if (history.reset) {
        // The journal was rebuilt under this session; the cached page is from a dead epoch.
        olderCount = 0
        state.page = null
        void loadOptions()
      }
      state.error = null
    } catch (failure) {
      if (!destroyed) state.error = failure.message
    }
    if (!destroyed) paint()
  }

  async function loadOlder() {
    const cursor = state.page?.window?.oldest
    if (!state.page || !cursor || state.loadingOlder) return
    state.loadingOlder = true
    state.error = null
    paint()
    const before = messages.scrollHeight
    try {
      const older = await hub.request('agentSession.history', {
        sessionId,
        direction: 'before',
        limit: 40,
        cursor
      })
      if (destroyed) return
      if (older.page) {
        const seen = new Set(state.page.items.map((item) => item.itemId))
        const added = older.page.items.filter((item) => !seen.has(item.itemId))
        olderCount += added.length
        state.page = {
          ...state.page,
          items: [...added, ...state.page.items],
          hasOlder: older.page.hasOlder
        }
      }
    } catch (failure) {
      if (!destroyed) state.error = failure.message
    } finally {
      if (!destroyed) {
        state.loadingOlder = false
        paint('offset', before)
      }
    }
  }

  // --- actions ----------------------------------------------------------
  async function send() {
    const body = {
      kind: 'message',
      role: 'user',
      blocks: [{ type: 'text', text: state.text.trimEnd() }]
    }
    if (!state.page?.fence || !state.text.trim() || state.busy) return
    state.busy = true
    state.error = null
    paint()
    try {
      const envelope = await mutationEnvelope({
        method: 'agentSession.send',
        sessionId,
        fence: state.page.fence,
        fields: { body }
      })
      const result = await hub.request('agentSession.send', { envelope, body })
      if (!result.ok) {
        state.error = refusalText(result)
        return
      }
      state.text = ''
      reply.value = ''
      await load()
    } catch (failure) {
      state.error = failure.message
    } finally {
      state.busy = false
      paint()
    }
  }

  async function stop() {
    const turn = state.page?.latestTurn?.turn
    if (!state.page?.fence || !turn) return
    state.busy = true
    state.error = null
    paint()
    try {
      const fields = { turnId: turn.turnId }
      const envelope = await mutationEnvelope({
        method: 'agentSession.cancel',
        sessionId,
        fence: state.page.fence,
        fields
      })
      const result = await hub.request('agentSession.cancel', { envelope, ...fields })
      if (!result.ok) state.error = refusalText(result)
      await load()
    } catch (failure) {
      state.error = failure.message
    } finally {
      state.busy = false
      paint()
    }
  }

  // Prompt fingerprints hash `agentSession.respondTo:<kind>`, not the RPC method name.
  async function respond(item, kind, fields) {
    if (!state.page?.fence) return
    state.busy = true
    state.error = null
    paint()
    try {
      const envelope = await mutationEnvelope({
        method: `agentSession.respondTo:${kind}`,
        sessionId,
        fence: state.page.fence,
        fields
      })
      const method = kind === 'approval' ? 'agentSession.respondToApproval' : 'agentSession.respondToQuestion'
      const result = await hub.request(method, { envelope, ...fields })
      if (!result.ok) state.error = refusalText(result)
      await load()
    } catch (failure) {
      state.error = failure.message
    } finally {
      state.busy = false
      paint()
    }
  }

  async function setOption(key, value) {
    if (!state.page?.fence) return
    state.optionBusyKey = key
    state.optionError = null
    paintOptions()
    try {
      const fields = { key, value }
      const envelope = await mutationEnvelope({
        method: 'agentSession.setOption',
        sessionId,
        fence: state.page.fence,
        fields
      })
      const result = await hub.request('agentSession.setOption', { envelope, ...fields })
      if (!result.ok) {
        state.optionError = refusalText(result)
        return
      }
      // The fence advanced and the provider may have reshuffled the catalog.
      await Promise.all([load(), loadOptions()])
    } catch (failure) {
      state.optionError = failure.message
    } finally {
      state.optionBusyKey = null
      // loadOptions() may settle after load()'s paint; repaint the byline too.
      paint()
    }
  }

  // --- subscriptions ----------------------------------------------------
  const unsubscribeStatus = hub.subscribe('agentSession.subscribeStatus', {}, (event) => {
    if (event.kind !== 'frame') return
    const frame = event.result
    const sessions =
      frame?.type === 'snapshot' ? frame.sessions : frame?.type === 'status' ? [frame.session] : []
    const match = sessions?.find((session) => session.sessionId === sessionId)
    if (match?.conversationName && match.conversationName !== state.liveTitle) {
      state.liveTitle = match.conversationName
      applyTitle()
      paintHead()
    }
  })

  let unsubscribeStream = () => {}

  const startStream = () => {
    unsubscribeStream()
    unsubscribeStream = hub.subscribe('agentSession.subscribe', { sessionId }, (event) => {
      if (event.kind === 'error') {
        state.error = event.message
        paint()
        return
      }
      // A reset needs an immediate refetch: the stream is new and nothing else will arrive.
      if (event.kind === 'reset') {
        if (debounce) {
          clearTimeout(debounce)
          debounce = null
        }
        void load()
        return
      }
      if (debounce) clearTimeout(debounce)
      // Frames arrive in bursts; one refetch per burst is enough for this view.
      debounce = setTimeout(() => void load(), 350)
    })
  }

  // --- rendering --------------------------------------------------------
  function applyTitle() {
    document.title = `${state.liveTitle ?? title} · orca 工作台`
  }

  function paint(mode = 'auto', beforeHeight = 0) {
    if (destroyed) return
    const running = state.page?.latestTurn?.turn.state === 'running'
    stopButton.hidden = !running
    paintHead(running)
    paintComposer(running)
    paintMessages(mode, beforeHeight)
    paintOptions()
  }

  function paintHead(running = state.page?.latestTurn?.turn.state === 'running') {
    const options = state.options
    const stateName = running ? 'working' : 'idle'
    const tone = toneForState(stateName)
    titleNode.textContent = state.liveTitle ?? title
    statePill.className = tone === 'green' ? 'pill pill-green' : tone === 'amber' ? 'pill pill-amber' : 'pill'
    render(
      statePill,
      running ? el('i', { className: 'spin', 'aria-hidden': 'true' }) : null,
      stateLabel(stateName)
    )
    // The pills keep their identity across stream updates so they stay tappable.
    sepOptions.hidden = !options
    modelButton.hidden = !options
    permissionButton.hidden = !options
    if (options) {
      modelButton.textContent = `模型 ${currentModelLabel(options)}`
      permissionButton.textContent = `权限 ${permissionModeLabel(options)}`
    }
    const updated = lastUpdated(state.page)
    sepUpdated.hidden = !updated
    updatedSpan.hidden = !updated
    updatedSpan.textContent = updated
    const disconnected = !state.status.gatewayConnected
    sepOffline.hidden = !disconnected
    offlineSpan.hidden = !disconnected
  }

  function paintComposer(running = state.page?.latestTurn?.turn.state === 'running') {
    reply.placeholder = running ? '回复（会排队）…' : '写一条回复…'
    sendButton.disabled = state.busy || !state.text.trim()
  }

  function paintMessages(mode = 'auto', beforeHeight = 0) {
    // A streamed reply must not yank a half-typed free-text answer out from
    // under the keyboard; defer the repaint until the field loses focus.
    const active = document.activeElement
    if (active && messages.contains(active) && active.classList.contains('answer-input')) {
      messagesDirty = true
      return
    }
    const nearBottom = messages.scrollHeight - messages.scrollTop - messages.clientHeight < 240
    const beforeTop = messages.scrollTop
    const height = mode === 'offset' ? beforeHeight : messages.scrollHeight

    offlineLine.hidden = Boolean(state.status.gatewayConnected)
    olderButton.hidden = !state.page?.hasOlder
    olderButton.disabled = state.loadingOlder
    olderButton.textContent = state.loadingOlder ? '载入中…' : '载入更早的消息'
    errorLine.hidden = !state.error
    errorLine.textContent = state.error ?? ''
    loadingLine.hidden = Boolean(state.page) || Boolean(state.error)
    noMessagesLine.hidden = !(state.page && state.page.items.length === 0)
    const running = state.page?.latestTurn?.turn.state === 'running'
    const activity = currentActivity()
    thinkingLine.hidden = !(running && activity && !state.error)
    thinkingLine.querySelector('.activity').textContent = activity

    const list = entries()
    const currentItems = new Set((state.page?.items ?? []).map((item) => item.itemId))
    for (const key of itemCache.keys()) if (!currentItems.has(key)) itemCache.delete(key)
    const currentSteps = new Set(list.filter((entry) => entry.kind === 'steps').map((entry) => entry.key))
    for (const key of stepCache.keys()) if (!currentSteps.has(key)) stepCache.delete(key)

    const nodes = [offlineLine, olderButton]
    for (const entry of list) {
      for (const node of entryNodes(entry)) nodes.push(node)
    }
    nodes.push(thinkingLine, noMessagesLine, loadingLine, errorLine)

    render(messages, nodes)
    if (mode === 'offset') {
      messages.scrollTop = beforeTop + (messages.scrollHeight - height)
    } else if (!messages.dataset.initialized || mode === 'bottom' || (mode === 'auto' && nearBottom)) {
      messages.scrollTop = messages.scrollHeight
      messages.dataset.initialized = 'true'
    }
  }

  let optionsSig = null
  function paintOptions() {
    if (!state.optionsOpen || !state.options) {
      root.querySelector('.sheet')?.remove()
      optionsSig = null
      return
    }
    const sig = JSON.stringify([state.options, state.optionBusyKey, state.optionError])
    if (root.querySelector('.sheet') && sig === optionsSig) return
    root.querySelector('.sheet')?.remove()
    root.appendChild(optionsSheet())
    optionsSig = sig
  }

  function openOptions() {
    state.optionsOpen = true
    state.optionError = null
    paintOptions()
  }

  function entries() {
    const list = []
    let previous = null
    let steps = []
    const flush = () => {
      if (steps.length > 0) {
        list.push({ kind: 'steps', key: steps[0].itemId, items: steps })
        steps = []
      }
    }
    for (const item of state.page?.items ?? []) {
      if (isStep(item)) {
        steps.push(item)
        continue
      }
      flush()
      const role = messageRole(item)
      const showRole = role !== null && role !== previous
      if (role !== null) previous = role
      list.push({ kind: 'item', key: item.itemId, item, showRole })
    }
    flush()
    return list
  }

  function entryNodes(entry) {
    if (entry.kind === 'steps') {
      if (entry.items.length > 2) return [stepGroup(entry.key, entry.items)]
      return entry.items.map((item) => itemNode(item, false)).filter(Boolean)
    }
    const node = itemNode(entry.item, entry.showRole)
    return node ? [node] : []
  }

  function stepGroup(key, items) {
    const sig = JSON.stringify(items)
    let entry = stepCache.get(key)
    if (entry && entry.sig === sig) return entry.node
    if (!entry) {
      const name = el('span', { className: 'name' })
      const what = el('span', { className: 'what' })
      const list = el('div', { className: 'step-list' })
      const node = el(
        'details',
        { className: 'step-group' },
        el('summary', {}, el('span', { className: 'lead' }, '↳'), name, what),
        list
      )
      // The group's own state is keyed apart from the first step's footnote.
      trackDetails(node, `group:${key}`)
      entry = { node, name, what, list, sig: null }
      stepCache.set(key, entry)
    }
    const tools = items.filter((item) => item.body.kind === 'tool-call')
    const lastName = tools[tools.length - 1]?.body?.name
    entry.name.textContent = `${items.length} 步`
    entry.what.textContent = lastName ? `最近 ${lastName}` : '思考与工具调用'
    render(entry.list, ...items.map((item) => itemNode(item, false)).filter(Boolean))
    entry.sig = sig
    return entry.node
  }

  function itemNode(item, showRole) {
    const body = item.body
    const busyAffectsNode = body.kind === 'approval' || body.kind === 'question'
    const sig = JSON.stringify([item, showRole, busyAffectsNode ? state.busy : false])
    const cached = itemCache.get(item.itemId)
    if (cached && cached.sig === sig) return cached.node
    const node = buildItemNode(item, showRole)
    if (!node) {
      itemCache.delete(item.itemId)
      return null
    }
    itemCache.set(item.itemId, { node, sig })
    return node
  }

  function buildItemNode(item, showRole) {
    const body = item.body
    if (body.kind === 'message') {
      const text = (body.blocks ?? [])
        .filter((block) => block.type === 'text' && block.text)
        .map((block) => block.text)
        .join('\n')
      if (!text) return null
      if (body.role === 'reasoning') {
        const details = el(
          'details',
          { className: 'reasoning' },
          el('summary', {}, '↳ 思考过程'),
          el('pre', { textContent: text })
        )
        trackDetails(details, item.itemId)
        return details
      }
      return el(
        'div',
        { className: body.role === 'user' ? 'turn turn-user' : 'turn' },
        showRole ? el('div', { className: 'role' }, body.role === 'user' ? '你' : (agent ?? 'agent')) : null,
        el('div', { className: 'body' }, body.role === 'user' ? text : markdown(text))
      )
    }

    if (body.kind === 'tool-call') {
      const summary = el(
        'summary',
        {},
        el('span', { className: 'lead' }, '↳'),
        el('span', { className: 'name' }, body.name ?? '工具'),
        el('span', { className: 'what' }, summarizeToolInput(body.input))
      )
      if (body.state === 'running') {
        summary.appendChild(
          el('span', { className: 'meta' }, el('i', { className: 'spin', 'aria-hidden': 'true' }), '进行中')
        )
      }
      const output =
        body.output?.head ?? (body.input === undefined ? '' : JSON.stringify(body.input, null, 2))
      const details = el('details', { className: 'footnote' }, summary, el('pre', { textContent: output }))
      trackDetails(details, item.itemId)
      return details
    }

    if (body.kind === 'approval') return approvalPrompt(item, body)
    if (body.kind === 'question') return questionPrompt(item, body)
    return null
  }

  function trackDetails(details, key) {
    if (expanded.has(key)) details.open = true
    details.addEventListener('toggle', () => {
      if (details.open) expanded.add(key)
      else expanded.delete(key)
    })
  }

  function approvalPrompt(item, body) {
    const pending = (body.resolution?.state ?? 'pending') === 'pending'
    const chosen = body.options?.find((option) => option.id === body.resolution?.selectedOptionId)
    const actions = el('div', { className: 'note-actions' })
    for (const [index, option] of (body.options ?? []).entries()) {
      const button = el(
        'button',
        { className: index === 0 ? 'allow' : 'deny', title: option.description, disabled: state.busy },
        option.label
      )
      button.addEventListener('click', () =>
        void respond(item, 'approval', {
          itemId: item.itemId,
          expectedRevision: item.revision,
          optionId: option.id
        })
      )
      actions.appendChild(button)
    }
    return el(
      'div',
      { className: 'note' },
      el('div', { className: 'note-label' }, '需要你的确认'),
      el('h3', {}, body.title || body.displayName || 'agent 请求执行操作'),
      body.description ? el('p', { className: 'note-desc' }, body.description) : null,
      body.detail ? el('pre', { className: 'note-detail', textContent: body.detail }) : null,
      pending ? actions : el('p', { className: 'resolved' }, chosen ? `已选择：${chosen.label}` : '已处理')
    )
  }

  function questionPrompt(item, body) {
    const questions = body.questions?.length
      ? body.questions
      : [
          {
            id: body.freeTextQuestionId ?? 'q1',
            question: body.question ?? '',
            options: body.options ?? [],
            multiSelect: false,
            ...(body.freeTextQuestionId ? { freeTextQuestionId: body.freeTextQuestionId } : {}),
            ...(body.freeTextInput ? { freeTextInput: body.freeTextInput } : {})
          }
        ]
    const pending = (body.resolution?.state ?? 'pending') === 'pending'
    if (!pending) {
      return el(
        'div',
        { className: 'note' },
        el('div', { className: 'note-label' }, '提问'),
        questions.map((question) => el('h3', {}, question.header ?? question.question)),
        el('p', { className: 'resolved' }, '已回答')
      )
    }

    let saved = questionState.get(item.itemId)
    if (!saved) {
      saved = { picked: new Map(), other: new Map() }
      questionState.set(item.itemId, saved)
    }

    const submit = el('button', { className: 'submit', disabled: true }, '提交')
    const refreshSubmit = () => {
      submit.disabled = state.busy || !questions.every((question) => answered(question, saved))
    }

    const nodes = questions.map((question) => {
      const options = el('div', { className: 'options' })
      for (const option of question.options ?? []) {
        const button = el(
          'button',
          { className: 'option', disabled: state.busy },
          el('span', { className: 'option-label' }, option.label),
          option.description ? el('small', {}, option.description) : null
        )
        const isActive = () => (saved.picked.get(question.id) ?? new Set()).has(option.id)
        if (isActive()) button.classList.add('option-active')
        button.addEventListener('click', () => {
          const current = saved.picked.get(question.id) ?? new Set()
          let next
          if (question.multiSelect) {
            next = new Set(current)
            if (next.has(option.id)) next.delete(option.id)
            else next.add(option.id)
          } else {
            next = new Set([option.id])
          }
          saved.picked.set(question.id, next)
          for (const sibling of options.querySelectorAll('button.option')) sibling.classList.remove('option-active')
          for (const sibling of options.querySelectorAll('button.option')) {
            const id = sibling.dataset.optionId
            if (next.has(id)) sibling.classList.add('option-active')
          }
          refreshSubmit()
        })
        button.dataset.optionId = option.id
        options.appendChild(button)
      }

      let input = null
      if (question.freeTextQuestionId) {
        input = el('textarea', {
          className: 'answer-input',
          name: `answer-${question.id}`,
          rows: 2,
          placeholder: question.freeTextInput?.placeholder ?? '其他答案…'
        })
        input.value = saved.other.get(question.id) ?? ''
        input.addEventListener('input', () => {
          saved.other.set(question.id, input.value)
          refreshSubmit()
        })
      }

      return el(
        'div',
        { className: 'question' },
        question.header ? el('p', { className: 'question-header' }, question.header) : null,
        el('p', { className: 'question-title' }, question.question),
        options,
        input
      )
    })

    submit.addEventListener('click', () => {
      const answers = questions.map((question) => {
        const typed = saved.other.get(question.id) ?? ''
        return {
          questionId: question.id,
          optionIds: [...(saved.picked.get(question.id) ?? [])],
          ...(question.freeTextQuestionId && typed.trim().length > 0 ? { other: typed } : {})
        }
      })
      void respond(item, 'question', {
        itemId: item.itemId,
        expectedRevision: item.revision,
        answers
      })
    })
    refreshSubmit()

    return el(
      'div',
      { className: 'note' },
      el('div', { className: 'note-label' }, '需要回答'),
      ...nodes,
      submit
    )
  }

  function optionsSheet() {
    const options = state.options
    const current = options.current ?? {}
    const body = el('div', { className: 'sheet-body' })
    if (state.optionBusyKey) body.appendChild(el('p', { className: 'muted small' }, `正在应用 ${state.optionBusyKey}…`))
    if (state.optionError) body.appendChild(el('p', { className: 'error' }, state.optionError))

    body.appendChild(el('p', { className: 'field-label' }, '模型（下一回合生效）'))
    if (options.models?.length) {
      const chips = el('div', { className: 'chips' })
      for (const model of options.models) {
        const button = el(
          'button',
          {
            className: model.id === current.model ? 'chip chip-active' : 'chip',
            title: model.description,
            disabled: state.optionBusyKey !== null
          },
          model.label ?? model.id
        )
        button.addEventListener('click', () => void setOption('model', model.id))
        chips.appendChild(button)
      }
      body.appendChild(chips)
    } else {
      body.appendChild(el('p', { className: 'muted small' }, '没有获取到可选模型'))
    }

    body.appendChild(el('p', { className: 'field-label' }, '权限（决定 agent 什么时候来找你确认）'))
    const modes = el('div', { className: 'chips' })
    for (const mode of PERMISSION_MODES) {
      const button = el(
        'button',
        {
          className: current.permissionMode === mode.value ? 'chip chip-active' : 'chip',
          title: mode.description,
          disabled: state.optionBusyKey !== null
        },
        mode.label
      )
      button.addEventListener('click', () => void setOption('permissionMode', mode.value))
      modes.appendChild(button)
    }
    body.appendChild(modes)

    return el(
      'div',
      { className: 'sheet' },
      el(
        'div',
        { className: 'topnav' },
        el(
          'button',
          {
            className: 'back',
            onclick: () => {
              state.optionsOpen = false
              paintOptions()
            }
          },
          '关闭'
        ),
        el('span', { className: 'sheet-title' }, '会话设置'),
        el('span', { className: 'where' }, shortLabel(current.model ?? ''))
      ),
      el('div', { className: 'rule' }),
      body
    )
  }

  // What the agent is doing right now, read off the tail.
  function currentActivity() {
    if (state.page?.latestTurn?.turn.state !== 'running') return ''
    const items = state.page?.items ?? []
    for (let index = items.length - 1; index >= 0; index--) {
      const item = items[index]
      if (item.body.kind === 'tool-call' && item.body.state === 'running') {
        return `正在 ${item.body.name ?? '动手'}`
      }
      if (messageRole(item) === 'assistant') return '正在回复'
    }
    return '正在思考'
  }

  messages.addEventListener('focusout', () => {
    setTimeout(() => {
      if (destroyed || !messagesDirty) return
      if (messages.contains(document.activeElement)) return
      messagesDirty = false
      paint()
    }, 0)
  })

  // --- lifecycle --------------------------------------------------------
  applyTitle()
  paint()
  if (state.status.gatewayConnected) {
    void load()
    void loadOptions()
    startStream()
  }

  return {
    element: root,
    setStatus(status) {
      const wasConnected = state.status.gatewayConnected
      state.status = status
      if (status.gatewayConnected && !wasConnected) {
        void load()
        void loadOptions()
        startStream()
      } else if (!status.gatewayConnected && wasConnected) {
        if (debounce) {
          clearTimeout(debounce)
          debounce = null
        }
        unsubscribeStream()
        unsubscribeStream = () => {}
      }
      paint()
    },
    destroy() {
      destroyed = true
      if (debounce) clearTimeout(debounce)
      unsubscribeStatus()
      unsubscribeStream()
      document.title = originalTitle || 'orca 工作台'
    }
  }
}

function refusalText(result) {
  return `${result.refusal.code}${result.refusal.message ? `: ${result.refusal.message}` : ''}`
}

function messageRole(item) {
  const body = item.body
  if (body.kind !== 'message' || body.role === 'reasoning') return null
  return body.role === 'user' ? 'user' : 'assistant'
}

function isStep(item) {
  return (
    item.body.kind === 'tool-call' ||
    (item.body.kind === 'message' && item.body.role === 'reasoning')
  )
}

function answered(question, saved) {
  if ((saved.picked.get(question.id)?.size ?? 0) > 0) return true
  if (!question.freeTextQuestionId) return false
  const typed = saved.other.get(question.id) ?? ''
  return question.freeTextInput?.allowEmpty === true || typed.trim().length > 0
}

function currentModelLabel(options) {
  const id = options.current?.model
  const known = options.models?.find((model) => model.id === id)
  if (known?.label) return shortLabel(known.label)
  return id ? shortLabel(id) : '未报'
}

function permissionModeLabel(options) {
  const value = options.current?.permissionMode ?? options.current?.reportedPermissionMode
  if (!value) return '未报'
  return PERMISSION_MODES.find((mode) => mode.value === value)?.label ?? value
}

function lastUpdated(page) {
  const last = page?.items[page.items.length - 1]
  return last?.observedAt ? relativeTime(last.observedAt) : ''
}

function summarizeToolInput(input) {
  if (!input || typeof input !== 'object') return ''
  const candidate = input.description ?? input.file_path ?? input.path ?? input.command ?? input.query ?? ''
  const text = typeof candidate === 'string' ? candidate : JSON.stringify(candidate)
  return text.length > 100 ? `${text.slice(0, 100)}…` : text
}
