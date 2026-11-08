// Static-file serving: URL paths must resolve under staticDir (a leading "/"
// is not absolute on Windows), assets must carry their real MIME type (module
// scripts and stylesheets are blocked under nosniff otherwise), and unknown
// routes must fall back to index.html for the SPA.

#include <orca/server/http_app.hpp>
#include <orca/server/hub_core.hpp>
#include <orca/server/session_store.hpp>

#include "test_util.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using orca::server::HttpApp;
using orca::server::HubCore;
using orca::server::ServerConfig;
using orca::server::SessionStore;

namespace {

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string nameOf(const orca::server::HttpResponse& response) {
    return response.fileTarget ? response.fileTarget->filename().string() : std::string();
}

}  // namespace

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "orca-http-static-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "views", ec);
    writeFile(root / "index.html", "<!doctype html><title>orca</title>");
    writeFile(root / "app.js", "export const ok = true\n");
    writeFile(root / "styles.css", "body{color:red}\n");
    writeFile(root / "manifest.webmanifest", "{\"name\":\"orca\"}\n");
    writeFile(root / "views" / "board.js", "export const board = true\n");
    writeFile(root.parent_path() / "orca-http-secret.txt", "secret");

    ServerConfig config;
    config.gatewayKey = "test-gateway-key";
    config.passwordHash = "not-used";
    config.staticDir = root.string();

    SessionStore sessions(60'000);
    HubCore hub(config.gatewayKey, [](const std::string&) {});
    HttpApp app(config, sessions, hub, [](const std::string&) {});

    const auto get = [&](const std::string& target) {
        return app.handle("GET", target, std::nullopt, std::nullopt, "127.0.0.1");
    };

    const auto js = get("/app.js");
    test::check(js.status == 200, "a root-level asset returns 200");
    test::check(nameOf(js) == "app.js", "the URL path resolves inside staticDir");
    test::check(js.fileContentType == "text/javascript; charset=utf-8",
                "module scripts are served as text/javascript");
    test::check(!js.fileNoStore, "assets keep their default caching");

    const auto css = get("/styles.css");
    test::check(css.fileContentType == "text/css; charset=utf-8",
                "stylesheets are served as text/css");

    const auto manifest = get("/manifest.webmanifest");
    test::check(manifest.fileContentType == "application/manifest+json",
                "web manifests use the manifest MIME type");

    const auto nested = get("/views/board.js");
    test::check(nameOf(nested) == "board.js", "nested module paths resolve");

    const auto query = get("/app.js?v=1");
    test::check(nameOf(query) == "app.js", "query strings do not break asset lookup");

    const auto spaIsh = get("/task/claude_abc");
    test::check(nameOf(spaIsh) == "index.html", "unknown routes fall back to index.html");
    test::check(spaIsh.fileNoStore, "index.html is never cached");

    // A traversal attempt must not escape staticDir.
    const auto traversal = get("/../orca-http-secret.txt");
    test::check(nameOf(traversal) == "index.html",
                "parent traversal stays inside staticDir");

    std::filesystem::remove_all(root, ec);
    std::filesystem::remove(root.parent_path() / "orca-http-secret.txt", ec);
    return test::finish("server_http");
}
