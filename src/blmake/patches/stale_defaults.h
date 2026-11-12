#ifndef BLMAKE_PATCHES_STALE_DEFAULTS_H
#define BLMAKE_PATCHES_STALE_DEFAULTS_H

#include "blcore/ue3/mapping/fwd.h"

namespace blmake {

bool install_stale_defaults_purge();

void purge_stale_defaults(const blcore::ue3::UClass* cls);

}  // namespace blmake

#endif  // BLMAKE_PATCHES_STALE_DEFAULTS_H
