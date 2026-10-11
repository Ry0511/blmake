#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "blmake/logging.h"
#include "blmake/patches/stale_defaults.h"
#include "blmake/structs.h"
#include "blmake/util.h"

namespace blmake {

// NOLINTBEGIN(*-pro-type-static-cast-downcast)

namespace {

TArray<UObject*>* gobjects = nullptr;
RenameFn fn_rename = nullptr;

UClass* cls_component = nullptr;
UClass* cls_object_prop = nullptr;
UClass* cls_array_prop = nullptr;
UClass* cls_struct_prop = nullptr;

std::vector<const UObject*> purged_packages;
uint32_t stale_count = 0;

int clear_refs_into(std::byte* data, const UStruct* scope, const UObject* target, bool own_only);

bool find_core_classes() {
    if (cls_component != nullptr) {
        return true;
    }

    cls_object_prop = reinterpret_cast<UClass*>(find_object(L"Core.ObjectProperty"));
    cls_array_prop = reinterpret_cast<UClass*>(find_object(L"Core.ArrayProperty"));
    cls_struct_prop = reinterpret_cast<UClass*>(find_object(L"Core.StructProperty"));
    cls_component = reinterpret_cast<UClass*>(find_object(L"Core.Component"));

    if (
        cls_object_prop == nullptr
        || cls_array_prop == nullptr
        || cls_struct_prop == nullptr
        || cls_component == nullptr
    ) {
        BLMAKE_LOG("couldn't find the Core classes, stale subobjects are left in place");
        return false;
    }

    return true;
}

bool is_recompiled_default(const UObject* obj, const UObject* package) {
    return (obj->ObjectFlags & RF_ClassDefaultObject) != 0
           && obj->Outer == package
           && obj->Class->ScriptText != nullptr;
}

int clear_value(std::byte* value, const UProperty* prop, const UObject* target) {
    // i shit you not there was thousands of these entries like this in a codebase from a prior
    // workplace funniest shit i ever seen.
    constexpr auto cnst_one{1};
    constexpr auto cnst_zero{0};

    if (is_a(prop, cls_object_prop)) {
        auto* ref = reinterpret_cast<UObject**>(value);
        if (*ref != nullptr && is_inside(*ref, target)) {
            *ref = nullptr;
            return cnst_one;
        }
        return cnst_zero;
    }

    if (is_a(prop, cls_struct_prop)) {
        return clear_refs_into(
            value,
            static_cast<const UStructProperty*>(prop)->Struct,
            target,
            false
        );
    }

    if (is_a(prop, cls_array_prop)) {
        const auto* array = reinterpret_cast<TArray<std::byte>*>(value);
        const UProperty* inner = static_cast<const UArrayProperty*>(prop)->Inner;
        int cleared = cnst_zero;
        for (int32_t i = 0; i < array->count; ++i) {
            cleared += clear_value(
                array->data + (static_cast<std::ptrdiff_t>(i) * inner->ElementSize),
                inner,
                target
            );
        }
        return cleared;
    }

    return cnst_zero;
}

int clear_refs_into(std::byte* data, const UStruct* scope, const UObject* target, bool own_only) {
    int cleared = 0;
    for (
        const UProperty* prop = scope->PropertyLink;
        prop != nullptr;
        prop = prop->PropertyLinkNext) {
        if (own_only && prop->Outer != scope) {
            continue;
        }

        for (int32_t i = 0; i < prop->ArrayDim; ++i) {
            cleared += clear_value(
                data + prop->Offset + (static_cast<std::ptrdiff_t>(i) * prop->ElementSize),
                prop,
                target
            );
        }
    }
    return cleared;
}

void purge_package(const UObject* package) {
    // Cooked packages get loaded before the make commandlet which means that the default objects
    // i.e., defaultproperties blocks are strongly referenced. That means that even if we recompile
    // the package the default properties block will still use the old/cooked version. We grab all
    // those objects before starting compilation, hide and rename them which means our newly
    // compiled defaultproperties will be found by the compiler.

    if (!find_core_classes()) {
        return;
    }

    std::vector<UObject*> defaults;
    std::vector<UObject*> stale;
    std::vector<UObject*> roots;

    for (int32_t i = 0; i < gobjects->count; ++i) {
        UObject* obj = gobjects->data[i];

        if (obj == nullptr) {
            continue;
        }

        if (is_recompiled_default(obj, package)) {
            defaults.push_back(obj);
            continue;
        }

        bool plain_above = false;
        bool under_default = false;
        for (const UObject* outer = obj->Outer; outer != nullptr; outer = outer->Outer) {
            if (is_recompiled_default(outer, package)) {
                under_default = true;
                break;
            }
            plain_above = plain_above || !is_a(outer, cls_component);
        }

        if (!under_default) {
            continue;
        }

        const bool plain = !is_a(obj, cls_component);
        if (plain || plain_above) {
            stale.push_back(obj);
        }

        if (plain && !plain_above) {
            roots.push_back(obj);
        }
    }

    // SavePackage will skip transient objects which is exactly what we want for these
    for (UObject* obj : stale) {
        obj->ObjectFlags |= RF_Transient;
    }

    // hide and rename the root objects so FindObject will not resolve to them
    for (UObject* obj : roots) {
        obj->ObjectFlags &= ~RF_Public;
        const std::wstring name = L"BlmakeStale_" + std::to_wstring(stale_count++);
        fn_rename(obj, name.c_str(), nullptr, REN_ForceNoResetLoaders);
    }

    // null out all the references to objects we have just hidden and renamed
    int cleared = 0;
    for (UObject* obj : defaults) {
        cleared += clear_refs_into(
            reinterpret_cast<std::byte*>(obj),
            obj->Class,
            obj,
            true
        );
    }

    BLMAKE_LOG(
        "package at {}: renamed {} stale subobject(s), {} flagged transient, {} stale reference(s) cleared",
        static_cast<const void*>(package),
        roots.size(),
        stale.size(),
        cleared
    );
}

}  // namespace

bool install_stale_defaults_purge() {
    gobjects = find_gobjects();

    const auto rename = find_rename();
    if (gobjects == nullptr || !rename.has_result()) {
        return false;
    }
    fn_rename = reinterpret_cast<RenameFn>(rename.get());

    BLMAKE_LOG("found GObjects at {}", static_cast<void*>(gobjects));
    BLMAKE_LOG("found Rename at {}", static_cast<void*>(rename.get()));
    return true;
}

void purge_stale_defaults(const UClass* cls) {
    const UObject* package = cls->Outer;
    if (
        fn_rename == nullptr
        || std::ranges::find(purged_packages, package) != purged_packages.end()
    ) {
        return;
    }

    purged_packages.push_back(package);
    purge_package(package);
}

// NOLINTEND(*-pro-type-static-cast-downcast)

}  // namespace blmake
