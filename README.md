# orca-cpp

把 `orca-web` 从 Bun/Node 自包含二进制重写为**纯 C++17**：一个 VPS hub、一个家里 Windows 的 **Qt Widgets 单文件客户端**、一套零构建的浏览器静态页。目标是把约 110 MB 的 Bun exe 和约 100 MB 的 deb 压到 MB 级，且部署机器上不需要任何运行时。

原项目在 `D:\work\orca-web`（保持只读参考）。本仓库不依赖 Node / npm / Vite / Bun；浏览器静态页也不需要构建工具，只有 `tools/export-vectors.mjs` 是开发期的向量生成器。

## 链路

```
手机浏览器 --HTTPS/WSS--> orca-server (VPS) <--主动外连 WSS-- orca-gateway (家里, Qt 窗口) --loopback WS--> 桌面 Orca
                           (登录/静态页/hub)                       (E2EE v2 / 配对 / 配置窗口)
```

三条不变量沿用原项目：家里机器从不监听公网端口；VPS 不持有 Orca 凭据；浏览器只和 VPS 说话。

## Qt 配置客户端

gateway 的配置界面**不再是 `127.0.0.1:8790` 的 HTML 管理页**，而是一个 Qt Widgets 窗口，与 gateway 后台服务同进程：主线程跑 Qt 事件循环，P4 起网络运行时跑在 worker 线程。因此没有本地 HTTP 监听、没有同源校验、也没有 CSP。发布物是**一个免安装 exe**：`orca-gateway-<版本>-windows-amd64.exe`（静态 Qt + 静态 CRT，无 DLL、无安装包、无自更新）。关闭窗口即停止 gateway，开机自启由启动项拉起。

窗口复刻旧页面的四象限状态、hub 地址/key 修改、配对码解析、配置文件查看与 3 秒刷新；**不含「检查更新」**。状态源通过 `StatusProvider` 接口注入，当前骨架实现读真实配置文件、连接状态留待 P4 接入。纯逻辑（配置读写、配对码解码、状态判定）由 `gateway_core` CTest 门禁覆盖，不依赖 Qt。

## 当前状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| P0 | 目录骨架、CMake 双平台构建、两个最小入口、体积门禁、CI | **已完成，本机编译 + 6/6 CTest** |
| P1-a | 免依赖部分：纯 C++ SHA-256 / HMAC / HKDF / base64、24 字段 transcript、canonical JSON、两条 wire 帧、golden 向量 | **已完成，逐字节比对 JS 独立实现通过** |
| P1-b | E2EE v2 免依赖切片：17 项能力集、42B header / 24B nonce / 双向计数器、明文握手帧与 strict `e2ee_ready` 校验 | **已完成，protocol 117 项断言全过** |
| P1-c | X25519 `crypto_box_beforenm` + secretbox 封包/解包 + scrypt（需 libsodium / 静态 OpenSSL） | 待 `x64-windows-static` 依赖就位 |
| P2 | Orca JSON-RPC + 真实 Orca 冒烟 | 待实现 |
| P3 | server：HTTP / 静态 / 登录会话 / 两个 WS / hub 状态机 | 待实现 |
| P4 | gateway 后台：配置 / 配对 / 两条外连 / hub 重连 | 待实现 |
| P4-UI | **Qt Widgets 配置窗口骨架**（状态、hub 配置、配对码、配置查看） | **已完成**：本机动态 Qt 编译 + offscreen smoke 通过，`gateway_core` 单测全绿 |
| P5 | 零构建原生前端（看板 / 任务 / 审批 / 自动化） | 待实现 |
| P6 | deb / systemd / Windows 自启 / release 产物 / 全链路联调 | 待实现 |

P0 的 server 入口**只实现 CLI 契约与配置路径检查**，明确打印"服务尚未实现"，不伪装成可用服务。
gateway 入口在检测到静态 Qt 未安装时会退回同样的 CLI 骨架并给出 WARNING；CI/release 用
`-DORCA_REQUIRE_QT=ON` 把这个回退变成 configure 失败，保证骨架不会被打包。

## 构建

```powershell
# Windows（普通 PowerShell；脚本会清掉重复代理变量，见 docs/BUILD.md）
.\scripts\setup-deps.ps1                          # 首次会从源码编译静态 Qt，约 1-2 小时
.\scripts\build.ps1 -RequireNetworkDeps -RequireQt
```

```bash
# Linux / CI（无 Qt：hub 是 headless 的）
bash scripts/setup-deps.sh
bash scripts/build.sh
```

P1 起 `nlohmann-json`（header-only）是编译必需，Windows 构建必须带 vcpkg toolchain；
`-NoToolchain` 从 P1 起不可用。**交付配置**是 `x64-windows-static` + `/MT` + 静态 Qt；
`x64-windows-static` 当前尚未在本机安装（本机只有 `x64-windows` 的动态 Qt 6.11.2），
因此静态配置**尚未在本机编译验证**。

在静态 Qt 装好之前，可用本机已有的动态 Qt 验证 UI 代码：

```powershell
.\scripts\build.ps1 -Triplet x64-windows -DynamicQt -RequireQt
```

该配置是 `/MD` + 动态 Qt，仅用于本地验证，**不是发布配置**：exe 旁会带 17 个 DLL（约 58 MB）。
CI/release 的 Windows job 用 `VCPKG_DEFAULT_BINARY_CACHE` 缓存静态 Qt 编译产物，避免每次重编。

`tools/export-vectors.mjs` 是一次性的向量生成器（Node 内置 crypto），**不参与构建、不参与运行**；
生成的 `tests/vectors/p1-vectors.json` 是静态基线。如果希望仓库里完全不出现 `.mjs`，可删除该脚本，
只保留静态向量文件。

## 体积

P0 实测（MSVC 19.51 / Release /O1 + LTO + 静态 CRT）：CLI 骨架 `orca-gateway.exe` 245.5 KiB、
`orca-server.exe` 240.5 KiB。动态 Qt 验证构建下 gateway exe 为 240,128 B（DLL 另计）。

门禁由 `cmake/SizeReport.cmake` 在 POST_BUILD 自动校验，超限直接构建失败：

| 目标 | 上限 | 说明 |
|---|---|---|
| `orca-server`（及 CLI 骨架） | 8 MiB | 纯 C++ 逻辑，不含 Qt |
| `orca-gateway`（静态 Qt） | **24 MiB** | 静态 Qt6 Widgets + OpenSSL/libsodium 的估算上限，首次真实构建后按实测收紧 |
| deb | 10 MiB | P6 打包步骤用同一脚本 |

24 MiB 是**估算值、尚未经静态构建实测**：Qt 静态库体积取决于 feature 裁剪，CI 首次跑完必须回填真实数字。
相对原项目约 110 MB 的 Bun exe，即使 24 MiB 仍是 4 倍以上的缩减；deb 侧不受 Qt 影响。

## 文档

| 文件 | 内容 |
|---|---|
| `docs/ARCHITECTURE.md` | 三进程架构、模块划分、JSON 透传原则、阶段划分 |
| `docs/BUILD.md` | 工具链、依赖、本机构建的环境坑、CI |
| `docs/NAMING.md` | 全部产物 / 安装路径 / 配置文件的命名契约 |
| `docs/DEPENDENCIES.md` | 依赖清单、体积影响、静态 CRT 决策、本机阻塞与备选方案 |
| `docs/PROTOCOL.md` | wire 常量、24 字段 transcript、P1 向量门禁与剩余待办 |
| `tests/vectors/README.md` | golden 向量来源与复现方式 |
| `tools/export-vectors.mjs` | 一次性向量生成器（仅开发期，非构建依赖） |
| `web/README.md` | 零构建静态页的约束 |
