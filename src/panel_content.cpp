#include "panel_content.hpp"
#include "set_catalog.generated.hpp"
#include <cstdint>
#include <stdexcept>

namespace itemsets {
namespace {
struct Rune { std::uint32_t code; std::string_view bytes; };
Rune NextRune(std::string_view value, std::size_t& offset) {
    const auto first = static_cast<unsigned char>(value[offset]);
    const int count = first < 0x80 ? 1 : (first & 0xE0) == 0xC0 ? 2 :
        (first & 0xF0) == 0xE0 ? 3 : (first & 0xF8) == 0xF0 ? 4 : 0;
    if (!count || offset + count > value.size()) throw std::invalid_argument("Invalid tooltip UTF-8");
    std::uint32_t code = first & (count == 1 ? 0x7F : count == 2 ? 0x1F : count == 3 ? 0x0F : 0x07);
    for (int i = 1; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(value[offset + i]);
        if ((byte & 0xC0) != 0x80) throw std::invalid_argument("Invalid tooltip UTF-8");
        code = (code << 6) | (byte & 0x3F);
    }
    if ((count == 2 && code < 0x80) || (count == 3 && code < 0x800) ||
        (count == 4 && code < 0x10000) || code > 0x10FFFF ||
        (code >= 0xD800 && code <= 0xDFFF)) throw std::invalid_argument("Invalid tooltip UTF-8");
    const auto start = offset;
    offset += count;
    return {code, value.substr(start, count)};
}
bool Cjk(std::uint32_t cp) {
    return (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
        (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
        (cp >= 0x20000 && cp <= 0x3134F);
}
bool SpaceOrBreak(std::uint32_t cp) {
    return cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t' || cp == 0xA0 || cp == 0x200B;
}
bool Opening(std::uint32_t cp) {
    return cp == 0x3010 || cp == 0xFF08 || cp == 0x300C || cp == 0x300E ||
        cp == 0x300A || cp == 0x3008 || cp == 0x2018 || cp == 0x201C;
}
bool Closing(std::uint32_t cp) {
    return cp == 0x3011 || cp == 0xFF09 || cp == 0x300D || cp == 0x300F ||
        cp == 0x300B || cp == 0x3009 || cp == 0x2019 || cp == 0x201D ||
        cp == 0x3001 || cp == 0x3002 || cp == 0xFF0C || cp == 0xFF1A ||
        cp == 0xFF1B || cp == 0xFF01 || cp == 0xFF1F;
}
std::size_t EstimatedLines(std::string_view body) {
    // InventoryTooltip.desc fits roughly 17 CJK glyphs and 11 lines at its
    // native 20px face. Use the actual native zero-width breaks for line width.
    constexpr std::size_t kLineUnits = 34;
    std::size_t lines = 1, units = 0, offset = 0;
    while (offset < body.size()) {
        const auto cp = NextRune(body, offset).code;
        if (cp == '\n') { ++lines; units = 0; continue; }
        if (cp == 0x200B) continue;
        const auto advance = Cjk(cp) ? 2U : 1U;
        if (units + advance > kLineUnits) { ++lines; units = 0; }
        units += advance;
    }
    return lines;
}
}

std::string AddNativeBreaks(std::string_view input) {
    // The game's word wrapper recognizes spaces, line feeds and U+200B only.
    // Chinese prose and the comma-joined member list have none of those.
    std::string text;
    text.reserve(input.size() * 2);
    std::size_t offset = 0;
    std::uint32_t previous = 0;
    bool marked_name = false;
    while (offset < input.size()) {
        const auto rune = NextRune(input, offset);
        if (previous && !marked_name && (Cjk(previous) || Cjk(rune.code)) &&
            !SpaceOrBreak(previous) && !SpaceOrBreak(rune.code) &&
            !Opening(previous) && !Closing(rune.code)) text += "\xE2\x80\x8B";
        text.append(rune.bytes);
        if (rune.code == 0x3010) marked_name = true;
        else if (rune.code == 0x3011) marked_name = false;
        previous = rune.code;
    }
    return text;
}

TooltipContent FormatSetTooltip(const ItemSet& set, std::string_view hovered_item) {
    TooltipContent out;
    out.key = std::string("ItemSetInfo.") + set.id;
    out.title = set.name;
    constexpr std::string_view suffix = "加成！";
    if (out.title.size() >= suffix.size() &&
        std::string_view(out.title).substr(out.title.size() - suffix.size()) == suffix)
        out.title.resize(out.title.size() - suffix.size());
    for (std::size_t i = 0; i < set.bonus_count; ++i) {
        if (i) out.body += "\n\n";
        out.body += "[效果]\n";
        out.body += set.bonuses[i].text;
    }
    if (set.member_count) {
        if (!out.body.empty()) out.body += "\n\n";
        out.body += "[套装部件]\n";
        for (std::size_t i = 0; i < set.member_count; ++i) {
            if (i) out.body += "、";
            // Mark the current item, and preserve the full member list.
            if (hovered_item == set.members[i].id) out.body += "【";
            out.body += set.members[i].name;
            if (hovered_item == set.members[i].id) out.body += "】";
        }
    }
    out.layout_lines = EstimatedLines(out.body);
    return out;
}

std::vector<TooltipContent> BuildItemTooltips(std::string_view item_id) {
    std::vector<TooltipContent> result;
    if (item_id.empty()) return result;
    for (const auto& set : kItemSets) {
        for (std::size_t i = 0; i < set.member_count; ++i) {
            if (item_id != set.members[i].id) continue;
            auto panel = FormatSetTooltip(set, item_id);
            panel.body = AddNativeBreaks(panel.body);
            result.push_back(std::move(panel));
            break;
        }
    }
    return result;
}
}
