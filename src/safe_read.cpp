#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "safe_read.hpp"
#include <array>
#include <cstring>
#include <limits>

namespace itemsets {
bool ReadBytes(std::uintptr_t address, void* destination, std::size_t size) noexcept {
    if (!destination || address < 0x10000 || size > 1024*1024 ||
        address > std::numeric_limits<std::uintptr_t>::max() - size) return false;
    __try { std::memcpy(destination, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadNarrowString(std::uintptr_t address, std::string& result, std::size_t limit) {
    std::array<std::uint64_t,4> storage{};
    if (!Read(address, storage)) return false;
    const auto size = storage[2], capacity = storage[3];
    if (size > limit || capacity < size || capacity > 1024*1024) return false;
    result.resize(static_cast<std::size_t>(size));
    return !size || ReadBytes(capacity < 16 ? address : storage[0], result.data(), result.size());
}
bool ReadTooltipRoot(std::uintptr_t box, std::uintptr_t& root) {
    root = 0;
    std::uintptr_t display = 0;
    // TooltipBox+0x130 directly owns the UI root. The native game's own
    // autoscale_bg passes this pointer unchanged to FindChildByName.
    return Read(box + 0x130, root) && root >= 0x10000 &&
        Read(root + 0x80, display) && display >= 0x10000;
}
}
