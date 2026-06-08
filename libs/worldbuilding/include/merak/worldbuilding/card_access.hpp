#pragma once
#include <merak/worldbuilding/world_models.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace merak::worldbuilding {

struct VersionConflictError : std::runtime_error {
    int current_version;
    VersionConflictError(int v)
        : std::runtime_error("card version conflict"), current_version(v) {}
};

struct CardBase {
    std::string id;
    std::string card_type;     // agent | scene | chapter | arc | world | foreshadow | secret
    std::string title;
    std::string summary;
    std::string status;        // drafting | writing | completed | archived
    std::string created_at;
    std::string updated_at;
    std::string updated_by;    // "user" | "agent_<run_id>"
    int version = 0;
};

// Card type enum for type-safe dispatch
enum class CardType {
    Agent,
    Scene,
    Chapter,
    Arc,
    World,
    Foreshadow,
    Secret
};

} // namespace merak::worldbuilding
