#pragma once

#include <cstdint>
#include <string>
#include <set>
#include <vector>

namespace merak {

enum class Capability {
    LSPServer,
    MemoryService,
    AgentSpawner,
    PlanLifecycle,
    Worldbuilding,
};

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
    std::vector<Capability> requires_caps;
    uint32_t schema_tokens = 0;
};

class CapabilitySet {
public:
    CapabilitySet() = default;

    static CapabilitySet platform_default() {
        CapabilitySet s;
        return s;
    }

    void add(Capability c) { caps_.insert(c); }
    void remove(Capability c) { caps_.erase(c); }

    bool has(Capability c) const {
        return caps_.find(c) != caps_.end();
    }

    bool has_all(const std::vector<Capability>& required) const {
        for (const auto c : required) {
            if (!has(c)) return false;
        }
        return true;
    }

    CapabilitySet operator|(Capability c) const {
        CapabilitySet result = *this;
        result.add(c);
        return result;
    }

    CapabilitySet operator|(const CapabilitySet& other) const {
        CapabilitySet result = *this;
        for (const auto c : other.caps_) result.add(c);
        return result;
    }

    CapabilitySet& operator|=(Capability c) {
        add(c);
        return *this;
    }

    CapabilitySet& operator|=(const CapabilitySet& other) {
        for (const auto c : other.caps_) add(c);
        return *this;
    }

private:
    std::set<Capability> caps_;
};

} // namespace merak
