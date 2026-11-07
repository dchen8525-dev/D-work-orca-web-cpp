# 命名契约

所有产物名、安装路径、配置文件名集中在这里定义，代码里通过 `common/include/orca/platform/paths.hpp`
统一取值，禁止在别处硬编码。改动命名必须同时改本文件、`paths.cpp`、packaging 与 CI。

## 构建产物（build 目录，不带平台后缀）

| 目标 | 文件名 | 说明 |
|---|---|---|
| hub | `orca-server`（Windows 为 `orca-server.exe`） | VPS / Linux 部署用 |
| 家里网关 | `orca-gateway`（Windows 为 `orca-gateway.exe`） | 正式交付的是 Windows 版 |
| 单元测试 | `orca_smoke_tests` | CTest 目标 |

平台由构建目录区分，不写进文件名，避免出现 `orca-gateway-x64-release.exe` 这类不一致命名。

## Release 产物

| 文件 | 用途 |
|---|---|
| `orca-gateway-<版本>-windows-amd64.exe` | Windows gateway **单文件 Portable 客户端**（静态 Qt + `/MT`，免安装、无 DLL、无 zip） |
| `orca-server-linux-amd64.deb` | VPS 的 deb（P6） |
| 以上各文件的 `.sha256` | 校验 |

`<版本>` 取 GitHub tag 名（`github.ref_name`），例如 `orca-gateway-0.0.2-windows-amd64.exe`。
每次发布文件名都带版本号，因此不同版本不会互相覆盖，也不再做自更新。
`amd64` 与 Linux 侧 `orca-server-linux-amd64.deb` 保持一致。

## 安装路径

| 内容 | Linux（deb） | Windows |
|---|---|---|
| 可执行文件 | `/opt/orca-web/dist-server/orca-server` | `%APPDATA%\orca-web\orca-gateway.exe` |
| 静态页 | `/opt/orca-web/web` | — |
| 配置 | `/etc/orca-web/server.json` | `%APPDATA%\orca-web\gateway.json` |
| 会话存储 | `/var/lib/orca-web/sessions.json` | `%APPDATA%\orca-web\sessions.json` |
| 日志 | `journalctl -u orca-web.service` | `%APPDATA%\orca-web\gateway.log` |
| systemd unit | `/lib/systemd/system/orca-web.service` | — |
| 自启 | — | Startup 里的 `orca-web-gateway.vbs` + `gateway-loop.cmd`（P6） |
| 客户端界面 | — | 同进程 Qt Widgets 窗口（不监听端口、无 URL） |

## 程序标识

| 项 | 值 |
|---|---|
| 版本注入 | CMake `-DORCA_BUILD_VERSION=<tag>`，默认 `0.0.1-dev`；tag `0.0.1` 注入 `0.0.1` |
| 版本命令 | 两个进程均支持 `--version` / `--help` |
| hub 监听 | `127.0.0.1:8788`（Caddy 反代） |
| 会话 cookie | `orca_web_session`（P3） |
| gateway 自检 | `--version` / `--help`（console 子系统，可管道化） |

## 为什么不再沿用旧文件名

旧项目用固定 URL `orca-gateway-windows-x64.exe` 是为了让 `bootstrap-gateway.ps1` 与
gateway 自更新逻辑零改动。现在**自更新已移除**、gateway 改成免安装的 Portable 单 exe，
发布物按 tag 带版本号，用户每次手动下载替换即可。因此 P6 的 Windows 自启脚本必须按
新命名生成快捷方式/启动项，不能再硬编码旧 URL；`bootstrap-gateway.ps1` 里
`Start-Process` 打开 HTML 管理页的步骤也应删除（改由 exe 自己弹窗）。

改动命名必须同时改本文件、`.github/workflows/release.yml`、`scripts/build.ps1` 与
`common/src/platform/paths.cpp`，避免出现不一致命名。
