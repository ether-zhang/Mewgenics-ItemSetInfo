#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "mewjector.h"
#include "native_signatures.generated.hpp"
#include "native_tooltip.hpp"
#include "panel_content.hpp"
#include "safe_read.hpp"
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr const char* kOwner = "ItemSetInfo";
using ShowItemTooltip = void (__fastcall*)(void*, const void*);
MewjectorAPI api{};
ShowItemTooltip next_show = nullptr;
std::uintptr_t image = 0;
bool stopped = false;

bool VerifyNativeCode() {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!itemsets::Read(image, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || dos.e_lfanew > 0x100000 ||
        !itemsets::Read(image + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.FileHeader.TimeDateStamp != itemsets::kImageTimestamp ||
        nt.OptionalHeader.SizeOfImage != itemsets::kImageSize) {
        api.Log(kOwner, "Unsupported executable; item set information is disabled.");
        return false;
    }
    for (const auto& check : itemsets::kNativeSignatures) {
        // Shared function entries can already belong to a Mewjector chain.
        // Independent layout/ownership guards still check the supported build.
        if (api.QueryHook(check.rva) > 0) continue;
        std::array<unsigned char,32> bytes{};
        if (check.size > bytes.size() ||
            !itemsets::ReadBytes(image + check.rva, bytes.data(), check.size) ||
            std::memcmp(bytes.data(), check.bytes, check.size)) {
            api.Log(kOwner, "Native signature mismatch: %s; item set information is disabled.", check.name);
            return false;
        }
    }
    return true;
}

bool AppendForEquipment(void* equipment) {
    try {
        std::string item;
        if (!equipment || !itemsets::ReadNarrowString(reinterpret_cast<std::uintptr_t>(equipment) + 8, item)) return true;
        // Only format when the hovered item changes. The native tooltip must
        // still be submitted each hover frame so its own lifetime logic works.
        static thread_local std::string cached_item;
        static thread_local std::vector<itemsets::TooltipContent> cached_content;
        if (item != cached_item) {
            auto content = itemsets::BuildItemTooltips(item);
            cached_item = std::move(item);
            cached_content = std::move(content);
        }
        for (const auto& content : cached_content)
            itemsets::AppendSetTooltip(image, content.key, content.title, content.body, content.layout_lines);
        return true;
    } catch (...) {
        return false;
    }
}

bool AppendSafely(void* equipment) noexcept {
    // Keep SEH outside the C++ frame; never swallow exceptions from the
    // original game tooltip. An unexpected native failure disables this mod.
    __try { return AppendForEquipment(equipment); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void __fastcall ShowItemTooltipHook(void* equipment, const void* keywords) {
    next_show(equipment, keywords);
    // Original keyword descriptions are submitted first. Their shared native
    // delay is also used by our card, so all auxiliary boxes appear together.
    if (!stopped && !AppendSafely(equipment)) {
        stopped = true;
        api.Log(kOwner, "Stopped extra tooltips after an unexpected error; original item descriptions remain enabled.");
    }
}

void Bootstrap(HMODULE module) {
    if (!MJ_Resolve(&api)) return;
    image = api.GetGameBase();
    if (!image || !VerifyNativeCode()) return;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Bootstrap), &pinned)) return;
    void* next = nullptr;
    if (!api.InstallHook(itemsets::kRvaShowItemTooltip, itemsets::kStolenShowItemTooltip,
            reinterpret_cast<void*>(&ShowItemTooltipHook), &next, 35, kOwner) || !next) {
        api.Log(kOwner, "Could not register the item-tooltip hook.");
        return;
    }
    next_show = reinterpret_cast<ShowItemTooltip>(next);
    api.Log(kOwner, "ItemSetInfo 0.1.4 ready; native set tooltip root lookup corrected.");
}
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        try { Bootstrap(module); }
        catch (...) { if (api.Log) api.Log(kOwner, "Bootstrap failed; extra tooltips are disabled."); }
    }
    return TRUE;
}
