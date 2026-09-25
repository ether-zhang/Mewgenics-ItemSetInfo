#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace itemsets {
bool ReadBytes(std::uintptr_t address, void* destination, std::size_t size) noexcept;
bool ReadNarrowString(std::uintptr_t address, std::string& result, std::size_t limit = 256);
bool ReadTooltipRoot(std::uintptr_t box, std::uintptr_t& root);
template<class T> bool Read(std::uintptr_t address, T& result) noexcept {
    return ReadBytes(address, &result, sizeof(result));
}
}
