import { mutationEnvelope } from '../envelope.js'
import { el } from '../dom.js'

/** New structured-agent session in `worktree` (mirrors CreateTask.tsx). */
export function createTaskSheet({ hub, worktree, candidates, onClose, onCreated }) {
  const chips = el('div', { className: 'chips' })
  const status = el('p', { className: 'muted small' }, '检查可用 agent…')
  const textarea = el('textarea', {
    className: 'sheet-textarea',
    name: 'message',
    rows: 7,
    placeholder: '描述要 agent 做什么…'
  })
  const error = el('p', { className: 'error' })
  error.hidden = true
  const create = el('button', { className: 'primary', disabled: true }, '创建')

  let agent = null
  let supported = null
  let busy = false

  const paintAgents = () => {
    chips.replaceChildren()
    if (supported === null) return
    if (supported.length === 0) {
      status.replaceWith(el('p', { className: 'error' }, '这个工作区没有可用的结构化 agent'))
      return
    }
    for (const name of supported) {
      const chip = el('button', { className: `chip${name === agent ? ' chip-active' : ''}` }, name)
      chip.addEventListener('click', () => {
        agent = name
        paintAgents()
        create.disabled = busy || !agent
      })
      chips.appendChild(chip)
    }
  }

  void (async () => {
    const replies = await Promise.all(
      candidates.map(async (name) => {
        try {
          const reply = await hub.request('agentSession.createSupport', {
            worktree: `id:${worktree.worktreeId}`,
            agent: name
          })
          return reply?.supported === true ? name : null
        } catch {
          return null
        }
      })
    )
    supported = replies.filter((name) => name !== null)
    agent = supported[0] ?? null
    status.hidden = true
    paintAgents()
    create.disabled = busy || !agent
  })()

  const submit = async () => {
    if (!agent || busy) return
    busy = true
    create.disabled = true
    create.textContent = '创建中…'
    error.hidden = true
    const sessionId = `${agent}_${crypto.randomUUID().replaceAll('-', '_')}`
    const message = textarea.value.trim()
    const fields = {
      worktree: `id:${worktree.worktreeId}`,
      agent,
      ...(message
        ? {
            firstMessage: {
              clientMessageId: crypto.randomUUID(),
              body: {
                kind: 'message',
                role: 'user',
                blocks: [{ type: 'text', text: message }]
              }
            }
          }
        : {})
    }
    try {
      const envelope = await mutationEnvelope({
        method: 'agentSession.create',
        sessionId,
        fence: null,
        fields
      })
      const result = await hub.request('agentSession.create', { envelope, ...fields })
      if (!result.ok) {
        error.textContent = `${result.refusal.code}${result.refusal.message ? `: ${result.refusal.message}` : ''}`
        error.hidden = false
        return
      }
      onCreated({
        sessionId,
        title: firstLine(message) || '新任务',
        agent
      })
    } catch (failure) {
      error.textContent = failure.message
      error.hidden = false
    } finally {
      busy = false
      create.disabled = !agent
      create.textContent = '创建'
    }
  }
  create.addEventListener('click', () => void submit())

  return el(
    'div',
    { className: 'sheet' },
    el(
      'div',
      { className: 'topnav' },
      el('button', { className: 'back', onclick: onClose }, '取消'),
      el('span', { className: 'sheet-title' }, '新建任务'),
      create
    ),
    el('div', { className: 'rule' }),
    el(
      'div',
      { className: 'sheet-body' },
      el('p', { className: 'sheet-context' }, `⑂ ${worktree.displayName ?? worktree.branch ?? worktree.path}`),
      el('p', { className: 'field-label' }, 'Agent'),
      status,
      chips,
      el('p', { className: 'field-label' }, '任务内容'),
      textarea,
      error
    )
  )
}

function firstLine(value) {
  const line = value.split('\n')[0] ?? ''
  return line.length > 60 ? `${line.slice(0, 60)}…` : line
}
