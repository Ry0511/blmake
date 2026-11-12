#include "blcore/engine/engine.h"
#include "blcore/ue3/tarray.h"
#include "blcore/ue3/uobject/uclass.h"
#include "blcore/ue3/uobject/uobject.h"
#include "blcore/ue3/uobject/ustruct.h"
#include "blcore/ue3/uprop/uarrayproperty.h"
#include "blcore/ue3/uprop/uproperty.h"
#include "blcore/ue3/uprop/ustructproperty.h"
#include "blcore/util/logging.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "blmake/flags.h"
#include "blmake/patches/stale_defaults.h"

namespace blmake {

namespace engine = blcore::engine;
namespace ue3 = blcore::ue3;

// NOLINTBEGIN(*-pro-type-static-cast-downcast)

namespace {

bool installed = false;

ue3::UClass* cls_component = nullptr;
ue3::UClass* cls_object_prop = nullptr;
ue3::UClass* cls_array_prop = nullptr;
ue3::UClass* cls_struct_prop = nullptr;

std::vector<const ue3::UObject*> purged_packages;
uint32_t stale_count = 0;

int clear_refs_into(std::byte* data, const ue3::UStruct* scope, const ue3::UObject* target, bool own_only);

bool find_core_classes() {
    if (cls_component != nullptr) {
        return true;
    }

    cls_object_prop = engine::find_class(L"Core.ObjectProperty");
    cls_array_prop = engine::find_class(L"Core.ArrayProperty");
    cls_struct_prop = engine::find_class(L"Core.StructProperty");
    cls_component = engine::find_class(L"Core.Component");

    if (
        cls_object_prop == nullptr
        || cls_array_prop == nullptr
        || cls_struct_prop == nullptr
        || cls_component == nullptr
    ) {
        BLCORE_ERROR("couldn't find the Core classes, stale subobjects are left in place");
        return false;
    }

    return true;
}

bool is_recompiled_default(const ue3::UObject* obj, const ue3::UObject* package) {
    return (obj->ObjectFlags() & RF_ClassDefaultObject) != 0
           && obj->Outer() == package
           && obj->Class()->ScriptText() != nullptr;
}

int clear_value(std::byte* value, const ue3::UProperty* prop, const ue3::UObject* target) {
    // i shit you not there was thousands of these entries like this in a codebase from a prior
    // workplace funniest shit i ever seen.
    constexpr auto cnst_one{1};
    constexpr auto cnst_zero{0};

    if (engine::is_instance(prop, cls_object_prop)) {
        auto* ref = reinterpret_cast<ue3::UObject**>(value);
        if (*ref != nullptr && engine::is_parent(*ref, target)) {
            *ref = nullptr;
            return cnst_one;
        }
        return cnst_zero;
    }

    if (engine::is_instance(prop, cls_struct_prop)) {
        return clear_refs_into(
            value,
            static_cast<const ue3::UStructProperty*>(prop)->Struct(),
            target,
            false
        );
    }

    if (engine::is_instance(prop, cls_array_prop)) {
        const auto* array = reinterpret_cast<ue3::TArray<std::byte>*>(value);
        const ue3::UProperty* inner = static_cast<const ue3::UArrayProperty*>(prop)->Inner();
        int cleared = cnst_zero;
        for (int32_t i = 0; i < array->ArrayNum; ++i) {
            cleared += clear_value(
                array->Data + (static_cast<std::ptrdiff_t>(i) * inner->ElementSize()),
                inner,
                target
            );
        }
        return cleared;
    }

    return cnst_zero;
}

int clear_refs_into(std::byte* data, const ue3::UStruct* scope, const ue3::UObject* target, bool own_only) {
    int cleared = 0;
    for (
        const ue3::UProperty* prop = scope->PropertyLink();
        prop != nullptr;
        prop = prop->PropertyLinkNext()) {
        if (own_only && prop->Outer() != scope) {
            continue;
        }

        for (int32_t i = 0; i < prop->ArrayDim(); ++i) {
            cleared += clear_value(
                data + prop->Offset() + (static_cast<std::ptrdiff_t>(i) * prop->ElementSize()),
                prop,
                target
            );
        }
    }
    return cleared;
}

void purge_package(const ue3::UObject* package) {
    // Cooked packages get loaded before the make commandlet which means that the default objects
    // i.e., defaultproperties blocks are strongly referenced. That means that even if we recompile
    // the package the default properties block will still use the old/cooked version. We grab all
    // those objects before starting compilation, hide and rename them which means our newly
    // compiled defaultproperties will be found by the compiler.

    if (!find_core_classes()) {
        return;
    }

    std::vector<ue3::UObject*> defaults;
    std::vector<ue3::UObject*> stale;
    std::vector<ue3::UObject*> roots;

    for (ue3::UObject* obj : engine::gobjects()) {
        if (obj == nullptr) {
            continue;
        }

        if (is_recompiled_default(obj, package)) {
            defaults.push_back(obj);
            continue;
        }

        bool plain_above = false;
        bool under_default = false;
        for (const ue3::UObject* outer = obj->Outer(); outer != nullptr; outer = outer->Outer()) {
            if (is_recompiled_default(outer, package)) {
                under_default = true;
                break;
            }
            plain_above = plain_above || !engine::is_instance(outer, cls_component);
        }

        if (!under_default) {
            continue;
        }

        const bool plain = !engine::is_instance(obj, cls_component);
        if (plain || plain_above) {
            stale.push_back(obj);
        }

        if (plain && !plain_above) {
            roots.push_back(obj);
        }
    }

    // SavePackage will skip transient objects which is exactly what we want for these
    for (ue3::UObject* obj : stale) {
        obj->ObjectFlags() |= RF_Transient;
    }

    // hide and rename the root objects so FindObject will not resolve to them
    for (ue3::UObject* obj : roots) {
        obj->ObjectFlags() &= ~RF_Public;
        const std::wstring name = L"BlmakeStale_" + std::to_wstring(stale_count++);
        obj->Rename(name.c_str(), nullptr, REN_ForceNoResetLoaders);
    }

    // null out all the references to objects we have just hidden and renamed
    int cleared = 0;
    for (ue3::UObject* obj : defaults) {
        cleared += clear_refs_into(
            reinterpret_cast<std::byte*>(obj),
            obj->Class(),
            obj,
            true
        );
    }

    BLCORE_INFO(
        "package at {}: renamed {} stale subobject(s), {} flagged transient, {} stale reference(s) cleared",
        static_cast<const void*>(package),
        roots.size(),
        stale.size(),
        cleared
    );
}

}  // namespace

bool install_stale_defaults_purge() {
    installed = engine::gobjects().data() != nullptr;
    return installed;
}

void purge_stale_defaults(const ue3::UClass* cls) {
    const ue3::UObject* package = cls->Outer();
    if (
        !installed
        || std::ranges::find(purged_packages, package) != purged_packages.end()
    ) {
        return;
    }

    purged_packages.push_back(package);
    purge_package(package);
}

// NOLINTEND(*-pro-type-static-cast-downcast)

}  // namespace blmake
