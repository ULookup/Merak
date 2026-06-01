#pragma once
#include <string>
#include <vector>
#include <functional>
#include <ftxui/component/component.hpp>

namespace merak::tui {

struct FuzzyItem {
    std::string display;
    std::string group;
    std::string hint;
    std::string search;
};

inline ftxui::Component fuzzy_list_component(
    std::vector<FuzzyItem>& /*items*/,
    std::function<void(const FuzzyItem&)> /*on_select*/,
    std::function<void()> /*on_cancel*/)
{
    using namespace ftxui;
    return Container::Vertical({});
}

} // namespace merak::tui
