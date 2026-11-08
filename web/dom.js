// Tiny DOM helpers. Every text node goes through textContent (or a Text node),
// so agent output can never become markup.

/**
 * el('div', { className: 'x', onclick: fn, dataset: {k: 'v'} }, child...)
 * Attributes worth special-casing: className, textContent, dataset, style,
 * checked/value/disabled/selected, and on* listener names.
 */
export function el(tag, props = {}, ...children) {
  const node = document.createElement(tag)
  for (const [key, value] of Object.entries(props ?? {})) {
    if (value === undefined || value === null || value === false) continue
    if (key === 'className') {
      node.className = value
    } else if (key === 'textContent') {
      node.textContent = String(value)
    } else if (key === 'dataset') {
      Object.assign(node.dataset, value)
    } else if (key === 'style') {
      Object.assign(node.style, value)
    } else if (key.startsWith('on') && typeof value === 'function') {
      node.addEventListener(key.slice(2).toLowerCase(), value)
    } else if (key in node) {
      node[key] = value
    } else {
      node.setAttribute(key, value === true ? '' : String(value))
    }
  }
  append(node, children)
  return node
}

function append(node, children) {
  for (const child of children) {
    if (child === undefined || child === null || child === false) continue
    if (Array.isArray(child)) {
      append(node, child)
    } else if (child instanceof Node) {
      node.appendChild(child)
    } else {
      node.appendChild(document.createTextNode(String(child)))
    }
  }
}

export function clear(node) {
  node.replaceChildren()
  return node
}

/** Replaces the children of `node` with `children` in one shot. */
export function render(node, ...children) {
  node.replaceChildren()
  append(node, children)
  return node
}
