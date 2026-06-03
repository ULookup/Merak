#include <merak/worldbuilding/secret_store.hpp>

#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/sqlite_helpers.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace merak::worldbuilding {
namespace {

void execute_bound(Statement& statement) {
    if (statement.step()) {
        throw std::runtime_error("unexpected sqlite row");
    }
}

void rollback_no_throw(SqliteDb& db) noexcept {
    try {
        db.exec("ROLLBACK");
    } catch (...) {
    }
}

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
                         std::filesystem::path data_root)
    : worlds_(worlds), foreshadowing_(foreshadowing), data_root_(std::move(data_root)) {
    initialize();
}

void SecretStore::initialize() {
    worlds_.initialize();
    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS secrets(
            id TEXT PRIMARY KEY,
            world_id TEXT NOT NULL,
            holder_id TEXT NOT NULL,
            status TEXT NOT NULL,
            FOREIGN KEY(world_id) REFERENCES worlds(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE INDEX IF NOT EXISTS secrets_by_status
        ON secrets(world_id, status, id)
    )sql");
    db.exec(R"sql(
        CREATE INDEX IF NOT EXISTS secrets_by_holder
        ON secrets(world_id, holder_id, id)
    )sql");
}

std::filesystem::path SecretStore::database_path() const {
    return data_root_ / "worlds.sqlite3";
}

Secret SecretStore::create(const std::string& world_id, Secret secret) {
    ensure_world_exists(worlds_, world_id);
    if (secret.id.empty()) {
        secret.id = make_id("secret");
    }
    secret.status = SecretStatus::Active;

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec("BEGIN");
    try {
        Statement insert(db, R"sql(
            INSERT INTO secrets(id, world_id, holder_id, status)
            VALUES(?1, ?2, ?3, ?4)
        )sql");
        bind_text(insert, 1, secret.id);
        bind_text(insert, 2, world_id);
        bind_text(insert, 3, secret.holder_id);
        bind_text(insert, 4, to_string(secret.status));
        execute_bound(insert);

        write_json(worlds_.world_path(world_id) / "secrets" / (secret.id + ".json"),
                   secret_json(secret));
        db.exec("COMMIT");
    } catch (...) {
        rollback_no_throw(db);
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

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec("BEGIN");
    try {
        write_json(path, secret_json(secret));

        Statement update(db, R"sql(
            UPDATE secrets SET status = ?1 WHERE world_id = ?2 AND id = ?3
        )sql");
        bind_text(update, 1, to_string(secret.status));
        bind_text(update, 2, world_id);
        bind_text(update, 3, secret_id);
        execute_bound(update);
        db.exec("COMMIT");
    } catch (...) {
        rollback_no_throw(db);
        write_json(path, old_json);
        throw;
    }

    // Auto-pay related foreshadowing items (best-effort, each is atomic)
    for (const auto& fs_id : secret.related_foreshadowing_ids) {
        const auto fs_path = worlds_.world_path(world_id) / "foreshadows" / (fs_id + ".json");
        if (std::filesystem::exists(fs_path)) {
            try {
                foreshadowing_.pay(world_id, fs_id, scene_id);
            } catch (...) {
                // Pay best-effort; don't roll back secret exposure
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

    SqliteDb db(database_path().string());
    Statement update(db, R"sql(
        UPDATE secrets SET status = ?1 WHERE world_id = ?2 AND id = ?3
    )sql");
    bind_text(update, 1, to_string(secret.status));
    bind_text(update, 2, world_id);
    bind_text(update, 3, secret_id);
    execute_bound(update);

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
            } else if (actor_is_suspicious(secret, pid)) {
                auto it = secret.believed_truths.find(pid);
                if (it != secret.believed_truths.end()) {
                    suspected_truths.push_back(secret.holder_id + "可能: " + it->get<std::string>());
                }
            } else {
                public_lies.push_back(secret.holder_id + ": " + secret.public_version);
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
