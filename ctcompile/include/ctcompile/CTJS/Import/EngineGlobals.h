//===- EngineGlobals.h - The VM's engine-internal class helpers -*- C++ -*-===//
//
// The ctbrowser compiler spells part of a class definition as calls of six
// engine helpers. The VM names them `%class_defined` and so on and keeps them
// apart from the globals table (ctbrowser/include/ctbrowser/script/vm/
// objects.hpp, internal_global), so no script can replace them. In the ctjs
// dialect they keep the names they had as ordinary globals,
// `__ctbrowser_class_defined` and so on, which the native class proofs and
// their tests are written against. The importer maps the VM name to the IR
// name; the boxed backend maps it back. A script's own use of one of those
// IR names is marked by the importer (kEngineReservedUse), because in the IR
// it would otherwise read as the helper.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctjs {

/// One helper: the VM's name and the IR's.
struct engine_helper {
    llvm::StringLiteral vm;
    llvm::StringLiteral ir;
};

inline constexpr engine_helper engineHelpers[] = {
    {"%class_defined", "__ctbrowser_class_defined"},
    {"%class_heritage", "__ctbrowser_class_heritage"},
    {"%init_fields", "__ctbrowser_init_fields"},
    {"%bind_this", "__ctbrowser_bind_this"},
    {"%super_get", "__ctbrowser_super_get"},
    {"%super_set", "__ctbrowser_super_set"},
};

/// On a ctjs.load_global or ctjs.store_global the script itself wrote under
/// one of the helpers' IR names: an ordinary global of the page, not the
/// helper. Native lowering refuses it by name.
inline constexpr llvm::StringLiteral kEngineReservedUse = "ctjs.engine_reserved";

/// The IR name of the helper the VM calls `vmName`, or empty.
inline llvm::StringRef engineHelperIRName(llvm::StringRef vmName) {
    for (const engine_helper & helper : engineHelpers) {
        if (helper.vm == vmName) { return helper.ir; }
    }
    return {};
}

/// The VM name of the helper the IR calls `irName`, or empty.
inline llvm::StringRef engineHelperVMName(llvm::StringRef irName) {
    for (const engine_helper & helper : engineHelpers) {
        if (helper.ir == irName) { return helper.vm; }
    }
    return {};
}

} // namespace ctcompile::ctjs
