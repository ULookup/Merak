#include <merak/storage/image_service.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

namespace fs = std::filesystem;

namespace merak {

ImageService::ImageService(std::shared_ptr<ImageStore> store,
                           ImageDbQuery db_query,
                           ImageDbOps db_exec,
                           IdGenerator id_gen,
                           TimestampProvider ts,
                           std::string upload_temp_dir)
    : store_(std::move(store))
    , db_query_(std::move(db_query))
    , db_exec_(std::move(db_exec))
    , id_gen_(std::move(id_gen))
    , ts_(std::move(ts))
    , upload_temp_dir_(std::move(upload_temp_dir))
{
}

std::string ImageService::now_iso() const {
    return ts_();
}

std::string ImageService::storage_key_for(const ImageRecord& rec) const {
    std::string ext;
    if (rec.mime_type == "image/png") ext = "png";
    else if (rec.mime_type == "image/jpeg") ext = "jpg";
    else if (rec.mime_type == "image/webp") ext = "webp";
    else if (rec.mime_type == "image/gif") ext = "gif";
    else if (rec.mime_type == "image/bmp") ext = "bmp";
    else if (rec.mime_type == "image/svg+xml") ext = "svg";
    else ext = "bin";

    return rec.agent_id + "/" + rec.id + "." + ext;
}

void ImageService::set_primary(const std::string& agent_id,
                               const std::string& image_type,
                               const std::string& image_id) {
    // Unset all existing primaries for this agent + image_type
    nlohmann::json unset_params;
    unset_params["agent_id"] = agent_id;
    unset_params["image_type"] = image_type;
    db_exec_(
        "UPDATE agent_images SET is_primary = 0 WHERE agent_id = @agent_id AND image_type = @image_type",
        unset_params
    );

    // Set the new primary
    nlohmann::json set_params;
    set_params["id"] = image_id;
    db_exec_(
        "UPDATE agent_images SET is_primary = 1 WHERE id = @id",
        set_params
    );
}

ImageRecord ImageService::upload(const std::string& world_id,
                                  const std::string& agent_id,
                                  const std::string& image_type,
                                  const std::string& file_name,
                                  const std::string& mime_type,
                                  const std::vector<unsigned char>& bytes) {
    ImageRecord rec;
    rec.id = id_gen_();
    rec.agent_id = agent_id;
    rec.image_type = image_type;
    rec.mime_type = mime_type;
    rec.original_name = file_name;
    rec.file_size_bytes = static_cast<int64_t>(bytes.size());
    rec.created_at = now_iso();

    // Build storage key and save binary data
    rec.storage_key = storage_key_for(rec);

    ImageData data;
    data.bytes = bytes;
    data.mime_type = mime_type;
    store_->save(rec.storage_key, data);

    // Auto-set is_primary if this is the first image for this agent + image_type
    {
        nlohmann::json query_params;
        query_params["agent_id"] = agent_id;
        query_params["image_type"] = image_type;
        auto existing = db_query_(
            "SELECT COUNT(*) as cnt FROM agent_images WHERE agent_id = @agent_id AND image_type = @image_type",
            query_params
        );

        int count = 0;
        if (existing.is_array() && !existing.empty()) {
            count = existing[0].value("cnt", 0);
        } else if (existing.is_object()) {
            count = existing.value("cnt", 0);
        }

        if (count == 0) {
            rec.is_primary = true;
        }
    }

    // Insert DB record
    {
        nlohmann::json insert_params;
        insert_params["id"] = rec.id;
        insert_params["agent_id"] = rec.agent_id;
        insert_params["image_type"] = rec.image_type;
        insert_params["storage_key"] = rec.storage_key;
        insert_params["mime_type"] = rec.mime_type;
        insert_params["original_name"] = rec.original_name;
        insert_params["file_size_bytes"] = rec.file_size_bytes;
        insert_params["is_primary"] = rec.is_primary;
        insert_params["sort_order"] = rec.sort_order;
        insert_params["created_at"] = rec.created_at;
        db_exec_(
            "INSERT INTO agent_images (id, agent_id, image_type, storage_key, mime_type, "
            "original_name, file_size_bytes, is_primary, sort_order, created_at) "
            "VALUES (@id, @agent_id, @image_type, @storage_key, @mime_type, "
            "@original_name, @file_size_bytes, @is_primary, @sort_order, @created_at)",
            insert_params
        );
    }

    return rec;
}

ChunkedUploadState ImageService::init_chunked(const std::string& world_id,
                                               const std::string& agent_id,
                                               const std::string& image_type,
                                               const std::string& file_name,
                                               const std::string& mime_type,
                                               int64_t total_size,
                                               int64_t chunk_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    ChunkedUploadState state;
    state.upload_id = id_gen_();
    state.world_id = world_id;
    state.agent_id = agent_id;
    state.image_type = image_type;
    state.file_name = file_name;
    state.mime_type = mime_type;
    state.total_size = total_size;
    state.chunk_size = chunk_size;
    state.chunks_total = static_cast<int>((total_size + chunk_size - 1) / chunk_size);

    // Create temp dir for this upload
    state.temp_dir = upload_temp_dir_ + "/" + state.upload_id;
    fs::create_directories(state.temp_dir);

    chunked_uploads_[state.upload_id] = state;
    return state;
}

void ImageService::upload_chunk(const std::string& upload_id,
                                 int chunk_idx,
                                 const std::vector<unsigned char>& data) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = chunked_uploads_.find(upload_id);
    if (it == chunked_uploads_.end()) {
        throw std::runtime_error("Chunked upload not found: " + upload_id);
    }

    auto& state = it->second;

    // Write chunk to numbered file
    std::ostringstream chunk_name;
    chunk_name << "chunk_" << std::setfill('0') << std::setw(4) << chunk_idx;
    fs::path chunk_path = fs::path(state.temp_dir) / chunk_name.str();

    std::ofstream out(chunk_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to write chunk file: " + chunk_path.string());
    }
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();

    state.uploaded_chunks.insert(chunk_idx);
}

std::set<int> ImageService::uploaded_chunks(const std::string& upload_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = chunked_uploads_.find(upload_id);
    if (it == chunked_uploads_.end()) {
        throw std::runtime_error("Chunked upload not found: " + upload_id);
    }

    return it->second.uploaded_chunks;  // copy
}

ImageRecord ImageService::complete_chunked(const std::string& upload_id) {
    // File I/O is done under the lock to prevent TOCTOU race with cancel_chunked.
    // Tradeoff: chunk reads block concurrent upload_chunk/init_chunked for other
    // upload_ids. Acceptable because chunked uploads are infrequent and local-disk
    // reads are fast (typically <100ms even for large files).
    // See: upload_chunk / cancel_chunked also acquire this mutex.
    ChunkedUploadState state;
    std::vector<unsigned char> complete_data;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = chunked_uploads_.find(upload_id);
        if (it == chunked_uploads_.end()) {
            throw std::runtime_error("Chunked upload not found: " + upload_id);
        }
        state = it->second;

        // Verify all chunks present
        for (int i = 0; i < state.chunks_total; ++i) {
            std::ostringstream chunk_name;
            chunk_name << "chunk_" << std::setfill('0') << std::setw(4) << i;
            if (!fs::exists(fs::path(state.temp_dir) / chunk_name.str())) {
                throw std::runtime_error("Missing chunk " + std::to_string(i) + " for upload " + upload_id);
            }
        }

        // Concatenate all chunks in order
        for (int i = 0; i < state.chunks_total; ++i) {
            std::ostringstream chunk_name;
            chunk_name << "chunk_" << std::setfill('0') << std::setw(4) << i;
            fs::path chunk_path = fs::path(state.temp_dir) / chunk_name.str();

            std::ifstream in(chunk_path, std::ios::binary | std::ios::ate);
            if (!in) {
                throw std::runtime_error("Failed to read chunk file: " + chunk_path.string());
            }
            auto file_size = in.tellg();
            in.seekg(0);
            std::vector<unsigned char> chunk_data(static_cast<size_t>(file_size));
            in.read(reinterpret_cast<char*>(chunk_data.data()), file_size);
            complete_data.insert(complete_data.end(), chunk_data.begin(), chunk_data.end());
        }

        // Verify total size
        if (static_cast<int64_t>(complete_data.size()) != state.total_size) {
            throw std::runtime_error(
                "Chunked upload size mismatch: expected " + std::to_string(state.total_size)
                + " bytes, got " + std::to_string(complete_data.size()) + " bytes"
            );
        }

        // Clean up temp dir and state before releasing lock
        std::error_code ec;
        fs::remove_all(state.temp_dir, ec);
        chunked_uploads_.erase(it);
    }
    // Lock released — upload() uses db_query_/db_exec_ which are independently synchronized

    return upload(state.world_id, state.agent_id, state.image_type,
                  state.file_name, state.mime_type, complete_data);
}

void ImageService::cancel_chunked(const std::string& upload_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = chunked_uploads_.find(upload_id);
    if (it == chunked_uploads_.end()) {
        return;  // nothing to cancel
    }

    // Remove temp dir
    std::error_code ec;
    fs::remove_all(it->second.temp_dir, ec);

    // Erase state from map
    chunked_uploads_.erase(it);
}

std::vector<ImageRecord> ImageService::list_images(const std::string& agent_id) {
    nlohmann::json query_params;
    query_params["agent_id"] = agent_id;
    auto rows = db_query_(
        "SELECT id, agent_id, image_type, storage_key, mime_type, "
        "original_name, file_size_bytes, is_primary, sort_order, created_at "
        "FROM agent_images WHERE agent_id = @agent_id ORDER BY sort_order ASC, created_at DESC",
        query_params
    );

    std::vector<ImageRecord> result;
    for (const auto& row : rows) {
        ImageRecord rec;
        rec.id = row.value("id", "");
        rec.agent_id = row.value("agent_id", "");
        rec.image_type = row.value("image_type", "");
        rec.storage_key = row.value("storage_key", "");
        rec.mime_type = row.value("mime_type", "");
        rec.original_name = row.value("original_name", "");
        rec.file_size_bytes = row.value("file_size_bytes", 0);
        rec.is_primary = row.value("is_primary", false);
        rec.sort_order = row.value("sort_order", 0);
        rec.created_at = row.value("created_at", "");
        result.push_back(std::move(rec));
    }
    return result;
}

std::optional<ImageRecord> ImageService::get_image(const std::string& image_id) {
    nlohmann::json query_params;
    query_params["id"] = image_id;
    auto rows = db_query_(
        "SELECT id, agent_id, image_type, storage_key, mime_type, "
        "original_name, file_size_bytes, is_primary, sort_order, created_at "
        "FROM agent_images WHERE id = @id",
        query_params
    );

    if (rows.empty() || (rows.is_array() && rows.empty())) {
        return std::nullopt;
    }

    const auto& row = (rows.is_array()) ? rows[0] : rows;
    ImageRecord rec;
    rec.id = row.value("id", "");
    rec.agent_id = row.value("agent_id", "");
    rec.image_type = row.value("image_type", "");
    rec.storage_key = row.value("storage_key", "");
    rec.mime_type = row.value("mime_type", "");
    rec.original_name = row.value("original_name", "");
    rec.file_size_bytes = row.value("file_size_bytes", 0);
    rec.is_primary = row.value("is_primary", false);
    rec.sort_order = row.value("sort_order", 0);
    rec.created_at = row.value("created_at", "");
    return rec;
}

std::unordered_map<std::string, ImageRecord> ImageService::list_primary_avatars(
    const std::vector<std::string>& agent_ids) {
    std::unordered_map<std::string, ImageRecord> result;
    if (agent_ids.empty()) return result;

    // Build IN clause with @a0, @a1, ... placeholders for parameterized query
    nlohmann::json query_params;
    std::ostringstream sql;
    sql << "SELECT id, agent_id, image_type, storage_key, mime_type, "
           "original_name, file_size_bytes, is_primary, sort_order, created_at "
           "FROM agent_images WHERE agent_id IN (";
    for (size_t i = 0; i < agent_ids.size(); ++i) {
        if (i > 0) sql << ", ";
        std::string param_name = "a" + std::to_string(i);
        sql << "@" << param_name;
        query_params[param_name] = agent_ids[i];
    }
    sql << ") AND image_type = 'avatar' AND is_primary = true";

    auto rows = db_query_(sql.str(), query_params);
    for (const auto& row : rows) {
        ImageRecord rec;
        rec.id = row.value("id", "");
        rec.agent_id = row.value("agent_id", "");
        rec.image_type = row.value("image_type", "");
        rec.storage_key = row.value("storage_key", "");
        rec.mime_type = row.value("mime_type", "");
        rec.original_name = row.value("original_name", "");
        rec.file_size_bytes = row.value("file_size_bytes", 0);
        rec.is_primary = row.value("is_primary", false);
        rec.sort_order = row.value("sort_order", 0);
        rec.created_at = row.value("created_at", "");
        result[rec.agent_id] = std::move(rec);
    }
    return result;
}

void ImageService::delete_image(const std::string& image_id) {
    // Load the record to get the storage_key for file removal
    auto record = get_image(image_id);
    if (!record.has_value()) {
        return;  // nothing to delete
    }

    // Remove the file via store
    if (!record->storage_key.empty()) {
        store_->remove(record->storage_key);
    }

    // Delete the DB record
    nlohmann::json delete_params;
    delete_params["id"] = image_id;
    db_exec_("DELETE FROM agent_images WHERE id = @id", delete_params);
}

void ImageService::update_image(const std::string& image_id,
                                 std::optional<bool> is_primary,
                                 std::optional<int> sort_order) {
    // Handle set_primary logic
    if (is_primary.has_value() && is_primary.value()) {
        auto record = get_image(image_id);
        if (record.has_value()) {
            set_primary(record->agent_id, record->image_type, image_id);
        }
    }

    // Update sort_order if provided
    if (sort_order.has_value()) {
        nlohmann::json update_params;
        update_params["id"] = image_id;
        update_params["sort_order"] = sort_order.value();
        db_exec_(
            "UPDATE agent_images SET sort_order = @sort_order WHERE id = @id",
            update_params
        );
    }
}

} // namespace merak
