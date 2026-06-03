#pragma once

#include <string>
#include <string_view>

namespace merak::worldbuilding {

std::string make_id(std::string_view prefix);
std::string now_iso_utc();

} // namespace merak::worldbuilding
