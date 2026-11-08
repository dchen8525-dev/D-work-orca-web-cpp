// Orca's mutation fingerprint: sha256 over canonical JSON (sorted keys,
// undefined dropped). crypto.subtle needs a secure context: HTTPS or localhost,
// which is exactly how this app is meant to be reached.

export async function payloadFingerprint(input) {
  const canonical = canonicalize({
    method: input.method,
    sessionId: input.sessionId,
    fields: input.fields
  })
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(canonical))
  return [...new Uint8Array(digest)].map((byte) => byte.toString(16).padStart(2, '0')).join('')
}

export function operationId() {
  return `${Date.now()}-${crypto.randomUUID().replaceAll('-', '').toLowerCase()}`
}

export async function mutationEnvelope(args) {
  return {
    sessionId: args.sessionId,
    clientOperationId: operationId(),
    expectedRuntimeFence: args.fence,
    payloadFingerprint: await payloadFingerprint({
      method: args.method,
      sessionId: args.sessionId,
      fields: args.fields
    })
  }
}

function canonicalize(value) {
  if (value === null || typeof value !== 'object') {
    return JSON.stringify(value ?? null)
  }
  if (Array.isArray(value)) {
    return `[${value.map(canonicalize).join(',')}]`
  }
  const entries = Object.entries(value)
    .filter(([, entry]) => entry !== undefined)
    .sort(([left], [right]) => (left < right ? -1 : left > right ? 1 : 0))
  return `{${entries
    .map(([key, entry]) => `${JSON.stringify(key)}:${canonicalize(entry)}`)
    .join(',')}}`
}
