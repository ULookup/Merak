#include <merak/dsl/renderer.hpp>

#include <string>
#include <vector>

namespace merak::dsl {

std::string Renderer::render(const std::string& template_text,
                              const std::vector<ResolvedContent>& resolved) {
    std::string result = template_text;

    for (const auto& item : resolved) {
        auto pos = result.find(item.ref_raw);
        if (pos != std::string::npos) {
            result.replace(pos, item.ref_raw.length(), item.rendered);
        }
    }

    return result;
}

} // namespace merak::dsl
