#pragma once

#include <string>
#include <vector>

namespace merak {

struct ImageData {
    std::vector<unsigned char> bytes;
    std::string mime_type;
};

class ImageStore {
public:
    virtual ~ImageStore() = default;

    // Save image bytes, return storage key
    virtual std::string save(const std::string& key, const ImageData& data) = 0;

    // Load image bytes by key
    virtual ImageData load(const std::string& key) = 0;

    // Delete image by key
    virtual void remove(const std::string& key) = 0;

    // Return a URL this image can be served at (relative or absolute)
    virtual std::string public_url(const std::string& key) const = 0;
};

} // namespace merak
