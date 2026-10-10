//===- ctcompile/lib/Support/Version.cpp - Version strings ------*- C++ -*-===//
//
// The compiler's and engine's version strings. CTCOMPILE_VERSION and
// CTCOMPILE_CTBROWSER_VERSION are injected by lib/Support/CMakeLists.txt.
//
//===----------------------------------------------------------------------===//

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
