#include <ctcompile/Support/Version.hpp>

#include <ctbrowser/script/bytecode.hpp>

namespace ctcompile {

std::string version_string() {
    return CTCOMPILE_VERSION;
}

std::string engine_summary() {
    return "ctbrowser " CTCOMPILE_CTBROWSER_VERSION ", " +
           std::to_string(ctbrowser::script::opcode_count) + " bytecode operations";
}

} // namespace ctcompile
