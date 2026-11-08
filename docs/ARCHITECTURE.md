# 架构

## 三个进程

| 进程 | 运行位置 | 职责 | 持有凭据 |
|---|---|---|---|
| `web/`（静态页） | 手机浏览器 | 登录、看板、任务详情、审批/提问、新建、中止 | 会话 cookie |
| `orca-server` | VPS | HTTP 登录、静态页、`/client` 与 `/gateway` 两个 WebSocket、id 重映射与流广播 | 密码 hash、gateway key |
| `orca-gateway` | 家里 Windows，与桌面 Orca 同机 | 主动外连 hub、一条 loopback 连到 Orca、E2EE v2、**Qt Widgets 配置窗口**（同进程） | gateway key、Orca device token |

TLS 由 VPS 前的 Caddy 终止；server 本身只听 `127.0.0.1`。gateway 侧需要真正的 WSS 客户端（OpenSSL）。
gateway 不再监听 `127.0.0.1:8790`：配置、配对、状态全部在 Qt 窗口内完成，本地 HTTP 攻击面归零。

## 模块

```
protocol/   24 字段 transcript、canonical JSON、wire 帧、17 项能力集、E2EE v2 帧几何与明文握手校验；P1-c 起补 secretbox 封包
common/     version / log / args / 平台路径 / 纯 C++ SHA-256、HMAC、HKDF、base64
server/     http_app / sessions / hub / static_files / auth
gateway/    main + ui/{main_window, config_store, pairing, status_model}；P4 起补 hub_link / orca_bridge
web/        index.html + styles.css + ES modules（无构建、无框架）
tests/      unit / vectors / integration
packaging/  deb / windows / caddy
```

## 最重要的一条：JSON 透传

原项目里 `mutation envelope`（canonical JSON + SHA-256 指纹、`clientOperationId`、`expectedRuntimeFence`）**在浏览器中构造**，
`agentSession.*` / `automation.*` 的 `method` / `params` / `result` 在 server 与 gateway 里全部当作不透明 JSON 搬运，
只做 id 重映射。因此 C++ 后端**不需要**实现 canonicalize、指纹、fence 语义或 create 重试。

必须由 C++ 精确实现的是：E2EE v2 密码学、两条 wire 协议、hub 状态机、会话/鉴权/限流、配对码解码、
能力集重播、部署契约。详见 `docs/PROTOCOL.md`。

## 信任边界（沿用原项目）

| 段 | 加密 | 谁能看到明文 |
|---|---|---|
| 手机 ↔ VPS | TLS | 手机、VPS |
| VPS 进程内 | — | VPS 能看到明文 RPC（v1 明确接受的取舍） |
| gateway ↔ Orca | E2EE v2 | 家里机器、Orca |

## 阶段

| P | 交付 | 门禁 |
|---|---|---|
| P0 | 骨架、双平台构建、体积门禁、CI | Windows / Linux 编译 + 测试 + 体积报告 |
| P1-a | 免依赖密码学基座、24 字段 transcript、canonical JSON、wire 帧、golden 向量 | **已完成**：6/6 CTest，与 JS 独立实现逐字节一致 |
| P1-b | E2EE v2 免依赖切片：能力集、header/nonce/计数器、明文握手帧与 strict ready 校验 | **已完成**：protocol 117 项断言，逐字节比对 JS 第二实现 |
| P1-c | X25519 派生 + secretbox 封包 + scrypt | 待 libsodium / 静态 OpenSSL 就位 |
| P2 | Orca JSON-RPC | 真实 Orca：`status.get` / `project.list` / `worktree.ps` |
| P3 | server 全量 | 本地集成 + `/api/health` |
| P4 | gateway 后台全量（hub link / Orca bridge / 订阅重放） | mock hub + 真实 Orca，断线重连不泄漏 Orca socket |
| P4-UI | Qt Widgets 配置窗口骨架 | **已完成**：动态 Qt 编译 + offscreen smoke，`gateway_core` 单测，7/7 CTest |
| P5 | 零构建原生前端 | 390×844 下完整写入周期 |
| P6 | 打包 + 全链路联调 | 真实 VPS + Orca + 手机端到端 |

## 已知风险

1. `nacl.box.before` 是 `HSalsa20(X25519(shared))`，libsodium 必须用 `crypto_box_beforenm`；直接用
   `crypto_scalarmult` 会派生出完全不同的密钥。
2. E2EE v2 transcript 是 24 个定长字段拼接（1 + 11 + 12）、header 42B / nonce 24B、双向独立 uint64 计数器；
   偏移错误表现为"对端无响应"，必须靠 P1 的固定向量测试定位。
3. Windows 上 OpenSSL 默认不加载系统根证书，gateway 的 WSS 需要显式 CA bundle 或注入系统 store。
4. 无框架前端的状态同步（输入框草稿、`<details>` 展开态、审批局部状态、markdown XSS）是 P5 主要工作量。
5. 静态 CRT（`/MT`）要求 vcpkg 使用 `x64-windows-static`；混用 `x64-windows`（动态 CRT）会链接冲突。
   本机 `x64-windows`（动态 Qt 6.11.2）与 `x64-windows-static`（静态 Qt 6.11.3）均已安装。
6. **Qt 静态构建**：`qtbase` 在 `x64-windows-static` 上从源码编译，首次约 1-2 小时；
   CI/release 依赖 `VCPKG_DEFAULT_BINARY_CACHE` 复用产物。静态链接还需履行 Qt LGPLv3 的
   可重链接义务（提供目标文件或改用商业授权）——发布前必须确认合规路径。
7. **静态 Qt 平台插件**：不能从 DLL 自动发现，必须在 `main.cpp` 用
   `Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)` 且 CMake 链接 `Qt6::QWindowsIntegrationPlugin`。
   该分支已于 2026-11-07 静态编译并真实弹窗验证（标题「orca 网关管理」）。
8. **静态 Qt 与 host 工具版本差**：vcpkg 静态 triplet 不装 `Qt6*Tools` 包，moc/rcc 来自
   动态 host triplet；两者 patch 版本不同（host 6.11.2 / port 6.11.3）会被 Qt 的包版本
   检查拒绝，需 `QT_NO_PACKAGE_VERSION_CHECK` 放行（消费者侧在 `cmake/Dependencies.cmake`，
   port 侧在本机 vcpkg 的 `windows.cmake` 补丁）。host 与 target 版本一致时该开关无副作用。
9. **体积门禁实测回填**：静态 Qt（vcpkg 默认 feature，含静态 ICU + OpenSSL）实测
   50.3 MiB，远超原 24 MiB 估算（ICU 主因，约 25-30 MB）。P4-a 的 beast 出站客户端再增
   约 5.7 MiB，P2 Orca 桥再约 1.2 MB（实测 57.1 MiB），门禁已回填 60 MiB；瘦身首选裁剪 qtbase feature
   （先 `-icu`），CI 需同步。
