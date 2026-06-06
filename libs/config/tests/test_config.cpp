#include <merak/config.hpp>
#include <merak/config_loader.hpp>
#include <merak/result.hpp>
#include <cassert>
#include <fstream>
#include <cstdio>

int main() {
    // 测试 1: default_config() 返回合理默认值
    {
        auto cfg = merak::ConfigLoader::default_config();
        assert(cfg.agent.max_tool_turns == 25);
        assert(cfg.agent.permission_mode == "ask");
        assert(cfg.llm.request_timeout_ms == 60000);
        assert(cfg.memory.enabled == true);
        assert(cfg.memory.top_k_retrieval == 5);
    }

    // 测试 2: 从 JSON 文件加载
    {
        const char* test_json = R"({
            "llm": {
                "api_key": "sk-test123",
                "api_base_url": "https://api.openai.com/v1",
                "default_model": "gpt-4o"
            },
            "models": [
                {"name": "gpt-4o", "provider": "openai", "max_context_tokens": 128000}
            ],
            "memory": {
                "enabled": true,
                "db_connection": "postgresql://localhost/test",
                "top_k_retrieval": 10
            },
            "agent": {
                "system_prompt": "You are a test agent.",
                "max_tool_turns": 10,
                "permission_mode": "auto",
                "sub_agents": {
                    "researcher": {
                        "system_prompt": "Research carefully.",
                        "model": "gpt-4o-mini",
                        "tool_allowlist": ["read_file", "grep"],
                        "can_delegate": false
                    },
                    "builder": {
                        "system_prompt": "Implement changes.",
                        "tool_allowlist": ["read_file", "edit_file"]
                    }
                }
            },
            "tui": {
                "theme": {
                    "preset": "dark",
                    "accent": "magenta",
                    "colors": {
                        "selected_bg": 236,
                        "selected_fg": "bright_white"
                    }
                }
            }
        })";

        std::ofstream f("/tmp/test_merak_config.json");
        f << test_json;
        f.close();

        auto result = merak::ConfigLoader::load_file("/tmp/test_merak_config.json");
        assert(result.has_value());

        auto& cfg = result.value();
        assert(cfg.llm.api_key == "sk-test123");
        assert(cfg.llm.default_model == "gpt-4o");
        assert(cfg.models.size() == 1);
        assert(cfg.models[0].name == "gpt-4o");
        assert(cfg.memory.db_connection == "postgresql://localhost/test");
        assert(cfg.memory.top_k_retrieval == 10);
        assert(cfg.agent.system_prompt == "You are a test agent.");
        assert(cfg.agent.max_tool_turns == 10);
        assert(cfg.agent.permission_mode == "auto");
        assert(cfg.agent.sub_agents.size() == 2);
        assert(cfg.agent.sub_agents["researcher"].id == "researcher");
        assert(cfg.agent.sub_agents["researcher"].system_prompt == "Research carefully.");
        assert(cfg.agent.sub_agents["researcher"].model == "gpt-4o-mini");
        assert(cfg.agent.sub_agents["researcher"].tool_allowlist.size() == 2);
        assert(cfg.agent.sub_agents["researcher"].tool_allowlist[1] == "grep");
        assert(cfg.agent.sub_agents["researcher"].can_delegate == false);
        assert(cfg.agent.sub_agents["builder"].id == "builder");
        assert(cfg.agent.sub_agents["builder"].can_delegate == false);
        assert(cfg.tui.theme.preset == "dark");
        assert(cfg.tui.theme.colors["accent"] == "magenta");
        assert(cfg.tui.theme.colors["selected_bg"] == "236");
        assert(cfg.tui.theme.colors["selected_fg"] == "bright_white");

        std::remove("/tmp/test_merak_config.json");
    }

    // 测试 3: 不存在的文件返回错误
    {
        auto result = merak::ConfigLoader::load_file("/tmp/not_exist_config_xyz.json");
        assert(result.has_error());
        assert(result.error().type() == merak::ErrorType::CONFIG_ERROR);
    }

    // 测试 4: 默认值回退 — 缺少字段时使用默认值
    {
        const char* minimal_json = R"({"agent": {}})";
        std::ofstream f("/tmp/test_minimal_config.json");
        f << minimal_json;
        f.close();

        auto result = merak::ConfigLoader::load_file("/tmp/test_minimal_config.json");
        assert(result.has_value());
        auto& cfg = result.value();
        assert(cfg.agent.max_tool_turns == 25);  // 默认值
        assert(cfg.llm.api_key == "");           // 未提供 llm 段时为空

        std::remove("/tmp/test_minimal_config.json");
    }

    return 0;
}
