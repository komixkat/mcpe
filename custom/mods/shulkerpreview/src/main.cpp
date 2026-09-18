#include <dlfcn.h>
#include <link.h>
#include <span>
#include <string>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include "main.h"
#include <libhat.hpp>
#include <libhat/scanner.hpp>

using namespace hat::literals::signature_literals;

BaseActorRenderContext_ctor_t BaseActorRenderContext_ctor = nullptr;
ItemRenderer_renderGuiItemNew_t ItemRenderer_renderGuiItemNew = nullptr;

extern "C" [[gnu::visibility("default")]] void mod_preinit() {}

namespace {

#if defined(ARCH_ARM64)
constexpr auto Align = hat::scan_alignment::X1;
#else
constexpr auto Align = hat::scan_alignment::X16;
#endif

bool makeWritable(void* addr) {
    long pageSize = sysconf(_SC_PAGESIZE);
    uintptr_t start = (uintptr_t)addr & ~(pageSize - 1);
    return mprotect((void*)start, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

#if defined(ARCH_X86_64)
constexpr auto NbtTreeFindSignature =
"55 48 89 E5 41 57 41 56 41 55 41 54 53 50 4C 8B 6F 08 48 83 C7 08 4D 85 ED 0F 84 ? ? ? ? 4C 8B 3E 4C 8B 66 08 48 89 7D D0 49 89 FE EB 1B 90 C1 F8 1F 0C 01 31 C9 84 C0 0F 9F C1 4D 0F 4E F5 4D 8B 6C CD 00 4D 85 ED 74 41 41 0F B6 45 20 49 8D 75 21 89 C3 D1 EB A8 01 49 0F 45 75 30 49 0F 45 5D 28 49 39 DC 48 89 DA 49 0F 42 D4 4C 89 FF E8 ? ? ? ?"_sig;
constexpr auto ItemStackBaseLoadItemSignature =
"55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC D8 00 00 00 49 89 F7 49 89 FC 64 48 8B 04 25 ? ? ? ? 48 89 45 ? 48 8D 3D"_sig;
constexpr auto ItemStackBaseGetDamageValueSignature =
"55 48 89 E5 41 57 41 56 53 48 83 EC 28 64 48 8B 04 25 ? ? ? ? 48 89 45 ? 48 8B 47 ? 48 85 C0 0F 84 ? ? ? ? ? ? ? ? 0F 84 ? ? ? ? 4C 8B 77"_sig;
constexpr auto ItemStackBaseCtorSignature =
"55 48 89 E5 41 57 41 56 53 50 49 89 FE 48 8D 05 ? ? ? ? ? ? ? 48 8D 5F ? 0F 57 C0 0F 11 47 ? 0F 11 47 ? 66 C7 47 ? ? ? C6 47 ? ? 0F 11 47 ? 0F 11 47 ? 0F 11 47 ? 0F 11 47 ? 0F 11 47 ? 48 C7 47 ? ? ? ? ? E8"_sig;
constexpr auto BaseActorRenderContextCtorSignature =
"55 48 89 E5 41 57 41 56 41 55 41 54 53 50 49 89 D7 49 89 F4 48 89 FB 48 8D 05"_sig;
constexpr auto ItemRendererRenderGuiItemNewSignature =
"55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC E8 00 00 00 4C 89 8D F0 FE FF FF F3 0F 11 A5 04 FF FF FF"_sig;

#elif defined(ARCH_ARM64)
constexpr auto NbtTreeFindSignature =
"? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 F3 03 00 AA ? ? ? F8 ? ? ? B4 ? ? ? A9 F5 03 13 AA ? ? ? 14 ? ? ? 52 ? ? ? 71 ? ? ? 54 ? ? ? 91 ? ? ? F9 ? ? ? B4 ? ? ? 39 ? ? ? 36 ? ? ? F9 ? ? ? 36 ? ? ? F9 1F 03 16 EB E0 03 14 AA 02 33 96 9A ? ? ? 94 ? ? ? 34 ? ? ? 37 ? ? ? 52 ? ? ? 71 ? ? ? 54 ? ? ? 14 ? ? ? 91 ? ? ? 37 ? ? ? D3 1F 03 16 EB E0 03 14 AA 02 33 96 9A ? ? ? 94 ? ? ? 35 DF 02 18 EB ? ? ? 54 E8 03 1F 2A ? ? ? 71 ? ? ? 54 F5 03 17 AA ? ? ? F9 ? ? ? B5 ? ? ? 14 ? ? ? 54 ? ? ? 17 BF 02 13 EB ? ? ? 54 ? ? ? 39 ? ? ? A9 E0 03 14 AA ? ? ? D3 ? ? ? 72 ? ? ? 91 01 01 8B 9A 57 01 89 9A FF 02 16 EB E2 32 96 9A ? ? ? 94 DF 02 17 EB E8 27 9F 1A 1F 00 00 71 E9 A7 9F 1A 08 01 89 1A 1F 01 00 71 73 12 95 9A E0 03 13 AA ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A8 C0 03 5F D6 ? ? ? A9"_sig;
constexpr auto ItemStackBaseLoadItemSignature =
"? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D5 F3 03 00 AA ? ? ? ? ? ? ? 91 ? ? ? F9 F5 03 01 AA"_sig;
constexpr auto ItemStackBaseGetDamageValueSignature =
"? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D5 ? ? ? F9 ? ? ? F8 ? ? ? F9 ? ? ? B4 ? ? ? F9 ? ? ? B4 ? ? ? F9 ? ? ? B4"_sig;
constexpr auto BaseActorRenderContextCtorSignature =
"? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? ? ? ? ? 91 ? ? ? A9 ? ? ? A9 F3 03 00 AA F4 03 02 AA"_sig;
constexpr auto ItemRendererRenderGuiItemNewSignature =
"FF C3 05 D1 EC 73 00 FD EB 2B 0F 6D E9 23 10 6D FD 7B 11 A9 FC 6F 12 A9 FA 67 13 A9 F8 5F 14 A9 F6 57 15 A9 F4 4F 16 A9 FD 43 04 91 5B D0 3B D5"_sig;
constexpr auto ItemStackBaseCtorSignature =
"?? ?? ?? A9 ?? ?? ?? F9 ?? ?? ?? A9 FD 03 00 91 ?? ?? ?? 6F ?? ?? ?? ?? ?? ?? ?? 91 F4 03 00 AA ?? ?? ?? F9 F3 03 00 AA ?? ?? ?? 52 ?? ?? ?? F8 ?? ?? ?? A9 ?? ?? ?? B8 ?? ?? ?? 78 ?? ?? ?? 39 ?? ?? ?? 3C ?? ?? ?? 3C ?? ?? ?? 3C ?? ?? ?? 3C ?? ?? ?? 3C ?? ?? ?? F9 ?? ?? ?? 94"_sig;
#endif

} // namespace

extern "C" [[gnu::visibility("default")]] void mod_init()
{
    void* mcLib = dlopen("libminecraftpe.so", RTLD_NOW);
    if (!mcLib) {
        printf("[SP] failed to open libminecraftpe.so\n");
        return;
    }

    std::span<std::byte> range1, range2;
    auto callback = [&](const dl_phdr_info &info)
    {
        if (auto h = dlopen(info.dlpi_name, RTLD_NOLOAD); dlclose(h), h != mcLib)
            return 0;
        range1 = {reinterpret_cast<std::byte *>(info.dlpi_addr + info.dlpi_phdr[1].p_vaddr), info.dlpi_phdr[1].p_memsz};
        range2 = {reinterpret_cast<std::byte *>(info.dlpi_addr + info.dlpi_phdr[2].p_vaddr), info.dlpi_phdr[2].p_memsz};
        return 1;
    };
    dl_iterate_phdr([](dl_phdr_info *info, size_t, void *data) { return (*static_cast<decltype(callback) *>(data))(*info); }, &callback);

    if (range1.empty() || range2.empty()) {
        printf("[SP] libminecraftpe text/data ranges not found; preview disabled\n");
        return;
    }

    auto scan = [range1](const auto&... sig) {
        void* addr;
        ((addr = hat::find_pattern(range1, sig, hat::scan_alignment::X16).get()) || ...);
        return addr;
    };

    SP_loadConfig();

#define FIND(sig) hat::find_pattern(range1, sig, Align).get()

    Nbt_treeFind = (Nbt_treeFind_t)FIND(NbtTreeFindSignature);
    ItemStackBase_loadItem = (ItemStackBase_loadItem_t)FIND(ItemStackBaseLoadItemSignature);
    ItemStackBase_getDamageValue = (ItemStackBase_getDamageValue_t)FIND(ItemStackBaseGetDamageValueSignature);
    BaseActorRenderContext_ctor = (BaseActorRenderContext_ctor_t)FIND(BaseActorRenderContextCtorSignature);
    ItemRenderer_renderGuiItemNew = (ItemRenderer_renderGuiItemNew_t)FIND(ItemRendererRenderGuiItemNewSignature);
    ItemStackBase_ctor = (ItemStackBase_ctor_t)FIND(ItemStackBaseCtorSignature);

#undef FIND

    // A game update can invalidate any of the byte signatures above. None of
    // these are safe to call as null, so refuse to hook and leave the game
    // untouched instead of crashing later.
    if (!Nbt_treeFind || !ItemStackBase_loadItem || !ItemStackBase_getDamageValue ||
        !BaseActorRenderContext_ctor || !ItemRenderer_renderGuiItemNew || !ItemStackBase_ctor) {
        printf("[SP] one or more function signatures did not match; preview disabled\n");
        return;
    }

    //vtable hooks
    auto ZTS_res = hat::find_pattern(range1, hat::object_to_signature("19ShulkerBoxBlockItem"));
    if (!ZTS_res.has_result()) {
        printf("[SP] ShulkerBoxBlockItem typeinfo not found; preview disabled\n");
        return;
    }
    auto ZTS = ZTS_res.get();
    auto ZTI_res = hat::find_pattern(range2, hat::object_to_signature(ZTS));
    if (!ZTI_res.has_result()) {
        printf("[SP] ShulkerBoxBlockItem typeinfo reference not found; preview disabled\n");
        return;
    }
    auto ZTI = ZTI_res.get() - sizeof(void*);
    auto ZTV_res = hat::find_pattern(range2, hat::object_to_signature(ZTI));
    if (!ZTV_res.has_result()) {
        printf("[SP] ShulkerBoxBlockItem vtable not found; preview disabled\n");
        return;
    }
    auto ZTV = ZTV_res.get() + sizeof(void*);
    void** vt = (void**)ZTV;

    if (vt[55] && makeWritable(&vt[55])) {
        ShulkerBoxBlockItem_appendFormattedHovertext_orig = (Shulker_appendHover_t)vt[55];
        vt[55] = (void*)&ShulkerBoxBlockItem_appendFormattedHovertext_hook;
    } else {
        printf("[SP] ShulkerBoxBlockItem hovertext slot unavailable; hover text hook skipped\n");
    }

    auto ZTS2_res = hat::find_pattern(range1, hat::object_to_signature("17HoverTextRenderer"));
    if (!ZTS2_res.has_result()) {
        printf("[SP] HoverTextRenderer typeinfo not found; hover box hook skipped\n");
    } else {
        auto ZTS2 = ZTS2_res.get();
        auto ZTI2_res = hat::find_pattern(range2, hat::object_to_signature(ZTS2));
        if (!ZTI2_res.has_result()) {
            printf("[SP] HoverTextRenderer typeinfo reference not found; hover box hook skipped\n");
        } else {
            auto ZTI2 = ZTI2_res.get() - sizeof(void*);
            auto ZTV2_res = hat::find_pattern(range2, hat::object_to_signature(ZTI2));
            if (!ZTV2_res.has_result()) {
                printf("[SP] HoverTextRenderer vtable not found; hover box hook skipped\n");
            } else {
                void** vt2 = (void**)(ZTV2_res.get() + sizeof(void*));
                if (vt2[17] && makeWritable(&vt2[17])) {
                    HoverRenderer_renderHoverBox_orig = (RenderHoverBoxFn)vt2[17];
                    vt2[17] = (void*)&HoverRenderer_renderHoverBox_hook;
                } else {
                    printf("[SP] HoverTextRenderer render slot unavailable; hover box hook skipped\n");
                }
            }
        }
    }

    SP_initModMenu();
    SP_register_keybinds();
}