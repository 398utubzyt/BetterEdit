#include "BetterEdit.hpp"
#include <Geode/Loader.hpp>

using namespace geode::prelude;

std::unordered_map<uintptr_t, Patch*> PATCHES;

void be::patch(uintptr_t address, geode::ByteVector const& bytes, bool apply) {
    if (apply) {
        // apply existing patch
        if (PATCHES.count(address)) {
            PATCHES.at(address)->apply();
        }
        // otherwise create new patch
        else if (auto p = Mod::get()->patch(
            reinterpret_cast<void*>(base::get() + address), bytes
        )) {
            PATCHES.insert({ address, p.value() });
        }
    } else {
        if (PATCHES.count(address)) {
            PATCHES.at(address)->restore();
        }
    }
}

void be::nopOut(uintptr_t address, size_t amount, bool apply) {
    be::patch(address, ByteVector(amount, 0x90), apply);
}
