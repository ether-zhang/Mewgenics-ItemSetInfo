#pragma once
#include <cstddef>
#include "catalog_types.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace itemsets {
struct TooltipContent {
    std::string key;
    std::string title;
    std::string body;
    std::size_t layout_lines = 0;
};
TooltipContent FormatSetTooltip(const ItemSet& set, std::string_view hovered_item);
std::string AddNativeBreaks(std::string_view text);
std::vector<TooltipContent> BuildItemTooltips(std::string_view item_id);
}
