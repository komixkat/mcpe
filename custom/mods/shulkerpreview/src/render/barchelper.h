#pragma once

#include <cstddef>

#include "helper.h"
#include "item/item.h"
#include "item/itemstackbase.h"

inline void* getMinecraftGameFromClient(void* clientInstance) {
    if (!clientInstance)
        return nullptr;

    auto** vtable = *reinterpret_cast<void***>(clientInstance);
    if (vtable && vtable[ClientGetMinecraftGameVfIndex]) {
        auto fn = reinterpret_cast<void*(*)(void*)>(vtable[ClientGetMinecraftGameVfIndex]);
        return fn(clientInstance);
    }

    return *reinterpret_cast<void**>(
        reinterpret_cast<char*>(clientInstance) + ClientMinecraftGameOffset);
}

inline void destroyBaseActorRenderContextInstance(void* barc) {
    if (!barc)
        return;

    auto** vtable = *reinterpret_cast<void***>(barc);
    if (!vtable || !vtable[0])
        return;

    auto dtor = reinterpret_cast<void(*)(void*)>(vtable[0]);
    dtor(barc);
}

inline void* getItemRendererFromBarc(void* barc) {
    if (!barc)
        return nullptr;

    return *reinterpret_cast<void**>(
        reinterpret_cast<std::byte*>(barc) + BarcItemRendererOffset);
}

inline void* getClientLocalPlayer(void* clientInstance) {
    if (!clientInstance)
        return nullptr;

    auto** vtable = *reinterpret_cast<void***>(clientInstance);
    if (!vtable || !vtable[ClientGetLocalPlayerVfIndex])
        return nullptr;

    auto fn = reinterpret_cast<void*(*)(void*)>(vtable[ClientGetLocalPlayerVfIndex]);
    return fn(clientInstance);
}

inline unsigned int getItemAuxIconValue(Item* item, void* localPlayer, ItemStackBase* stack) {
    if (!item || !localPlayer || !stack)
        return 0;

    auto** vtable = *reinterpret_cast<void***>(item);
    if (!vtable || !vtable[ItemAuxIconValueVfIndex])
        return 0;

    using AuxIconValueFn = unsigned int (*)(Item*, void*, int, ItemStackBase*, int);
    return reinterpret_cast<AuxIconValueFn>(vtable[ItemAuxIconValueVfIndex])(item, localPlayer, 0, stack, 1);
}
