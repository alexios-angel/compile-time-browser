#include <ctcompile/Support/Version.hpp>

#include <ctbrowser/script/bytecode.hpp>

namespace ctcompile {

std::string version_string() {
    return CTCOMPILE_VERSION;
}

std::string engine_summary() {
    // `halt` is the last enumerator of ctbrowser::script::op, so its value plus
    // one is how many operations the engine defines. A fragile way to count an
    // enum, and deliberately the SIMPLEST thing that is true today: Phase 0
    // replaces it with the generated X-macro table
    // (script/bytecode_opcodes.def) and a static_assert that the table and the
    // VM's decoder agree. Until that table exists, counting here would be
    // inventing a second source of truth for it.
    const int opcodes = static_cast<int>(ctbrowser::script::op::halt) + 1;
    return "ctbrowser " CTCOMPILE_CTBROWSER_VERSION ", " + std::to_string(opcodes) +
           " bytecode operations";
}

} // namespace ctcompile
