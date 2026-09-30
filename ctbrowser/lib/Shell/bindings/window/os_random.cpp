// The OS CSPRNG behind os_random.hpp - the only file that names the platform
// call.

#include "os_random.hpp"

#if defined(_WIN32)
#include <windows.h>
// windows.h first: bcrypt.h uses its types.
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#else
#include <cerrno>
#include <sys/random.h>
#endif

namespace ctbrowser::shell {

bool os_random(std::span<std::uint8_t> out) noexcept {
#if defined(_WIN32)
    // Callers ask for at most 65536 bytes (getRandomValues' quota), well
    // inside a ULONG.
    return BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#elif defined(__APPLE__)
    arc4random_buf(out.data(), out.size());
    return true;
#else
    // getrandom(2) with no flags blocks only until the pool is first
    // initialised, and may return short for a request over 256 bytes.
    std::size_t done = 0;
    while (done < out.size()) {
        const ssize_t got = getrandom(out.data() + done, out.size() - done, 0);
        if (got < 0) {
            if (errno == EINTR) { continue; }
            return false;
        }
        done += static_cast<std::size_t>(got);
    }
    return true;
#endif
}

} // namespace ctbrowser::shell
