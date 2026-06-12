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
