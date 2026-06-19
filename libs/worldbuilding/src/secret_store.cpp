#include <merak/worldbuilding/secret_store.hpp>

#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/pg_helpers.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace merak::worldbuilding {
namespace {

void write_json(const std::filesystem::path& path, const nlohmann::json& json) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to write " + path.string());
    }
    output << json.dump(2);
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to read " + path.string());
    }
    return nlohmann::json::parse(input);
}

SecretStatus secret_status_from_string(const std::string& value) {
    if (value == "active") return SecretStatus::Active;
    if (value == "exposed") return SecretStatus::Exposed;
    if (value == "abandoned") return SecretStatus::Abandoned;
    throw std::runtime_error("unknown secret status: " + value);
}

nlohmann::json secret_json(const Secret& secret) {
    nlohmann::json json{
        {"id", secret.id},
        {"holder_id", secret.holder_id},
        {"truth", secret.truth},
        {"public_version", secret.public_version},
        {"stakes", secret.stakes},
        {"aware_character_ids", secret.aware_character_ids},
        {"suspicious_character_ids", secret.suspicious_character_ids},
        {"related_foreshadowing_ids", secret.related_foreshadowing_ids},
        {"believed_truths", secret.believed_truths},
        {"status", to_string(secret.status)},
    };
    json["planted_at"] = secret.planted_at.has_value() ? nlohmann::json(*secret.planted_at) : nlohmann::json(nullptr);
    json["exposed_at"] = secret.exposed_at.has_value() ? nlohmann::json(*secret.exposed_at) : nlohmann::json(nullptr);
    return json;
}

Secret secret_from_json(const nlohmann::json& json) {
    Secret secret;
    secret.id = json.at("id").get<std::string>();
    secret.holder_id = json.at("holder_id").get<std::string>();
    secret.truth = json.at("truth").get<std::string>();
    secret.public_version = json.at("public_version").get<std::string>();
    secret.stakes = json.at("stakes").get<std::string>();
    secret.aware_character_ids = json.at("aware_character_ids").get<std::vector<std::string>>();
    secret.suspicious_character_ids = json.at("suspicious_character_ids").get<std::vector<std::string>>();
    secret.related_foreshadowing_ids = json.at("related_foreshadowing_ids").get<std::vector<std::string>>();
    secret.believed_truths = json.at("believed_truths");
    secret.status = secret_status_from_string(json.at("status").get<std::string>());
    if (!json.at("planted_at").is_null()) {
        secret.planted_at = json.at("planted_at").get<std::string>();
    }
    if (!json.at("exposed_at").is_null()) {
        secret.exposed_at = json.at("exposed_at").get<std::string>();
    }
    return secret;
}

void ensure_world_exists(WorldStore& worlds, const std::string& world_id) {
    if (!worlds.get_world(world_id).has_value()) {
        throw std::runtime_error("unknown world: " + world_id);
    }
}

bool actor_knows_truth(const Secret& secret, const std::string& character_id) {
    if (character_id == secret.holder_id) return true;
    if (std::find(secret.aware_character_ids.begin(),
                   secret.aware_character_ids.end(),
                   character_id) != secret.aware_character_ids.end()) {
        return true;
    }
    return false;
}

bool actor_is_suspicious(const Secret& secret, const std::string& character_id) {
    return std::find(secret.suspicious_character_ids.begin(),
                      secret.suspicious_character_ids.end(),
                      character_id) != secret.suspicious_character_ids.end();
}

} // namespace

SecretStore::SecretStore(WorldStore& worlds,
                         ForeshadowingStore& foreshadowing,
                         std::string_view pg_conninfo,
                         std::filesystem::path data_root)
    : worlds_(worlds),
      foreshadowing_(foreshadowing),
      data_root_(std::move(data_root)),
      pool_(std::make_unique<PgPool>(pg_conninfo)) {
    initialize();
}

void SecretStore::initialize() {
    worlds_.initialize();
    PgConn conn(*pool_);
    conn.exec("CREATE TABLE IF NOT EXISTS secrets("
              "id TEXT PRIMARY KEY, world_id TEXT NOT NULL,"
              "secret_type TEXT DEFAULT 'background',"
              "status TEXT DEFAULT 'active',"
              "holder_ids TEXT DEFAULT '[]',"
              "known_by_ids TEXT DEFAULT '[]',"
              "content TEXT, stakes TEXT, deeper_truth TEXT,"
              "exposed_in_scene_id TEXT,"
              "created_at TIMESTAMPTZ DEFAULT now(),"
              "updated_at TIMESTAMPTZ DEFAULT now())");
    conn.exec("CREATE INDEX IF NOT EXISTS secrets_by_status"
              " ON secrets(world_id, status, id)");
    conn.exec("CREATE INDEX IF NOT EXISTS secrets_by_holder"
              " ON secrets(world_id, holder_ids, id)");
}

Secret SecretStore::create(const std::string& world_id, Secret secret) {
    ensure_world_exists(worlds_, world_id);
    if (secret.id.empty()) {
        secret.id = make_id("secret");
    }
    secret.status = SecretStatus::Active;

    PgConn conn(*pool_);
    conn.exec("BEGIN");
    try {
        conn.query(
            "INSERT INTO secrets(id, world_id, holder_ids, status, content, stakes)"
            " VALUES($1, $2, $3, $4, $5, $6)",
            {secret.id, world_id,
             nlohmann::json(std::vector<std::string>{secret.holder_id}).dump(),
             to_string(secret.status), secret.truth, secret.stakes});

        write_json(worlds_.world_path(world_id) / "secrets" / (secret.id + ".json"),
                   secret_json(secret));
        conn.exec("COMMIT");
    } catch (const std::exception& e) {
        spdlog::error("SecretStore::create failed: {}", e.what());
        try { conn.exec("ROLLBACK"); } catch (const std::exception& re) {
            spdlog::critical("ROLLBACK also failed: {}", re.what());
        }
        throw;
    }
    return secret;
}

Secret SecretStore::transfer(const std::string& world_id,
                              const std::string& secret_id,
                              const std::string& character_id) {
    ensure_world_exists(worlds_, world_id);
    const auto path = worlds_.world_path(world_id) / "secrets" / (secret_id + ".json");
    auto json = read_json(path);
    auto secret = secret_from_json(json);

    if (std::find(secret.aware_character_ids.begin(),
                   secret.aware_character_ids.end(),
                   character_id) == secret.aware_character_ids.end()) {
        secret.aware_character_ids.push_back(character_id);
    }

    write_json(path, secret_json(secret));
    return secret;
}

Secret SecretStore::expose(const std::string& world_id,
                            const std::string& secret_id,
                            const std::string& scene_id) {
    ensure_world_exists(worlds_, world_id);
    const auto path = worlds_.world_path(world_id) / "secrets" / (secret_id + ".json");
    auto old_json = read_json(path);
    auto secret = secret_from_json(old_json);

    secret.status = SecretStatus::Exposed;
    secret.exposed_at = scene_id;

    PgConn conn(*pool_);
    conn.exec("BEGIN");
    try {
        write_json(path, secret_json(secret));

        conn.query(
            "UPDATE secrets SET status = $1, exposed_in_scene_id = $2"
            " WHERE world_id = $3 AND id = $4",
            {to_string(secret.status), scene_id, world_id, secret_id});
        conn.exec("COMMIT");
    } catch (const std::exception& e) {
        spdlog::error("SecretStore::expose failed: {}", e.what());
        try { conn.exec("ROLLBACK"); } catch (const std::exception& re) {
            spdlog::critical("ROLLBACK also failed: {}", re.what());
        }
        write_json(path, old_json);
        throw;
    }

    // Auto-pay related foreshadowing items (best-effort, each is atomic)
    for (const auto& fs_id : secret.related_foreshadowing_ids) {
        const auto fs_path = worlds_.world_path(world_id) / "foreshadows" / (fs_id + ".json");
        if (std::filesystem::exists(fs_path)) {
            try {
                foreshadowing_.pay(world_id, fs_id, scene_id);
            } catch (const std::exception& e) {
                spdlog::debug("foreshadowing pay skipped for {}: {}", fs_id, e.what());
            }
        }
    }

    return secret;
}

Secret SecretStore::abandon(const std::string& world_id,
                             const std::string& secret_id) {
    ensure_world_exists(worlds_, world_id);
    const auto path = worlds_.world_path(world_id) / "secrets" / (secret_id + ".json");
    auto json = read_json(path);
    auto secret = secret_from_json(json);
    secret.status = SecretStatus::Abandoned;

    write_json(path, secret_json(secret));

    PgConn conn(*pool_);
    conn.query(
        "UPDATE secrets SET status = $1 WHERE world_id = $2 AND id = $3",
        {to_string(secret.status), world_id, secret_id});

    return secret;
}

Secret SecretStore::reverse_truth(const std::string& world_id,
                                   const std::string& secret_id,
                                   std::string deeper_truth,
                                   std::string new_stakes) {
    ensure_world_exists(worlds_, world_id);
    // Archive old: mark original as abandoned
    abandon(world_id, secret_id);

    // Read old for context
    const auto old_path = worlds_.world_path(world_id) / "secrets" / (secret_id + ".json");
    auto old = secret_from_json(read_json(old_path));

    Secret deeper;
    deeper.holder_id = old.holder_id;
    deeper.truth = std::move(deeper_truth);
    deeper.public_version = old.public_version;
    deeper.stakes = std::move(new_stakes);
    deeper.aware_character_ids = old.aware_character_ids;
    deeper.suspicious_character_ids = old.suspicious_character_ids;
    deeper.related_foreshadowing_ids = old.related_foreshadowing_ids;
    deeper.believed_truths = old.believed_truths;

    return create(world_id, deeper);
}

bool SecretStore::patch(const std::string& world_id,
                         const std::string& id,
                         const nlohmann::json& fields) {
    ensure_world_exists(worlds_, world_id);

    const auto path =
        worlds_.world_path(world_id) / "secrets" / (id + ".json");
    if (!std::filesystem::exists(path)) return false;
    auto json = read_json(path);

    // Update JSON fields
    if (fields.contains("status")) json["status"] = fields["status"];
    if (fields.contains("aware_character_ids"))
        json["aware_character_ids"] = fields["aware_character_ids"];
    if (fields.contains("suspicious_character_ids"))
        json["suspicious_character_ids"] = fields["suspicious_character_ids"];
    if (fields.contains("public_version")) {
        json["public_version"] = fields["public_version"];
    }
    if (fields.contains("stakes")) json["stakes"] = fields["stakes"];
    if (fields.contains("truth")) json["truth"] = fields["truth"];

    write_json(path, json);

    // Build dynamic SET clause for DB update
    std::vector<std::string> set_parts;
    std::vector<std::string> params;
    int param_idx = 1;

    if (fields.contains("status")) {
        set_parts.push_back("status = $" + std::to_string(param_idx++));
        params.push_back(fields["status"].get<std::string>());
    }
    if (fields.contains("stakes")) {
        set_parts.push_back("stakes = $" + std::to_string(param_idx++));
        params.push_back(fields["stakes"].get<std::string>());
    }
    if (fields.contains("truth")) {
        set_parts.push_back("content = $" + std::to_string(param_idx++));
        params.push_back(fields["truth"].get<std::string>());
    }
    if (fields.contains("aware_character_ids")) {
        set_parts.push_back("known_by_ids = $" + std::to_string(param_idx++));
        params.push_back(fields["aware_character_ids"].dump());
    }

    if (set_parts.empty()) return true;

    std::string sql = "UPDATE secrets SET ";
    for (size_t i = 0; i < set_parts.size(); i++) {
        if (i > 0) sql += ", ";
        sql += set_parts[i];
    }
    sql += ", updated_at = NOW() WHERE id = $" + std::to_string(param_idx++) +
           " AND world_id = $" + std::to_string(param_idx++);
    params.push_back(id);
    params.push_back(world_id);

    PgConn conn(*pool_);
    conn.query(sql, params);

    return true;
}

std::vector<Secret> SecretStore::list(const std::string& world_id,
                                       std::optional<SecretStatus> status) const {
    ensure_world_exists(worlds_, world_id);
    std::vector<Secret> result;
    const auto base = worlds_.world_path(world_id) / "secrets";

    if (!std::filesystem::exists(base)) return result;

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto secret = secret_from_json(read_json(entry.path()));
        if (!status.has_value() || secret.status == *status) {
            result.push_back(std::move(secret));
        }
    }

    return result;
}

std::vector<KnowledgeView>
SecretStore::scene_asymmetry(const std::string& world_id, const Scene& scene) const {
    ensure_world_exists(worlds_, world_id);
    std::vector<KnowledgeView> views;
    const auto base = worlds_.world_path(world_id) / "secrets";

    for (const auto& pid : scene.participant_ids) {
        KnowledgeView view;
        view.character_id = pid;

        if (!std::filesystem::exists(base)) {
            view.state = KnowledgeState::Unknown;
            views.push_back(view);
            continue;
        }

        std::vector<std::string> known_truths;
        std::vector<std::string> suspected_truths;
        std::vector<std::string> public_lies;

        for (const auto& entry : std::filesystem::directory_iterator(base)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            auto secret = secret_from_json(read_json(entry.path()));
            if (secret.status != SecretStatus::Active) continue;

            if (actor_knows_truth(secret, pid)) {
                known_truths.push_back(secret.holder_id + "的秘密: " + secret.truth);
                view.visible_secrets.push_back(secret);
            } else if (actor_is_suspicious(secret, pid)) {
                auto it = secret.believed_truths.find(pid);
                if (it != secret.believed_truths.end()) {
                    suspected_truths.push_back(secret.holder_id + "可能: " + it->get<std::string>());
                }
                view.unknown_secrets.push_back(secret);
            } else {
                public_lies.push_back(secret.holder_id + ": " + secret.public_version);
                view.unknown_secrets.push_back(secret);
            }
        }

        if (!known_truths.empty()) {
            view.state = KnowledgeState::Secret;
            for (const auto& t : known_truths) {
                if (!view.context_snippet.empty()) view.context_snippet += "\n";
                view.context_snippet += t;
            }
        } else if (!suspected_truths.empty()) {
            view.state = KnowledgeState::Secret;
            for (const auto& t : suspected_truths) {
                if (!view.context_snippet.empty()) view.context_snippet += "\n";
                view.context_snippet += t;
            }
        } else if (!public_lies.empty()) {
            view.state = KnowledgeState::Public;
            for (const auto& p : public_lies) {
                if (!view.context_snippet.empty()) view.context_snippet += "\n";
                view.context_snippet += p;
            }
        } else {
            view.state = KnowledgeState::Unknown;
        }

        views.push_back(view);
    }

    return views;
}

std::vector<LeakRisk>
SecretStore::check_leak_risk(const std::string& world_id,
                              const Scene& scene,
                              const std::string& draft_text) const {
    ensure_world_exists(worlds_, world_id);
    std::vector<LeakRisk> risks;
    const auto base = worlds_.world_path(world_id) / "secrets";

    if (!std::filesystem::exists(base)) return risks;

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto secret = secret_from_json(read_json(entry.path()));
        if (secret.status != SecretStatus::Active) continue;

        // Check if draft contains truth keywords that shouldn't be known
        for (const auto& pid : scene.participant_ids) {
            if (actor_knows_truth(secret, pid)) continue;

            // Simple substring check for truth and key terms
            auto pos = draft_text.find(secret.truth);
            if (pos != std::string::npos && secret.truth.size() >= 3) {
                LeakRisk risk;
                risk.secret_id = secret.id;
                risk.character_id = pid;
                risk.reason = "场景文本暴露了 " + secret.holder_id + " 的秘密: " + secret.truth;
                risks.push_back(risk);
            }
        }
    }

    return risks;
}

} // namespace merak::worldbuilding
