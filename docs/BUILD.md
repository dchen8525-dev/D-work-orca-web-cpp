# 构建

## 工具链（本机实测）

| 项 | 路径 / 版本 |
|---|---|
| Visual Studio | `C:\Program Files\Microsoft Visual Studio\18\Community`（VS 18 / 2026） |
| MSVC | `VC\Tools\MSVC\14.51.36231`，`cl.exe` 19.51.36260.0 |
| CMake | VS 自带 4.3.1-msvc1，`Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Windows SDK | 10.0.26100.0 |
| vcpkg | `D:\work\vcpkg`（2026-09-26），已装 77 个包，只有 `x64-windows` triplet |
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
| Windows | `scripts/setup-deps.ps1`（vcpkg，`x64-windows-static`） |
| Linux / CI | `scripts/setup-deps.sh`（apt） |

`ORCA_REQUIRE_NETWORK_DEPS=OFF`（P0 默认）时，缺失的网络依赖只在 configure 阶段报告，不影响编译；
P1-c 起设为 `ON`，缺依赖直接 configure 失败，避免带着残缺依赖进入打包。
（P1-a / P1-b 的免依赖切片用当前的 `x64-windows` + header-only `nlohmann-json` 即可编译验证。）

**从 P1 起 `nlohmann-json` 是编译必需**（wire 帧与 canonical JSON 用它，header-only）。
它已在 vcpkg 里装好，Linux 由 `nlohmann-json3-dev` 提供；因此构建必须带 vcpkg toolchain
（或 Linux 系统包），`-NoToolchain` 只适用于已经过去的 P0 骨架。

## 常用命令

```powershell
.\scripts\build.ps1                                                    # 默认带 vcpkg toolchain
.\scripts\build.ps1 -Triplet x64-windows-static -RequireNetworkDeps    # P1-c+ 完整构建
# 注意：-NoToolchain 从 P1 起会因找不到 nlohmann-json 而 configure 失败
```

```bash
bash scripts/build.sh                    # Linux：configure + build + ctest + size gate
ORCA_VERSION=v0.1.0 bash scripts/build.sh
```

## 体积门禁

`cmake/SizeReport.cmake` 在两个可执行目标的 POST_BUILD 自动运行，默认上限 8 MiB：

```
-- [orca-size] size: 251392 bytes (245.50 KiB) / budget: 8388608 bytes / verdict: OK
```

deb 的 10 MiB 门禁在 P6 的打包步骤里用同一脚本校验。P0 实测：
`orca-gateway.exe` 251,392 B、`orca-server.exe` 246,272 B。

## 编译器策略

Release 以**体积**为优先：MSVC `/O1` + `/OPT:REF /OPT:ICF` + LTCG；GCC `-Os` +
`-ffunction-sections -fdata-sections` + `--gc-sections`；静态链接 CRT（`/MT`）与 libstdc++/libgcc。
不引入 Qt、ICU 等重库。
