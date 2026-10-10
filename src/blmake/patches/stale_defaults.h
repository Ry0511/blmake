#ifndef BLMAKE_PATCHES_STALE_DEFAULTS_H
#define BLMAKE_PATCHES_STALE_DEFAULTS_H

namespace blmake {

struct UClass;

bool install_stale_defaults_purge();

void purge_stale_defaults(const UClass* cls);

}  // namespace blmake

#endif  // BLMAKE_PATCHES_STALE_DEFAULTS_H
