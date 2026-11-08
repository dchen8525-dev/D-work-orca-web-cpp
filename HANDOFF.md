# HANDOFF — 当前进展交接

> 生成时间:2026-11-08 10:1x GMT+8(第四轮,接同日上午的第三轮)
> 仓库:`D:\work\orca-cpp`(分支 `main`,远端 `git@github.com:dchen8525-dev/D-work-orca-web-cpp.git`)
> 本文件是**当前状态的权威快照**;`README.md` 是设计说明,协议细节以 `docs/PROTOCOL.md` 为准。
> 原项目 `D:\work\orca-web` 保持只读参考,不要改动。

---

## 0. 一句话状态

**P4-a(gateway 出站 hub 客户端)已完成并全链路验证**:编译、回环 live 测试 8/8、
ctest 13/13、`build.ps1` 退出 0、真实本地 `orca-server` 冒烟(UI 状态卡显示「已连接」)。
当前 HEAD 为本次 P4-a 收尾提交,共 9 个本地 commit,**全部未 push**;远端 `main` 仍是 `f11dc93`。
下一步是 **P4-b + P2:真实 Orca JSON-RPC 桥**(见 §2)。

---

## 1. 已完成里程碑(全部已提交、已验证)

| 里程碑 | commit | 内容 | 验证 |
|---|---|---|---|
| 静态交付配置 | `0b415e5` | 静态 Qt 单 exe,门禁回填 | 空目录独立运行 + 真实弹窗 + ctest |
| P1-c E2EE | `a8efa8b` | `box_crypto` + `e2ee_session` 计数器会话 | tweetnacl 跨实现向量逐字节对齐 |
| deb 打包 | `8226b2b` | `packaging/build-deb.sh` + systemd unit + conffile | VPS 构建 233.8 KiB;dpkg 冲突保护实测 |
| P3-a hub | `72636bc` | HTTP API 五件套 + WS 双端 + scrypt 登录 + 会话存储 + `--init-config` + 限速/CSP | ctest 11/11、HTTP 10 轮、Node WS 端到端 9/9 |
| **P4-a hub 客户端** | 本次提交 | 出站 WS 客户端 + hello/ping/pong/request 转发 + 退避重连 + UI 真实状态接线 | live 8/8、ctest 13/13、真实 hub UI 冒烟 |

提交栈(main,9 个 commit,均未 push):
```
HEAD     P4-a: finish gateway hub client — WS upgrade, clean shutdown, live test
f785e72  P4-a (WIP): gateway outbound hub client
72636bc  P3-a: real hub service
e39a7fa  Fix GCC warnings surfaced by the VPS deb build
8226b2b  Add plain dpkg-deb packaging for the hub
a8efa8b  P1-c: E2EE v2 + tweetnacl golden vectors
0b415e5  Verify static delivery config end-to-end; size gate 56 MiB
5b37d23  Docs: Qt version + windeployqt
1d0cf8a  Qt Widgets gateway config UI + static-Qt build path
f11dc93  ← 远端 main 在这里
```

---

## 2. 后端任务安排(按优先级,接手后照此推进)

### 🔴 P4-b + P2:Orca JSON-RPC 桥(让 orcaState 卡片变真)
1. 读原项目 `src/gateway/orca/rpc-client.ts` + `agent-session.ts` + `local-pairing.ts`,
   实现 C++ `OrcaBridge` 真实版(loopback 连接桌面 Orca,E2EE v2 会话已就绪于 P1-c)。
2. `orca-state` 变更事件推送上 hub(hub_client.cpp 的 `handleBridgeEvent` 已留好挂点)。
3. 配对码解析(`gateway/ui/pairing.cpp` 已有)+ device token 接入。
4. 注意 `NullOrcaBridge` 是当前唯一实现,`request` 恒返回 `orca_unavailable`;
   换实现时 live 测试要同步改成注入假 bridge,保持可测。

### 🟡 P5:零构建前端
`web/` 只有方案文档。按 `web/README.md` 的硬约束写 `index.html`+ES modules+
`styles.css`(复用原 1783 行 CSS);server 侧静态文件服务已就绪(http_app serveStatic)。

### 🟡 P6:打包/发布/切换
1. **VPS 重打 deb**(P3-a 代码后的版本):`git archive HEAD | ssh tar` →
   `setup-deps.sh` → `build-deps` → `ORCA_VERSION=0.0.1 bash packaging/build-deb.sh` →
   scp 回 `dist/`。流程已验证(见 §5)。
2. **VPS 切换演练**(危险!):备份 `/etc/orca-web/server.json` + `domain.txt` →
   stop 旧 `orca-web-server 0.1.5` → dpkg -i 新包 → enable → 联调。**旧包是生产环境,
   切换前必须用户点头**。
3. release.yml 静态流水线首跑;产物命名 `orca-gateway-<版本>-windows-amd64.exe` +
   `orca-server-<版本>-linux-amd64.deb` + `.sha256`。
4. LGPLv3 合规确认(静态 Qt);wss 证书加载(OpenSSL 默认不读 Windows 根证书库,
   见 `hub_client.cpp` 的注释)。

### 🟢 随时
- **push**:9 个 commit 全在本地,远端 main 是 P0 骨架。push 后 CI 会首跑静态流水线。
- 三个构建目录策略统一(build/ 交付、build-ui/ 动态 Qt 验证、build-noqt/ 骨架)。

---

## 3. P4-a 完成记录(接手参考)

### 3.1 收尾时修掉的三个真问题

| 问题 | 症状 | 修法 |
|---|---|---|
| **缺 WS 客户端升级** | TCP/TLS 连上后直接把 hello 当裸 WS 帧写出,hub 看不到 HTTP Upgrade,写操作立刻 operation_aborted | 新增 `startWsHandshake()`:对 `TcpWs`/`SslWs` 调 `async_handshake(host, path)`,Host 带非默认端口 |
| boost 1.90 API 变更 | `basic_socket::async_connect(results, ...)` 已删除 | 改用自由函数 `asio::async_connect(*socket, results, handler)` |
| 关闭时挂死 | `stop()` 只 `io_.stop()`,socket 未关、work guard 未释放;对端仍有 pending read,测试 join 永久阻塞 | `stop()` 向 io_ post:取消 ping/retry 定时器、`beast::get_lowest_layer(...).close()`、`workGuard_.reset()`,再 join;stream 在 join 后才析构 |

附带:`finishAttempt()` 在 `stopped_` 后直接返回(避免 stop 期间再排重连定时器);
retry 定时器改为成员 `retryTimer_`,可取消;删除未使用的 `kConnectTimeoutMs/kReadSlackMs`;
`onFrame` 的 `if constexpr` 补 `else`,消掉 C4702。

### 3.2 验证链(已全部跑过)

1. `cmake --build build --config Release --target orca_hub_client_live_tests` ✅
2. live 测试 8/8:hello 字段、hubConnected 快照、ping→pong、null bridge 的
   `orca_unavailable` 回程、断线重连第二次 hello、stop 不崩 ✅
3. `ctest` 13/13;`scripts/build.ps1` 退出 0 ✅
4. 真实冒烟:`orca-server --init-config` + `orca-gateway --config`,日志出现
   `hub: hub accepted gateway smoke-gw`,截图确认状态卡「hub 已连通 / 已连接」✅

### 3.3 体积门禁

- 静态 Qt gateway:beast 使实测从 50.3 MiB 涨到 **58,750,464 B(56.0 MiB)**,
  门禁 56 → **60 MiB**(`gateway/CMakeLists.txt`),README/BUILD/DEPENDENCIES/
  ARCHITECTURE 已同步。
- 动态 Qt 验证构建:exe **1,167,872 B(1.14 MiB)**,8 MiB 门禁不变。

---

## 4. 关键文件索引(P3/P4 新增)

| 路径 | 作用 |
|---|---|
| `server/`(include/orca/server + src) | hub:server_config / session_store / hub_core / http_app / server_runner |
| `gateway/net/` + `gateway/include/orca/gateway/net/` | hub_link_core(纯逻辑)/ hub_client(beast 客户端)/ orca_bridge(接口+null) |
| `gateway/ui/runtime_status_provider.hpp` | P4 状态源(Qt 窗口用) |
| `common/src/crypto/scrypt.cpp` | 登录密码哈希(与 passwords.ts 格式互通) |
| `tests/unit/hub_client_live_test.cpp` | 回环 beast hub fixture + 真实 HubClient 集成测试 |
| `tests/vectors/p1c-vectors.json` / `p3-vectors.json` | P1-c / P3 金标准向量 |
| `packaging/build-deb.sh` + `packaging/deb/` | deb 直打包(VPS 已验证) |
| `docs/PROTOCOL.md` §P1-c/P3-a | 逐模块对齐表与冒烟记录 |

---

## 5. 环境事实(累计验证)

- **本机**:VS 18 / MSVC 19.51,VS cmake(`.../Common7/IDE/.../CMake/bin/cmake.exe`,
  不在 Git Bash PATH);vcpkg `D:\work\vcpkg`(x64-windows-static 全量 + x64-windows
  含 boost-beast);构建目录 build/(交付)/ build-ui/(动态验证)/ build-noqt/。
- **代理坑**:MSBuild 对大小写重复代理变量抛 MSB6001 —— 只设大写;vcpkg 直连
  GitHub 40KB/s,走 VPS SOCKS 隧道 1MB/s(`ssh -i ~/.ssh/id_ed25519 -D 1080 -N
  root@144.34.186.164`,密钥免密已配好;**VPS 密码绝不落盘**)。
- **TaskStop 杀后台 PowerShell 时 vcpkg 子进程会变孤儿持锁**:杀
  `Get-Process vcpkg` + 删 `D:\work\vcpkg\installed\vcpkg\vcpkg-running.lock`。
- **VPS**(144.34.186.164,Ubuntu 26.04,2C/1G/13G):跑着**生产 hub**
  `orca-web-server 0.1.5`(Bun 版,unit `orca-web.service` active,生产网关会连入)。
  C++ deb 与它同名同路径,**切换日之前禁止在生产 VPS dpkg -i / stop**。
  `/root/orca-build` 有源码副本;构建依赖已装好。
- 已缓存的大下载:cmake-4.4.4、PowerShell-7.6.9、qtbase-6.11.3、openssl-3.6.5
  均在 `D:\work\vcpkg\downloads`。

---

## 6. 约束(不要做)

- 不要恢复 `127.0.0.1:8790` 的 HTML 管理页,不要加「检查更新」/自更新。
- 不要把 VPS 凭据写进仓库、文档、脚本、日志或记忆;SSH 一律密钥免密。
- 不要改 `D:\work\orca-web`(只读参考)。
- 不要绕过「编译失败但 ctest 跑旧二进制」的假绿门禁:构建先看 BUILD_EXIT
  (build.ps1 已内置 throw)。
- 新增依赖前先说明用途与影响,由用户决定。
- **生产 VPS 上的旧 hub 在切换日之前不许动**(dpkg -i / systemctl stop 都不行)。
