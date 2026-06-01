// Merak Agent CLI — 终端交互入口
#include <merak/agent_loop.hpp>
#include <merak/openai_provider.hpp>
#include <merak/anthropic_provider.hpp>
#include <merak/builtin_tools.hpp>
#include <merak/sub_agent_runner.hpp>
#include <merak/mcp_client.hpp>
#include <merak/mcp_tool_wrapper.hpp>
#include <merak/config_loader.hpp>
#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <fstream>
#include <cstring>
#include "theme/theme.hpp"
#include "output/cli_output.hpp"
#include "commands/command_registry.hpp"
#include "commands/command_router.hpp"
#include "tui/panel.hpp"
#include "tui/screen_manager.hpp"
#include "tui/panels/chat_panel.hpp"

using namespace merak;

static const char* SETTINGS_TEMPLATE = R"({
    "llm": {
        "provider": "openai",
        "api_key": "sk-your-api-key-here",
        "api_base_url": "https://api.openai.com/v1",
        "default_model": "gpt-4o"
    },
    "agent": {
        "system_prompt": "You are a helpful AI assistant. Use tools to complete tasks."
    },
    "memory": {
        "enabled": false
    }
})";

static void show_help() {
    std::cout << R"(Merak Agent v0.1.0

Usage: merak [command|config-file]

Commands:
  --init        Generate ~/.merak/settings.local.json template and exit
  --help        Show this help and exit

Config search order (low to high):
  1. Built-in defaults
  2. ~/.merak/settings.json
  3. .merak/settings.json           (searched upward from CWD)
  4. .merak/settings.local.json     (for secrets, do not commit)
  5. MERAK_* environment variables

Environment variables:
  MERAK_PROVIDER          LLM provider (openai / anthropic)
  MERAK_API_KEY           API key
  MERAK_API_BASE_URL      API base URL
  MERAK_MODEL             Default model name
  MERAK_TIMEOUT_MS        Request timeout in milliseconds
  MERAK_SYSTEM_PROMPT     System prompt
  MERAK_PERMISSION_MODE   Permission mode (auto / ask / deny)

Examples:
  merak                          # auto-load config
  merak --init                   # generate template in ~/.merak/
  merak /path/to/custom.json     # load specific config file
)";
}

static void do_init() {
    namespace fs = std::filesystem;

    // 优先使用 ~/.merak/（用户级配置）
    fs::path dir;
    const char* home = std::getenv("HOME");
    if (home) {
        dir = fs::path(home) / ".merak";
    } else {
        dir = ".merak";
    }

    fs::path file = dir / "settings.local.json";

    if (fs::exists(file)) {
        std::cerr << "Already exists: " << file << std::endl;
        std::cerr << "Remove it first if you want a fresh template." << std::endl;
        return;
    }

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "Failed to create " << dir << ": " << ec.message() << std::endl;
        return;
    }

    std::ofstream f(file);
    if (!f) {
        std::cerr << "Failed to write " << file << std::endl;
        return;
    }
    f << SETTINGS_TEMPLATE;
    f.close();

    std::cout << "Created " << file << std::endl;
    std::cout << "Edit this file to set your API key and model, then run: merak" << std::endl;
    std::cout << std::endl;
    std::cout << "Config search order:" << std::endl;
    std::cout << "  " << dir / "settings.json          (user-level, shared across projects)" << std::endl;
    std::cout << "  " << dir / "settings.local.json    (user-level secrets)" << std::endl;
    std::cout << "  .merak/settings.json               (project-level, shared via git)" << std::endl;
    std::cout << "  .merak/settings.local.json         (project-level secrets)" << std::endl;
}

static void run_repl(
    const Config& cfg,
    std::shared_ptr<LlmProvider> llm,
    std::shared_ptr<ToolRegistry> registry,
    std::shared_ptr<MemoryStore> memory,
    std::shared_ptr<ContextAssembler> ctx,
    std::shared_ptr<Compactor> comp)
{
    AgentLoop::Config loop_cfg;
    loop_cfg.system_prompt = cfg.agent.system_prompt;
    loop_cfg.max_turns = cfg.agent.max_tool_turns;
    loop_cfg.default_model = cfg.llm.default_model;

    auto loop = std::make_unique<AgentLoop>(
        loop_cfg, llm, registry, memory, ctx, comp);

    AgentLoop::Callbacks cbs;
    cbs.on_text_delta = [](std::string text) {
        std::cout << text << std::flush;
    };
    cbs.on_tool_start = [](ToolCall tc) {
        std::cerr << theme::run_prefix() << tc.name << std::flush;
    };
    cbs.on_tool_end = [](ToolResult tr) {
        if (tr.is_error) {
            std::cerr << " " << theme::styled(theme::ANSI_ERROR, "failed") << "\n";
        } else {
            std::cerr << " " << theme::styled(theme::ANSI_SUCCESS, "done") << "\n";
        }
    };
    cbs.on_state_change = [](TurnState /*from*/, TurnState /*to*/) {
        // Debug hook — muted in release
    };
    cbs.on_permission_ask = [](ToolCall tc) -> bool {
        std::cerr << "\n" << theme::warn_prefix() << "Allow " << tc.name << "? (y/n) " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        return answer == "y" || answer == "yes";
    };
    loop->set_callbacks(cbs);

    cli::section("Session");
    std::string user_input;
    std::cout << "\n> " << std::flush;

    while (std::getline(std::cin, user_input)) {
        if (user_input.empty()) {
            std::cout << "> " << std::flush;
            continue;
        }
        if (user_input == "/exit" || user_input == "/quit") break;
        if (user_input == "/help") {
            std::cerr << "\n";
            for (auto& cmd : commands::all_commands()) {
                std::string line = cmd.name;
                if (cmd.arg_hint) line += " " + *cmd.arg_hint;
                cli::kv(line, cmd.description);
            }
            cli::rule();
            cli::dim("/ Cmd palette  Ctrl+O Context  F1 Help  Ctrl+D Exit");
            std::cout << "> " << std::flush;
            continue;
        }
        if (user_input == "/memory") {
            std::cerr << "Working memory: " << memory->message_count()
                << " messages\n" << std::flush;
            std::cout << "> " << std::flush;
            continue;
        }
        if (user_input == "/tools") {
            std::cerr << "\n";
            auto specs = registry->all_tools();
            cli::section("Tools (" + std::to_string(specs.size()) + ")");
            for (auto& spec : specs) {
                cli::bullet(spec.name + " (" + spec.source + ")");
            }
            std::cout << "> " << std::flush;
            continue;
        }

        std::cout << "\n";
        auto response_future = loop->run(user_input);
        auto response = response_future.get();

        std::cerr << theme::styled(theme::ANSI_BORDER, "─── ")
                  << theme::styled(theme::ANSI_DIM,
                      std::to_string(response.total_input_tokens) + " tok in · "
                      + std::to_string(response.total_output_tokens) + " tok out");
        if (!response.tool_results.empty()) {
            std::cerr << theme::styled(theme::ANSI_DIM,
                " · " + std::to_string(response.tool_results.size()) + " tools");
        }
        std::cerr << theme::styled(theme::ANSI_BORDER, " ───") << "\n";
        std::cout << "\n> " << std::flush;
    }
    std::cout << "\nGoodbye!" << std::endl;
}

int main(int argc, char* argv[]) {
    // ——— 命令行参数处理 ———
    if (argc > 1) {
        if (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
            show_help();
            return 0;
        }
        if (std::strcmp(argv[1], "--init") == 0) {
            do_init();
            return 0;
        }
    }

    // ——— 1. 加载配置 ———
    auto cfg_result = ConfigLoader::load();
    bool found_config = cfg_result.has_value();
    Config cfg = found_config ? cfg_result.value() : ConfigLoader::default_config();

    if (argc > 1) {
        auto extra = ConfigLoader::load_file(argv[1]);
        if (extra.has_value()) {
            cfg = extra.value();
            found_config = true;
        }
    }

    // ——— 首次运行引导 ———
    if (!found_config || cfg.llm.api_key.empty()) {
        std::cerr << "\n";
        cli::section("Merak Agent v0.2.0");
        cli::info("No configuration found. To get started:");
        cli::numbered(1, "Generate a template: merak --init");
        cli::numbered(2, "Edit ~/.merak/settings.local.json and set your API key");
        cli::numbered(3, "Run merak again");
        cli::dim("Or set environment variables: export MERAK_API_KEY=sk-...");
        return cfg.llm.api_key.empty() ? 1 : 0;
    }

    cli::section("Merak Agent v0.2.0");
    cli::kv("Provider", cfg.llm.provider);
    cli::kv("Model", cfg.llm.default_model);
    cli::kv("Memory", cfg.memory.enabled ? "enabled" : "disabled");
    cli::kv("MCP Servers", std::to_string(cfg.mcp_servers.size()));
    cli::rule();

    // ——— 2. 初始化 Provider ———
    std::shared_ptr<LlmProvider> llm;
    if (cfg.llm.provider == "anthropic") {
        llm = std::make_shared<AnthropicProvider>(cfg.llm);
    } else {
        llm = std::make_shared<OpenAIProvider>(cfg.llm);
    }

    // ——— 3. 初始化工具注册表 ———
    auto registry = std::make_shared<ToolRegistry>();
    registry->register_tool(std::make_unique<tools::ReadFileTool>());
    registry->register_tool(std::make_unique<tools::WriteFileTool>());
    registry->register_tool(std::make_unique<tools::EditFileTool>());
    registry->register_tool(std::make_unique<tools::GlobTool>());
    registry->register_tool(std::make_unique<tools::GrepTool>());
    registry->register_tool(std::make_unique<tools::BashTool>());

    cli::ok(std::to_string(registry->size()) + " tools loaded");
    cli::dim("(read_file, write_file, edit_file, glob, grep, bash)");

    // ——— 3.5 MCP Server 连接 ———
    std::vector<std::shared_ptr<McpClient>> mcp_clients;
    for (auto& mcp_cfg : cfg.mcp_servers) {
        if (!mcp_cfg.enabled) continue;
        auto client = std::make_shared<McpClient>(mcp_cfg);
        auto conn_result = client->connect();
        if (conn_result.has_value()) {
            auto import_future = registry->import_from_mcp(client);
            auto import_result = import_future.get();
            if (import_result.has_value()) {
                cli::ok("MCP '" + mcp_cfg.name + "': " + std::to_string(import_result.value()) + " tools");
                mcp_clients.push_back(client);
            } else {
                cli::err("MCP '" + mcp_cfg.name + "' import failed: " + import_result.error().what());
            }
        } else {
            cli::err("MCP '" + mcp_cfg.name + "' connect failed: " + conn_result.error().what());
        }
    }

    // ——— 4. 初始化记忆 ———
    auto memory = std::make_shared<MemoryStore>(
        cfg.memory,
        nullptr  // EmbeddingProvider — 暂未实现
    );
    if (cfg.memory.enabled && !cfg.memory.db_connection.empty()) {
        try {
            auto db_result = memory->init_db();
            if (!db_result.has_value()) {
                cli::warn("Memory init failed: " + std::string(db_result.error().what()));
                cfg.memory.enabled = false;
            }
        } catch (const std::exception& e) {
            cli::warn("Memory init exception: " + std::string(e.what()));
            cfg.memory.enabled = false;
        }
    } else if (cfg.memory.enabled) {
        cli::warn("Memory disabled: no db_connection configured");
        cfg.memory.enabled = false;
    }

    // ——— 5. 初始化上下文 + 压缩 ———
    auto counter = std::make_shared<TokenCounter>();
    TokenBudget budget{128000, 0.15, 0.20};
    auto ctx = std::make_shared<ContextAssembler>(budget, counter);
    auto comp = std::make_shared<Compactor>(llm, counter);

    // ——— 7. Main loop (TUI or REPL) ———
    if (!theme::is_tty()) {
        run_repl(cfg, llm, registry, memory, ctx, comp);
    } else {
        AgentLoop::Config loop_cfg;
        loop_cfg.system_prompt = cfg.agent.system_prompt;
        loop_cfg.max_turns = cfg.agent.max_tool_turns;
        loop_cfg.default_model = cfg.llm.default_model;

        auto loop = std::make_unique<AgentLoop>(
            loop_cfg, llm, registry, memory, ctx, comp
        );

        auto chat = std::make_unique<tui::ChatPanel>();
        auto* chat_ptr = chat.get();
        tui::ScreenManager tui(std::move(chat));

        tui.status_bar().set_provider(cfg.llm.provider);
        tui.status_bar().set_model(cfg.llm.default_model);
        tui.status_bar().set_token_info("0/128k");

        // Welcome banner
        chat_ptr->add_line("");
        chat_ptr->add_line("    ▟██▙        ");
        chat_ptr->add_line("   ▟█▀▀█▙       ");
        chat_ptr->add_line("  ▟█▘  ▝█▙      ");
        chat_ptr->add_line("  ▀▀████▀▀      ");
        chat_ptr->add_line("    Merak       ");
        chat_ptr->add_line("");
        chat_ptr->add_line("  Merak Agent v0.2.0");
        chat_ptr->add_line("  " + cfg.llm.provider + " · " + cfg.llm.default_model);
        chat_ptr->add_line("");
        chat_ptr->add_line("  /help for commands  / palette  Ctrl+O context  Esc back");
        chat_ptr->add_line("  Type a message to start, or /exit to quit.");
        chat_ptr->add_line("");

        // Wire submit callback
        chat_ptr->set_on_submit([&](std::string input) {
            if (input == "/exit" || input == "/quit") {
                tui.exit();
                return;
            }
            if (input == "/help") {
                tui.open_help();
                return;
            }
            if (input == "/model") {
                tui.open_model_selector();
                return;
            }
            if (input == "/tools") {
                tui.open_tools();
                return;
            }
            if (input == "/memory") {
                tui.open_memory();
                return;
            }
            if (input == "/") {
                tui.open_command_palette();
                return;
            }
            // Normal chat message
            if (!input.starts_with("/")) {
                auto response_future = loop->run(input);
                auto response = response_future.get();
                chat_ptr->add_line("");
                tui.status_bar().set_token_info(
                    std::to_string(response.total_input_tokens) + "/128k");
            }
        });

        tui.run();
    }

    return 0;
}
