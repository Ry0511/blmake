#include "blcore/engine/engine.h"
#include "blcore/util/logging.h"

#include "blmake/patches/delegate_import.h"
#include "blmake/patches/make_commandlet.h"
#include "blmake/patches/patches.h"
#include "blmake/patches/stale_defaults.h"

namespace blmake {

bool install_patches(bool no_compress) {
    if (blcore::engine::static_find_object_address() == nullptr) {
        BLCORE_ERROR("couldn't find StaticFindObject");
        return false;
    }

    if (!install_make_commandlet_patches(no_compress)) {
        BLCORE_ERROR("couldn't install the make commandlet patches");
        return false;
    }

    if (!install_stale_defaults_purge()) {
        BLCORE_ERROR("couldn't install stale defaults purge fix");
    }

    if (!install_delegate_import_fix()) {
        BLCORE_ERROR("couldn't install delegate import fix");
    }

    BLCORE_INFO("all patches applied successfully");
    return true;
}

}  // namespace blmake
