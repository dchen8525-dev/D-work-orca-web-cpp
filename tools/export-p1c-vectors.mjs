// One-off golden vector generator for P1-c (libsodium-backed E2EE v2 sealing).
// Uses tweetnacl from the ORIGINAL project's node_modules as the independent
// second implementation, so the C++ side is pinned byte-for-byte to real
// cross-implementation output (p1-vectors.json could not cover secretbox:
// Node's builtin crypto has no XSalsa20-Poly1305).
//
// Run once on a dev machine; the JSON is committed and the build never needs
// Node:
//
//   node tools/export-p1c-vectors.mjs tests/vectors/p1c-vectors.json tests/vectors/p1-vectors.json

import { createHash, hkdfSync } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
// Read-only reference project: only node_modules/tweetnacl is loaded, nothing
// in D:/work/orca-web is modified.
const nacl = require('D:/work/orca-web/node_modules/tweetnacl/nacl-fast.js');

const hex = (b) => Buffer.from(b).toString('hex');
const unhex = (s) => Buffer.from(s, 'hex');
const utf8 = (s) => Buffer.from(s, 'utf8');
const sha256 = (b) => createHash('sha256').update(b).digest();
const seq = (start, length) => Buffer.from(Array.from({ length }, (_, i) => (start + i) & 0xff));
const b64 = (b) => Buffer.from(b).toString('base64');
const u64be = (value) => {
  if (value < 0n || value > 0xffffffffffffffffn) throw new Error('bad counter');
  const out = Buffer.alloc(8);
  out.writeBigUInt64BE(value, 0);
  return out;
};

// Byte layout mirrored from e2ee-v2-client.ts (already proven against
// p1-vectors.json sealFrames by the C++ protocol_test).
const SESSION_ID_LENGTH = 32;
const HEADER_LENGTH = 42;
const NONCE_LENGTH = 24;
const FRAME_VERSION = 2;

const encodeHeader = (sessionId, direction, kind, counter) =>
  Buffer.concat([Buffer.from(sessionId), Buffer.from([direction, kind]), u64be(counter)]);
const encodeNonce = (sessionId, direction, kind, counter) =>
  Buffer.concat([
    Buffer.from(sessionId.subarray(0, 12)),
    Buffer.from([FRAME_VERSION, direction, kind, 0]),
    u64be(counter),
  ]);

// ---------------------------------------------------------------------------
// 1. X25519 / box priming vectors (canonical NaCl test pair).
//    Shared secret inputs are hardcoded; every public key and the primed
//    shared secret are COMPUTED here so a typo cannot poison the vector.
// ---------------------------------------------------------------------------
const aliceSecretKey = unhex('77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a');
const bobSecretKey = unhex('5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb');
const alicePublicKey = nacl.box.keyPair.fromSecretKey(new Uint8Array(aliceSecretKey)).publicKey;
const bobPublicKey = nacl.box.keyPair.fromSecretKey(new Uint8Array(bobSecretKey)).publicKey;
const rawScalarAlice = nacl.scalarMult(new Uint8Array(aliceSecretKey), new Uint8Array(bobPublicKey));
const primed = nacl.box.before(new Uint8Array(bobPublicKey), new Uint8Array(aliceSecretKey));

// ---------------------------------------------------------------------------
// 2. secretbox (XSalsa20-Poly1305) vector: canonical NaCl firstkey test.
//    nacl.secretbox output layout = MAC(16) || ciphertext, identical to
//    libsodium's crypto_secretbox_easy.
// ---------------------------------------------------------------------------
const sbKey = unhex('1b27556473e985d462cd51197a9a46c76009549eac6474f206c4ee0844f68389');
const sbNonce = unhex('69696ee955b62b73cd62bda875fc73d68219e0036b7a0b37');
const sbPlaintext = utf8(
  "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it."
);
const sbCiphertext = nacl.secretbox(new Uint8Array(sbPlaintext), new Uint8Array(sbNonce), new Uint8Array(sbKey));

// ---------------------------------------------------------------------------
// 3. Key schedule + sealed frames from the P1 vector inputs. Loading
//    p1-vectors.json (instead of rebuilding the transcript here) keeps the two
//    vector files provably consistent; the schedule below must reproduce p1's
//    three derived values, which the script asserts.
// ---------------------------------------------------------------------------
const p1Path = process.argv[3] ?? 'tests/vectors/p1-vectors.json';
const p1 = JSON.parse(readFileSync(p1Path, 'utf8'));
const sharedSecret = unhex(p1.input.sharedSecretHex);
const clientNonce = unhex(p1.input.clientNonceHex);
const hostNonce = unhex(p1.input.hostNonceHex);
const transcript = unhex(p1.transcriptHex);
if (!transcript.equals(unhex(p1.transcriptHex))) throw new Error('transcript round-trip');

const transcriptHash = sha256(transcript);
const salt = sha256(Buffer.concat([utf8('orca-mobile-e2ee/v2/salt\0'), clientNonce, hostNonce]));
const info = Buffer.concat([utf8('orca-mobile-e2ee/v2/session\0'), transcriptHash]);
const expanded = Buffer.from(hkdfSync('sha256', sharedSecret, salt, info, 96));
const mobileToDesktopKey = expanded.subarray(0, 32);
const desktopToMobileKey = expanded.subarray(32, 64);
const sessionId = expanded.subarray(64, 96);

for (const [name, mine, theirs] of [
  ['mobileToDesktopKey', mobileToDesktopKey, p1.mobileToDesktopKeyHex],
  ['desktopToMobileKey', desktopToMobileKey, p1.desktopToMobileKeyHex],
  ['sessionId', sessionId, p1.sessionIdHex],
]) {
  if (hex(mine) !== theirs) throw new Error(`schedule mismatch vs p1-vectors: ${name}`);
}

const sealFrame = (direction, kind, counter, plaintext) => {
  const nonce = encodeNonce(sessionId, direction, kind, counter);
  const body = Buffer.concat([encodeHeader(sessionId, direction, kind, counter), plaintext]);
  const box = nacl.secretbox(new Uint8Array(body), new Uint8Array(nonce), new Uint8Array(
    direction === 0 ? mobileToDesktopKey : desktopToMobileKey
  ));
  return Buffer.concat([nonce, Buffer.from(box)]);
};

const plaintextM2D0 = utf8('orca-cpp p1c seal vector 0');
const plaintextM2D1 = utf8('orca-cpp p1c seal vector 1');
const plaintextM2DBin = seq(0xa0, 16);
const plaintextD2M0 = utf8('orca-cpp p1c open vector 0');
const plaintextD2M5 = utf8('replay me');

const vectors = {
  source: 'tweetnacl (D:/work/orca-web node_modules) via tools/export-p1c-vectors.mjs',
  note: 'One-off cross-implementation vectors for the libsodium-backed P1-c slice. Not part of the build.',
  box: {
    aliceSecretKeyHex: hex(aliceSecretKey),
    bobSecretKeyHex: hex(bobSecretKey),
    alicePublicKeyHex: hex(alicePublicKey),
    bobPublicKeyHex: hex(bobPublicKey),
    scalarMultHex: hex(rawScalarAlice),
    beforenmHex: hex(primed),
  },
  secretbox: {
    keyHex: hex(sbKey),
    nonceHex: hex(sbNonce),
    plaintextHex: hex(sbPlaintext),
    ciphertextHex: hex(sbCiphertext),
  },
  schedule: {
    sharedSecretHex: hex(sharedSecret),
    clientNonceHex: hex(clientNonce),
    hostNonceHex: hex(hostNonce),
    transcriptHashHex: hex(transcriptHash),
    mobileToDesktopKeyHex: hex(mobileToDesktopKey),
    desktopToMobileKeyHex: hex(desktopToMobileKey),
    sessionIdHex: hex(sessionId),
  },
  frames: {
    m2dTextCounter0: {
      direction: 0,
      kind: 0,
      counterHex: '0000000000000000',
      plaintextHex: hex(plaintextM2D0),
      frameHex: hex(sealFrame(0, 0, 0n, plaintextM2D0)),
    },
    m2dTextCounter1: {
      direction: 0,
      kind: 0,
      counterHex: '0000000000000001',
      plaintextHex: hex(plaintextM2D1),
      frameHex: hex(sealFrame(0, 0, 1n, plaintextM2D1)),
    },
    m2dBinarySparse: {
      direction: 0,
      kind: 1,
      counterHex: '0102030405060708',
      plaintextHex: hex(plaintextM2DBin),
      frameHex: hex(sealFrame(0, 1, 0x0102030405060708n, plaintextM2DBin)),
    },
    d2mTextCounter0: {
      direction: 1,
      kind: 0,
      counterHex: '0000000000000000',
      plaintextHex: hex(plaintextD2M0),
      frameHex: hex(sealFrame(1, 0, 0n, plaintextD2M0)),
    },
    d2mTextCounter5: {
      direction: 1,
      kind: 0,
      counterHex: '0000000000000005',
      plaintextHex: hex(plaintextD2M5),
      frameHex: hex(sealFrame(1, 0, 5n, plaintextD2M5)),
    },
  },
};

if (process.argv[2] && process.argv[2].length > 2) {
  writeFileSync(process.argv[2], JSON.stringify(vectors, null, 2) + '\n');
  console.log('wrote ' + process.argv[2]);
  console.log('beforenm = ' + hex(primed));
} else {
  console.error('usage: node tools/export-p1c-vectors.mjs <out.json> <p1-vectors.json>');
  process.exit(1);
}
