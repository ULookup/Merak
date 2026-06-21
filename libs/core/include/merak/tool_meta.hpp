#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace merak {

enum class ToolDomain : uint32_t {
    General    = 0,
    WorldQuery = 1 << 0,
};

inline constexpr ToolDomain operator&(ToolDomain a, ToolDomain b) {
    return static_cast<ToolDomain>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline constexpr ToolDomain operator|(ToolDomain a, ToolDomain b) {
    return static_cast<ToolDomain>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr bool operator!(ToolDomain a) {
    return static_cast<uint32_t>(a) == 0;
}

enum class IntentType {
    CodeEdit,
    CodeRead,
    Git,
    Network,
    CodeIntel,
    Memory,
    Introspect,
    AgentOp,
    TaskMgmt,
    DomainRead,
    DomainWrite,
};

enum class Scope {
    Local,
    LocalGit,
    External,
    CrossSession,
};

enum class Category {
    ReadOnly,
    Consultative,
    Mutating,
    Shell,
};

struct ToolMeta {
    std::string name;
    std::string description;
    std::vector<std::string> triggers;
    bool pinned = false;
    std::vector<IntentType> intents;
    Scope scope = Scope::Local;
    uint32_t schema_tokens = 0;
};

} // namespace merak
