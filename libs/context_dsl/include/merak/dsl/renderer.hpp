#pragma once

#include <merak/dsl/resolver.hpp>

#include <string>
#include <vector>

namespace merak::dsl {

class Renderer {
public:
    static std::string render(const std::string& template_text,
                              const std::vector<ResolvedContent>& resolved);
};

} // namespace merak::dsl
