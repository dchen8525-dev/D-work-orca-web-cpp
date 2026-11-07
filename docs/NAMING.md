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

## Release 产物（GitHub Release，沿用旧项目文件名以兼容 bootstrap 脚本）

| 文件 | 用途 |
|---|---|
| `orca-gateway-windows-x64.exe` | Windows gateway 单文件 |
| `orca-server-linux-amd64.deb` | VPS 的 deb（P6） |
| `orca-web-release.tar.gz` | 手动兜底（P6） |
| 以上各文件的 `.sha256` | 校验 |

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
| 管理页 | — | `http://127.0.0.1:8790` |

## 程序标识

| 项 | 值 |
|---|---|
| 版本注入 | CMake `-DORCA_BUILD_VERSION=<tag>`，默认 `0.0.1-dev`；tag `0.0.1` 注入 `0.0.1` |
| 版本命令 | 两个进程均支持 `--version` / `--help` |
| hub 监听 | `127.0.0.1:8788`（Caddy 反代） |
| 管理页端口 | `8790`（`ORCA_WEB_ADMIN_PORT` 可覆盖，P4） |
| 会话 cookie | `orca_web_session`（P3） |

## 为什么 release 名沿用旧的

`bootstrap-server.sh` / `bootstrap-gateway.ps1` 与 gateway 自更新逻辑按
`orca-gateway-windows-x64.exe` 这个固定 URL 下载。一次性全量替换虽然不要求新旧二进制互操作，
但沿用文件名可以让部署脚本与自更新链路零改动；P6 若改名，必须同步改这三处。
