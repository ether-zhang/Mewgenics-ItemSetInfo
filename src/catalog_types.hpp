#pragma once
#include <cstddef>

namespace itemsets {
struct SetMember {
    const char* id;
    const char* name;
};

struct SetBonus {
    int pieces;
    const char* text;
};

struct ItemSet {
    const char* id;
    const char* name;
    const SetMember* members;
    std::size_t member_count;
    const SetBonus* bonuses;
    std::size_t bonus_count;
};
}  // namespace itemsets
