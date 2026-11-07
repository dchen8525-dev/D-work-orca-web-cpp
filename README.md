# orca-cpp

把 `orca-web` 从 Bun/Node 自包含二进制重写为**纯 C++17**：一个 VPS hub、一个家里 Windows gateway、一套零构建的浏览器静态页。目标是把约 110 MB 的 exe 和约 100 MB 的 deb 压到 10 MB 以下，且部署机器上不需要任何运行时。

原项目在 `D:\work\orca-web`（保持只读参考）。本仓库不依赖 Node / npm / Vite / Bun。

## 链路

```
手机浏览器 --HTTPS/WSS--> orca-server (VPS) <--主动外连 WSS-- orca-gateway (家里) --loopback WS--> 桌面 Orca
                           (登录/静态页/hub)                       (E2EE v2 / 配对/管理页)
```

三条不变量沿用原项目：家里机器从不监听公网端口；VPS 不持有 Orca 凭据；浏览器只和 VPS 说话。

## 当前状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| P0 | 目录骨架、CMake 双平台构建、两个最小入口、体积门禁、CI | **已完成，本机编译 + 6/6 CTest** |
| P1-a | 免依赖部分：纯 C++ SHA-256 / HMAC / HKDF / base64、24 字段 transcript、canonical JSON、两条 wire 帧、golden 向量 | **已完成，逐字节比对 JS 独立实现通过** |
| P1-b | E2EE v2 免依赖切片：17 项能力集、42B header / 24B nonce / 双向计数器、明文握手帧与 strict `e2ee_ready` 校验 | **已完成，protocol 117 项断言全过** |
| P1-c | X25519 `crypto_box_beforenm` + secretbox 封包/解包 + scrypt（需 libsodium / 静态 OpenSSL） | 待 `x64-windows-static` 依赖就位 |
| P2 | Orca JSON-RPC + 真实 Orca 冒烟 | 待实现 |
| P3 | server：HTTP / 静态 / 登录会话 / 两个 WS / hub 状态机 | 待实现 |
| P4 | gateway：配置 / 配对 / 管理页 / 自更新 / 两条外连 | 待实现 |
| P5 | 零构建原生前端（看板 / 任务 / 审批 / 自动化） | 待实现 |
| P6 | deb / systemd / Windows 自启 / release 产物 / 全链路联调 | 待实现 |

P0 的两个入口**只实现 CLI 契约与配置路径检查**，明确打印"服务尚未实现"，不伪装成可用服务。

## 构建

```powershell
# Windows（普通 PowerShell；脚本会清掉重复代理变量，见 docs/BUILD.md）
.\scripts\build.ps1
```

```bash
# Linux / CI
bash scripts/setup-deps.sh
bash scripts/build.sh
```

P1 起 `nlohmann-json`（header-only，vcpkg 已装）是编译必需，`scripts/build.ps1` 默认带上
vcpkg toolchain；`-NoToolchain` 从 P1 起不可用。libsodium / boost-beast 只影响 P1-c 及以后，
本机请按 `docs/DEPENDENCIES.md` 运行 `scripts/setup-deps.ps1`。

`tools/export-vectors.mjs` 是一次性的向量生成器（Node 内置 crypto），**不参与构建、不参与运行**；
生成的 `tests/vectors/p1-vectors.json` 是静态基线。如果希望仓库里完全不出现 `.mjs`，可删除该脚本，
只保留静态向量文件。

## 体积

P0 实测（MSVC 19.51 / Release /O1 + LTO + 静态 CRT）：`orca-gateway.exe` 245.5 KiB、`orca-server.exe` 240.5 KiB。
完整实现后的门禁为 **exe ≤ 8 MiB、deb ≤ 10 MiB**，构建后由 `cmake/SizeReport.cmake` 自动校验，超限直接构建失败。

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
