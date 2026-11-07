# 构建

## 工具链（本机实测）

| 项 | 路径 / 版本 |
|---|---|
| Visual Studio | `C:\Program Files\Microsoft Visual Studio\18\Community`（VS 18 / 2026） |
| MSVC | `VC\Tools\MSVC\14.51.36231`，`cl.exe` 19.51.36260.0 |
| CMake | VS 自带 4.3.1-msvc1，`Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Windows SDK | 10.0.26100.0 |
| vcpkg | `D:\work\vcpkg`（2026-09-26），`x64-windows` 已装 81 个包（含动态 Qt 6.11.2）；`x64-windows-static` **为空** |
| CMake 生成器 | 不指定 `-G` 时自动选 `Visual Studio 18 2026`（多配置，用 `--config Release`） |

## 一个必须知道的环境坑

会话环境里同时存在 `HTTPS_PROXY` 和 `https_proxy`（仅大小写不同）。MSBuild 用 .NET 的
`ProcessStartInfo.EnvironmentVariables`（不区分大小写的 Hashtable）构造子进程环境，遇到这种重复键会抛：

```
MSB6001: System.ArgumentException: 已添加项。字典中的关键字:"HTTPS_PROXY"
         所添加的关键字:"https_proxy"
```

表现为 CMake 报 `The CXX compiler identification is unknown`，看着像"没装 MSVC"，实际是环境变量冲突。
`scripts/build.ps1` 在调用 cmake 前移除 `http_proxy` / `https_proxy` / `all_proxy` 三个小写别名，
保留大写代理变量。手动构建时可用：

```bash
env -u https_proxy -u http_proxy -u all_proxy cmake -S . -B build --fresh
```

另一个本机限制：PowerShell 执行策略禁止运行未签名的 `.ps1`（`UnauthorizedAccess`）。
两个脚本已通过 `Parser::ParseFile` 语法校验，但要在普通终端里执行，需要先设置一次：

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

或用 `powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1`。CI 不受此限制（GitHub Actions
直接调用 cmake，不经过本地脚本）。

## 依赖

| 平台 | 安装方式 |
|---|---|
| Windows（交付） | `scripts/setup-deps.ps1`（vcpkg `x64-windows-static`：`openssl nlohmann-json libsodium boost-beast qtbase`） |
| Windows（仅验证 UI） | 用本机已有的 `x64-windows` 动态 Qt，`-DynamicQt` 切换 `/MD` |
| Linux / CI | `scripts/setup-deps.sh`（apt，**不装 Qt**） |

`ORCA_REQUIRE_NETWORK_DEPS=OFF`（P0 默认）时，缺失的网络依赖只在 configure 阶段报告，不影响编译；
P1-c 起设为 `ON`。`ORCA_REQUIRE_QT` 同理：CI/release 设为 `ON`，缺 Qt6 Widgets 直接 configure 失败，
保证 CLI 骨架不会被当成客户端打包。

**从 P1 起 `nlohmann-json` 是编译必需**（wire 帧与 canonical JSON 用它，header-only）。
它已在 vcpkg 里装好，Linux 由 `nlohmann-json3-dev` 提供；因此构建必须带 vcpkg toolchain
（或 Linux 系统包），`-NoToolchain` 只适用于已经过去的 P0 骨架。

### Qt 的两种配置

| 开关 | CRT | Qt | 用途 |
|---|---|---|---|
| 默认 `ORCA_QT_STATIC=ON` | `/MT` | 静态 `x64-windows-static` | **交付**：单文件 exe，无 DLL |
| `-DORCA_QT_STATIC=OFF` | `/MD` | 动态 `x64-windows` | 本机验证 UI；输出目录会带 17 个 DLL（约 58 MB） |

静态 Qt 首次从源码编译约 1-2 小时，CI 用 `VCPKG_DEFAULT_BINARY_CACHE` 缓存。
静态构建还必须在 `main.cpp` 导入 `QWindowsIntegrationPlugin`（已实现，但**尚未经静态编译验证**）。

本机验证动态 Qt 时需给 offscreen smoke 指定插件路径：

```bash
QT_QPA_PLATFORM=offscreen \
QT_QPA_PLATFORM_PLUGIN_PATH="D:/work/vcpkg/installed/x64-windows/Qt6/plugins/platforms" \
ORCA_UI_SMOKE=1 ./orca-gateway.exe --config <path>
```

`ORCA_UI_SMOKE=1` 会构造完整窗口、跑一次状态刷新后立刻退出事件循环，用于无桌面环境的冒烟。

### 动态 Qt 的运行时部署（windeployqt）

动态 Qt 验证构建的 exe **不能单独拷走运行**：首次启动会以 `0xC0000409` 崩溃，
日志停在 `starting Qt client`——Qt 只在 exe 旁找 `platforms/qwindows.dll`，
vcpkg 的插件目录不会自动回退。每次**清理构建目录后**都要重新部署：

```powershell
$wdq = "D:\work\vcpkg\installed\x64-windows\tools\Qt6\bin\windeployqt.exe"
& $wdq --release --no-translations --no-system-d3d-compiler --no-opengl-sw `
       --no-compiler-runtime "D:\work\orca-cpp\build-ui\gateway\Release\orca-gateway.exe"
```

部署后写入 `platforms/`、`styles/`、`imageformats/`、`tls/`、`networkinformation/`、
`Qt6Network.dll` 等（整个输出目录约 61 MB）。交付配置（静态 Qt）没有这一步——
这正是选择静态链接的原因。

## 常用命令

```powershell
.\scripts\setup-deps.ps1                                             # 首次含静态 Qt，约 1-2 小时
.\scripts\build.ps1 -RequireNetworkDeps -RequireQt                    # 交付配置（x64-windows-static）
# setup-deps.ps1 -SkipQt 可只装网络依赖，先跳过耗时的静态 Qt 编译
.\scripts\build.ps1 -Triplet x64-windows -DynamicQt -RequireQt       # 本机 UI 验证（非发布配置）
```

```bash
bash scripts/build.sh                    # Linux：configure + build + ctest + size gate
ORCA_VERSION=0.0.1 bash scripts/build.sh
```

### 版本号与本地缓存

`ORCA_BUILD_VERSION` 默认是 `project(VERSION)` + `-dev`；tag 构建由 CI 用
`github.ref_name` 注入。因为 `-D` 写的是 **cache 变量**，手动改过
`CMakeLists.txt` 的 `VERSION` 后，旧值可能仍留在 `build/CMakeCache.txt`，
表现为 `--version` 显示旧版本而测试却全绿。清理方式：

```bash
cmake -S . -B build -U ORCA_BUILD_VERSION
```

`smoke` 测试同时校验「注入值」与「`project()` 版本」两者一致，能抓到这种缓存钉住。

## 体积门禁

`cmake/SizeReport.cmake` 在两个可执行目标的 POST_BUILD 自动运行：

| 目标 | 上限 |
|---|---|
| `orca-server` / 无 Qt 的 CLI 骨架 | 8 MiB |
| `orca-gateway` 链接**静态 Qt** 时 | 24 MiB（估算，首次静态构建后按实测收紧） |
| `orca-gateway` 动态 Qt 验证构建 | 8 MiB（DLL 体积不计入 exe 本体） |
| deb | 10 MiB（P6 打包步骤用同一脚本） |

```
-- [orca-size] size: 251392 bytes (245.50 KiB) / budget: 8388608 bytes / verdict: OK
```

本机实测：CLI 骨架 `orca-gateway.exe` 251,392 B、`orca-server.exe` 246,272 B；
动态 Qt 验证构建的 gateway exe 为 240,128 B（旁边 17 个 DLL 约 58 MB，正是不发布动态配置的原因）。

## 编译器策略

Release 以**体积**为优先：MSVC `/O1` + `/OPT:REF /OPT:ICF` + LTCG；GCC `-Os` +
`-ffunction-sections -fdata-sections` + `--gc-sections`；静态链接 CRT（`/MT`）与 libstdc++/libgcc。
不引入 QML / 独立 ICU 等额外重量级组件；Qt 仅用 `qtbase` 的 Widgets 部分。
