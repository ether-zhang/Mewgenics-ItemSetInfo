#pragma once
#include <cstdint>
#include <string>

namespace itemsets {
// Called on the game's UI thread after the original item/keyword tooltips.
// All strings are UTF-8; key is stable across hover frames.
// Native ToolTip owns the submitted copy, positioning, animation and lifetime.
bool AppendSetTooltip(std::uintptr_t image, const std::string& key,
    const std::string& title, const std::string& body, std::size_t layout_lines);
}
