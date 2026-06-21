#include <merak/storage/local_file_image_store.hpp>
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace merak {

LocalFileImageStore::LocalFileImageStore(std::string base_dir, std::string url_prefix)
    : base_dir_(std::move(base_dir)), url_prefix_(std::move(url_prefix)) {}

std::string LocalFileImageStore::save(const std::string& key, const ImageData& data) {
    // Ensure base_dir_ exists for canonical resolution
    fs::create_directories(base_dir_);

    // Path traversal guard
    fs::path target = fs::weakly_canonical(fs::path(base_dir_) / key);
    fs::path base = fs::weakly_canonical(fs::path(base_dir_));
    std::string target_str = target.string();
    std::string base_str = base.string();
    if (target_str.size() < base_str.size() ||
        target_str.compare(0, base_str.size(), base_str) != 0) {
        throw std::runtime_error("Path traversal rejected: " + key);
    }

    fs::create_directories(target.parent_path());

    std::ofstream out(target, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to write file: " + target.string());
    }
    out.write(reinterpret_cast<const char*>(data.bytes.data()),
              static_cast<std::streamsize>(data.bytes.size()));
    return key;
}

ImageData LocalFileImageStore::load(const std::string& key) const {
    // Path traversal guard
    fs::path target = fs::weakly_canonical(fs::path(base_dir_) / key);
    fs::path base = fs::weakly_canonical(fs::path(base_dir_));
    std::string target_str = target.string();
    std::string base_str = base.string();
    if (target_str.size() < base_str.size() ||
        target_str.compare(0, base_str.size(), base_str) != 0) {
        throw std::runtime_error("Path traversal rejected: " + key);
    }

    std::ifstream in(target, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("File not found: " + target.string());
    }

    auto size = static_cast<size_t>(in.tellg());
    in.seekg(0);

    ImageData data;
    data.bytes.resize(size);
    in.read(reinterpret_cast<char*>(data.bytes.data()), static_cast<std::streamsize>(size));

    // Detect mime type from extension
    std::string ext = target.extension().string();
    if (ext == ".png") data.mime_type = "image/png";
    else if (ext == ".jpg" || ext == ".jpeg") data.mime_type = "image/jpeg";
    else if (ext == ".webp") data.mime_type = "image/webp";
    else if (ext == ".gif") data.mime_type = "image/gif";
    else data.mime_type = "application/octet-stream";

    return data;
}

void LocalFileImageStore::remove(const std::string& key) {
    // Path traversal guard
    fs::path target = fs::weakly_canonical(fs::path(base_dir_) / key);
    fs::path base = fs::weakly_canonical(fs::path(base_dir_));
    std::string target_str = target.string();
    std::string base_str = base.string();
    if (target_str.size() < base_str.size() ||
        target_str.compare(0, base_str.size(), base_str) != 0) {
        throw std::runtime_error("Path traversal rejected: " + key);
    }

    std::error_code ec;
    fs::remove(target, ec);
}

std::string LocalFileImageStore::public_url(const std::string& key) const {
    return url_prefix_ + "/" + key;
}

} // namespace merak
