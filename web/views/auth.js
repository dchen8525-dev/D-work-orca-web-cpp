import { api } from '../api.js'
import { el } from '../dom.js'

export function loginView({ onSuccess }) {
  const password = el('input', {
    type: 'password',
    name: 'password',
    autoComplete: 'current-password',
    placeholder: 'password',
    'aria-label': '密码',
    autoFocus: true
  })
  const error = el('p', { className: 'error' })
  error.hidden = true
  const submit = el('button', { className: 'login-submit', type: 'submit', disabled: true }, '进入')
  password.addEventListener('input', () => {
    submit.disabled = password.value.length === 0
  })

  const form = el(
    'form',
    { className: 'login' },
    el('div', { className: 'login-kicker' }, 'orca-web · private edition'),
    el('h1', {}, 'Orca'),
    el('p', { className: 'login-tagline' }, '你的工作，在看得到的地方继续。'),
    password,
    error,
    submit,
    el('p', { className: 'login-fine' }, '只有一台机器、一个人。密码错误时请稍候再试。')
  )

  form.addEventListener('submit', async (event) => {
    event.preventDefault()
    submit.disabled = true
    submit.textContent = '登录中…'
    error.hidden = true
    try {
      if (await api.login(password.value)) {
        onSuccess()
        return
      }
      error.textContent = '密码不正确'
      error.hidden = false
    } catch {
      error.textContent = '无法连接到服务器'
      error.hidden = false
    } finally {
      submit.textContent = '进入'
      submit.disabled = password.value.length === 0
    }
  })
  return form
}

const PASSWORD_ERRORS = {
  invalid_credentials: '当前密码不对。',
  new_password_too_short: '新密码至少 8 位。',
  persist_failed: '服务器没把新密码写进配置，稍后再试。'
}

export function changePasswordSheet({ onClose }) {
  const current = el('input', { type: 'password', autoComplete: 'current-password' })
  const next = el('input', { type: 'password', autoComplete: 'new-password', placeholder: '至少 8 位' })
  const repeat = el('input', { type: 'password', autoComplete: 'new-password' })
  const error = el('p', { className: 'error' })
  error.hidden = true
  const save = el('button', { className: 'primary' }, '保存')

  const change = async () => {
    if (next.value.length < 8) {
      error.textContent = '新密码至少 8 位。'
      error.hidden = false
      return
    }
    if (next.value !== repeat.value) {
      error.textContent = '两次输入的新密码不一致。'
      error.hidden = false
      return
    }
    save.disabled = true
    save.textContent = '提交中…'
    error.hidden = true
    const result = await api.changePassword(current.value, next.value)
    save.disabled = false
    save.textContent = '保存'
    if (!result.ok) {
      error.textContent = PASSWORD_ERRORS[result.error] ?? '改密码失败，稍后再试。'
      error.hidden = false
      return
    }
    onClose()
  }
  save.addEventListener('click', () => void change())

  return el(
    'div',
    { className: 'sheet' },
    el(
      'div',
      { className: 'topnav' },
      el('button', { className: 'back', onclick: onClose }, '取消'),
      el('span', { className: 'sheet-title' }, '改登录密码'),
      save
    ),
    el('div', { className: 'rule' }),
    el(
      'div',
      { className: 'sheet-body' },
      el('p', { className: 'field-label' }, '当前密码'),
      current,
      el('p', { className: 'field-label' }, '新密码'),
      next,
      el('p', { className: 'field-label' }, '再输一遍'),
      repeat,
      el('p', { className: 'sheet-context' }, '保存后其他登录过的设备都会退出，需要在各自设备重新登录；本机保持登录。'),
      error
    )
  )
}
