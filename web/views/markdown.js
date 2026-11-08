// Dependency-free markdown subset for agent replies: fences, headings, lists,
// quotes, tables, inline code, bold/italic and links. Anything else renders as
// plain text. Everything is built with DOM nodes -- no innerHTML anywhere.

import { el } from '../dom.js'

const INLINE_PATTERN = /(`[^`\n]+`|\*\*[^*\n]+\*\*|\*[^*\n]+\*|\[[^\]]+\]\([^)\s]+\))/g

export function markdown(text) {
  const container = el('div', { className: 'md' })
  for (const block of parseBlocks(String(text ?? ''))) {
    container.appendChild(renderBlock(block))
  }
  return container
}

function renderBlock(block) {
  switch (block.type) {
    case 'code':
      return el('pre', { className: 'md-code', textContent: block.text })
    case 'heading':
      return el('p', { className: 'md-heading' }, ...inline(block.text))
    case 'item':
      return el('p', { className: 'md-item' }, el('span', { className: 'md-marker', textContent: block.marker }), ...inline(block.text))
    case 'quote':
      return el('p', { className: 'md-quote' }, ...inline(block.text))
    case 'table': {
      const head = el('thead', {}, el('tr', {}, block.head.map((cell) => el('th', {}, ...inline(cell)))))
      const body = el('tbody', {}, block.rows.map((row) => el('tr', {}, row.map((cell) => el('td', {}, ...inline(cell))))))
      return el('div', { className: 'md-table-wrap' }, el('table', { className: 'md-table' }, head, body))
    }
    default:
      return el('p', {}, ...inline(block.text))
  }
}

function parseBlocks(text) {
  const blocks = []
  const lines = text.split('\n')
  let paragraph = []

  const flush = () => {
    if (paragraph.length > 0) {
      blocks.push({ type: 'paragraph', text: paragraph.join('\n') })
      paragraph = []
    }
  }

  for (let index = 0; index < lines.length; index++) {
    const line = lines[index]
    if (line.startsWith('```')) {
      flush()
      const body = []
      index += 1
      while (index < lines.length && !lines[index].startsWith('```')) {
        body.push(lines[index])
        index += 1
      }
      blocks.push({ type: 'code', text: body.join('\n') })
      continue
    }
    const heading = /^(#{1,6})\s+(.*)$/.exec(line)
    if (heading) {
      flush()
      blocks.push({ type: 'heading', text: heading[2] })
      continue
    }
    if (line.trim().startsWith('|')) {
      const rows = []
      let cursor = index
      while (cursor < lines.length && lines[cursor].trim().startsWith('|')) {
        rows.push(lines[cursor])
        cursor += 1
      }
      if (rows.length >= 2 && isTableDivider(rows[1])) {
        flush()
        blocks.push({
          type: 'table',
          head: splitRow(rows[0]),
          rows: rows.slice(2).map(splitRow)
        })
        index = cursor - 1
        continue
      }
    }
    const bullet = /^\s*[-*+]\s+(.*)$/.exec(line)
    if (bullet) {
      flush()
      blocks.push({ type: 'item', marker: '•', text: bullet[1] })
      continue
    }
    const ordered = /^\s*(\d+)[.)]\s+(.*)$/.exec(line)
    if (ordered) {
      flush()
      blocks.push({ type: 'item', marker: `${ordered[1]}.`, text: ordered[2] })
      continue
    }
    const quote = /^>\s?(.*)$/.exec(line)
    if (quote) {
      flush()
      blocks.push({ type: 'quote', text: quote[1] })
      continue
    }
    if (line.trim() === '') {
      flush()
      continue
    }
    paragraph.push(line)
  }
  flush()
  return blocks
}

function splitRow(row) {
  return row
    .trim()
    .replace(/^\|/, '')
    .replace(/\|$/, '')
    .split('|')
    .map((cell) => cell.trim())
}

function isTableDivider(row) {
  const cells = splitRow(row)
  return cells.length > 0 && cells.every((cell) => /^:?-{2,}:?$/.test(cell))
}

function inline(text) {
  const nodes = []
  let last = 0
  for (const match of text.matchAll(INLINE_PATTERN)) {
    const token = match[0]
    const start = match.index ?? 0
    if (start > last) nodes.push(document.createTextNode(text.slice(last, start)))
    if (token.startsWith('`')) {
      nodes.push(el('code', { className: 'md-inline-code', textContent: token.slice(1, -1) }))
    } else if (token.startsWith('**')) {
      nodes.push(el('strong', {}, ...inline(token.slice(2, -2))))
    } else if (token.startsWith('[')) {
      const link = /^\[([^\]]+)\]\(([^)]+)\)$/.exec(token)
      // External links open in a new tab; rel keeps the opener reference private.
      nodes.push(
        el('a', { href: link?.[2] ?? '#', target: '_blank', rel: 'noreferrer noopener' },
          link?.[1] ?? token)
      )
    } else {
      nodes.push(el('em', {}, ...inline(token.slice(1, -1))))
    }
    last = start + token.length
  }
  if (last < text.length) nodes.push(document.createTextNode(text.slice(last)))
  return nodes
}
