//===- NativeRealm.h - Globals a native program's realm provides -*- C++ -*-===//
//
// The global names the realm of a native program provides: the JavaScript
// standard library the script VM installs, and the browser host's window
// names. --ctjs-resolve-globals uses it to decide that a name is absent.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctjs {

/// Whether the realm a native program runs in provides a global named `name`.
///
/// A whole-program compile treats each script as a closed program: no other
/// script runs in its realm, so the globals it can see are the ones it stores
/// and the ones the realm provides. A name it never stores and this function
/// rejects is absent, so `typeof name` is "undefined" and a plain read throws a
/// ReferenceError. The VM reference the checker compares against
/// (ctcompile-test-native-reference) agrees for the names bundles probe, such as
/// `define`, `exports` and `module`.
///
/// The answer errs toward "provided". A name wrongly provided costs only a
/// fold that does not happen; a name wrongly absent would fold a feature test
/// the host passes. So the set is the script VM's own standard library, read
/// from a fresh realm, plus every window name the ctbrowser host installs and
/// the common web platform names, plus every `on*` event handler attribute.
[[nodiscard]] bool nativeRealmProvides(llvm::StringRef name);

} // namespace ctcompile::ctjs
