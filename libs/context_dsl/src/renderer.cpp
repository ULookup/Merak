#include <merak/dsl/renderer.hpp>

#include <string>
#include <vector>

namespace merak::dsl {

std::string Renderer::render(const std::string& template_text,
                              const std::vector<ResolvedContent>& resolved) {
    std::string result = template_text;

    // Pass 1: replace each @xxx{...} reference with a unique null-byte-delimited placeholder.
    // This ensures no rendered content can accidentally match another reference pattern.
    std::vector<std::string> placeholders;
    placeholders.reserve(resolved.size());
    for (size_t i = 0; i < resolved.size(); ++i) {
        std::string ph = "\x00DSL:" + std::to_string(i) + "\x00";
        placeholders.push_back(ph);
        auto pos = result.find(resolved[i].ref_raw);
        if (pos != std::string::npos) {
            result.replace(pos, resolved[i].ref_raw.length(), ph);
        }
    }

    // Pass 2: replace each placeholder with the actual rendered content.
    // Since placeholders use \x00 (never present in normal text), no collision possible.
    for (size_t i = 0; i < resolved.size(); ++i) {
        auto pos = result.find(placeholders[i]);
        if (pos != std::string::npos) {
            result.replace(pos, placeholders[i].length(), resolved[i].rendered);
        }
    }

    return result;
}

} // namespace merak::dsl
