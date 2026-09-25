#include "../src/panel_content.hpp"
#include "../src/set_catalog.generated.hpp"
#include "../src/safe_read.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>
#include <stdexcept>
#include <array>
#include <cstring>

std::string WithoutNativeBreaks(std::string text) {
    constexpr const char* zws = "\xE2\x80\x8B";
    for (auto at = text.find(zws); at != std::string::npos; at = text.find(zws)) text.erase(at, 3);
    return text;
}

int main() {
    using namespace itemsets;
    // Game's autoscale_bg passes TooltipBox+0x130 directly to the child
    // finder. Regress the live failure where +0x38 was incorrectly followed.
    std::array<unsigned char,0x178> box{};
    std::array<unsigned char,0x88> root_node{};
    const auto expected_root = reinterpret_cast<std::uintptr_t>(root_node.data());
    const std::uintptr_t display = 0x10000, wrong_outer_component = 0x101;
    std::memcpy(box.data()+0x130,&expected_root,sizeof(expected_root));
    std::memcpy(root_node.data()+0x80,&display,sizeof(display));
    std::memcpy(root_node.data()+0x38,&wrong_outer_component,sizeof(wrong_outer_component));
    std::uintptr_t actual_root = 0;
    assert(ReadTooltipRoot(reinterpret_cast<std::uintptr_t>(box.data()),actual_root));
    assert(actual_root == expected_root && actual_root != wrong_outer_component);
    std::memset(root_node.data()+0x80,0,sizeof(display));
    assert(!ReadTooltipRoot(reinterpret_cast<std::uintptr_t>(box.data()),actual_root));
    const SetMember members[] = {{"A", "测试甲"}, {"B", "测试乙"}, {"C", "测试丙"}};
    const SetBonus bonuses[] = {{2, "力量 +1"}, {3, "测试功能效果"}};
    const ItemSet set{"Example", "测试套装", members, 3, bonuses, 2};
    const auto panel = FormatSetTooltip(set, "B");
    assert(panel.key == "ItemSetInfo.Example" && panel.title == "测试套装");
    assert(panel.body == "[效果]\n力量 +1\n\n[效果]\n测试功能效果\n\n[套装部件]\n测试甲、【测试乙】、测试丙");
    const ItemSet localized{"Used", "二手套装加成！", members, 3, bonuses, 2};
    assert(FormatSetTooltip(localized, "A").title == "二手套装");
    const auto source = std::string("力量 +4，测试【当前物品】、中文说明");
    const auto wrapped = AddNativeBreaks(source);
    assert(wrapped.find("\xE2\x80\x8B") != std::string::npos);
    assert(wrapped.find("【当前物品】") != std::string::npos);
    assert(WithoutNativeBreaks(wrapped) == source);
    assert(WithoutNativeBreaks(AddNativeBreaks("⭐中文\n说明")) == "⭐中文\n说明");
    bool malformed_rejected = false;
    try { AddNativeBreaks("\xC0\xAF"); } catch (const std::invalid_argument&) { malformed_rejected = true; }
    assert(malformed_rejected);
    assert(FormatSetTooltip(set, "Unknown").body.find("【") == std::string::npos);
    assert(BuildItemTooltips("").empty());
    assert(BuildItemTooltips("__ItemSetInfo_unknown_item__").empty());
    // Every generated membership resolves to exactly one matching panel;
    // overlapping sets must not hide each other or generate duplicate keys.
    std::set<std::string> items;
    bool has_multiple_sets = false;
    const ItemSet* long_set = nullptr;
    for (const auto& current : kItemSets)
        for (std::size_t i = 0; i < current.member_count; ++i) {
            items.insert(current.members[i].id);
            if (std::string(current.id) == "Rock") long_set = &current;
        }
    for (const auto& id : items) {
        const auto panels = BuildItemTooltips(id);
        std::set<std::string> expected;
        for (const auto& current : kItemSets)
            for (std::size_t i = 0; i < current.member_count; ++i)
                if (id == current.members[i].id) expected.insert(std::string("ItemSetInfo.") + current.id);
        std::set<std::string> actual;
        for (const auto& p : panels) {
            assert(!p.title.empty() && !p.body.empty());
            assert(actual.insert(p.key).second);
            assert(WithoutNativeBreaks(p.body).find("[效果]") != std::string::npos);
            assert(WithoutNativeBreaks(p.body).find("[套装部件]") != std::string::npos);
            assert(p.layout_lines > 0);
        }
        assert(expected == actual);
        has_multiple_sets |= expected.size() > 1;
    }
    assert(!items.empty() && has_multiple_sets);
    const ItemSet* used_set = nullptr;
    for (const auto& current : kItemSets) if (std::string(current.id) == "Used") used_set = &current;
    assert(used_set && used_set->member_count == 22);
    const auto used_panels = BuildItemTooltips(used_set->members[0].id);
    const auto used_key = std::string("ItemSetInfo.") + used_set->id;
    const auto used = std::find_if(used_panels.begin(), used_panels.end(), [&](const auto& p) { return p.key == used_key; });
    assert(used != used_panels.end() && used->title == "二手套装");
    assert(WithoutNativeBreaks(used->body).find("[效果]") != std::string::npos);
    assert(WithoutNativeBreaks(used->body).find("[套装部件]") != std::string::npos);
    assert(std::none_of(used_panels.begin(), used_panels.end(), [&](const auto& p) { return p.key == used_key + ".members"; }));
    assert(long_set && long_set->member_count >= 30);
    const auto rock_panels = BuildItemTooltips(long_set->members[0].id);
    const auto rock_key = std::string("ItemSetInfo.") + long_set->id;
    const auto effect = std::find_if(rock_panels.begin(), rock_panels.end(), [&](const auto& p) { return p.key == rock_key; });
    assert(effect != rock_panels.end() && effect->layout_lines > 12);
    assert(WithoutNativeBreaks(effect->body).find(long_set->bonuses[0].text) != std::string::npos);
    assert(WithoutNativeBreaks(effect->body).find("[套装部件]") != std::string::npos);
    assert(WithoutNativeBreaks(effect->body).find(long_set->members[0].name) != std::string::npos);
    assert(std::none_of(rock_panels.begin(), rock_panels.end(), [&](const auto& p) { return p.key == rock_key + ".members"; }));
    std::cout << "Item set content passed: bonus tiers, CJK breaks, complete one-card content for all sets, current-item marking and multi-set membership.\n";
}
