# 依赖

选定方案：**vcpkg 现成库 + 静态链接**（用户在 P0 前确认）。用户随后明确要求把 gateway 配置页改成 Qt 客户端，
因此 `qtbase` 作为第 6 个依赖加入，交付配置统一为 `x64-windows-static` + `/MT`。
下表是实际用途与体积影响；除 Qt 外没有再增加新依赖。

| 库 | 状态 | 用途 | 体积影响（静态链接估算） |
|---|---|---|---|
| `openssl` | 本机仅 `x64-windows`（动态）；交付需 `x64-windows-static` | SHA-256 / HMAC-HKDF / WSS 客户端 TLS（`EVP_PBE_scrypt` 原项目 E2EE 路径零使用，已移出范围） | ≈ 2.5 MB |
| `nlohmann-json` | 本机已装（header-only） | wire 帧、配置 JSON | ≈ 0 |
| `libsodium` | 本机已装 `x64-windows-static`（2026-11-07） | X25519（`crypto_box_beforenm`）、XSalsa20-Poly1305（`crypto_secretbox_easy`）、随机字节。E2EE v2 必需，OpenSSL 没有 XSalsa20 | ≈ 0.4 MB |
| `boost-beast` (+asio) | 本机已装 `x64-windows-static`（2026-11-07） | HTTP 服务端、WS 服务端、WSS 客户端 | ≈ 1 MB，header-only 为主 |
| `qtbase`（Qt6 Widgets） | 动态 `x64-windows` 6.11.2（10-31 装）；**静态** `x64-windows-static` 6.11.3（11-07 装，约 22 分钟源码编译） | gateway 的 Qt Widgets 配置窗口（替代原 loopback HTML 管理页） | **实测**：单 exe 50.3 MiB（静态 ICU + OpenSSL 主因，见下文） |
| 仍不引入 | — | `qtdeclarative`（QML）、独立 ICU、zlib permessage-deflate | 与 Qt Widgets 选型无关，且会进一步增大体积 |

Qt 的动态集合实测（本机 `x64-windows`）：`Qt6Core 5.1M + Qt6Gui 6.7M + Qt6Widgets 5.9M ≈ 17.7 MB`，
再加 17 个依赖 DLL 后输出目录约 58 MB —— 这正是选择**静态 Qt** 以保住单文件 exe 的原因。

Linux 侧对应系统包：`libssl-dev`、`nlohmann-json3-dev`、`libsodium-dev`、`libboost-all-dev`。
**Linux 不装 Qt**：hub 是 headless 的，Linux 构建走 CLI 骨架（`ORCA_HAVE_QT=OFF`）。

## 本机安装为何没成功（如实记录，已定位到具体原因）

P0/P1 阶段尝试 `vcpkg install ... --triplet x64-windows`，两个问题先后出现：

1. **Boost 的 LICENSE 下载（已解决）**：`boost-cmake` 端口需要从 `raw.githubusercontent.com`
   下载 `LICENSE_1_0.txt`，当前代理对该域名 TLS 握手失败（`curl error 35 / schannel`）。
   已从 `fastly.jsdelivr.net` 镜像取回同一文件，**SHA512 与 portfile 期望值完全一致**，
   放入 `D:\work\vcpkg\downloads\boost-1.92.0-LICENSE_1_0.txt`。重跑时日志出现
   `Using cached boost-1.92.0-LICENSE_1_0.txt`，`boost-cmake` 安装成功。
   其余 boost 子包源码都从 `github.com` 下载，可达。
2. **libsodium（根因已修正）**：宿主依赖 `pkgconf` 的 CMake 编译器 ABI 探测失败。
   最初误判为沙箱禁止 `reg.exe`，查看 `buildtrees/pkgconf/*-out.log` 后确认真实原因是
   `RC Pass 1: command "rc ..." ... no such file or directory` ——
   Windows SDK 的 `rc.exe` 不在 vcpkg Ninja 子进程的 PATH 里。`boost-headers` 也是同一错误。
   `rc.exe` 位于 `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe`。
   在 Bash 会话里注入 PATH 无效（vcpkg 会给子进程构造干净环境），
   `scripts/setup-deps.ps1` 现在会自动探测最新 SDK 并在**进程内**注入 PATH。

因此在 agent 沙箱内仍未完成安装；请在**普通 PowerShell 窗口**运行
`D:\work\orca-cpp\scripts\setup-deps.ps1`。CI runner 的网络与 PATH 均正常，不受影响。

> 本轮曾考虑借外部主机做下载通道，实际未使用；相关凭据没有写入任何文件、
> 脚本、文档或日志。Boost 文件走公共镜像即可解决，libsodium 的问题也并非网络问题。

`scripts/setup-deps.ps1` 把安装命令固化成一条命令，请在**普通 PowerShell 窗口**（非 agent 沙箱）运行：

```powershell
.\scripts\setup-deps.ps1                       # 默认 x64-windows-static
.\scripts\build.ps1 -Triplet x64-windows-static -RequireNetworkDeps
```

CI 的 ubuntu / windows runner 网络与进程不受限，会正常安装。

## 静态 CRT 与 Qt 的决策

目标是一个无需 VC++ redist、无需额外 DLL、无需安装包的单文件 exe，因此项目用 `/MT`
（`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`），vcpkg 统一 **`x64-windows-static`**。
Qt 也必须是静态构建：动态 Qt 会强制 `/MD` 并要求随包携带 DLL 与 `platforms/qwindows.dll`，
与「单文件 Portable」冲突。静态 Qt 不能自动发现平台插件，`main.cpp` 用
`Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)`、CMake 链接 `Qt6::QWindowsIntegrationPlugin`。

本机 `x64-windows-static` 已于 2026-11-07 装齐全量依赖（openssl / nlohmann-json /
libsodium / boost-beast / qtbase 6.11.3）。所以：

- 交付配置（`/MT` + 静态 Qt）**已在本机编译验证**：单 exe 50.3 MiB 在空目录独立运行，
  `--version`、`--help` 正常，真实窗口（静态平台插件）弹出成功；
- 本机 UI 代码仍可 `-DORCA_QT_STATIC=OFF` 切换到 `/MD` + 动态 Qt 做快速迭代，
  这是**验证配置，不是发布配置**；
- `ORCA_REQUIRE_QT=ON` 让 CI/release 在 Qt 缺失时直接 configure 失败，避免把 CLI 骨架打包。

`scripts/setup-deps.ps1` 已加入 `qtbase`。静态 Qt 从源码编译，首次约 1-2 小时；
CI/release 用 `VCPKG_DEFAULT_BINARY_CACHE` 复用二进制包。`-SkipQt` 可只装网络依赖。

## Qt 静态链接的合规与体积（合规未决，体积已实测）

Qt 6 是 LGPLv3 / GPLv3 / 商业三许可。**静态链接单 exe 需要履行 LGPL 的可重链接义务**
（提供目标文件或改用商业授权）——发布正式版前必须确认合规路径，本骨架阶段不做法律判断。

体积门禁已按 2026-11-07 实测回填：gateway 单 exe **52,764,160 B（50.3 MiB）**，
远超最初 24 MiB 估算——主因是 vcpkg qtbase 默认 feature 打开了**静态 ICU**
（约 25-30 MB，QSql/QNetwork 的 locale 支持用）。门禁设 56 MiB；若产物需要瘦身，
优先裁剪 qtbase feature（先 `-icu`，需与 CI 安装参数同步），而不是放宽到接近原 Bun exe 的量级。

## 备选方案（仅在依赖始终装不上时启用，需重新确认）

| 方案 | 代价 |
|---|---|
| `ixwebsocket` 替代 Beast | 同为 vcpkg 现成库、依赖更少（zlib + openssl），但 API 与 WS 服务端能力需重新评估 |
| 手写 HTTP + RFC 6455 | 零新增网络依赖、体积最小，约 +500–800 行 C++，工期最长 |
| 自实现 XSalsa20 / Poly1305 | 摆脱 libsodium，但自写密码学风险高，必须配完整向量测试，**不推荐** |

## 体积回退策略

体积门禁分两层：`orca-server` 与无 Qt 的 CLI 骨架仍是 **8 MiB**；链接静态 Qt 的
`orca-gateway` 实测 50.3 MiB、门禁 **56 MiB**。`deb ≤ 10 MiB` 不受 Qt 影响（hub 是 headless 的）。

若静态 gateway 需要进一步缩小，`/O1` + LTO + `--gc-sections` 已启用，下一步按顺序考虑：
裁剪 qtbase feature（先 `-icu`）→ 换 `ixwebsocket` 缩小网络栈 → 重新评估 Qt 与「单文件」的取舍。
任何回退都需要重新确认部署形态，不能单方面放宽门禁。
