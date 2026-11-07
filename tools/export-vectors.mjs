// One-off golden vector generator for P1. This is an INDEPENDENT second
// implementation of the TypeScript logic in
//   D:/work/orca-web/src/gateway/orca/e2ee-v2-client.ts
//   D:/work/orca-web/apps/web/src/envelope.ts
// It is run once on a dev machine and writes a static JSON file. It is NOT part
// of the configure/build/test chain: orca-cpp builds and runs without Node.
//
//   node tools/export-vectors.mjs tests/vectors/p1-vectors.json

import { createHash, hkdfSync } from 'node:crypto';
import { writeFileSync } from 'node:fs';
import { dirname } from 'node:path';
import { mkdirSync } from 'node:fs';

const SALT_LABEL = 'orca-mobile-e2ee/v2/salt\0';
const INFO_LABEL = 'orca-mobile-e2ee/v2/session\0';
const TRANSCRIPT_DOMAIN = 'orca-mobile-e2ee/v2/transcript';
const PROTOCOL = 'orca-mobile-e2ee';

const u32 = (n) => { const b = Buffer.alloc(4); b.writeUInt32BE(n, 0); return b; };
const utf8 = (s) => Buffer.from(s, 'utf8');
const numList = (xs) => Buffer.concat([u32(xs.length), ...xs.map(u32)]);
const strList = (xs) => Buffer.concat([u32(xs.length), ...xs.map((s) => Buffer.concat([u32(utf8(s).length), utf8(s)]))]);
const field = (name, value) => {
  const n = utf8(name);
  return Buffer.concat([u32(n.length), n, u32(value.length), value]);
};
const sha256 = (b) => createHash('sha256').update(b).digest();
const hex = (b) => Buffer.from(b).toString('hex');
const seq = (start, length) => Buffer.from(Array.from({ length }, (_, i) => (start + i) & 0xff));

// Fixed inputs so the C++ side can reproduce byte for byte.
const clientPublicKey = seq(0x01, 32);
const clientNonce = seq(0x21, 32);
const hostPublicKey = seq(0x41, 32);
const hostNonce = seq(0x61, 32);
const sharedSecret = seq(0x81, 32);
const transport = 'direct';
const relayHostId = '';

const framing = numList([2]);
const payloadKinds = strList(['text', 'binary']);
const ctxProtocol = utf8(PROTOCOL);
const ctxInitiator = utf8('mobile');
const ctxResponder = utf8('desktop');
const ctxTransport = utf8(transport);
const ctxRelay = utf8(relayHostId);

// 24 fields, in the exact order of encodeTranscript() in e2ee-v2-client.ts.
const transcript = Buffer.concat([
  field('domain', utf8(TRANSCRIPT_DOMAIN)),
  field('mobile-to-desktop.type', utf8('e2ee_hello')),
  field('mobile-to-desktop.version', u32(2)),
  field('mobile-to-desktop.client-public-key', clientPublicKey),
  field('mobile-to-desktop.client-nonce', clientNonce),
  field('mobile-to-desktop.capabilities.framing', framing),
  field('mobile-to-desktop.capabilities.payload-kinds', payloadKinds),
  field('mobile-to-desktop.context.protocol', ctxProtocol),
  field('mobile-to-desktop.context.initiator', ctxInitiator),
  field('mobile-to-desktop.context.responder', ctxResponder),
  field('mobile-to-desktop.context.transport', ctxTransport),
  field('mobile-to-desktop.context.relay-host-id', ctxRelay),
  field('desktop-to-mobile.type', utf8('e2ee_ready')),
  field('desktop-to-mobile.version', u32(2)),
  field('desktop-to-mobile.desktop-public-key', hostPublicKey),
  field('desktop-to-mobile.client-nonce-echo', clientNonce),
  field('desktop-to-mobile.desktop-nonce', hostNonce),
  field('desktop-to-mobile.selection.framing', u32(2)),
  field('desktop-to-mobile.selection.payload-kinds', payloadKinds),
  field('desktop-to-mobile.context.protocol', ctxProtocol),
  field('desktop-to-mobile.context.initiator', ctxInitiator),
  field('desktop-to-mobile.context.responder', ctxResponder),
  field('desktop-to-mobile.context.transport', ctxTransport),
  field('desktop-to-mobile.context.relay-host-id', ctxRelay),
]);

const transcriptHash = sha256(transcript);
const salt = sha256(Buffer.concat([utf8(SALT_LABEL), clientNonce, hostNonce]));
const info = Buffer.concat([utf8(INFO_LABEL), transcriptHash]);
const expanded = Buffer.from(hkdfSync('sha256', sharedSecret, salt, info, 96));

// Independent implementation of apps/web/src/envelope.ts canonicalize().
function canonicalize(value) {
  if (value === null || typeof value !== 'object') return JSON.stringify(value ?? null);
  if (Array.isArray(value)) return `[${value.map(canonicalize).join(',')}]`;
  const entries = Object.entries(value)
    .filter(([, v]) => v !== undefined)
    .sort(([l], [r]) => (l < r ? -1 : l > r ? 1 : 0));
  return `{${entries.map(([k, v]) => `${JSON.stringify(k)}:${canonicalize(v)}`).join(',')}}`;
}

const canonicalFields = { b: 2, a: 'x', c: null, d: [true, false, 'y'], e: { z: 1, a: 'n' } };
const canonicalInput = { method: 'agentSession.send', sessionId: 's-1', fields: canonicalFields };
const canonical = canonicalize(canonicalInput);
const fingerprint = hex(sha256(utf8(canonical)));

// 17 entries, verbatim from
// D:/work/orca-web/src/gateway/orca/client-capabilities.ts.
// The digest is over JSON.stringify(array): nlohmann::json::array().dump()
// produces the identical byte string for these plain-ASCII strings.
const clientCapabilities = [
  'agent-session.structured.v1',
  'agent-session.structured.claude.v1',
  'agent-session.structured.registered-agents.v1',
  'agent-session.structured.client-launch-mode.v1',
  'agent-session.options.optional-model.v1',
  'agent-session.pending-send-result.v1',
  'agent-session.turn-item.v1',
  'agent-session.current-requests.v1',
  'agent-session.commands-on-request.v1',
  'agent-session.coherent-model-catalog.v1',
  'agent-session.session-boundary.v1',
  'agent-session.background-task-stop.v1',
  'agent-session.background-task-row-stop.v1',
  'agent-session.background-task-child-views.v1',
  'agentSession.create.message.v1',
  'session-tabs.close-intent.v1',
  'session-tabs.authoritative-inventory.v1',
];
const capabilitiesSha256Hex = hex(sha256(utf8(JSON.stringify(clientCapabilities))));

// Independent reimplementation of encodeHeader / encodeNonce / writeUint64
// from e2ee-v2-client.ts. Uses Buffer.writeBigUInt64BE (a different primitive
// than the TS manual byte loop) so the C++ layout is checked against a second
// implementation rather than against a copy of itself.
const sealSessionId = expanded.subarray(64, 96);
function encodeHeader(sessionId, direction, kind, counter) {
  const b = Buffer.alloc(42);
  sessionId.copy(b, 0);
  b[32] = direction;
  b[33] = kind;
  b.writeBigUInt64BE(counter, 34);
  return b;
}
function encodeNonce(sessionId, direction, kind, counter) {
  const b = Buffer.alloc(24);
  sessionId.copy(b, 0, 0, 12);
  b[12] = 2;
  b[13] = direction;
  b[14] = kind;
  b[15] = 0;
  b.writeBigUInt64BE(counter, 16);
  return b;
}
const MAX_COUNTER = (1n << 64n) - 1n;
const sealCases = [
  { name: 'first mobile->desktop text', direction: 0, kind: 0, counter: 0n },
  { name: 'first desktop->mobile text', direction: 1, kind: 0, counter: 0n },
  { name: 'second mobile->desktop text', direction: 0, kind: 0, counter: 1n },
  { name: 'mobile->desktop binary, sparse counter', direction: 0, kind: 1, counter: 0x0102030405060708n },
  { name: 'desktop->mobile binary, MAX_COUNTER', direction: 1, kind: 1, counter: MAX_COUNTER },
].map((c) => ({
  name: c.name,
  direction: c.direction,
  kind: c.kind,
  counterHex: c.counter.toString(16).padStart(16, '0'),
  headerHex: hex(encodeHeader(sealSessionId, c.direction, c.kind, c.counter)),
  nonceHex: hex(encodeNonce(sealSessionId, c.direction, c.kind, c.counter)),
}));

// Plaintext E2EE v2 handshake frames, built the way the TS constructor /
// validateReady() expect them. relayHostId is deliberately OMITTED for the
// direct transport: sameContext() treats undefined and '' as different.
const b64 = (b) => Buffer.from(b).toString('base64');
const fixedDeviceToken = 'device-token-fixed';
const handshakeContext = { protocol: PROTOCOL, initiator: 'mobile', responder: 'desktop', transport };
const handshake = {
  clientPublicKeyB64: b64(clientPublicKey),
  clientNonceB64: b64(clientNonce),
  desktopPublicKeyB64: b64(hostPublicKey),
  desktopNonceB64: b64(hostNonce),
  transcriptHashB64: b64(transcriptHash),
  deviceToken: fixedDeviceToken,
  hello: {
    type: 'e2ee_hello',
    v: 2,
    clientPublicKeyB64: b64(clientPublicKey),
    clientNonceB64: b64(clientNonce),
    capabilities: { framing: [2], payloadKinds: ['text', 'binary'] },
    context: handshakeContext,
  },
  ready: {
    type: 'e2ee_ready',
    v: 2,
    desktopPublicKeyB64: b64(hostPublicKey),
    clientNonceB64: b64(clientNonce),
    desktopNonceB64: b64(hostNonce),
    selection: { framing: 2, payloadKinds: ['text', 'binary'] },
    context: handshakeContext,
  },
  auth: {
    type: 'e2ee_auth',
    v: 2,
    transcriptHashB64: b64(transcriptHash),
    deviceToken: fixedDeviceToken,
  },
  authenticated: {
    type: 'e2ee_authenticated',
    v: 2,
    transcriptHashB64: b64(transcriptHash),
  },
};

const vectors = {
  source: 'D:/work/orca-web e2ee-v2-client.ts + envelope.ts (independent JS reimplementation)',
  generatedBy: 'tools/export-vectors.mjs (one-off; NOT part of the build)',
  input: {
    clientPublicKeyHex: hex(clientPublicKey),
    clientNonceHex: hex(clientNonce),
    hostPublicKeyHex: hex(hostPublicKey),
    hostNonceHex: hex(hostNonce),
    sharedSecretHex: hex(sharedSecret),
    transport,
    relayHostId,
  },
  transcriptHex: hex(transcript),
  transcriptFieldCount: 24,
  transcriptHashHex: hex(transcriptHash),
  saltHex: hex(salt),
  infoHex: hex(info),
  hkdfExpandedHex: hex(expanded),
  mobileToDesktopKeyHex: hex(expanded.subarray(0, 32)),
  desktopToMobileKeyHex: hex(expanded.subarray(32, 64)),
  sessionIdHex: hex(expanded.subarray(64, 96)),
  canonicalInput,
  canonical,
  fingerprintHex: fingerprint,
  clientCapabilities,
  capabilitiesCount: clientCapabilities.length,
  capabilitiesSha256Hex,
  sealFrames: {
    sessionIdHex: hex(sealSessionId),
    headerLength: 42,
    nonceLength: 24,
    secretboxMacLength: 16,
    minSealedFrameLength: 24 + 16 + 42,
    maxCounterHex: MAX_COUNTER.toString(16).padStart(16, '0'),
    cases: sealCases,
  },
  handshake,
  base64: {
    empty: '',
    oneByte: hex(Buffer.from('a')),
    oneByteEncoded: Buffer.from('a').toString('base64'),
    twoBytes: hex(Buffer.from('ab')),
    twoBytesEncoded: Buffer.from('ab').toString('base64'),
    threeBytes: hex(Buffer.from('abc')),
    threeBytesEncoded: Buffer.from('abc').toString('base64'),
  },
};

const out = process.argv[2] ?? 'tests/vectors/p1-vectors.json';
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, JSON.stringify(vectors, null, 2) + '\n', 'utf8');
console.log(`wrote ${out} (${transcript.length} transcript bytes, ${expanded.length} HKDF bytes)`);
