#include <merak/config_loader.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <optional>
#include <cstdlib>
#include <filesystem>

namespace merak {

// ——— 内置默认值 ———
Config ConfigLoader::default_config() {
    Config cfg;
    cfg.llm.api_base_url = "https://api.openai.com/v1";
    cfg.llm.default_model = "gpt-4o";
    cfg.llm.max_output_tokens = 4096;
    cfg.llm.provider = "openai";
    return cfg;
}

// ——— 从单个 JSON 文件解析 ———
static std::optional<Config> parse_config_file(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return std::nullopt;

    try {
        nlohmann::json j;
        f >> j;

        Config cfg;

        if (j.contains("llm")) {
            auto& l = j["llm"];
            if (l.contains("provider")) cfg.llm.provider = l["provider"];
            if (l.contains("api_key")) cfg.llm.api_key = l["api_key"];
            if (l.contains("api_base_url")) cfg.llm.api_base_url = l["api_base_url"];
            if (l.contains("default_model")) cfg.llm.default_model = l["default_model"];
            if (l.contains("max_output_tokens")) cfg.llm.max_output_tokens = l["max_output_tokens"];
            if (l.contains("request_timeout_ms")) cfg.llm.request_timeout_ms = l["request_timeout_ms"];
            if (l.contains("max_retries")) cfg.llm.max_retries = l["max_retries"];
            if (l.contains("thinking")) {
                auto& t = l["thinking"];
                ThinkingConfig thinking;
                thinking.type = t.value("type", "disabled");
                thinking.effort = t.value("effort", "");
                thinking.budget_tokens = t.value("budget_tokens", 0);
                cfg.llm.thinking = std::move(thinking);
            }
        }

        if (j.contains("models")) {
            for (auto& m : j["models"]) {
                ModelEntry entry;
                entry.name = m.value("name", "");
                entry.provider = m.value("provider", "openai");
                entry.max_context_tokens = m.value("max_context_tokens", 128000);
                cfg.models.push_back(std::move(entry));
            }
        }

        if (j.contains("memory")) {
            auto& mem = j["memory"];
            if (mem.contains("enabled")) cfg.memory.enabled = mem["enabled"];
            if (mem.contains("db_connection")) cfg.memory.db_connection = mem["db_connection"];
            if (mem.contains("embedding_model")) cfg.memory.embedding_model = mem["embedding_model"];
            if (mem.contains("top_k_retrieval")) cfg.memory.top_k_retrieval = mem["top_k_retrieval"];
            if (mem.contains("confidence_decay")) cfg.memory.confidence_decay = mem["confidence_decay"];
            if (mem.contains("decay_interval_days")) cfg.memory.decay_interval_days = mem["decay_interval_days"];
        }

        if (j.contains("mcp_servers")) {
            for (auto& s : j["mcp_servers"]) {
                MCPServerConfig sc;
                sc.name = s.value("name", "");
                sc.command = s.value("command", "");
                sc.enabled = s.value("enabled", true);
                if (s.contains("args")) {
                    for (auto& a : s["args"]) sc.args.push_back(a.get<std::string>());
                }
                if (s.contains("env")) {
                    for (auto& [k, v] : s["env"].items()) sc.env[k] = v.get<std::string>();
                }
                cfg.mcp_servers.push_back(std::move(sc));
            }
        }

        if (j.contains("agent")) {
            auto& a = j["agent"];
            if (a.contains("system_prompt")) cfg.agent.system_prompt = a["system_prompt"];
            if (a.contains("default_model")) cfg.agent.default_model = a["default_model"];
            if (a.contains("max_tool_turns")) cfg.agent.max_tool_turns = a["max_tool_turns"];
            if (a.contains("reserve_ratio")) cfg.agent.reserve_ratio = a["reserve_ratio"];
            if (a.contains("memory_budget_ratio")) cfg.agent.memory_budget_ratio = a["memory_budget_ratio"];
            if (a.contains("permission_mode")) cfg.agent.permission_mode = a["permission_mode"];
        }

        return cfg;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "Config parse error in " << filepath << ": " << e.what() << std::endl;
        return std::nullopt;
    }
}

// ——— 逐层合并 ———
void ConfigLoader::merge(Config& base, const Config& override_cfg) {
    auto& l = override_cfg.llm;
    if (!l.provider.empty()) base.llm.provider = l.provider;
    if (!l.api_key.empty()) base.llm.api_key = l.api_key;
    if (!l.api_base_url.empty()) base.llm.api_base_url = l.api_base_url;
    if (!l.default_model.empty()) base.llm.default_model = l.default_model;
    if (l.max_output_tokens > 0) base.llm.max_output_tokens = l.max_output_tokens;
    if (l.request_timeout_ms > 0) base.llm.request_timeout_ms = l.request_timeout_ms;
    if (l.max_retries > 0) base.llm.max_retries = l.max_retries;
    if (l.thinking.has_value()) base.llm.thinking = l.thinking;

    if (!override_cfg.models.empty()) base.models = override_cfg.models;

    // 布尔值无条件覆盖，确保能设为 false
    base.memory.enabled = override_cfg.memory.enabled;
    if (!override_cfg.memory.db_connection.empty()) base.memory.db_connection = override_cfg.memory.db_connection;
    if (!override_cfg.memory.embedding_model.empty()) base.memory.embedding_model = override_cfg.memory.embedding_model;
    if (override_cfg.memory.top_k_retrieval > 0) base.memory.top_k_retrieval = override_cfg.memory.top_k_retrieval;
    if (override_cfg.memory.confidence_decay != 0.0f) base.memory.confidence_decay = override_cfg.memory.confidence_decay;
    if (override_cfg.memory.decay_interval_days > 0) base.memory.decay_interval_days = override_cfg.memory.decay_interval_days;

    if (!override_cfg.mcp_servers.empty()) base.mcp_servers = override_cfg.mcp_servers;

    auto& a = override_cfg.agent;
    if (!a.system_prompt.empty()) base.agent.system_prompt = a.system_prompt;
    if (!a.default_model.empty()) base.agent.default_model = a.default_model;
    if (a.max_tool_turns > 0) base.agent.max_tool_turns = a.max_tool_turns;
    if (a.reserve_ratio > 0.0) base.agent.reserve_ratio = a.reserve_ratio;
    if (a.memory_budget_ratio > 0.0) base.agent.memory_budget_ratio = a.memory_budget_ratio;
    if (!a.permission_mode.empty()) base.agent.permission_mode = a.permission_mode;
}

// ——— 环境变量覆盖 ———
void ConfigLoader::apply_env_overrides(Config& cfg) {
    auto env_str = [](const char* name) -> const char* {
        return std::getenv(name);
    };
    auto env_int = [&](const char* name) -> std::optional<int> {
        const char* v = env_str(name);
        if (!v) return std::nullopt;
        try { return std::stoi(v); } catch (...) { return std::nullopt; }
    };

    if (auto* v = env_str("MERAK_PROVIDER")) cfg.llm.provider = v;
    if (auto* v = env_str("MERAK_API_KEY")) cfg.llm.api_key = v;
    if (auto* v = env_str("MERAK_API_BASE_URL")) cfg.llm.api_base_url = v;
    if (auto* v = env_str("MERAK_MODEL")) cfg.llm.default_model = v;
    if (auto v = env_int("MERAK_MAX_OUTPUT_TOKENS")) cfg.llm.max_output_tokens = *v;
    if (auto v = env_int("MERAK_TIMEOUT_MS")) cfg.llm.request_timeout_ms = *v;
    if (auto v = env_int("MERAK_MAX_RETRIES")) cfg.llm.max_retries = *v;
    if (auto* v = env_str("MERAK_THINKING_TYPE")) {
        if (!cfg.llm.thinking) cfg.llm.thinking = ThinkingConfig{};
        cfg.llm.thinking->type = v;
    }
    if (auto* v = env_str("MERAK_EFFORT_LEVEL")) {
        if (!cfg.llm.thinking) cfg.llm.thinking = ThinkingConfig{};
        cfg.llm.thinking->effort = v;
    }
    if (auto v = env_int("MERAK_THINKING_BUDGET_TOKENS")) {
        if (!cfg.llm.thinking) cfg.llm.thinking = ThinkingConfig{};
        cfg.llm.thinking->budget_tokens = *v;
    }

    if (auto* v = env_str("MERAK_DB_CONNECTION")) cfg.memory.db_connection = v;
    if (auto* v = env_str("MERAK_SYSTEM_PROMPT")) cfg.agent.system_prompt = v;
    if (auto v = env_int("MERAK_MAX_TOOL_TURNS")) cfg.agent.max_tool_turns = *v;
    if (auto* v = env_str("MERAK_PERMISSION_MODE")) cfg.agent.permission_mode = v;
}

static void apply_provider_defaults(Config& cfg) {
    if (cfg.llm.api_base_url.empty()) {
        cfg.llm.api_base_url = cfg.llm.provider == "anthropic"
            ? "https://api.anthropic.com"
            : "https://api.openai.com/v1";
    }
    if (cfg.llm.default_model.empty()) {
        cfg.llm.default_model = cfg.llm.provider == "anthropic"
            ? "claude-sonnet-4-6"
            : "gpt-4o";
    }
}

static std::optional<std::string> validate_thinking_config(const Config& cfg) {
    if (cfg.llm.max_output_tokens <= 0) {
        return "llm.max_output_tokens must be greater than 0";
    }
    if (!cfg.llm.thinking.has_value()) return std::nullopt;
    const auto& thinking = *cfg.llm.thinking;
    if (thinking.type != "disabled" &&
        thinking.type != "adaptive" &&
        thinking.type != "enabled") {
        return "llm.thinking.type must be disabled, adaptive, or enabled";
    }
    if (!thinking.effort.empty() &&
        thinking.effort != "low" &&
        thinking.effort != "medium" &&
        thinking.effort != "high" &&
        thinking.effort != "xhigh" &&
        thinking.effort != "max") {
        return "llm.thinking.effort must be low, medium, high, xhigh, or max";
    }
    if (thinking.type == "enabled" && thinking.budget_tokens < 1024) {
        return "llm.thinking.budget_tokens must be at least 1024 when type is enabled";
    }
    if (thinking.type == "enabled" &&
        thinking.budget_tokens >= cfg.llm.max_output_tokens) {
        return "llm.thinking.budget_tokens must be less than llm.max_output_tokens";
    }
    return std::nullopt;
}

// ——— 从标准路径自动加载 ———
Result<Config, AgentError> ConfigLoader::load() {
    Config cfg = default_config();

    auto try_load = [&](const std::string& path, const std::string& label) {
        if (auto loaded = parse_config_file(path)) {
            std::cout << "Config: loaded " << label << std::endl;
            merge(cfg, *loaded);
        }
    };

    // 1. 用户级：~/.merak/settings.json + settings.local.json
    if (const char* home = std::getenv("HOME")) {
        std::string user_dir = std::string(home) + "/.merak";
        auto user_json = user_dir + "/settings.json";
        auto user_local = user_dir + "/settings.local.json";
        try_load(user_json, "user-level (" + user_json + ")");
        try_load(user_local, "user local (" + user_local + ")");
    }

    // 2. 项目级：从当前目录向上搜索 .merak/（先找到的优先）
    namespace fs = std::filesystem;
    fs::path search_dir = fs::current_path();
    while (true) {
        auto merak_dir = search_dir / ".merak";
        if (fs::is_directory(merak_dir)) {
            auto project_path = merak_dir / "settings.json";
            auto local_path = merak_dir / "settings.local.json";

            if (fs::exists(project_path)) {
                try_load(project_path.string(), "project-level (" + project_path.string() + ")");
            }
            if (fs::exists(local_path)) {
                try_load(local_path.string(), "local override (" + local_path.string() + ")");
            }
            break;
        }
        auto parent = search_dir.parent_path();
        if (parent == search_dir) break;
        search_dir = parent;
    }

    // 3. 环境变量（最高优先级）
    apply_env_overrides(cfg);
    apply_provider_defaults(cfg);
    if (auto error = validate_thinking_config(cfg)) {
        return AgentError(ErrorType::CONFIG_ERROR, *error);
    }

    return cfg;
}

// ——— 从指定文件加载 ———
Result<Config, AgentError> ConfigLoader::load_file(const std::string& filepath) {
    if (auto loaded = parse_config_file(filepath)) {
        Config cfg = default_config();
        merge(cfg, *loaded);
        apply_env_overrides(cfg);
        apply_provider_defaults(cfg);
        if (auto error = validate_thinking_config(cfg)) {
            return AgentError(ErrorType::CONFIG_ERROR, *error);
        }
        return cfg;
    }
    return AgentError(
        ErrorType::CONFIG_ERROR,
        "Cannot load config file: " + filepath
    );
}

} // namespace merak
