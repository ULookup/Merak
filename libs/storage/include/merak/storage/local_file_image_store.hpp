#pragma once

#include <merak/storage/image_store.hpp>
#include <string>

namespace merak {

class LocalFileImageStore : public ImageStore {
public:
    // base_dir: root directory for file storage
    // url_prefix: URL path prefix for serving images (e.g., "/api/worldbuilding/images")
    LocalFileImageStore(std::string base_dir, std::string url_prefix);

    std::string save(const std::string& key, const ImageData& data) override;
    ImageData load(const std::string& key) override;
    void remove(const std::string& key) override;
    std::string public_url(const std::string& key) const override;

private:
    std::string base_dir_;
    std::string url_prefix_;
};

} // namespace merak
