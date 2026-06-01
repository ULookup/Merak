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
        std::cout << "Merak Agent v0.1.0" << std::endl;
        std::cout << std::endl;
        std::cout << "No configuration found. To get started:" << std::endl;
        std::cout << std::endl;
        std::cout << "  1. Generate a template:" << std::endl;
        std::cout << "     merak --init" << std::endl;
        std::cout << std::endl;
        std::cout << "  2. Edit ~/.merak/settings.local.json and set your API key" << std::endl;
        std::cout << std::endl;
        std::cout << "  3. Run merak again" << std::endl;
        std::cout << std::endl;
        std::cout << "  Or set environment variables:" << std::endl;
        std::cout << "     export MERAK_API_KEY=sk-..." << std::endl;
        std::cout << "     export MERAK_MODEL=gpt-4o" << std::endl;
        std::cout << "     merak" << std::endl;
        return cfg.llm.api_key.empty() ? 1 : 0;
    }

    std::cout << "Merak Agent v0.1.0" << std::endl;
    std::cout << "Provider: " << cfg.llm.provider << std::endl;
    std::cout << "Model: " << cfg.llm.default_model << std::endl;
    std::cout << "Memory: " << (cfg.memory.enabled ? "enabled" : "disabled") << std::endl;
    std::cout << "MCP Servers: " << cfg.mcp_servers.size() << std::endl;
    std::cout << "Type /help for commands, Ctrl+D to exit" << std::endl;
    std::cout << "---" << std::endl;

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

    std::cout << "Tools loaded: " << registry->size() << std::endl;

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
                std::cout << "MCP '" << mcp_cfg.name << "': "
                    << import_result.value() << " tools" << std::endl;
                mcp_clients.push_back(client);
            } else {
                std::cerr << "MCP '" << mcp_cfg.name << "' import failed: "
                    << import_result.error().what() << std::endl;
            }
        } else {
            std::cerr << "MCP '" << mcp_cfg.name << "' connect failed: "
                << conn_result.error().what() << std::endl;
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
                std::cerr << "Memory init failed: " << db_result.error().what()
                    << " — continuing without long-term memory" << std::endl;
                cfg.memory.enabled = false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Memory init exception: " << e.what()
                << " — continuing without long-term memory" << std::endl;
            cfg.memory.enabled = false;
        }
    } else if (cfg.memory.enabled) {
        std::cerr << "Memory disabled: no db_connection configured" << std::endl;
        cfg.memory.enabled = false;
    }

    // ——— 5. 初始化上下文 + 压缩 ———
    auto counter = std::make_shared<TokenCounter>();
    TokenBudget budget{128000, 0.15, 0.20};
    auto ctx = std::make_shared<ContextAssembler>(budget, counter);
    auto comp = std::make_shared<Compactor>(llm, counter);

    // ——— 6. 初始化 Loop ———
    AgentLoop::Config loop_cfg;
    loop_cfg.system_prompt = cfg.agent.system_prompt;
    loop_cfg.max_turns = cfg.agent.max_tool_turns;
    loop_cfg.default_model = cfg.llm.default_model;

    auto loop = std::make_unique<AgentLoop>(
        loop_cfg, llm, registry, memory, ctx, comp
    );

    // 设置回调 — 流式渲染
    AgentLoop::Callbacks cbs;
    cbs.on_text_delta = [](std::string text) {
        std::cout << text << std::flush;
    };
    cbs.on_tool_start = [](ToolCall tc) {
        std::cout << "\n[Tool " << tc.name << "]" << std::flush;
    };
    cbs.on_tool_end = [](ToolResult tr) {
        if (tr.is_error) {
            std::cout << " error: " << tr.output.substr(0, 80) << std::endl;
        } else {
            std::cout << " done" << std::endl;
        }
    };
    cbs.on_state_change = [](TurnState from, TurnState to) {
        // 调试用，正式版可隐藏
    };
    cbs.on_permission_ask = [](ToolCall tc) -> bool {
        std::cout << "\n[Allow " << tc.name << "? (y/n)] " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        return answer == "y" || answer == "yes";
    };
    loop->set_callbacks(cbs);

    // ——— 7. 主循环 ———
    std::string user_input;
    std::cout << "\n> " << std::flush;

    while (std::getline(std::cin, user_input)) {
        if (user_input.empty()) {
            std::cout << "> " << std::flush;
            continue;
        }

        if (user_input == "/exit" || user_input == "/quit") break;
        if (user_input == "/help") {
            std::cout << "Commands: /exit /help /memory /tools\n" << std::flush;
            std::cout << "> " << std::flush;
            continue;
        }
        if (user_input == "/memory") {
            std::cout << "Working memory: " << memory->message_count()
                << " messages\n" << std::flush;
            std::cout << "> " << std::flush;
            continue;
        }
        if (user_input == "/tools") {
            std::cout << "Available tools:\n";
            for (auto& spec : registry->all_tools()) {
                std::cout << "  - " << spec.name << " (" << spec.source << ")\n";
            }
            std::cout << std::flush;
            std::cout << "> " << std::flush;
            continue;
        }

        std::cout << std::endl;
        auto response_future = loop->run(user_input);
        auto response = response_future.get();

        std::cout << "\n---\n";
        std::cout << "Tokens: " << response.total_input_tokens
            << " in + " << response.total_output_tokens << " out";
        if (!response.tool_results.empty()) {
            std::cout << " | Tools: " << response.tool_results.size();
        }
        std::cout << "\n> " << std::flush;
    }

    std::cout << "\nGoodbye!" << std::endl;
    return 0;
}
