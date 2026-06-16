#include <merak/app/application.hpp>
#include <merak/config_loader.hpp>
#include "client/runtime_client.hpp"
#include "tui/tui_runner.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace merak;

// ─── settings template ───

static const char* SETTINGS_TEMPLATE = R"({
  "llm": {"provider":"openai","api_key":"sk-your-api-key-here","default_model":"gpt-4o","max_output_tokens":4096},
  "agent": {"system_prompt":"You are a helpful AI assistant. Use tools to complete tasks."},
  "memory": {"enabled":false},
  "knowledge_graph": {
    "enabled": false,
    "neo4j": {
      "uri": "bolt://localhost:7687",
      "user": "neo4j",
      "password": "",
      "database": "merak"
    }
  },
  "tui": {
    "theme": {
      "preset": "auto",
      "accent": "yellow",
      "selected_bg": 236,
      "selected_fg": 252
    }
  }
})";

// ─── environment helpers ───

static std::filesystem::path merak_home() {
    if (auto* v = std::getenv("MERAK_HOME")) return v;
#ifdef _WIN32
    if (auto* v = std::getenv("APPDATA")) return std::filesystem::path(v) / "Merak";
    if (auto* v = std::getenv("USERPROFILE")) return std::filesystem::path(v) / ".merak";
#endif
    if (auto* v = std::getenv("HOME")) return std::filesystem::path(v) / ".merak";
    return ".merak";
}

static std::filesystem::path exe_dir_path() {
#ifdef _WIN32
    WCHAR path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return {};
    return std::filesystem::path(path).parent_path();
#else
    return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
}

// ─── CLI helpers ───

static void show_help() {
    std::cout << "Merak Agent\n\n"
              << "  merak --init\n"
              << "  merak serve [--port 3888]\n"
              << "  merak tui [--server http://127.0.0.1:3888] [--session <id>]\n";
}

static int do_init() {
    auto dir  = merak_home();
    auto file = dir / "settings.local.json";
    std::filesystem::create_directories(dir);
    if (std::filesystem::exists(file)) {
        std::cerr << "Already exists: " << file << "\n";
        return 1;
    }
    std::ofstream out(file);
    out << SETTINGS_TEMPLATE;
    std::cout << "Created " << file << "\n";
    return 0;
}

static Config load_config() {
    auto result = ConfigLoader::load();
    if (!result.has_value())
        throw std::runtime_error(result.error().what());
    auto cfg = result.value();
    if (cfg.llm.api_key.empty())
        throw std::runtime_error("Missing API key. Run merak --init.");
    return cfg;
}

static int parse_port(int argc, char** argv) {
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--port")
            return std::stoi(argv[i + 1]);
    return 3888;
}

static std::string option(int argc, char** argv,
                          const std::string& name,
                          const std::string& fallback = "") {
    for (int i = 2; i + 1 < argc; ++i)
        if (name == argv[i]) return argv[i + 1];
    return fallback;
}

// Wait for the embedded HTTP server to become ready.
static bool wait_for_server(const std::string& url,
                            std::chrono::seconds timeout = std::chrono::seconds(5)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            client::RuntimeClient api(url);
            api.metadata();  // will throw if not reachable
            return true;
        } catch (...) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return false;
}

// ─── main ───

int main(int argc, char** argv) {
    try {
        if (argc < 2) { show_help(); return 1; }
        std::string cmd = argv[1];

        if (cmd == "--help" || cmd == "-h") { show_help(); return 0; }
        if (cmd == "--init") return do_init();

        auto cfg = load_config();
        app::Application::Options opts;
        opts.home_dir = merak_home();
        opts.exe_dir  = exe_dir_path();
        opts.port     = parse_port(argc, argv);

        if (cmd == "serve") {
            app::Application app(std::move(cfg), std::move(opts));
            app.serve();
            return 0;
        }

        if (cmd == "tui") {
            auto server     = option(argc, argv, "--server", "");
            auto session_id = option(argc, argv, "--session", "");

            if (server.empty()) {
                // Embedded mode: start the server in-process on a background
                // thread, then connect the TUI to localhost.
                app::Application app(std::move(cfg), std::move(opts));
                app.start();

                std::thread http_thread([&app] { app.serve(); });

                auto local_url = "http://127.0.0.1:" + std::to_string(opts.port);
                if (!wait_for_server(local_url)) {
                    std::cerr << "Error: embedded server failed to start\n";
                    app.stop();
                    http_thread.join();
                    return 1;
                }

                tui::run_tui(local_url, session_id);

                app.stop();
                http_thread.join();
            } else {
                // Remote mode: connect to an existing merak serve instance.
                tui::run_tui(server, session_id);
            }
            return 0;
        }

        show_help();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
