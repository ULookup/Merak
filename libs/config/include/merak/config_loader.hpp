#pragma once
#include <merak/config.hpp>
#include <merak/error.hpp>
#include <merak/result.hpp>
#include <string>

namespace merak {

// 配置加载器：多层级搜索 → 逐层 merge → 环境变量覆盖
class ConfigLoader {
public:
    // 从标准路径自动加载（优先级从低到高）：
    //   1. 内置默认值
    //   2. ~/.merak/settings.json         （用户级）
    //   3. ./.merak/settings.json          （项目级，可提交 git）
    //   4. ./.merak/settings.local.json    （本地覆盖，不提交 git）
    //   5. MERAK_* 环境变量               （最高优先级）
    static Result<Config, AgentError> load();

    // 从指定文件加载（命令行参数用）
    static Result<Config, AgentError> load_file(const std::string& filepath);

    // 返回最小可运行配置
    static Config default_config();

private:
    static void merge(Config& base, const Config& override_cfg);
    static void apply_env_overrides(Config& cfg);
};

// ——— User Preferences ———
UserPreferences load_preferences();
bool save_preferences(const UserPreferences& prefs);

} // namespace merak
