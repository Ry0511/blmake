#include <libhat.hpp>

#include "blmake/patches/sha_verification.h"
#include "blmake/util.h"

namespace blmake {

#if !BLMAKE_ENHANCED
namespace {

// void appOnFailSHAVerification
bool patch_sha_verification_failure() {
    // Steam build checks script sha hashes against an internal list and raises
    // "SHA Verification failed for '%s'" if you modify any of the existing script packages.
    // Weirdly enough BL1E does not have this issue which is another weird thing about
    // the enhanced version.

    // appErrorf(TEXT("SHA Verification failed for '%s'. Reason: %s"), ...);
    constexpr auto signature = hat::compile_signature<
        " ?? 7C 24 08 00"  // cmp dword [esp + 8], 0 <- ret
        " B9 ?? ?? ?? ??"  // mov ecx, "Missing hash"
        " 75 05"           // jnz
        " B9 ?? ?? ?? ??"  // mov ecx, "Bad hash"
        " 8B 44 24 04"     // mov eax, [esp + 4]
        " 85 C0"           // test eax, eax
        " 75 05"           // jnz
        " B8 ?? ?? ?? ??"  // mov eax, "Unknown file"
        " 51"              // push ecx
        " 50"              // push eax
        " A1 ?? ?? ?? ??"  // mov eax, [GError]
        " 68 ?? ?? ?? ??"  // push "SHA Verification failed for '%s'. Reason: %s"
        " 50"              // push eax
        " E8"              // call
        >();

    constexpr auto offset = 0;
    constexpr uint8_t bytes[] = {0xC3};
    return apply_patch(
        __func__,
        hat::find_pattern(signature, ".text"),
        offset,
        bytes,
        sizeof bytes
    );
}

}  // namespace
#endif

bool install_sha_verification_bypass() {
#if BLMAKE_ENHANCED
    return true;
#else
    static bool patched = false;
    if (patched) {
        return true;
    }
    return patched = patch_sha_verification_failure();
#endif
}

}  // namespace blmake
