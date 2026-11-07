# 依赖

选定方案：**vcpkg 现成库 + 静态链接**（用户在 P0 前确认）。下表是实际用途与体积影响，新增依赖不会在未经确认的情况下再增加。

| 库 | 状态 | 用途 | 体积影响（静态链接估算） |
|---|---|---|---|
| `openssl` | vcpkg 已装（`x64-windows`，动态）；CI 需 `x64-windows-static` | SHA-256 / HMAC-HKDF / `EVP_PBE_scrypt` / base64 / WSS 客户端 TLS | ≈ 2.5 MB |
| `nlohmann-json` | vcpkg 已装 | wire 帧与配置 JSON | header-only，≈ 0 |
| `libsodium` | **新增，本机未装成功** | X25519（`crypto_box_beforenm`）、XSalsa20-Poly1305（`crypto_secretbox_easy`）、随机字节。E2EE v2 必需，OpenSSL 没有 XSalsa20 | ≈ 0.4 MB |
| `boost-beast` (+asio) | **新增，本机未装成功** | HTTP 服务端、WS 服务端、WSS 客户端 | ≈ 1 MB，header-only 为主 |
| 不引入 | — | Qt6 / ICU / zlib permessage-deflate | 会破坏体积目标 |

Linux 侧对应系统包：`libssl-dev`、`nlohmann-json3-dev`、`libsodium-dev`、`libboost-all-dev`。

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

## 静态 CRT 的决策

目标是一个无需 VC++ redist、无需额外 DLL 的单文件 exe，因此项目用 `/MT`（`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`）。
这要求 vcpkg 用 **`x64-windows-static`** triplet（静态库 + 静态 CRT）。若用默认的 `x64-windows`（动态 CRT + DLL OpenSSL），
会出现 CRT 混用链接冲突，且部署要携带 DLL。本机现有 openssl 是 `x64-windows`，所以 P1 前必须补装 static triplet。

## 备选方案（仅在依赖始终装不上时启用，需重新确认）

| 方案 | 代价 |
|---|---|
| `ixwebsocket` 替代 Beast | 同为 vcpkg 现成库、依赖更少（zlib + openssl），但 API 与 WS 服务端能力需重新评估 |
| 手写 HTTP + RFC 6455 | 零新增网络依赖、体积最小，约 +500–800 行 C++，工期最长 |
| 自实现 XSalsa20 / Poly1305 | 摆脱 libsodium，但自写密码学风险高，必须配完整向量测试，**不推荐** |

## 体积回退策略

若 P1+ 静态链接后 exe > 8 MiB：`/O1` + LTO + `--gc-sections` 已启用，下一步优先换 `ixwebsocket`、
再考虑动态链接 OpenSSL 并随包携带 DLL（会放弃单文件目标）。任何回退都需要重新确认部署形态。
