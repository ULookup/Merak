#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace merak {

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

enum class ToolDomain : uint8_t {
    General    = 0,
    Write      = 1 << 0,
    WorldQuery = 1 << 1,
};

inline constexpr bool operator&(ToolDomain a, ToolDomain b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

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
    ToolDomain domain = ToolDomain::General;
};

} // namespace merak
