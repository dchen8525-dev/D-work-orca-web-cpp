#include <orca/args.hpp>

#include <string_view>

namespace orca {

namespace {

bool takesValue(AppKind kind, std::string_view flag) {
    if (flag == "--config") return true;
    if (kind == AppKind::kServer) {
        return flag == "--init-config" || flag == "--public-origin";
    }
    return false;
}

bool isKnown(AppKind kind, std::string_view flag) {
    if (flag == "--version" || flag == "--help" || flag == "--config") return true;
    if (kind == AppKind::kServer) {
        return flag == "--init-config" || flag == "--public-origin";
    }
    return flag == "--local-orca";
}

void store(AppKind kind, CliOptions& out, std::string_view flag, std::string_view value) {
    if (flag == "--config") out.configPath = std::string(value);
    else if (flag == "--init-config") {
        out.initConfig = true;
        out.initConfigPath = std::string(value);
    } else if (flag == "--public-origin") {
        out.publicOrigin = std::string(value);
    } else if (kind == AppKind::kGateway && flag == "--local-orca") {
        out.localOrca = true;
    }
}

}  // namespace

CliOptions parseArgs(AppKind kind, int argc, char** argv) {
    CliOptions out;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag(argv[i]);
        if (!isKnown(kind, flag)) {
            out.errors.emplace_back("未知参数: " + std::string(flag));
            continue;
        }
        if (flag == "--version") {
            out.showVersion = true;
            continue;
        }
        if (flag == "--help") {
            out.showHelp = true;
            continue;
        }
        if (takesValue(kind, flag)) {
            if (i + 1 >= argc) {
                out.errors.emplace_back(std::string(flag) + " 需要一个参数");
                continue;
            }
            store(kind, out, flag, argv[++i]);
        } else {
            store(kind, out, flag, {});
        }
    }
    return out;
}

std::string usage(AppKind kind, const char* program) {
    std::string text = "用法: " + std::string(program);
    if (kind == AppKind::kServer) {
        text +=
            " [--config <path> | --init-config <path>] [--public-origin <origin>]\n"
            "\n"
            "  --config <path>         读取 hub 配置（部署路径 /etc/orca-web/server.json）\n"
            "  --init-config <path>    生成新配置并一次性打印初始密码（拒绝覆盖已有文件）\n"
            "  --public-origin <url>   与 --init-config 同用，写入 publicOrigin\n";
    } else {
        text +=
            " [--config <path>] [--local-orca]\n"
            "\n"
            "  --config <path>         读取 gateway 配置（默认 %APPDATA%/orca-web/gateway.json）\n"
            "  --local-orca            直接读取本机桌面 Orca 的配对材料（开发用）\n";
    }
    text +=
        "  --version               打印版本\n"
        "  --help                  显示本帮助\n";
    return text;
}

}  // namespace orca
