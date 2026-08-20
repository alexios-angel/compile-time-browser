#include <ctbrowser/core/allocator.hpp>

#include <cstddef>
#include <new>

#if CTBROWSER_WITH_MIMALLOC
#include <mimalloc.h>
#endif

// THE GLOBAL ALLOCATOR, ROUTED THROUGH mimalloc.
//
// Allocator traffic is ~4.8% of a Phaser frame - `_int_malloc`, `_int_free`,
// `malloc`, `free`, `malloc_consolidate` and `operator new` between them - and
// it is the one hot item in this engine where a LIBRARY is the whole answer. A
// drop-in allocator is a link-line change that cannot alter results, only what
// they cost. docs/performance.md has the measurement, including jemalloc, which
// tied on wall clock and lost on the Windows cross-build.
//
// OPERATOR NEW, NOT MI_OVERRIDE. mimalloc can patch the platform's `malloc`
// instead, but on Windows that means redirecting the CRT at load time, which is
// a far more invasive thing than linking an allocator and depends on load
// order. Overriding the C++ operators is explicit, identical on both platforms,
// and covers what this engine actually allocates - std::string, std::vector,
// the script heap - since the C libraries it links (zlib, libpng,
// libjpeg-turbo) allocate a handful of buffers rather than a stream of them.
//
// ALL EIGHT FORMS, and that is not pedantry. Replacing `operator new(size_t)`
// and leaving the sized or aligned `delete` to libstdc++ hands a mimalloc
// pointer to the system `free`, which is undefined behaviour that usually
// survives long enough to become a crash somewhere else entirely.
//
// WHETHER THIS FILE IS EVEN LINKED IS A REAL QUESTION - see
// core/allocator.hpp. A static archive member is pulled in only to satisfy an
// undefined symbol, so an override that nothing references can be silently
// dropped and the build will simply use the system allocator while looking
// exactly the same. `ctbrowser::allocator_name()` exists to be asked, and
// unittests/unit/core_basics asserts on it.

#if CTBROWSER_WITH_MIMALLOC

void * operator new(std::size_t size) {
    if (void * p = mi_malloc(size)) { return p; }
    throw std::bad_alloc{};
}
void * operator new[](std::size_t size) {
    if (void * p = mi_malloc(size)) { return p; }
    throw std::bad_alloc{};
}
void * operator new(std::size_t size, const std::nothrow_t &) noexcept {
    return mi_malloc(size);
}
void * operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    return mi_malloc(size);
}
void * operator new(std::size_t size, std::align_val_t align) {
    if (void * p = mi_malloc_aligned(size, static_cast<std::size_t>(align))) { return p; }
    throw std::bad_alloc{};
}
void * operator new[](std::size_t size, std::align_val_t align) {
    if (void * p = mi_malloc_aligned(size, static_cast<std::size_t>(align))) { return p; }
    throw std::bad_alloc{};
}
void * operator new(std::size_t size, std::align_val_t align, const std::nothrow_t &) noexcept {
    return mi_malloc_aligned(size, static_cast<std::size_t>(align));
}
void * operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t &) noexcept {
    return mi_malloc_aligned(size, static_cast<std::size_t>(align));
}

void operator delete(void * p) noexcept {
    mi_free(p);
}
void operator delete[](void * p) noexcept {
    mi_free(p);
}
void operator delete(void * p, const std::nothrow_t &) noexcept {
    mi_free(p);
}
void operator delete[](void * p, const std::nothrow_t &) noexcept {
    mi_free(p);
}
void operator delete(void * p, std::size_t size) noexcept {
    mi_free_size(p, size);
}
void operator delete[](void * p, std::size_t) noexcept {
    mi_free(p);
}
void operator delete(void * p, std::align_val_t align) noexcept {
    mi_free_aligned(p, static_cast<std::size_t>(align));
}
void operator delete[](void * p, std::align_val_t align) noexcept {
    mi_free_aligned(p, static_cast<std::size_t>(align));
}
void operator delete(void * p, std::size_t size, std::align_val_t align) noexcept {
    mi_free_size_aligned(p, size, static_cast<std::size_t>(align));
}
void operator delete[](void * p, std::size_t, std::align_val_t align) noexcept {
    mi_free_aligned(p, static_cast<std::size_t>(align));
}
void operator delete(void * p, std::align_val_t align, const std::nothrow_t &) noexcept {
    mi_free_aligned(p, static_cast<std::size_t>(align));
}
void operator delete[](void * p, std::align_val_t align, const std::nothrow_t &) noexcept {
    mi_free_aligned(p, static_cast<std::size_t>(align));
}

#endif // CTBROWSER_WITH_MIMALLOC

namespace ctbrowser {

#if CTBROWSER_WITH_MIMALLOC
const char * allocator_name() noexcept {
    // ASKED, NOT ASSUMED. `mi_is_in_heap_region` answers whether a pointer came
    // out of mimalloc's own regions, so this is the override reporting on
    // itself rather than a build flag reporting on what it intended.
    void * probe = ::operator new(64);
    const bool ours = mi_is_in_heap_region(probe);
    ::operator delete(probe);
    return ours ? "mimalloc" : "system";
}

int allocator_version() noexcept {
    return mi_version();
}
#else
// -DCTBROWSER_USE_MIMALLOC=OFF: no overrides are compiled at all, and this says
// so rather than claiming an allocator the binary does not have.
const char * allocator_name() noexcept {
    return "system";
}

int allocator_version() noexcept {
    return 0;
}
#endif

} // namespace ctbrowser
