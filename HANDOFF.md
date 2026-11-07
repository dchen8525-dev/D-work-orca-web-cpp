# HANDOFF — 当前进展交接

> 生成时间：2026-11-07 15:50 GMT+8
> 仓库：`D:\work\orca-cpp`（分支 `main`，远端 `git@github.com:dchen8525-dev/D-work-orca-web-cpp.git`）
> 本文件是**当前状态的权威快照**；`README.md` 是设计说明，细节以本文件为准。
> 原项目 `D:\work\orca-web` 保持只读参考，不要改动。

---

## 0. 一句话状态

Qt Widgets 版 gateway 的配置窗口**已经在本机编译成功并真实弹出过**（窗口标题「orca 网关管理」），`build-ui` 的 CTest 7/7 全绿；但这是**动态 Qt 验证构建**（exe 旁有 61 MB DLL），**交付配置（静态 Qt 单 exe）尚未编译验证**。所有 Qt UI 改动**仍未 commit / 未 push**，远端只有 `0.0.1` 的旧骨架。

---

## 1. 本轮实际做了什么（带证据）

| # | 动作 | 命令 / 事实 | 结果 |
|---|---|---|---|
| 1 | 重新编译 GUI | `cmake --build D:\work\orca-cpp\build-ui --config Release --target orca-gateway`（VS 自带 cmake） | `BUILD_EXIT=0`；POST_BUILD 体积门禁 `240128 bytes (234.50 KiB)` / budget 8 MiB / `verdict OK` |
| 2 | 首次启动 | `Start-Process orca-gateway.exe` | **崩溃**，退出码 `-1073740791` = `0xC0000409`；日志只写到 `starting Qt client`，停在 `QApplication` 之前 |
| 3 | 抓诊断 | `QT_DEBUG_PLUGINS=1` + 重定向 stderr | `Could not find the Qt platform plugin "windows" in ""`；Qt 只检查 `.../Release/platforms` |
| 4 | 补运行时 | `windeployqt.exe --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime <exe>` | 写入 `platforms/qwindows.dll`、`styles/qmodernwindowsstyle.dll`、`imageformats/`、`tls/`、`generic/`、`networkinformation/`、`Qt6Network.dll` 等 |
| 5 | 再次启动 | 同上 | **成功**：`PID=4536`，`MainWindowHandle=2230184`，`MainWindowTitle=[orca 网关管理]`，进程 `still running` |
| 6 | 复核测试 | `ctest --test-dir build-ui -C Release --output-on-failure` | **7/7 Passed**（smoke / crypto / gateway_core / protocol / server_version / gateway_version / server_unknown_flag），0.20 s |
| 7 | 窗口截图 | 多次尝试 | **未完成**，见 §4；未产出 PNG，未改动任何源码 |

补充事实（本次核对）：
- `orca-gateway.exe --version` → `orca-gateway 0.0.1-dev`（`build-ui` 配置时注入的是 dev 版本；release tag 是 `0.0.1`，两者不同是**预期**，见 §4）。
- exe：`240128 B`，时间戳 `2026-11-07 15:24:13`；部署后整个 `Release/` 目录 `61 MB`。
- 窗口进程 **PID 4536 现已退出**（本轮结束时 `tasklist` 查不到）。想再看窗口，按 §3.4 重新启动即可。
- 本机 `vcpkg list` 实测 `qtbase:x64-windows = 6.11.2`；`README.md` 写的是 6.11.3（见 §4 的已知不一致）。
- `D:\work\vcpkg\installed\x64-windows-static` **不存在**，静态 Qt 从未在本机安装。

---

## 2. 当前工作树（**最重要**）

```
HEAD  f11dc93 Fix CI size gate, add .gitattributes, align version to 0.0.1
tag   0.0.1  (pre-release 已发布：https://github.com/dchen8525-dev/D-work-orca-web-cpp/releases/tag/0.0.1)
remote origin  git@github.com:dchen8525-dev/D-work-orca-web-cpp.git
```

`git status --short` = **19 项，全部未提交**：

```
 M .github/workflows/ci.yml              M .github/workflows/release.yml
 M CMakeLists.txt                        M README.md
 M cmake/Dependencies.cmake              M cmake/SizeReport.cmake
 M common/include/orca/log.hpp           M common/src/log.cpp
 M docs/ARCHITECTURE.md                  M docs/BUILD.md
 M docs/DEPENDENCIES.md                  M docs/NAMING.md
 M gateway/CMakeLists.txt                M gateway/main.cpp
 M scripts/build.ps1                     M scripts/setup-deps.ps1
 M tests/CMakeLists.txt
?? gateway/ui/                           ?? tests/unit/gateway_core_test.cpp
```

**风险**：远端 `main` 仍然是 `f11dc93`（P1-b CLI 骨架，**没有任何 Qt UI**）。
如果接手的 agent 在另一台机器 clone 仓库，会完全看不到这 19 项改动。
→ **建议接手第一步就 `git add -A && git commit`**（是否 push 由用户决定，本轮未擅自提交）。

`gateway/ui/` 新文件清单：`config_store.{hpp,cpp}`、`pairing.{hpp,cpp}`、`status_model.{hpp,cpp}`、`main_window.{hpp,cpp}`。

---

## 3. 环境与可复现命令

### 3.1 环境事实

| 项 | 值 |
|---|---|
| 源码根 | `D:\work\orca-cpp` |
| 原项目（只读） | `D:\work\orca-web` |
| vcpkg | `D:\work\vcpkg`；已装 `qtbase:x64-windows 6.11.2`、`openssl 3.6.5#2`、`nlohmann-json 3.12.0#2` |
| 静态 triplet | `x64-windows-static` **未安装**（交付配置被卡在这里） |
| MSVC | VS 18 Community，MSBuild 18.11.1 |
| cmake / ctest | `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\` 下；**不在 Git Bash 的 PATH** |
| 三个构建目录 | `build/`（P0 老配置）、`build-ui/`（**当前 GUI 验证配置**）、`build-noqt/`（无 Qt CLI 骨架） |
| `build-ui` 关键 CMake 变量 | `ORCA_BUILD_GATEWAY_UI=ON`、`ORCA_QT_STATIC=OFF`、`ORCA_REQUIRE_QT=ON`、`ORCA_REQUIRE_NETWORK_DEPS=OFF`、`ORCA_SIZE_GATE=ON`、`VCPKG_TARGET_TRIPLET=x64-windows` |

### 3.2 重新编译 GUI（PowerShell，必须）

```powershell
Remove-Item Env:http_proxy,Env:https_proxy,Env:all_proxy -ErrorAction SilentlyContinue
$cmake = Join-Path $env:ProgramFiles "Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake --build "D:\work\orca-cpp\build-ui" --config Release --target orca-gateway
# 成功标志：BUILD_EXIT=0 且出现 "-- [orca-size] verdict : OK"
```

### 3.3 部署 Qt 运行时（**每次清理构建目录后都要重做**，否则启动必崩）

```powershell
$wdq = "D:\work\vcpkg\installed\x64-windows\tools\Qt6\bin\windeployqt.exe"
& $wdq --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime `
        "D:\work\orca-cpp\build-ui\gateway\Release\orca-gateway.exe"
```

也可以直接双击 `D:\work\orca-cpp\build-ui\gateway\Release\orca-gateway.exe` —— **目前该目录已部署好**，可直接运行；但只有 exe 本体拷走会失败。

### 3.4 打开窗口 / 验证

```powershell
$p = Start-Process -FilePath "D:\work\orca-cpp\build-ui\gateway\Release\orca-gateway.exe" -PassThru
$p.WaitForInputIdle(10000) | Out-Null
(Get-Process -Id $p.Id).MainWindowTitle   # 期望 "orca 网关管理"
```

窗口是 700×820 的暖白（`#faf7f1`）Qt Widgets 页：状态总览 + hub 连接 + 桌面 Orca 配对 + 本机信息四张卡片，3 秒刷新，**没有「检查更新」按钮**，没有 `127.0.0.1:8790`。当前状态全部是**骨架数据**（`hubConnected=false`、`orcaState=disconnected`），`LocalStatusProvider::snapshot()` 只真实读 `gateway.json`，连接状态等 P4 接入。

### 3.5 常用验证命令

```powershell
# CLI 契约（不创建 QApplication，stdout 可管道）
& "...\orca-gateway.exe" --version        # orca-gateway 0.0.1-dev
& "...\orca-gateway.exe" --help

# 无头 smoke（构造完整窗口后退出事件循环）
$env:QT_QPA_PLATFORM = "offscreen"; $env:ORCA_UI_SMOKE = "1"
& "...\orca-gateway.exe"; Remove-Item Env:QT_QPA_PLATFORM,Env:ORCA_UI_SMOKE

# 全部单测
& $ctest --test-dir "D:\work\orca-cpp\build-ui" -C Release --output-on-failure   # 7/7
```

日志：`%APPDATA%\orca-web\gateway.log`；配置：`%APPDATA%\orca-web\gateway.json`（不存在时由 UI 首次保存创建）。

### 3.6 交付配置（**尚未跑通**）

```powershell
.\scripts\setup-deps.ps1                       # 需从源码编译静态 Qt，约 1-2 小时，本机未执行
.\scripts\build.ps1 -RequireNetworkDeps -RequireQt
```

该配置 `ORCA_QT_STATIC=ON` → `/MT` + `Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)`，产物 `orca-gateway-<版本>-windows-amd64.exe`。
CI/release workflow 已按静态 Qt 改写，但**首次静态构建从未实跑**，`24 MiB` 门禁是估算值。

---

## 4. 踩过的坑（接手前请读）

| 现象 | 原因 | 处理 |
|---|---|---|
| Git Bash 里 `cmake: No such file or directory` | cmake 未进 Git Bash PATH | 用 PowerShell + VS cmake 绝对路径 |
| MSBuild / `Start-Process` 报「已添加项。字典中的关键字: HTTPS_PROXY / https_proxy」 | .NET 环境变量字典大小写不敏感，大写与小写代理变量同时存在 | 每个 PowerShell 命令开头 `Remove-Item Env:http_proxy,Env:https_proxy,Env:all_proxy`；Bash 构建用 `env -u ...` |
| 首次启动 `0xC0000409` | Qt6 只查 exe 旁的 `platforms/`，`build-ui` 里没有；vcpkg 的插件路径不会自动回退 | `windeployqt`（§3.3） |
| `Add-Type` / `New-Object -ComObject WScript.Shell` 被安全策略拒绝 | 宿主禁止运行时编译 .NET 代码与 COM 实例化 | 本轮**截图因此未做成**；若要截图，用纯 Python `ctypes` + GDI（不需要 Add-Type/COM），或给 Qt 加一个 offscreen `QWidget::grab()` 钩子（需改 `gateway/main.cpp`，本轮未改） |
| `--version` 输出 `0.0.1-dev` 而 tag 是 `0.0.1` | `build-ui` 配置时 `ORCA_BUILD_VERSION` 用的是默认值；版本由 configure 注入，改版本要重新 configure | 交付构建由 `build.ps1 -Version 0.0.1` 注入，smoke 门禁校验一致性 |
| 控制台窗口与 GUI 共存 | 为保证 `--version/--help` 可管道，exe 用 **console subsystem**，`main.cpp` 在 `window.show()` 后调用 `hideConsoleWindow()` | 别改回 `WIN32_EXECUTABLE`，会破坏 CI 版本门禁 |
| `README.md` 写 Qt 6.11.3 | 实测 `vcpkg list` 是 **6.11.2** | 小不一致，未擅自改；建议下次文档更新时顺手校正 |
| 静态 Qt 分支 `Q_IMPORT_PLUGIN` | 只在 `ORCA_QT_STATIC=ON` 时编译 | 该分支**从未编译验证过**，是本仓最大的未知项 |

---

## 5. 未完成 / 风险（分层）

**致命（影响能否交付）**
1. `x64-windows-static` 静态 Qt 未安装，交付配置（单 exe / `/MT`）**从未编译**；`Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)` 分支未验证，`24 MiB` 门禁未实测。
2. 19 项 Qt UI 改动**未提交**，远端 `main` 没有 UI；换机器/换 agent 从远端 clone 会全部丢失。

**重要（影响功能真实度）**
3. 窗口目前是**UI 骨架**：`LocalStatusProvider::snapshot()` 里 `hubConnected=false`、`orcaState="disconnected"` 是硬编码，配置读写/配对码解析/状态判定是真的，但**没有任何真实网络连接**（P3 server、P4 gateway 后台、P2 Orca JSON-RPC 都未实现）。
4. P1-c 的 `crypto_box_beforenm` / secretbox / scrypt 依赖 libsodium + 静态 OpenSSL，同样卡在静态 triplet。
5. 静态 Qt 的 **LGPLv3 可重链接义务**尚未确认（正式发布前必须处理）。
6. 全链路真实联调（用户定的验收标准）尚未开始；`web/` 仍是零构建静态页的说明与约束，页面本体未实现（P5）。

**建议（清理项）**
7. `README.md` 的 Qt 版本 6.11.3 → 6.11.2；`docs/BUILD.md` 可补一节 §3.3 的 windeployqt 步骤。
8. 三个构建目录 `build/`、`build-ui/`、`build-noqt/` 都在 `.gitignore` 里，状态各异；建议统一命名/清理策略。
9. 截图工具未落盘；若需要「UI 视觉回归」产物，建议做成 offscreen grab 的 CTest，而不是依赖 Windows 截屏 API。

---

## 6. 建议的下一步（按此顺序）

1. **先保存**：`git add -A && git commit`（至少本地落盘）；是否 push / 打新 tag 由用户决定。
2. **装静态 Qt**：`scripts\setup-deps.ps1`（不带 `-SkipQt`），确认 `x64-windows-static` 就位。
3. **跑交付配置**：`.\scripts\build.ps1 -RequireNetworkDeps -RequireQt`，验证 `Q_IMPORT_PLUGIN` 分支、单 exe 可独立运行、回填 `24 MiB` 门禁实测值。
4. **P1-c**：静态 libsodium/OpenSSL → E2EE v2 真实加解密 + golden 向量。
5. **P3 / P4 / P2**：server HTTP-WS-hub、gateway 后台网络运行时、Orca JSON-RPC；把 `StatusProvider` 从 `LocalStatusProvider` 换成真实状态源。
6. **P5 / P6**：零构建前端、deb/systemd/Windows 自启、release 产物、全链路联调。
7. **发布前**：LGPLv3 合规、体积门禁收紧、产物命名统一（`orca-gateway-<版本>-windows-amd64.exe`）。

---

## 7. 关键文件索引

| 路径 | 作用 |
|---|---|
| `CMakeLists.txt` | `ORCA_BUILD_GATEWAY_UI` / `ORCA_QT_STATIC` / `ORCA_REQUIRE_QT` / CRT 选择（`/MT` vs `/MD`） |
| `gateway/CMakeLists.txt` | Qt 目标定义、AUTOMOC、静态插件链接、体积门禁上限 |
| `gateway/main.cpp` | CLI 契约 → `QApplication` → `MainWindow` → 隐藏控制台 → `ORCA_UI_SMOKE` |
| `gateway/ui/main_window.cpp` | 四卡片 UI、QSS、表单交互、3s `QTimer` |
| `gateway/ui/config_store.cpp` | `gateway.json` 读写、原子写、保留未知键、BOM |
| `gateway/ui/pairing.cpp` | `orca://pair?code=` base64url JSON 解码与校验 |
| `gateway/ui/status_model.cpp` | 四象限判定 + **当前骨架状态源** |
| `tests/unit/gateway_core_test.cpp` | 40 项不依赖 Qt 的断言 |
| `common/src/log.cpp` | `setLogFile()` → `%APPDATA%/orca-web/gateway.log` |
| `scripts/{setup-deps,build}.ps1` | 依赖安装（含 `-SkipQt` / `-DynamicQt`）与构建 + CTest |
| `.github/workflows/{ci,release}.yml` | 静态 Qt / 二进制缓存 / release 产物名 |
| `docs/{ARCHITECTURE,BUILD,NAMING,DEPENDENCIES,PROTOCOL}.md` | 设计、构建、命名、依赖、协议基线 |

---

## 8. 约束（不要做）

- 不要恢复 `127.0.0.1:8790` 的 HTML 管理页，不要加「检查更新」/自更新（用户已明确删除）。
- 不要把 VPS 凭据写进仓库、文档、脚本或日志；本轮全程未使用、未落盘。
- 不要改 `D:\work\orca-web`；它是只读参考。
- 不要绕过「编译失败但 ctest 跑旧二进制」的假绿门禁：构建必须先判断 `BUILD_EXIT`。
- 新增依赖前先说明用途与影响，由用户决定。
