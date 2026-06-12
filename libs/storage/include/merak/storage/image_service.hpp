#pragma once

#include <merak/storage/image_store.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <set>

namespace merak {

struct ImageRecord {
    std::string id;
    std::string agent_id;
    std::string image_type;
    std::string storage_key;
    std::string mime_type;
    std::string original_name;
    int64_t file_size_bytes = 0;
    bool is_primary = false;
    int sort_order = 0;
    std::string created_at;
};

struct ChunkedUploadState {
    std::string upload_id;
    std::string world_id;
    std::string agent_id;
    std::string image_type;
    std::string file_name;
    std::string mime_type;
    int64_t total_size;
    int64_t chunk_size;
    int chunks_total;
    std::string temp_dir;
    std::set<int> uploaded_chunks;
};

// Callback types for DB operations — decouples ImageService from worldbuilding DB
using ImageDbOps = std::function<bool(const std::string& sql, const nlohmann::json& params)>;
using ImageDbQuery = std::function<nlohmann::json(const std::string& sql, const nlohmann::json& params)>;
using IdGenerator = std::function<std::string()>;
using TimestampProvider = std::function<std::string()>;

class ImageService {
public:
    ImageService(std::shared_ptr<ImageStore> store,
                 ImageDbQuery db_query,
                 ImageDbOps db_exec,
                 IdGenerator id_gen,
                 TimestampProvider ts,
                 std::string upload_temp_dir);

    // Simple upload
    ImageRecord upload(const std::string& world_id,
                       const std::string& agent_id,
                       const std::string& image_type,
                       const std::string& file_name,
                       const std::string& mime_type,
                       const std::vector<unsigned char>& bytes);

    // Chunked upload lifecycle
    ChunkedUploadState init_chunked(const std::string& world_id,
                                     const std::string& agent_id,
                                     const std::string& image_type,
                                     const std::string& file_name,
                                     const std::string& mime_type,
                                     int64_t total_size,
                                     int64_t chunk_size);

    void upload_chunk(const std::string& upload_id,
                      int chunk_idx,
                      const std::vector<unsigned char>& data);

    std::set<int> uploaded_chunks(const std::string& upload_id) const;

    ImageRecord complete_chunked(const std::string& upload_id);

    void cancel_chunked(const std::string& upload_id);

    // Image management
    std::vector<ImageRecord> list_images(const std::string& agent_id);
    std::optional<ImageRecord> get_image(const std::string& image_id);
    void delete_image(const std::string& image_id);
    void update_image(const std::string& image_id,
                      std::optional<bool> is_primary,
                      std::optional<int> sort_order);

    // Delegate to store for URL generation and binary access
    const ImageStore& store() const { return *store_; }
    std::string public_url(const std::string& storage_key) const { return store_->public_url(storage_key); }
    ImageData load_image_data(const std::string& storage_key) const { return store_->load(storage_key); }

private:
    std::string storage_key_for(const ImageRecord& rec) const;
    void set_primary(const std::string& agent_id, const std::string& image_type,
                     const std::string& image_id);
    std::string now_iso() const;

    std::shared_ptr<ImageStore> store_;
    ImageDbQuery db_query_;
    ImageDbOps db_exec_;
    IdGenerator id_gen_;
    TimestampProvider ts_;
    std::string upload_temp_dir_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ChunkedUploadState> chunked_uploads_;
};

} // namespace merak
