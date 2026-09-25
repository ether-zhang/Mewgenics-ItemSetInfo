#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "native_tooltip.hpp"
#include "safe_read.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <limits>
#include <string_view>

namespace itemsets {
namespace {
constexpr std::uintptr_t kTooltipGlobal = 0x13DE660;
constexpr std::uintptr_t kTooltipVtable = 0x11237C8;
constexpr std::uintptr_t kConstructData = 0x5B960;
constexpr std::uintptr_t kDestroyData = 0x5BAA0;
constexpr std::uintptr_t kConstructNarrow = 0x52AF0;
constexpr std::uintptr_t kDestroyNarrow = 0x52780;
constexpr std::uintptr_t kAssignNarrow = 0x520D0;
constexpr std::uintptr_t kIndexTextMap = 0x60A40;
constexpr std::uintptr_t kAssignWide = 0x5B150;
constexpr std::uintptr_t kAppendSecondary = 0x8CD330;
constexpr std::uintptr_t kIndexFrameMap = 0x60BA0;
constexpr std::uintptr_t kFindDirectChild = 0xE8FB0;
constexpr std::uintptr_t kDynamicTextVtable = 0x112CFF8;
constexpr std::uintptr_t kEditTextVtable = 0x113DCB8;
constexpr std::size_t kEditTextSize = 0x130;
constexpr std::size_t kSecondaryStride = 0x178;
constexpr int kEmptyStatusFrame = 1000;

template<class F> F Function(std::uintptr_t image, std::uintptr_t offset) {
    return reinterpret_cast<F>(image + offset);
}
using Construct = void* (__fastcall*)(void*);
using Destroy = void (__fastcall*)(void*);
using InitNarrow = void* (__fastcall*)(void*, const char*);
using AssignNarrow = void* (__fastcall*)(void*, const char*, std::size_t);
using IndexTextMap = void* (__fastcall*)(void*, void*);
using FindDirectChild = void* (__fastcall*)(void*, void*);
using AssignWide = void* (__fastcall*)(void*, const wchar_t*, std::size_t);
using Append = void (__fastcall*)(void*, void*, void*);

bool DecodeUtf8(const std::string& source, std::wstring& result) {
    if (source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    if (source.empty()) { result.clear(); return true; }
    const int size = static_cast<int>(source.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        source.data(), size, nullptr, 0);
    if (!count) return false;
    result.resize(static_cast<std::size_t>(count));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        source.data(), size, result.data(), count) == count;
}

// The executable statically links its allocator. Never put a DLL-owned
// std::string/std::map in engine storage: all allocations and frees below use
// the engine's own constructors, setters and destructors.
struct EngineString {
    alignas(16) std::array<std::byte, 0x20> bytes{};
    std::uintptr_t image;
    bool owned = false;

    EngineString(std::uintptr_t imageBase, const char* text) : image(imageBase) {
        Function<InitNarrow>(image, kConstructNarrow)(bytes.data(), text);
        owned = true;
    }
    ~EngineString() {
        if (owned) Function<Destroy>(image, kDestroyNarrow)(bytes.data());
    }
    EngineString(const EngineString&) = delete;
    EngineString& operator=(const EngineString&) = delete;
};

struct EngineTooltipData {
    alignas(16) std::array<std::byte, 0x110> bytes{};
    std::uintptr_t image;
    bool owned = false;

    explicit EngineTooltipData(std::uintptr_t imageBase) : image(imageBase) {
        Function<Construct>(image, kConstructData)(bytes.data());
        owned = true;
    }
    ~EngineTooltipData() {
        if (owned) Function<Destroy>(image, kDestroyData)(bytes.data());
    }
    EngineTooltipData(const EngineTooltipData&) = delete;
    EngineTooltipData& operator=(const EngineTooltipData&) = delete;

    void SetTemplate(std::string_view name) {
        Function<AssignNarrow>(image, kAssignNarrow)(bytes.data() + 0x20,
            name.data(), name.size());
    }
    void SetText(const char* name, const std::wstring& value) {
        EngineString field(image, name);
        // operator[] moves the key if it inserts a node, resetting the source
        // to an empty string. Its destructor is valid in either case.
        void* destination = Function<IndexTextMap>(image, kIndexTextMap)(
            bytes.data() + 0x40, field.bytes.data());
        Function<AssignWide>(image, kAssignWide)(destination, value.data(), value.size());
    }
    void SetFrame(const char* name, int frame) {
        EngineString field(image, name);
        auto* destination = Function<IndexTextMap>(image, kIndexFrameMap)(
            bytes.data() + 0x50, field.bytes.data());
        std::memcpy(destination, &frame, sizeof(frame));
    }
};

void GrowOwnDescription(std::uintptr_t image, std::uintptr_t tooltip,
    const std::string& key, std::size_t lines) {
    if (lines <= 11 || lines > 128) return;
    std::uintptr_t begin = 0, end = 0;
    if (!Read(tooltip + 0x1D0, begin) || !Read(tooltip + 0x1D8, end) ||
        begin < 0x10000 || end < begin ||
        (end - begin) % kSecondaryStride ||
        (end - begin) / kSecondaryStride > 128) return;

    for (auto box = begin; box < end; box += kSecondaryStride) {
        std::string name;
        if (!ReadNarrowString(box, name, 512) ||
            name.size() <= key.size() ||
            name.compare(name.size() - key.size(), key.size(), key) != 0 ||
            name[name.size() - key.size() - 1] != '|') continue;
        std::uintptr_t scene = 0, expected = 0, live = 0, root = 0;
        unsigned char destroying = 1;
        if (!Read(box + 0xB0, scene) || scene < 0x10000 ||
            !Read(box + 0xB8, expected) || !Read(scene - 8, live) ||
            live != expected || !Read(scene + 0x4DB, destroying) || destroying ||
            !ReadTooltipRoot(box, root)) return;
        EngineString field(image, "desc");
        // The native child lookup consumes its short name argument, like the
        // MewUI API's own wrapper for this entry point.
        field.owned = false;
        auto* node = Function<FindDirectChild>(image, kFindDirectChild)(
            reinterpret_cast<void*>(root), field.bytes.data());
        std::uintptr_t type = 0, definition = 0, definition_type = 0;
        if (!node || !Read(reinterpret_cast<std::uintptr_t>(node), type) ||
            type != image + kDynamicTextVtable ||
            !Read(reinterpret_cast<std::uintptr_t>(node) + 0x98, definition) ||
            definition < 0x10000 || !Read(definition, definition_type)) return;

        // The DefineEditText resource is shared by every native keyword card.
        // A private immutable copy changes this one instance only. The game's
        // DynamicTextBox destructor does not own/release its resource pointer;
        // retain each small copy for this process, avoiding allocator crossing.
        static std::map<std::pair<std::uintptr_t,std::size_t>, std::uintptr_t> copies;
        for (const auto& [source_and_lines, copy] : copies)
            if (definition == copy) return;
        if (definition_type != image + kEditTextVtable) return;
        const auto requested = std::min<std::size_t>(lines, 32);
        const auto entry = std::make_pair(definition, requested);
        auto found = copies.find(entry);
        std::uintptr_t copy = found == copies.end() ? 0 : found->second;
        if (!copy) {
            float top = 0, bottom = 0;
            if (!Read(definition + 0xD0, top) || !Read(definition + 0xD4, bottom) ||
                !std::isfinite(top) || !std::isfinite(bottom) ||
                bottom - top < 100 || bottom - top > 100000) return;
            auto* storage = VirtualAlloc(nullptr, kEditTextSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!storage) return;
            if (!ReadBytes(definition, storage, kEditTextSize)) { VirtualFree(storage, 0, MEM_RELEASE); return; }
            const float factor = std::min(3.0f, static_cast<float>(requested + 2) / 11.0f);
            const float extended = top + (bottom - top) * factor;
            std::memcpy(static_cast<unsigned char*>(storage) + 0xD4, &extended, sizeof(extended));
            copy = reinterpret_cast<std::uintptr_t>(storage);
            copies.emplace(entry, copy);
        }
        std::memcpy(reinterpret_cast<unsigned char*>(node) + 0x98, &copy, sizeof(copy));
        return;
    }
}
}

bool AppendSetTooltip(std::uintptr_t image, const std::string& key,
    const std::string& title, const std::string& body, std::size_t layout_lines) {
    if (!image || key.empty() || key.size() > 256 || key.find('\0') != std::string::npos ||
        title.empty() || body.empty()) return false;
    std::uintptr_t tooltip = 0, vtable = 0, expectedCookie = 0, liveCookie = 0;
    unsigned char disabled = 1, submitted = 0;
    if (!Read(image + kTooltipGlobal, tooltip) || tooltip < sizeof(std::uintptr_t) ||
        !Read(image + kTooltipGlobal + 8, expectedCookie) ||
        !Read(tooltip - 8, liveCookie) || liveCookie != expectedCookie ||
        !Read(tooltip, vtable) || vtable != image + kTooltipVtable ||
        !Read(tooltip + 0x50, disabled) || disabled ||
        !Read(tooltip + 0x138, submitted) || !submitted) return false;

    std::wstring wideTitle, wideBody;
    if (!DecodeUtf8(title, wideTitle) || !DecodeUtf8(body, wideBody)) return false;
    EngineTooltipData data(image);
    data.SetTemplate("KeywordTooltip");
    data.SetText("itemname", wideTitle);
    data.SetText("desc", wideBody);
    // The game's status MovieClip has a 1,015-frame icon reel. Without a
    // selected frame it cycles through unrelated symbols. Frame 1000 has no
    // display children, so this set card has no status icon.
    data.SetFrame("status", kEmptyStatusFrame);
    // The game's descriptive keyword cards use 0.5s at TooltipData+E8.
    // Match that delay after the original cards have been submitted.
    const double keyword_delay = 0.5;
    std::memcpy(data.bytes.data() + 0xE8, &keyword_delay, sizeof(keyword_delay));
    EngineString instance(image, key.c_str());

    // This is the ABI for two C++ by-value arguments, not reference arguments.
    // The callee copies them into its keyed secondary-box collection and then
    // destroys both argument objects, including on its early-return path.
    data.owned = false;
    instance.owned = false;
    Function<Append>(image, kAppendSecondary)(reinterpret_cast<void*>(tooltip),
        instance.bytes.data(), data.bytes.data());
    GrowOwnDescription(image, tooltip, key, layout_lines);

    // Native ToolTip::update aligns the first secondary box's top-left corner
    // with the main box's top-right (0x8CC9BA). Further boxes stack below it;
    // the game also handles screen edges, fade-out and scene destruction.
    return true;
}
}
