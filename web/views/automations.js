// Scheduled-task management: Orca owns every automation (rrule, missed-run grace,
// runs), so this page only surfaces the automation.* RPC surface (mirrors
// Automations.tsx).

import { el, render } from '../dom.js'
import { fmtStamp, sessionIdFromPaneKey } from '../util.js'

const PRESETS = [
  { key: 'daily', label: '每天' },
  { key: 'weekdays', label: '工作日' },
  { key: 'weekly', label: '每周' },
  { key: 'hourly', label: '每小时' },
  { key: 'minutes', label: '每几分钟' },
  { key: 'cron', label: '自定义 cron' }
]

const WEEKDAYS = ['周日', '周一', '周二', '周三', '周四', '周五', '周六']

const RUN_STATUS = {
  pending: '等待',
  dispatching: '启动中',
  dispatched: '已启动',
  completed: '已完成',
  failed: '失败'
}

const LONG_OUTPUT_LINES = 40

export function automationsView({ hub, onBack, onOpenTask }) {
  const state = {
    status: { connected: false, gatewayConnected: false, orcaState: null },
    automations: null,
    projects: [],
    worktrees: [],
    error: null,
    stale: false,
    note: null,
    busyId: null,
    runsFor: null,
    outputFor: null,
    confirming: null,
    runs: [],
    editor: undefined, // undefined closed, null create, object edit
    expandedOutputs: new Set()
  }
  let destroyed = false
  let busy = false
  let pollTimer = null
  const originalTitle = document.title

  const topnav = el(
    'div',
    { className: 'topnav' },
    el('button', { className: 'back', onclick: onBack }, '返回'),
    el('span', { className: 'sheet-title' }, '定时任务'),
    el('button', { className: 'primary', onclick: () => openEditor(null) }, '新建')
  )
  const newButton = topnav.querySelector('button.primary')
  const main = el('main', { className: 'article' })
  const root = el('div', { className: 'task-view' }, topnav, el('div', { className: 'rule' }), main)

  // --- data -------------------------------------------------------------
  async function load() {
    try {
      const [list, projectList, ps] = await Promise.all([
        hub.request('automation.list', {}),
        hub.request('project.list', {}),
        hub.request('worktree.ps', {})
      ])
      if (destroyed) return
      state.automations = list.automations ?? []
      state.projects = projectList.projects ?? []
      state.worktrees = ps.worktrees ?? []
      state.error = null
      state.stale = false
      state.note = null
    } catch (failure) {
      if (destroyed) return
      state.error = failure.message
      state.stale = true
      // The error line replaces the loader; "载入中…" must not linger forever.
      state.automations = state.automations ?? []
    }
    paint()
  }

  async function reloadRuns(id) {
    try {
      const reply = await hub.request('automation.runs', { automationId: id })
      if (destroyed) return
      state.runs = reply.runs ?? []
      state.runsFor = id
    } catch (failure) {
      if (!destroyed) {
        state.runsFor = null
        state.note = failure.message
      }
    }
    paint()
  }

  async function mutate(id, action, remove = false) {
    // A double tap must not start two runs; busyId only paints the button.
    if (busy) return
    busy = true
    state.busyId = id
    state.note = null
    paint()
    try {
      await action()
      await load()
      if (remove) {
        state.runsFor = null
        state.outputFor = null
        state.confirming = null
      } else if (state.runsFor === id) {
        // The automation may vanish between click and refetch; its history is then gone too.
        try {
          const reply = await hub.request('automation.runs', { automationId: id })
          state.runs = reply.runs ?? []
        } catch {
          state.runsFor = null
        }
      }
    } catch (failure) {
      state.note = failure.message
    } finally {
      busy = false
      state.busyId = null
      paint()
    }
  }

  async function toggleRuns(id) {
    if (busy) return
    if (state.runsFor === id) {
      state.runsFor = null
      state.outputFor = null
      paint()
      return
    }
    busy = true
    state.busyId = id
    paint()
    try {
      const reply = await hub.request('automation.runs', { automationId: id })
      if (destroyed) return
      state.runs = reply.runs ?? []
      state.runsFor = id
    } catch (failure) {
      if (!destroyed) state.note = failure.message
    } finally {
      busy = false
      state.busyId = null
      paint()
    }
  }

  function openRun(run) {
    const sessionId = runSessionId(run)
    if (sessionId) onOpenTask(sessionId, run.title ?? '定时任务')
  }

  // --- rendering --------------------------------------------------------
  function paint() {
    if (destroyed) return
    newButton.disabled = !state.status.gatewayConnected
    const nodes = []
    if (!state.status.gatewayConnected) {
      nodes.push(el('p', { className: 'empty' }, '家里电脑的 gateway 未连接。'))
    }
    if (state.error) {
      nodes.push(el('p', { className: 'error' }, state.stale ? `${state.error}（以下是上次成功载入的内容）` : state.error))
    }
    if (state.note) nodes.push(el('p', { className: 'error' }, state.note))
    if (state.automations === null && state.status.gatewayConnected) {
      nodes.push(el('p', { className: 'empty' }, '载入中…'))
    }
    if (state.automations?.length === 0 && !state.error) {
      nodes.push(
        el('p', { className: 'empty' },
          '还没有定时任务。到点由家里桌面执行；Orca 没开时错过的任务会在宽限期内补跑一次。')
      )
    }
    for (const automation of state.automations ?? []) nodes.push(automationSection(automation))
    nodes.push(el('p', { className: 'footnote' }, '调度和补跑都由桌面 Orca 负责；这页只是它的遥控器。'))
    render(main, nodes)
  }

  function automationSection(automation) {
    const head = el(
      'div',
      { className: 'autom-head' },
      el('span', { className: 'task-mark' }, automation.enabled ? '✳' : '·'),
      el('h3', { className: 'autom-title' }, automation.name),
      el('span', { className: automation.enabled ? 'pill pill-green' : 'pill pill-amber' },
        automation.enabled ? '已启用' : '已暂停')
    )
    const meta = [
      automation.agentId,
      describeRrule(automation.rrule),
      automation.timezone || null,
      automation.nextRunAt ? `下次 ${fmtStamp(automation.nextRunAt, automation.timezone)}` : null
    ].filter(Boolean).join(' · ')

    const actions = el('div', { className: 'autom-actions' })
    const busyHere = state.busyId === automation.id
    const runNow = el('button', { disabled: busyHere }, '运行一次')
    runNow.addEventListener('click', () =>
      void mutate(automation.id, () => hub.request('automation.runNow', { id: automation.id }))
    )
    const toggle = el('button', { disabled: busyHere }, automation.enabled ? '暂停' : '启用')
    toggle.addEventListener('click', () =>
      void mutate(automation.id, () =>
        hub.request('automation.update', { id: automation.id, updates: { enabled: !automation.enabled } })
      )
    )
    const runsButton = el('button', { disabled: busyHere }, '运行记录')
    runsButton.addEventListener('click', () => void toggleRuns(automation.id))
    const edit = el('button', {}, '编辑')
    edit.addEventListener('click', () => openEditor(automation))
    actions.append(runNow, toggle, runsButton, edit)

    if (state.confirming === automation.id) {
      const confirm = el('button', { className: 'autom-danger', disabled: busyHere }, '确认删除')
      confirm.addEventListener('click', () =>
        void mutate(automation.id, () => hub.request('automation.delete', { id: automation.id }), true)
      )
      const cancel = el('button', {}, '取消')
      cancel.addEventListener('click', () => {
        state.confirming = null
        paint()
      })
      actions.append(confirm, cancel)
    } else {
      const remove = el('button', { className: 'autom-danger' }, '删除')
      remove.addEventListener('click', () => {
        state.confirming = automation.id
        paint()
      })
      actions.appendChild(remove)
    }

    const section = el(
      'section',
      { className: 'autom' },
      head,
      el('p', { className: 'autom-meta' }, meta),
      el('p', { className: 'autom-prompt' }, automation.prompt),
      actions
    )
    if (state.runsFor === automation.id) section.appendChild(runsList())
    return section
  }

  function runsList() {
    const box = el('div', { className: 'autom-runs' })
    if (state.runs.length === 0) box.appendChild(el('p', { className: 'muted small' }, '还没有运行记录。'))
    for (const run of state.runs) {
      const linked = canOpenRun(run)
      const output = run.outputSnapshot?.content ?? ''
      const open = state.outputFor === run.id
      const action = linked ? '会话 ›' : output ? (open ? '收起' : '输出 ›') : ''
      const row = el(
        'button',
        { className: 'run-row', disabled: !linked && !output },
        el('span', { className: 'run-status' }, runStatus(run)),
        el(
          'span',
          { className: 'run-title' },
          fmtStamp(run.scheduledFor, null),
          run.trigger === 'manual' ? ' · 手动' : '',
          run.error ? ` · ${run.error}` : ''
        ),
        el('span', { className: 'run-open' }, action)
      )
      row.addEventListener('click', () => {
        if (linked) openRun(run)
        else if (output) {
          state.outputFor = open ? null : run.id
          paint()
        }
      })
      box.appendChild(row)
      if (open) box.appendChild(runOutput(run))
    }
    return box
  }

  function runOutput(run) {
    const snapshot = run.outputSnapshot
    if (!snapshot) return el('span', { hidden: true })
    const expanded = state.expandedOutputs.has(run.id)
    const lines = String(snapshot.content ?? '').split('\n')
    const clipped = !expanded && lines.length > LONG_OUTPUT_LINES
    const box = el(
      'div',
      { className: 'run-output' },
      el('pre', { className: 'run-output-text', textContent: clipped ? lines.slice(0, LONG_OUTPUT_LINES).join('\n') : snapshot.content })
    )
    if (clipped || expanded) {
      const more = el('button', { className: 'run-output-more' },
        clipped ? `展开其余 ${lines.length - LONG_OUTPUT_LINES} 行` : '收起')
      more.addEventListener('click', () => {
        if (expanded) state.expandedOutputs.delete(run.id)
        else state.expandedOutputs.add(run.id)
        paint()
      })
      box.appendChild(more)
    }
    box.appendChild(
      el(
        'p',
        { className: 'run-output-meta' },
        snapshot.truncated ? 'Orca 只保留了末尾部分输出 · ' : '',
        `记录于 ${fmtStamp(snapshot.capturedAt, null)}`
      )
    )
    return box
  }

  // --- editor sheet -----------------------------------------------------
  const managed = () => state.worktrees.filter((row) => Boolean(row.worktreeId))
  const repoName = () => {
    const names = new Map()
    for (const project of state.projects) {
      for (const repoId of project.sourceRepoIds ?? []) names.set(repoId, project.displayName)
    }
    return names
  }
  const agentOptions = () => {
    // worktree.ps only names agents that have run at least once here.
    const seen = new Set(['claude', 'codex', 'gemini', 'grok', 'copilot'])
    for (const row of state.worktrees) {
      for (const agent of row.agents ?? []) {
        if (agent.agentType && /^[a-z][a-z0-9-]*$/.test(agent.agentType)) seen.add(agent.agentType)
      }
    }
    return [...seen]
  }

  function openEditor(automation) {
    closeEditor()
    state.editor = automation
    root.appendChild(editorSheet(automation))
  }

  function closeEditor() {
    state.editor = undefined
    root.querySelector('.sheet')?.remove()
  }

  function editorSheet(editing) {
    const names = repoName()
    const managedRows = managed()
    const agents = agentOptions()
    const repoOptions = []
    const seenRepos = new Set()
    for (const row of state.worktrees) {
      if (seenRepos.has(row.repoId)) continue
      seenRepos.add(row.repoId)
      repoOptions.push([row.repoId, names.get(row.repoId) ?? row.repoId])
    }

    const local = {
      name: editing?.name ?? '',
      prompt: editing?.prompt ?? '',
      agent: editing?.agentId ?? (agents.includes('claude') ? 'claude' : (agents[0] ?? null)),
      preset: editing ? presetOf(editing.rrule) : 'daily',
      time: editing ? clockFromRrule(editing.rrule) : '09:30',
      weekday: editing ? dayFromRrule(editing.rrule) : 1,
      minutes: editing ? intervalFromRrule(editing.rrule) : 30,
      cron: editing?.rrule ?? '0 9 * * *',
      mode: editing?.workspaceMode === 'new_per_run' ? 'new_per_run' : 'existing',
      workspace: editing?.workspaceId ?? managedRows[0]?.worktreeId ?? null,
      repo: repoOptions[0]?.[0] ?? null,
      busy: false,
      error: null
    }

    // The sheet can open before its selector lists arrive; falling back keeps the first
    // worktree selected instead of saving "no workspace chosen".
    const activeWorkspace = () => local.workspace ?? managedRows[0]?.worktreeId ?? null
    const activeRepo = () => local.repo ?? repoOptions[0]?.[0] ?? null
    const editingWorkspace = editing
      ? (managedRows.find((row) => row.worktreeId === editing.workspaceId) ?? null)
      : null

    const error = el('p', { className: 'error' })
    error.hidden = true
    const saveButton = el('button', { className: 'primary' }, '保存')

    const body = el('div', { className: 'sheet-body' })
    if (editing) {
      body.appendChild(
        el(
          'p',
          { className: 'sheet-context' },
          editingWorkspace
            ? `_workspace ${names.get(editingWorkspace.repoId) ?? ''} / ${editingWorkspace.displayName ?? editingWorkspace.branch ?? editingWorkspace.path}`
            : editing.workspaceMode === 'new_per_run'
              ? '每次运行新建工作区'
              : '固定工作区（未在列表中找到）'
        )
      )
    }

    const nameInput = el('input', { className: 'inline-input', name: 'name', placeholder: '每日站前简报' })
    nameInput.value = local.name
    nameInput.addEventListener('input', () => {
      local.name = nameInput.value
    })
    body.append(el('p', { className: 'field-label' }, '名称'), nameInput)

    const promptInput = el('textarea', {
      className: 'sheet-textarea',
      name: 'prompt',
      placeholder: '到点要把这项提示交给 agent 执行……'
    })
    promptInput.value = local.prompt
    promptInput.addEventListener('input', () => {
      local.prompt = promptInput.value
    })
    body.append(el('p', { className: 'field-label' }, '任务内容'), promptInput)

    body.appendChild(el('p', { className: 'field-label' }, 'Agent'))
    const agentChips = el('div', { className: 'chips' })
    const paintAgentChips = () => {
      agentChips.replaceChildren()
      for (const name of agents) {
        const chip = el('button', { className: name === local.agent ? 'chip chip-active' : 'chip' }, name)
        chip.addEventListener('click', () => {
          local.agent = name
          paintAgentChips()
        })
        agentChips.appendChild(chip)
      }
    }
    paintAgentChips()
    body.appendChild(agentChips)

    body.appendChild(el('p', { className: 'field-label' }, '时间表'))
    const presetChips = el('div', { className: 'chips' })
    const scheduleInputs = el('div', { className: 'schedule-inputs' })
    const paintSchedule = () => {
      presetChips.replaceChildren()
      for (const entry of PRESETS) {
        const chip = el('button', { className: entry.key === local.preset ? 'chip chip-active' : 'chip' }, entry.label)
        chip.addEventListener('click', () => {
          local.preset = entry.key
          paintSchedule()
        })
        presetChips.appendChild(chip)
      }
      paintScheduleInputs(scheduleInputs, local)
    }
    paintSchedule()
    body.append(presetChips, scheduleInputs)

    body.appendChild(
      el('p', { className: 'field-label' }, editing ? '工作区（不可改，需删除重建）' : '工作区')
    )
    if (editing) {
      body.appendChild(
        el(
          'p',
          { className: 'muted small' },
          editingWorkspace
            ? `${names.get(editingWorkspace.repoId) ?? ''} / ${editingWorkspace.displayName ?? editingWorkspace.branch ?? editingWorkspace.path}`
            : editing.workspaceMode === 'new_per_run'
              ? '每次运行新建工作区'
              : editing.workspaceId ?? '—'
        )
      )
    } else {
      const modeChips = el('div', { className: 'chips' })
      const workspaceOptions = el('div', { className: 'ws-options' })
      const paintWorkspace = () => {
        modeChips.replaceChildren()
        for (const entry of [
          { key: 'existing', label: '沿用现有工作区' },
          { key: 'new_per_run', label: '每次新建工作区' }
        ]) {
          const chip = el('button', { className: entry.key === local.mode ? 'chip chip-active' : 'chip' }, entry.label)
          chip.addEventListener('click', () => {
            local.mode = entry.key
            paintWorkspace()
          })
          modeChips.appendChild(chip)
        }
        workspaceOptions.replaceChildren()
        if (local.mode === 'existing') {
          for (const row of managedRows) {
            workspaceOptions.appendChild(
              radioRow(
                'automation-workspace',
                activeWorkspace() === row.worktreeId,
                `${names.get(row.repoId) ?? '其他'} / ${row.displayName ?? row.branch ?? row.path}`,
                () => {
                  local.workspace = row.worktreeId
                  paintWorkspace()
                }
              )
            )
          }
          if (managedRows.length === 0) {
            workspaceOptions.appendChild(el('p', { className: 'muted small' }, '没有可用的工作区，改成"每次新建"试试。'))
          }
        } else {
          for (const [id, label] of repoOptions) {
            workspaceOptions.appendChild(
              radioRow('automation-repo', activeRepo() === id, label, () => {
                local.repo = id
                paintWorkspace()
              })
            )
          }
        }
      }
      paintWorkspace()
      body.append(modeChips, workspaceOptions)
    }

    body.appendChild(error)

    const save = async () => {
      if (local.busy) return
      if (!local.agent) {
        error.textContent = '选择一个 agent'
        error.hidden = false
        return
      }
      const rrule = buildRrule(local)
      if (!rrule) {
        error.textContent = '时间表没有可运行的时刻，请调整'
        error.hidden = false
        return
      }
      local.busy = true
      local.error = null
      error.hidden = true
      saveButton.disabled = true
      saveButton.textContent = '保存中…'
      try {
        if (editing) {
          await hub.request('automation.update', {
            id: editing.id,
            updates: {
              name: local.name.trim() || editing.name,
              agentId: local.agent,
              prompt: local.prompt.trim(),
              rrule
            }
          })
        } else {
          const selector =
            local.mode === 'existing'
              ? activeWorkspace()
                ? { workspace: `id:${activeWorkspace()}` }
                : null
              : activeRepo()
                ? { repo: `id:${activeRepo()}` }
                : null
          if (!selector) {
            error.textContent = local.mode === 'existing' ? '选择一个工作区' : '选择一个项目'
            error.hidden = false
            return
          }
          await hub.request('automation.create', {
            name: local.name.trim() || '定时任务',
            prompt: local.prompt.trim(),
            agentId: local.agent,
            rrule,
            dtstart: Date.now() + 5 * 60_000,
            timezone: Intl.DateTimeFormat().resolvedOptions().timeZone ?? 'Asia/Shanghai',
            enabled: true,
            ...selector
          })
        }
        closeEditor()
        await load()
      } catch (failure) {
        error.textContent = failure.message
        error.hidden = false
      } finally {
        local.busy = false
        saveButton.disabled = false
        saveButton.textContent = '保存'
      }
    }
    saveButton.addEventListener('click', () => void save())

    return el(
      'div',
      { className: 'sheet' },
      el(
        'div',
        { className: 'topnav' },
        el('button', { className: 'back', onclick: closeEditor }, '取消'),
        el('span', { className: 'sheet-title' }, editing ? '编辑定时' : '新建定时'),
        saveButton
      ),
      el('div', { className: 'rule' }),
      body
    )
  }

  // --- lifecycle --------------------------------------------------------
  document.title = '定时 · orca 工作台'
  paint()

  return {
    element: root,
    setStatus(status) {
      const wasConnected = state.status.gatewayConnected
      state.status = status
      if (status.gatewayConnected && !wasConnected) {
        void load()
        clearInterval(pollTimer)
        // The list loads once and the runs' dispatch→done lifecycle is never pushed,
        // so a slow tick is the only way the rows stay honest.
        pollTimer = setInterval(() => {
          if (state.status.gatewayConnected) void load()
        }, 60_000)
      } else if (!status.gatewayConnected && wasConnected) {
        clearInterval(pollTimer)
        pollTimer = null
      }
      paint()
    },
    destroy() {
      destroyed = true
      clearInterval(pollTimer)
      document.title = originalTitle || 'orca 工作台'
    }
  }
}

function paintScheduleInputs(container, local) {
  container.replaceChildren()
  const controls = el('div', { className: 'schedule-controls' })
  if (local.preset === 'daily' || local.preset === 'weekdays' || local.preset === 'weekly') {
    if (local.preset === 'weekly') {
      const select = el('select', { className: 'inline-select' })
      WEEKDAYS.forEach((label, index) => {
        const option = el('option', { value: String(index) }, label)
        if (index === local.weekday) option.selected = true
        select.appendChild(option)
      })
      select.value = String(local.weekday)
      select.addEventListener('change', () => {
        local.weekday = Number(select.value)
      })
      controls.appendChild(select)
    }
    const time = el('input', { type: 'time', className: 'inline-input' })
    time.value = local.time
    time.addEventListener('change', () => {
      local.time = time.value
    })
    controls.appendChild(time)
  } else if (local.preset === 'minutes') {
    const minutes = el('input', { type: 'number', min: 1, max: 59, className: 'inline-input' })
    minutes.value = String(local.minutes)
    minutes.addEventListener('change', () => {
      local.minutes = Number(minutes.value)
    })
    controls.append(minutes, el('span', { className: 'muted small' }, '分钟'))
  } else if (local.preset === 'hourly') {
    const minute = el('input', { type: 'number', min: 0, max: 59, className: 'inline-input' })
    minute.value = String(Number(local.time.split(':')[1] ?? 0))
    minute.addEventListener('change', () => {
      local.time = `00:${String(minute.value).padStart(2, '0')}`
    })
    controls.append(minute, el('span', { className: 'muted small' }, '每小时第 N 分钟'))
  } else if (local.preset === 'cron') {
    const cron = el('input', { className: 'inline-input', name: 'cron', placeholder: '0 9 * * *' })
    cron.value = local.cron
    cron.addEventListener('input', () => {
      local.cron = cron.value
    })
    controls.appendChild(cron)
  }
  container.appendChild(controls)
}

function radioRow(name, checked, label, onPick) {
  const input = el('input', { type: 'radio', name })
  input.checked = checked
  input.addEventListener('change', onPick)
  const row = el('label', { className: 'ws-option' }, input, el('span', {}, label))
  return row
}

/** Runs launch terminals, not structured sessions: a terminalPaneKey without the
 * structured prefix names no session TaskView can load, so the row stays unlinked. */
function runSessionId(run) {
  if (run.chatSessionId) return run.chatSessionId
  if (!run.terminalPaneKey) return null
  return sessionIdFromPaneKey(run.terminalPaneKey)
}

function canOpenRun(run) {
  return runSessionId(run) !== null
}

function runStatus(run) {
  if (run.status.startsWith('skipped')) return '跳过'
  return RUN_STATUS[run.status] ?? run.status
}

export function describeRrule(rrule) {
  const parts = String(rrule ?? '').trim().split(/\s+/)
  if (parts.length === 5) {
    const [minute, hour, dom, month, dow] = parts
    if (/^\*\/\d+$/.test(minute) && hour === '*' && dom === '*' && month === '*' && dow === '*') {
      return `每 ${minute.slice(2)} 分钟`
    }
    if (/^\d+$/.test(minute) && hour === '*' && dom === '*' && month === '*' && dow === '*') {
      return `每小时 ${minute} 分`
    }
    if (/^\d+$/.test(minute) && /^\d+$/.test(hour) && dom === '*' && month === '*') {
      const clock = `${hour.padStart(2, '0')}:${minute.padStart(2, '0')}`
      // cron accepts 7 as Sunday too; both 0 and 7 land on the same label.
      const day =
        dow === '*'
          ? '每天'
          : dow === '1-5'
            ? '工作日'
            : /^\d$/.test(dow)
              ? (WEEKDAYS[Number(dow) % 7] ?? dow)
              : dow
      return `${day} ${clock}`
    }
  }
  return rrule
}

function presetOf(rrule) {
  const parts = String(rrule ?? '').trim().split(/\s+/)
  if (parts.length !== 5) return 'cron'
  const [minute, hour, dom, month, dow] = parts
  if (dom === '*' && month === '*') {
    if (/^\*\/\d+$/.test(minute) && hour === '*') return 'minutes'
    if (hour === '*' && /^\d+$/.test(minute)) return 'hourly'
    if (/^\d+$/.test(minute) && /^\d+$/.test(hour)) {
      if (dow === '*') return 'daily'
      if (dow === '1-5') return 'weekdays'
      if (/^\d$/.test(dow)) return 'weekly'
    }
  }
  return 'cron'
}

function clockFromRrule(rrule) {
  const parts = String(rrule ?? '').trim().split(/\s+/)
  if (parts.length === 5 && /^\d+$/.test(parts[0]) && /^\d+$/.test(parts[1])) {
    return `${parts[1].padStart(2, '0')}:${parts[0].padStart(2, '0')}`
  }
  return '09:30'
}

function dayFromRrule(rrule) {
  const dow = String(rrule ?? '').trim().split(/\s+/)[4] ?? ''
  if (!/^\d$/.test(dow)) return 1
  return Number(dow) % 7
}

function intervalFromRrule(rrule) {
  const step = /^\*\/(\d+)$/.exec(String(rrule ?? '').trim().split(/\s+/)[0] ?? '')
  return step ? Number(step[1]) : 30
}

/** Each preset must produce a cron Orca accepts (five fields, real occurrences). */
function buildRrule(input) {
  const [hourText, minuteText] = String(input.time ?? '').split(':')
  const minute = Number(minuteText ?? 0)
  const hour = Number(hourText ?? 0)
  if (input.preset === 'daily' && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
    return `${minute} ${hour} * * *`
  }
  if (input.preset === 'weekdays' && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
    return `${minute} ${hour} * * 1-5`
  }
  if (input.preset === 'weekly' && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
    return `${minute} ${hour} * * ${input.weekday}`
  }
  if (input.preset === 'hourly' && minute >= 0 && minute <= 59) {
    return `${minute} * * * *`
  }
  // Orca rejects a minute step above the 60-value domain, so 1–59 is the legal span.
  if (input.preset === 'minutes' && Number.isInteger(input.minutes) && input.minutes >= 1 && input.minutes <= 59) {
    return `*/${input.minutes} * * * *`
  }
  if (input.preset === 'cron') {
    const value = String(input.cron ?? '').trim()
    return /^[0-9*\/,-]+(\s+[0-9*\/,-]+){4}$/.test(value) ? value : null
  }
  return null
}
