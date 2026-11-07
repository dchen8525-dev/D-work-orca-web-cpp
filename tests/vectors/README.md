# tests/vectors

P1 的 E2EE v2 golden 向量目录。当前已有 `p1-vectors.json`（静态提交，6/6 CTest 通过；
`protocol` 用例内含 117 项逐字节 / 形状断言）。

向量由 `tools/export-vectors.mjs` **一次性**生成：它把旧 TypeScript 的
`encodeTranscript` / `deriveKeySchedule` / `canonicalize` / `encodeHeader` / `encodeNonce`
以及 `client-capabilities.ts`、`validateReady` 期望的帧形状，用 Node 内置 crypto 独立重写一遍，
固定 X25519 密钥材料、client/host nonce 与 sharedSecret。文件包含四组基线：

| 字段 | 内容 |
|---|---|
| `transcriptHex` / `hkdfExpandedHex` / `canonical` / `fingerprintHex` | 24 字段 transcript、salt、info、HKDF 96 字节、三个派生值、canonical JSON 与指纹 |
| `clientCapabilities` / `capabilitiesSha256Hex` | 17 项能力集，逐项 + JSON 数组 SHA-256 摘要 |
| `sealFrames.cases` | 5 组 42B header / 24B nonce，含 `MAX_COUNTER`；`minSealedFrameLength = 82` |
| `handshake` | `e2ee_hello` / `e2ee_ready` / `e2ee_auth` / `e2ee_authenticated` 帧形状与 transcriptHashB64 |

重新生成（仅开发机，需要 Node；**orca-cpp 的 configure / build / ctest / 运行都不需要 Node**）：

```bash
node tools/export-vectors.mjs tests/vectors/p1-vectors.json
```

约定：

- 向量是**只读基线**，改动必须连同 `docs/PROTOCOL.md` 与旧实现出处一起说明。
- 导出脚本只在开发机上用 Node 跑一次，**不进本仓库的构建链**：`orca-cpp` 的 configure / build /
  ctest 全都不依赖 Node。
- C++ 侧测试逐个字段比对，失败时打印第一个不一致的字节偏移，而不是笼统报"解密失败"。
