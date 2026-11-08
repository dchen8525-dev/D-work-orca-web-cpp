# HANDOFF — 当前进展交接

> 生成时间:2026-11-08 13:3x GMT+8(第六轮,P5 完成)
> 仓库:`D:\work\orca-cpp`(分支 `main`,远端 `git@github.com:dchen8525-dev/D-work-orca-web-cpp.git`)
> 本文件是**当前状态的权威快照**;`README.md` 是设计说明,协议细节以 `docs/PROTOCOL.md` 为准。
> 原项目 `D:\work\orca-web` 保持只读参考,不要改动。

---

## 0. 一句话状态

**P5 零构建前端已完成并真机联调**:`web/` 是可被 hub 直接托管的 ES modules
应用(登录/看板/任务/新建/定时/改密),真机 390×844 跑通回复、停止、建任务、
回答 AskUserQuestion、断线重连;同时修掉静态文件服务的三个真问题(Windows
路径、缺 Content-Type、`.webmanifest` MIME),新增 `server_http` 回归测试。
当前 HEAD `a400bf6`(另有 HANDOFF 文档 commit),13 个本地 commit,**全部未 push**;
远端 `main` 仍是 `f11dc93`。下一步 **P6 打包/发布/切换**(见 §2)。

---

## 1. 已完成里程碑(全部已提交、已验证)

| 里程碑 | commit | 内容 | 验证 |
|---|---|---|---|
| 静态交付配置 | `0b415e5` | 静态 Qt 单 exe,门禁回填 | 空目录独立运行 + 真实弹窗 + ctest |
| P1-c E2EE | `a8efa8b` | `box_crypto` + `e2ee_session` 计数器会话 | tweetnacl 跨实现向量逐字节对齐 |
| deb 打包 | `8226b2b` | `packaging/build-deb.sh` + systemd unit + conffile | VPS 构建 233.8 KiB;dpkg 冲突保护实测 |
| P3-a hub | `72636bc` | HTTP API 五件套 + WS 双端 + scrypt 登录 + 会话存储 + `--init-config` + 限速/CSP | ctest 11/11、HTTP 10 轮、Node WS 端到端 9/9 |
| P4-a hub 客户端 | `19bab95` | 出站 WS 客户端 + hello/ping/pong/request 转发 + 退避重连 + UI 真实状态接线 | live 8/8、ctest 13/13、真实 hub UI 冒烟 |
| **P2 Orca 桥** | `2b5dff5` | `OrcaConnection`(E2EE+密封 RPC)+ `OrcaBridgeImpl`(重连/订阅回放)+ Windows 根证书 + `--local-orca` | live 19/19、ctest 14/14、真实 Orca 冒烟 |
| **P5 零构建前端** | `a400bf6` | `web/` ES modules:看板/任务/新建/定时/登录/改密 + hub 客户端;server 静态文件三处修复 | ctest 15/15、build.ps1 退出 0、390×844 真机全流程 |

提交栈(main,13 个 commit 领先远端,均未 push):
```
HEAD     P5: zero-build web frontend; static assets keep their MIME type
8a584e1  Docs: hand off P2 — Orca bridge verified, P5 is next
2b5dff5  P2: real Orca bridge — E2EE v2 RPC client, reconnect loop, hub orca-state
19bab95  P4-a: finish gateway hub client — WS upgrade, clean shutdown, live test
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

### ✅ P5:零构建前端(已完成,见 §2.6)
`web/` 已是可直接交付的前端;hub 的 `staticDir` 指向它即可。

### 🟢 P2 余项(不阻塞,后继任务顺手做)
- `agentSession.*` 方法级封装(现在桥已能透传任意方法与订阅;`agent-session.ts` 里的
  消息规整/分页属于前端会话页,等 P5 再做)。
- 配对码 UI → 配置落盘 → **不重启**换 Orca 连接(当前改配对需重启 gateway,与 TS 一致)。
- relay 传输(`transport="relay"` + `relayHostId`)已透传,但未对真实 relay 验证。

### 🟡 P6:打包/发布/切换(下一个)
1. **VPS 重打 deb**(P3-a 代码后的版本):`git archive HEAD | ssh tar` →
   `setup-deps.sh` → `build-deps` → `ORCA_VERSION=0.0.1 bash packaging/build-deb.sh` →
   scp 回 `dist/`。流程已验证(见 §5)。
2. **VPS 切换演练**(危险!):备份 `/etc/orca-web/server.json` + `domain.txt` →
   stop 旧 `orca-web-server 0.1.5` → dpkg -i 新包 → enable → 联调。**旧包是生产环境,
   切换前必须用户点头**。
3. release.yml 静态流水线首跑;产物命名 `orca-gateway-<版本>-windows-amd64.exe` +
   `orca-server-<版本>-linux-amd64.deb` + `.sha256`。
4. LGPLv3 合规确认(静态 Qt)。wss 根证书已解决(`gateway/net/tls_roots.cpp`
   读 Windows ROOT/CA 库;Linux 走 OpenSSL 默认路径)。

### 🟢 随时
- **push**:13 个 commit 全在本地,远端 main 是 P0 骨架。push 后 CI 会首跑静态流水线。
- 三个构建目录策略统一(build/ 交付、build-ui/ 动态 Qt 验证、build-noqt/ 骨架)。

---

## 2.5 P2 完成记录(接手参考)

### 2.5.1 新增模块

| 文件 | 作用 |
|---|---|
| `gateway/net/orca_rpc_client.cpp` | 一条已认证的 Orca socket:ws/wss 连接 → 明文 `e2ee_hello`/`e2ee_ready` → 密封 `e2ee_auth` → 每 socket 重放 `runtime.clientCapabilities.update` → 密封 JSON-RPC(逐请求 steady_timer 超时)、订阅、优雅关闭;`onClose` 在握手前装好,成功即刻断线也不丢 |
| `gateway/net/orca_bridge_impl.cpp` | 对应 `apps/gateway/src/orca-bridge.ts`:自有 io 线程、500ms→30s ±20% 重连、`status.get` 描述符、重连后先发 `stream-reset` 再回放订阅、`agentSession.unsubscribe` 通知 Orca |
| `gateway/net/tls_roots.cpp` | vcpkg 的 OpenSSL 在 Windows 没有 CA bundle → 读系统 ROOT/CA 库;否则 wss:// hub 对真实 VPS 一律 `certificate verify failed` |
| `gateway/ui/local_orca.cpp` | `--local-orca`:读本机桌面 Orca 的 `orca-runtime/devices/e2ee-keypair.json`(布局在 `orca-runtime.json` 里是 `{kind, endpoint}` 对象数组;`0.0.0.0`→`127.0.0.1`);可用 `ORCA_WEB_LOCAL_ORCA_DIR` 覆盖 |
| `tests/unit/orca_bridge_live_test.cpp` | 回环 Orca fixture(自己持 X25519 密钥、按 desktop→mobile 方向封包,不复用 `E2eeSession`)驱动**真实** `OrcaBridgeImpl` |

接线:`main.cpp` 在 `file.orca` 或 `--local-orca` 三类字段齐全时构造 `OrcaBridgeImpl`
并 `start()`,否则退回 `NullOrcaBridge`;`RuntimeStatusProvider` 读 `hub.orcaState/orcaMachineName`;
`hub_client.cpp` 的 `handleBridgeEvent` 推 `orca-state`(带 message)/`stream-*`,
`kOrca` 只更新快照(`machineName`),descriptor 由下一次 hello 带走(与 TS 一致)。
CMake:二者需 `ORCA_BEAST_INCLUDE_DIR` + `ORCA_HAVE_SODIUM`(定义 `ORCA_HAVE_ORCA_BRIDGE`),
并链接 `crypt32`。

### 2.5.2 调试中发现并修掉的四个真问题

| 问题 | 症状 | 修法 |
|---|---|---|
| **fixture 方向搞反** | 密封帧解不开(auth 超时) | `E2eeSession` 是 mobile 端(封 mobile→desktop、开 desktop→mobile),fixture 作为 desktop 端必须直接用 `sealFrameAt/openFrameAt` + 手动方向计数器 |
| **空完成回调被调用** | 收到 `agentSession.unsubscribe` 的 `ok:false` 时 `0xc0000409` 崩溃 | `dispatch()` 对 `!request.done` 直接返回;超时路径同样只移动回调后判空 |
| **迭代中改 map** | 取消订阅后流仍活着 | `dispatch()` 先拷 sink 再投递;`unsubscribe` 先 `erase` 再关流 |
| **浏览器未推 orca-state 的 message** | hub 看不到断线原因 | `handleBridgeEvent` 带 `message` |

另:`start()` 用 `started_` CAS 防重复起线程(此前用 `worker_.joinable()` 会误 join 造成自等待隐患)。

### 2.5.3 验证链(已全部跑过)

1. `orca_bridge_live` 19/19:握手→capabilities→descriptor→RPC 往返→错误码→订阅首帧/流帧/流错误→
   退订后旧流静默→断线 reported→重拨→`stream-reset`→再次 connected ✅
2. 动态 Qt(`build-ui`,`scripts/build.ps1 -Triplet x64-windows -DynamicQt`):ctest **14/14** ✅
   (动态 triplet 原先没装 libsodium,本轮 `vcpkg install libsodium:x64-windows`)
3. 静态 `scripts/build.ps1` 退出 0,ctest 14/14,体积门禁 OK ✅
4. 真实冒烟:本地 `orca-server`(p2-smoke)+ `orca-gateway --local-orca --config …`,
   日志 `orca: orca connected: ws://127.0.0.1:6768`、`hub: orca state: connected`;
   截图:绿条「一切正常」、hub「已连接」、Orca「在线 · 已配对,正在和 DESKTOP-5C2U2HV 上的 Orca 通信」✅
   注意:用默认配置(生产 wss hub)在 Windows 上**现在也能过证书校验**,但本机新网关 id
   未被生产 hub 放行,冒烟请用本地 hub。

### 2.5.4 体积

- 静态 gateway:**59,913,728 B(57.1 MiB)**,门禁 60 MiB(P2 再增 1.16 MB:`tls_roots` + 证书)。
- 动态 Qt exe:**1,867,776 B(1.78 MiB)**,8 MiB 门禁不变。

### 2.6 P5 完成记录(接手参考)

#### 2.6.1 新增文件

| 文件 | 作用 |
|---|---|
| `web/index.html` | 唯一的 HTML;`<script type="module" src="/app.js">`,无内联脚本 |
| `web/app.js` | 会话检查、登录/退出、路由(pushState/popstate)、HubClient 生命周期、authExpired 处理 |
| `web/hub.js` | 浏览器 `/client` WS:欢迎/admitted、500ms→15s 退避、25s ping/75s 静默、重连后重订阅 + `reset`、`/api/session` 判 cookie 失效 |
| `web/api.js` `web/envelope.js` `web/dom.js` `web/util.js` | 同源 JSON API、canonical-JSON mutation envelope、DOM 构造(只用 Text node/textContent)、格式化 |
| `web/views/board.js` | 看板:project.list + worktree.ps + subscribeStatus、搜索/筛选/折叠/15s 刷新/下拉刷新、提醒 |
| `web/views/task.js` | 任务:history tail/before、350ms 去抖 refetch、发送/停止/审批/回答/setOption、markdown |
| `web/views/create.js` `web/views/automations.js` `web/views/auth.js` `web/views/markdown.js` | 新建任务、定时任务列表+编辑器、登录/改密、依赖零的 markdown 子集 |
| `web/styles.css` | 原 1783 行样式原样复用,末尾只加了一条 `[hidden]{display:none!important}` |
| `tests/unit/server_http_test.cpp` | 静态文件回归:路径解析、MIME、SPA 回退、目录穿越 |

#### 2.6.2 本轮修掉的真问题

| 问题 | 症状 | 修法 |
|---|---|---|
| **Windows 静态路径** | 所有 `/app.js`、`/styles.css` 都返回 index.html(200 但内容错) | URL 以 `/` 开头;Windows 上 `root / "/app.js"` 会落到盘符当前目录。`serveStatic` 先把前导 `/` 去掉再拼 |
| **静态响应没有 Content-Type** | `nosniff` 下浏览器拒绝执行 module script / 应用 CSS | `server_runner` 读取文件后补 `Content-Type: <fileContentType>`;`index.html` 再补 `Cache-Control: no-store` |
| **`.webmanifest` 被当二进制** | manifest 解析失败 | MIME 表补 `application/manifest+json` |
| **`hidden` 被 CSS 覆盖** | 空任务的「载入更早的消息」仍然显示 | 末尾加 `[hidden]{display:none!important}` |
| **流式重绘吞点击** | 状态帧/历史刷新整块重建 DOM,Playwright 与真机 tap 都可能落空 | 看板用签名去重 + 复用任务/项目头/新建按钮节点;任务页按 itemId 复用节点、选项面板按签名复用;状态行做成持久节点 |
| **setOption 后标题不更新** | load() 先 paint、loadOptions() 后落地,权限 pill 仍是旧值 | `loadOptions()` 落地后补 `paintHead()`;setOption 的 finally 改 `paint()` |

#### 2.6.3 验证链(已全部跑过)

1. `node --input-type=module --check` 全部 12 个模块通过。
2. `ctest -C Release` **15/15**(新增 `server_http` 11 项断言全过)。
3. `scripts/build.ps1` 退出 0:静态 Qt 构建 + ctest 15/15 + 体积门禁;
   `orca-server.exe` **5,946,880 B(5.8 MiB)** / 8 MiB。
4. 真机(本地 hub `127.0.0.1:8788` + `--local-orca` 真桌面 Orca,emulate 390×844):
   登录(错误密码/服务器离线文案)、看板真实项目与任务、搜索/筛选/折叠、
   任务详情+载入更早、会话设置(catalog 真实拉取)、权限改为「每次都问」、
   发送→agent 回复、停止运行中的回合、新建空任务、AskUserQuestion 选 A→提交→「已回答」、
   定时页列表与编辑器(每天/每周/每分钟/自定义、沿用/每次新建)、改密面板、退出再登录;
   kill hub → UI 显示断线 → 重启 hub → 自动重连并刷新(重订阅生效)。
   控制台只有 kill 期间的 `ERR_CONNECTION_REFUSED` 与两条 a11y 提示,无 JS/CSP 报错。
5. 测试留下一个空任务会话「仅回复“收到”」(fcp,idle)和该会话的权限改动,可随时关闭。

#### 2.6.4 本地联调方法(下次照做)

- `orca-server --init-config <临时>/server.json`,把 `staticDir` 改成
  `D:/work/orca-cpp/web`(绝对路径),`sessionStorePath` 也指向临时目录。
- `orca-gateway --config <临时>/gateway.json --local-orca`;
  `serverUrl` 必须写全 `ws://127.0.0.1:8788/gateway`(配置读取不会自动补 `/gateway`)。
- 浏览器 390×844 用 Chrome DevTools MCP 的 `emulate` 设 viewport(窗口
  `resize_page` 会被最小宽度钳到 ~500);页面在 `http://127.0.0.1:8788/`。

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
| `gateway/net/` + `gateway/include/orca/gateway/net/` | hub_link_core(纯逻辑)/ hub_client(beast 客户端)/ orca_bridge(接口+null)/ **orca_rpc_client + orca_bridge_impl(P2)** / tls_roots |
| `gateway/ui/local_orca.cpp` | `--local-orca` 读取本机桌面 Orca 配对材料 |
| `gateway/ui/runtime_status_provider.hpp` | P4 状态源(Qt 窗口用) |
| `common/src/crypto/scrypt.cpp` | 登录密码哈希(与 passwords.ts 格式互通) |
| `tests/unit/hub_client_live_test.cpp` | 回环 beast hub fixture + 真实 HubClient 集成测试 |
| `tests/unit/orca_bridge_live_test.cpp` | 回环 Orca fixture(E2EE v2)+ 真实 OrcaBridgeImpl 集成测试 |
| `web/` | 零构建前端;`staticDir` 指到这里即可,无 npm/Vite/React |
| `tests/unit/server_http_test.cpp` | 静态文件路径/MIME/SPA 回退/穿越回归 |
| `tests/vectors/p1c-vectors.json` / `p3-vectors.json` | P1-c / P3 金标准向量 |
| `packaging/build-deb.sh` + `packaging/deb/` | deb 直打包(VPS 已验证) |
| `docs/PROTOCOL.md` §P1-c/P3-a | 逐模块对齐表与冒烟记录 |

---

## 5. 环境事实(累计验证)

- **本机**:VS 18 / MSVC 19.51,VS cmake(`.../Common7/IDE/.../CMake/bin/cmake.exe`,
  不在 Git Bash PATH);vcpkg `D:\work\vcpkg`(x64-windows-static 全量 + x64-windows
  含 boost-beast + libsodium);构建目录 build/(交付)/ build-ui/(动态验证)/ build-noqt/。
- **直接跑测试 exe 时 bash 里退出码不可靠**(控制台子进程),用 `ctest -C Release -R <name> -V`
  看结果;测试里 `std::cout` 加 `unitbuf`,崩溃时前面的 ok 行不丢。
- **前端联调**:Chrome DevTools MCP 的 `resize_page` 受窗口最小宽度限制(~500px),
  390×844 要用 `emulate` 的 `viewport: "390x844x3,mobile,touch"`。
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
