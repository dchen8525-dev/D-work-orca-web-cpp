// Shared formatting helpers (mirrors the pure functions in Board.tsx / Automations.tsx).

export function relativeTime(timestamp) {
  const seconds = Math.max(0, Math.round((Date.now() - timestamp) / 1000))
  if (seconds < 60) return '刚刚'
  const minutes = Math.round(seconds / 60)
  if (minutes < 60) return `${minutes} 分钟前`
  const hours = Math.round(minutes / 60)
  if (hours < 24) return `${hours} 小时前`
  return `${Math.round(hours / 24)} 天前`
}

export function stateLabel(state) {
  switch (state) {
    case 'working':
      return '进行中'
    case 'attention':
      return '待确认'
    case 'idle':
      return '空闲'
    default:
      return '未知'
  }
}

export function toneForState(state) {
  if (state === 'working') return 'green'
  if (state === 'attention') return 'amber'
  return 'grey'
}

export function markForState(state) {
  if (state === 'working') return '✳'
  if (state === 'attention') return '!'
  return '·'
}

export function connectionTone(status) {
  if (!status.connected) return 'red'
  if (!status.gatewayConnected) return 'amber'
  return status.orcaState === 'connected' ? 'green' : 'amber'
}

export function connectionLabel(status) {
  if (!status.connected) return '正在连接服务器…'
  if (!status.gatewayConnected) return '家里电脑未连接'
  switch (status.orcaState) {
    case 'connected':
      return '已连接'
    case 'connecting':
      return 'Orca 连接中'
    case 'disconnected':
      return 'Orca 已断开'
    default:
      return '已连接'
  }
}

export function orcaNote(orcaState) {
  switch (orcaState) {
    case 'connected':
      return null
    case 'connecting':
      return '正在连接'
    default:
      return '已断开'
  }
}

// The board shows one readable line, not the agent's markdown source.
export function plainText(value) {
  return String(value ?? '')
    .replace(/```[\s\S]*?```/g, ' ')
    .replace(/!\[[^\]]*\]\([^)]*\)/g, ' ')
    .replace(/\[([^\]]+)\]\([^)]*\)/g, '$1')
    .replace(/(^|\s)#{1,6}\s+/g, '$1')
    .replace(/(^|\s)>\s?/g, '$1')
    .replace(/(^|\s)[-*+]\s+/g, '$1')
    .replace(/(^|\s)\d+[.)]\s+/g, '$1')
    .replace(/`+/g, '')
    .replace(/\*\*|__/g, '')
    .replace(/(^|[\s(])\*([^*\n]+)\*/g, '$1$2')
    .replace(/\|/g, ' ')
    .replace(/\s+/g, ' ')
    .trim()
}

export function firstLine(value, limit = 120) {
  const line = plainText(value).slice(0, limit)
  return line ? `${line}${line.length >= limit ? '…' : ''}` : ''
}

export function shortId(sessionId) {
  return sessionId.length > 12 ? `${sessionId.slice(0, 5)}…${sessionId.slice(-4)}` : sessionId
}

export function shortLabel(value) {
  return value.length > 24 ? `${value.slice(0, 23)}…` : value
}

/** Renders in the automation's own zone: the schedule fires there, wherever the phone is. */
export function fmtStamp(ms, zone) {
  const date = new Date(ms)
  const options = zone ? { timeZone: zone } : {}
  const day = date.toLocaleDateString('zh-CN', { month: 'numeric', day: 'numeric', ...options })
  const time = date.toLocaleTimeString('zh-CN', {
    hour: '2-digit',
    minute: '2-digit',
    hour12: false,
    ...options
  })
  return `${day} ${time}`
}

export function sessionIdFromPaneKey(paneKey) {
  const match = /^structured-agent-session-(.+):[^:]+$/.exec(paneKey ?? '')
  return match ? match[1] : null
}

export function safeGet(item) {
  try {
    return localStorage.getItem(item)
  } catch {
    return null
  }
}

export function safeSet(item, value) {
  try {
    localStorage.setItem(item, value)
  } catch {
    // Private-mode WebViews throw; the preference simply does not stick.
  }
}

export function safeRemove(item) {
  try {
    localStorage.removeItem(item)
  } catch {
    // Same as above.
  }
}
