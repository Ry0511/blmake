#include "blcore/blcore.h"
#include "blcore/plugin/plugin.h"
#include "blcore/plugin/plugin_api_v1.h"
#include "blcore/util/process_args.h"

#include <string_view>

#include "blmake/patches/patches.h"

namespace {

constexpr std::string_view NAME = "blmake";

void BLCORE_CALL get_name(const char** out_name, size_t* out_size) {
    *out_name = NAME.data();
    *out_size = NAME.size();
}

void BLCORE_CALL get_version(blcore::PluginVersion_v1* out) {
    *out = {
        .major = BLMAKE_VERSION_MAJOR,
        .minor = BLMAKE_VERSION_MINOR,
        .patch = BLMAKE_VERSION_PATCH,
    };
}

blcore::ApiResult BLCORE_CALL on_load() {
    const blcore::util::ProcessArgs args{};
    if (!args.is_make_commandlet()) {
        return blcore::API_UNAVAILABLE;
    }

    return blmake::install_patches(args.contains("-nocompress"))
               ? blcore::API_OK
               : blcore::API_FAIL;
}

void BLCORE_CALL on_shutdown() {}

}  // namespace

BLCORE_PLUGIN_ENTRY(handle) {
    const blcore::PluginApi_v1 api{
        .get_name = &get_name,
        .get_version = &get_version,
        .on_load = &on_load,
        .on_shutdown = &on_shutdown,
    };
    return blcore::register_api(handle, api);
}
