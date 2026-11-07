# 协议基线

本文件记录**必须与旧实现逐字节对齐**的常量与帧形状，来源是 `D:\work\orca-web` 的 TypeScript 源码
（只读参考）。P0 落地常量；P1-a / P1-b 的免依赖部分已实现下列编解码，并以 `tests/vectors/` 的 golden 向量做门禁。

## 常量（已在 `protocol/include/orca/protocol/versions.hpp`）

| 常量 | 值 | 来源 |
|---|---|---|
| `LINK_PROTOCOL_VERSION` | 1 | `src/shared/link-protocol.ts` |
| `WEB_PROTOCOL_VERSION` | 1 | `src/shared/web-protocol.ts` |
| `FRAME_VERSION` | 2 | `e2ee-v2-client.ts` |
| `SESSION_ID_LENGTH` | 32 | 同上 |
| `NONCE_LENGTH` | 24 | 同上 |
| `HEADER_LENGTH` | 42 = 32 + 1(direction) + 1(kind) + 8(uint64 counter) | 同上 |
| secretbox MAC | 16 | tweetnacl / libsodium |
| `PROTOCOL` | `orca-mobile-e2ee` | 同上 |
| `TRANSCRIPT_DOMAIN` | `orca-mobile-e2ee/v2/transcript` | 同上 |
| `SALT_LABEL` / `INFO_LABEL` | 含结尾 NUL 的字节串 | 同上（`TextEncoder()` 会编码 NUL） |

## 两条 wire 协议（一条 JSON 文本帧 = 一条消息）

**浏览器 ↔ hub**：浏览器发 `request` / `subscribe` / `unsubscribe` / `ping`；
hub 回 `welcome` / `request-result` / `stream-frame` / `stream-error` / `stream-reset` / `gateway-state` / `pong`。

**gateway ↔ hub**：gateway 首发 `hello{v, key, gatewayId, orcaState, orca?}`，之后
`request-result` / `stream-*` / `orca-state` / `ping` / `pong`；hub 发 `welcome` / `request` /
`subscribe` / `unsubscribe` / `ping` / `pong` / `reject`。

hub 是转发器不是翻译器：只维护 id 归属、订阅表和 gateway 状态，`method` / `params` / `result` / `error` 原样搬运。

## E2EE v2（P1 实现，逐字节对齐）

握手三帧（WS 文本帧）：
`e2ee_hello`（明文）→ `e2ee_ready`（明文，需校验 pinned host key / nonce 回显 / context / selection）
→ `e2ee_auth`（sealed，带 `transcriptHashB64` + `deviceToken`）→ `e2ee_authenticated`（sealed）。

密钥派生：

```
sharedSecret = crypto_box_beforenm(hostPublicKey, clientSecretKey)   // 不是裸 X25519！
transcript   = 24 个定长字段拼接（1 + 11 + 12）：[u32 nameLen][name][u32 valueLen][value] ...
transcriptHash = sha256(transcript)
salt         = sha256(SALT_LABEL || clientNonce || hostNonce)
info         = INFO_LABEL || transcriptHash
expanded     = HKDF-SHA256(sharedSecret, salt, info, 96)
  mobileToDesktopKey = expanded[0:32]
  desktopToMobileKey = expanded[32:64]
  sessionId          = expanded[64:96]
```

封包：`frame = nonce(24) || crypto_secretbox_easy(header(42) || plaintext, nonce, key)`，再 base64 作为 WS 文本。

```
header(42) = sessionId(32) || direction(1) || kind(1) || uint64BE(counter)
nonce(24)  = sessionId[0:12] || 0x02 || direction || kind || 0x00 || uint64BE(counter)
最短 frame = 24(nonce) + 16(MAC) + 42(header) = 82 字节
```

`direction`：0 = mobile→desktop，1 = desktop→mobile；`kind`：0 = text，1 = binary。
出入方向各一个独立计数器，nonce 必须**逐字节等于**按期望计数器重算的值，失步即整条连接作废并重连；
计数器到 `2^64-1` 后禁止回绕，必须重新握手。

## Orca JSON-RPC（P2 实现）

请求 `{id, deviceToken, method, params?}`（`params === undefined` 时必须省略该键），
响应 `{id, ok:true, result}|{id, ok:false, error}`；`_keepalive:true` 帧忽略。
握手成功后**每条连接**必须重播 `runtime.clientCapabilities.update`（**17 个**固定字符串，顺序不能变），
否则 `agentSession.*` 会被 Orca 拒绝。清单的唯一事实来源是
`src/gateway/orca/client-capabilities.ts` 的 `ORCA_WEB_CLIENT_CAPABILITIES`；早期调研笔记写的 18 个是错的。
C++ 侧常量在 `protocol/include/orca/protocol/capabilities.hpp`，并由 golden 向量的 SHA-256 摘要锁死顺序与内容。

## P1 门禁（免依赖部分已落地）

`tools/export-vectors.mjs` 是旧 TypeScript 逻辑的**独立第二实现**，一次性生成
`tests/vectors/p1-vectors.json` 后静态提交；脚本不参与 configure / build / ctest，
`orca-cpp` 的构建与运行不需要 Node。

C++ 侧 `tests/unit/protocol_test.cpp` 逐字段比对：24 字段 transcript（1317 字节）、
transcriptHash、salt、info、HKDF 96 字节、三个派生密钥/会话 id、canonical JSON 与指纹；
`tests/unit/crypto_test.cpp` 另用 SHA-256 / RFC 4231 / RFC 5869 标准向量验证纯 C++ 加密基座。
失败时打印第一个不一致的字节偏移。

### P1-b：已完成的免依赖切片

不需要 libsodium 的部分已经全部落地并接入同一套 golden 向量门禁：

| 模块 | 依据 | 门禁 |
|---|---|---|
| `capabilities.hpp/.cpp` | `client-capabilities.ts` 的 17 个字符串 + `rpc-client.ts` 的请求形状 | 逐项比对 + JSON 数组 SHA-256 摘要 vs JS 独立实现 |
| `seal_frame.hpp/.cpp` | `encodeHeader` / `encodeNonce` / `writeUint64` | 5 组 header(42B) + nonce(24B) 逐字节比对，含稀疏计数器与 `MAX_COUNTER`；`decodeHeader` 往返；常量时间 nonce/header 校验 |
| `e2ee_handshake.hpp/.cpp` | `e2ee_hello` / `validateReady` / `authText` / `isAuthenticatedControl` | 4 种帧形状比对 + 15 类变异 `e2ee_ready` 逐条拒绝 |

`validateReady` 里几个容易漏掉的细节，C++ 已按源码复刻：
`clientNonceB64` 是**字符串**比较而非解码后比较；`selection.payloadKinds.join()` 必须等于 `text,binary`；
`context.relayHostId` 缺省（`undefined`）与 `''` **不相等**；desktop 公钥/随机数必须过严格 base64
round-trip 且恰好 32 字节；最后用常量时间比较把 host key 钉在 pinned key 上（MITM 防线）。

`nlohmann::json` 会按 key 排序，而 `JSON.stringify` 保留插入顺序；这里只比较解析后的 JSON 语义，
因为 Orca 按 JSON 解析这些帧、transcript 也由具名字段构造而非由帧文本构造，键序不进任何哈希。

### P1-c：libsodium 背书的加密封装（已落地）

`tools/export-p1c-vectors.mjs` 用**原项目的 tweetnacl**（Node 内置 crypto 没有
XSalsa20-Poly1305，所以第一份向量文件只覆盖了纯 C++ 可测的 header/nonce）生成
`tests/vectors/p1c-vectors.json`：NaCl 经典 box 测试对（scalarmult / beforenm）、
canonical secretbox 密文，以及用 p1 向量输入派生出密钥表后封出的 **5 个完整 sealed
frame**。C++ 侧 `tests/unit/e2ee_session_test.cpp` 逐字节复现，并测会话语义：
双向独立计数器、replay / 乱序 / 篡改全部拒绝且不动计数器、`2^64-1` 拒绝回绕。

| 模块 | 依据 | 门禁 |
|---|---|---|
| `box_crypto.hpp/.cpp` | `crypto_scalarmult` / `crypto_box_beforenm` / `crypto_secretbox_easy`（libsodium） | NaCl 经典向量 + 5 个跨实现 sealed frame 逐字节 |
| `e2ee_session.hpp/.cpp` | `E2eeV2Client` 的 `seal`/`open` 计数器语义 | replay、乱序、篡改、短帧、方向错位、计数器回绕 |

仍需真实联调的部分：对着真机 Orca 的握手冒烟（P2/P4）。`EVP_PBE_scrypt` 在原项目
E2EE 路径中**零使用**，从 P1 范围移除；若后续配对流程需要再评估。
