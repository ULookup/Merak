#include <merak/storage/local_file_image_store.hpp>
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

namespace merak {

LocalFileImageStore::LocalFileImageStore(std::string base_dir, std::string url_prefix)
    : base_dir_(std::move(base_dir)), url_prefix_(std::move(url_prefix)) {}

std::string LocalFileImageStore::save(const std::string& key, const ImageData& data) {
    fs::path file_path = fs::path(base_dir_) / key;
    fs::create_directories(file_path.parent_path());

    std::ofstream out(file_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to write file: " + file_path.string());
    }
    out.write(reinterpret_cast<const char*>(data.bytes.data()),
              static_cast<std::streamsize>(data.bytes.size()));
    return key;
}

ImageData LocalFileImageStore::load(const std::string& key) {
    fs::path file_path = fs::path(base_dir_) / key;
    std::ifstream in(file_path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("File not found: " + file_path.string());
    }

    auto size = static_cast<size_t>(in.tellg());
    in.seekg(0);

    ImageData data;
    data.bytes.resize(size);
    in.read(reinterpret_cast<char*>(data.bytes.data()), static_cast<std::streamsize>(size));

    // Detect mime type from extension
    std::string ext = file_path.extension().string();
    if (ext == ".png") data.mime_type = "image/png";
    else if (ext == ".jpg" || ext == ".jpeg") data.mime_type = "image/jpeg";
    else if (ext == ".webp") data.mime_type = "image/webp";
    else if (ext == ".gif") data.mime_type = "image/gif";
    else data.mime_type = "application/octet-stream";

    return data;
}

void LocalFileImageStore::remove(const std::string& key) {
    fs::path file_path = fs::path(base_dir_) / key;
    std::error_code ec;
    fs::remove(file_path, ec);
}

std::string LocalFileImageStore::public_url(const std::string& key) const {
    return url_prefix_ + "/" + key;
}

} // namespace merak
