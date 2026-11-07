# HANDOFF — 当前进展交接

> 生成时间：2026-11-07 18:1x GMT+8（第二轮，接 15:50 的第一轮）
> 仓库：`D:\work\orca-cpp`（分支 `main`，远端 `git@github.com:dchen8525-dev/D-work-orca-web-cpp.git`）
> 本文件是**当前状态的权威快照**；`README.md` 是设计说明，细节以本文件为准。
> 原项目 `D:\work\orca-web` 保持只读参考，不要改动。

---

## 0. 一句话状态

**交付配置（静态 Qt 单 exe）已在本机端到端验证成功**：单 exe 50.3 MiB 在空目录独立运行、
`--version`/`--help` 正常、静态平台插件真实弹窗（「orca 网关管理」）、ctest 7/7 全绿、
体积门禁按实测回填 56 MiB。第一轮的两大"致命"项已消灭一项（静态从未编译 → 已验证）；
另一项（远端没有 UI 改动）已通过本地 3 个 commit 落盘，但**尚未 push**，远端 `main` 仍是 `f11dc93`。

---

## 1. 本轮实际做了什么（带证据）

| # | 动作 | 命令 / 事实 | 结果 |
|---|---|---|---|
| 1 | 本地落盘第一轮 UI 改动 | `git add -A && git commit` | `1d0cf8a`（27 文件，+1934/-112，含 gateway/ui/ 8 文件 + HANDOFF.md）；提交前扫描过 diff 无凭据 |
| 2 | 静态依赖安装 | `scripts\setup-deps.ps1`（首轮） | 卡在 GitHub 直连 40 KB/s（cmake 4.4.4 54MB + PowerShell 7.6.9 110MB） |
| 3 | VPS 加速 | 用户提供 VPS；SSH 密钥免密 + `ssh -D 1080` SOCKS 隧道 + vcpkg 走代理 | 直连 40 KB/s → 隧道 ~1 MB/s（25×）；PowerShell 109.6MB 约 100 秒下完 |
| 4 | 静态包安装（除 qtbase） | vcpkg 增量 | openssl/libsodium/boost-beast/icu/freetype/harfbuzz 等全部装上；zstd 首次失败是打断误杀，重跑即过 |
| 5 | qtbase 6.11.3 静态编译 | 首次失败（见 §4 坑 3）；工具链补丁后重跑 | **22 分钟编译成功**，二进制缓存已回填；`Qt6::QWindowsIntegrationPlugin` target 存在 |
| 6 | 交付构建 | `.\scripts\build.ps1 -Version 0.0.1 -RequireNetworkDeps -RequireQt` | configure 首次失败（见 §4 坑 4）→ `Dependencies.cmake` 修复 → **exit 0**：`Qt6 Widgets: ON (static=ON)` |
| 7 | 体积门禁 | POST_BUILD | 实测 **52,764,160 B（50.3 MiB）** 超原 24 MiB 估算 → 门禁回填 **56 MiB**（`gateway/CMakeLists.txt` 带实测注释）→ `verdict OK`；orca-server 241 KiB OK |
| 8 | 单测 | build.ps1 内置 ctest | **7/7 Passed**（smoke / crypto / gateway_core / protocol / server_version / gateway_version / server_unknown_flag） |
| 9 | 单 exe 独立验证 | 拷贝 exe 到空目录 | `--version` → `orca-gateway 0.0.1`（无 -dev）；`--help` 正常；PE32+ console |
| 10 | 真实窗口 | `Start-Process` | `TITLE=[orca 网关管理]`，HWND 非零，静态 `QWindowsIntegrationPlugin` 生效（**第一轮未验证的分支就此闭环**） |
| 11 | 文档修正 | 4 文档 + CMake + Dependencies.cmake | 版本表述精确化、实测回填、windeployqt 章节等 |

---

## 2. 当前工作树（**最重要**）

```
HEAD  = 本轮收尾 commit（见 git log）
  1d0cf8a  Add Qt Widgets gateway config UI and static-Qt delivery build path
  5b37d23  Docs: correct installed Qt version to 6.11.2 …, document windeployqt …
  <本轮第3个 commit>  Static delivery config verified: size gate 56 MiB …
tag   0.0.1（pre-release，指向 f11dc93，不含 Qt UI）
remote origin  git@github.com:dchen8525-dev/D-work-orca-web-cpp.git
```

**远端 `main` 仍是 `f11dc93`（没有任何 Qt UI），三个本地 commit 都没 push**——
是否 push 由用户决定。换机器 clone 依然看不到全部 UI 与交付验证成果。

---

## 3. 环境与可复现命令

### 3.1 环境事实（本轮更新）

| 项 | 值 |
|---|---|
| 静态 triplet | `x64-windows-static` **已装齐**：openssl / nlohmann-json / libsodium / boost-beast / qtbase **6.11.3**（2026-11-07，qtbase 源码编译 22 分钟，二进制缓存已回填） |
| 动态 triplet | `x64-windows` 保持 81 包（动态 Qt **6.11.2**，10-31 装）——注意 host(6.11.2) 与静态 port(6.11.3) 版本差是 §4 坑 3/4 的根源 |
| 版本表述口径 | **动态 6.11.2 / 静态 6.11.3**；旧文档笼统写 6.11.3 或 6.11.2 都不准确，已按此口径修正 |
| vcpkg 本地补丁 | `D:\work\vcpkg\scripts\toolchains\windows.cmake` 末尾有 `[orca-cpp local patch]` 块（仅静态 triplet 生效，见 §4 坑 3）；**换 vcpkg 目录或 `vcpkg update` 后需检查该补丁是否还在** |
| 交付构建产物 | `build/gateway/Release/orca-gateway.exe` = 52,764,160 B；`--version` = `orca-gateway 0.0.1` |
| 三个构建目录 | `build/` 已重建为**交付配置**（静态）；`build-ui/` 动态验证配置仍在（exe 旁有 windeployqt 部署）；`build-noqt/` 无 Qt CLI 骨架 |
| SSH 隧道 | `ssh -i ~/.ssh/id_ed25519 -D 1080 -N root@144.34.186.164`（VPS，Ubuntu 26.04；本机公钥已装，密钥免密；**VPS 密码仅会话内用过一次，未写进任何文件**） |

### 3.2 重新构建交付配置（已验证可跑通）

```powershell
# 依赖已装齐，setup-deps 秒过；全新机器才需要重跑（qtbase 22 分钟 + 缓存）
.\scripts\setup-deps.ps1
.\scripts\build.ps1 -Version 0.0.1 -RequireNetworkDeps -RequireQt
# 成功标志：Qt6 Widgets: ON (static=ON)、[orca-size] verdict : OK、100% tests passed (7/7)
```

### 3.3 单 exe 独立验证（已验证）

```powershell
Copy-Item build\gateway\Release\orca-gateway.exe D:\任意空目录\
& D:\任意空目录\orca-gateway.exe --version     # orca-gateway 0.0.1
Start-Process D:\任意空目录\orca-gateway.exe   # 弹窗「orca 网关管理」
```

### 3.4 动态验证配置（快速迭代 UI 用，非发布配置）

```powershell
.\scripts\build.ps1 -Triplet x64-windows -DynamicQt -RequireQt
# build-ui/ 目录已 windeployqt 部署过，可直接跑；清目录后按 docs/BUILD.md §windeployqt 重做
```

---

## 4. 本轮踩过的坑（接手前必读）

| # | 现象 | 根因 | 处理 |
|---|---|---|---|
| 1 | vcpkg 直连 GitHub 40 KB/s，cmake/PowerShell 工具链下载要几十分钟 | 本机网络 | VPS SSH SOCKS 隧道 + `ALL_PROXY=socks5h://127.0.0.1:1080 HTTPS_PROXY=socks5h://…`（只设大写，防 MSB6001）；vcpkg 打印 `Using %HTTP(S)_PROXY%` 即生效 |
| 2 | TaskStop 杀后台 PowerShell 后，vcpkg.exe 成孤儿继续占锁 | 外壳死了子进程还在 | `Get-Process vcpkg \| Stop-Process -Force`；锁在 `installed\vcpkg\vcpkg-running.lock`（**不是** buildtrees 那个） |
| 3 | qtbase 静态 configure 报 `Could not find ... Qt6CoreTools ... 6.11.2 not compatible with 6.11.3` | 静态 port 要同版本 host 工具；host 动态 qtbase 是 10-31 装的 6.11.2 | **port 侧**：vcpkg `windows.cmake` 补丁设 `QT_NO_PACKAGE_VERSION_CHECK`（moc/rcc 跨 patch 版兼容，省去重编动态 Qt 的 1-2h） |
| 4 | 交付 configure 仍报 Qt6 Widgets OFF（同款版本错误，但发生在消费侧） | **windows.cmake 只在 port 构建时被链入**（port 显式传 `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`），消费者构建根本不加载它 | **消费者侧**：`cmake/Dependencies.cmake` 在 `find_package(Qt6)` 前设普通变量 `QT_NO_PACKAGE_VERSION_CHECK`（带注释；版本一致的环境如 CI 是 no-op） |
| 5 | 体积门禁失败：52,764,160 > 25,165,824 | 24 MiB 是估算，没算**静态 ICU**（约 25-30 MB，vcpkg qtbase 默认 feature） | 门禁回填 56 MiB；瘦身方案（裁 `-icu`）写入文档，需与 CI 同步才做 |
| 6 | 用户打断前台命令会连带杀掉同 shell 树里的后台进程（zstd 假死、隧道断） | 进程组 | 长任务用 Bash 工具的 run_in_background（独立任务），不要用 `&` 挂在前台 shell 里 |

---

## 5. 未完成 / 风险（分层，已按第一轮框架更新）

**致命（影响能否交付）**
1. 三个 commit **未 push**：远端 `main` 仍无 UI、无交付验证成果；push 与否由用户决定。
2. CI/release workflow 的静态 Qt 流水线**仍未实跑**（本机首跑已通过，CI 侧理论风险大幅降低，但没验证过；release.yml 的产物名/门禁与本地一致，首次 release 前要盯一次）。

**重要（影响功能真实度）**
3. 窗口仍是**骨架数据**：`hubConnected=false`、`orcaState=disconnected` 硬编码；P3 server、P4 gateway 后台网络运行时、P2 Orca JSON-RPC 都未实现。
4. **P1-c 已完成（2026-11-07）**：`box_crypto`（scalarmult/beforenm/secretbox）+ `e2ee_session`（计数器会话）落地，tweetnacl 跨实现向量 `p1c-vectors.json` 逐字节对齐，ctest 8/8（新增 e2ee_session）。scrypt 在原项目 E2EE 路径零使用，已移出范围。剩余：真机 Orca 握手冒烟（P2/P4）。
5. 静态 Qt 的 **LGPLv3 可重链接义务**仍未确认（发布前必须处理）。
6. 全链路真实联调（用户定的验收标准）尚未开始；`web/` 零构建静态页未实现（P5）。

**建议（清理项）**
7. 静态产物瘦身：裁 qtbase `-icu` feature 可省约 25-30 MB（50.3 → ~25 MiB 量级），**必须与 CI 同步改**；是否值得由用户定。
8. 三个构建目录命名/清理策略仍未统一（`build/` 现在是交付配置，`build-ui/` 动态验证，`build-noqt/` 骨架）。
9. 本机 vcpkg 的 `windows.cmake` 补丁是机器本地的，`vcpkg update` 或换目录会丢；已在补丁处留注释说明。

---

## 6. 建议的下一步（按此顺序）

1. **push 三个本地 commit**（用户决定时机；push 后 CI 会第一次实跑静态流水线，盯 ci.yml）。
2. ~~P1-c~~ **已完成**（box_crypto + e2ee_session + 跨实现向量，ctest 8/8）。
3. **P3 / P4 / P2**：server HTTP-WS-hub、gateway 后台网络运行时、Orca JSON-RPC；把 `StatusProvider` 从 `LocalStatusProvider` 换成真实状态源。
4. **P5 / P6**：零构建前端、deb/systemd/Windows 自启、release 产物、全链路联调。
5. **发布前**：LGPLv3 合规、（可选）ICU 裁剪瘦身、体积门禁再收紧到实测+小余量、产物命名统一。

---

## 7. 关键文件索引（本轮新增/改动）

| 路径 | 作用 |
|---|---|
| `cmake/Dependencies.cmake` | **本轮改**：`find_package(Qt6)` 前设 `QT_NO_PACKAGE_VERSION_CHECK`（消费者侧版本差放行，带注释） |
| `gateway/CMakeLists.txt` | **本轮改**：静态分支门禁 24 MiB → 56 MiB（实测注释）；`if(TARGET Qt6::QWindowsIntegrationPlugin)` 已在静态构建中确认通过 |
| `scripts/{setup-deps,build}.ps1` | 未改；本机均实跑通过 |
| `D:\work\vcpkg\scripts\toolchains\windows.cmake` | **机器本地补丁**（port 侧放行版本差）；换 vcpkg 会丢 |
| `docs/{ARCHITECTURE,BUILD,DEPENDENCIES}.md`、`README.md` | 本轮按实测更新（版本口径、体积、验证状态） |

---

## 8. 约束（不要做）

- 不要恢复 `127.0.0.1:8790` 的 HTML 管理页，不要加「检查更新」/自更新（用户已明确删除）。
- **不要把 VPS 凭据写进仓库、文档、脚本、日志或任何落盘文件**；本轮仅会话内临时使用（askpass 即用即删），随后已配好 SSH 密钥免密，后续一律密钥登录。
- 不要改 `D:\work\orca-web`；它是只读参考。
- 不要绕过「编译失败但 ctest 跑旧二进制」的假绿门禁：构建必须先判断 BUILD_EXIT（build.ps1 已内置 throw）。
- 新增依赖前先说明用途与影响，由用户决定。
