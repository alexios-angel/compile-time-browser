#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ctbrowser/core/core.hpp>

#include <ctbrowser/script/value.hpp>
#include <ctbrowser/script/vm.hpp>

// The JavaScript standard library.
//
// The VM started with NONE of this. The whole property surface was `.length`, numeric
// indexing and named lookup on plain objects - no `arr.push`, no `str.split`,
// no `Math.floor`, no `JSON.parse`. A VM can be complete and still useless if
// nothing can be done with a value once you have one, and that is what this
// closes.
//
// It is a SUBSET, chosen by what pages actually call rather than by what the
// spec lists. The omissions that matter are named at the bottom of this file
// rather than left to be discovered.

namespace ctbrowser::script {

// What a regex LITERAL compiles to. A reserved name rather than `RegExp` so a
// page that shadows the constructor cannot change what its own literals mean.
inline constexpr std::string_view regexp_factory_name = "__ctbrowser_regexp";

// Install the standard library into a context.
//
// DECLARED here and DEFINED in builtins.cpp, which is the whole point: the
// definition is ~1000 lines of lambdas, and while it lived in this interface
// every translation unit that imported the module re-instantiated and
// re-optimised all of it. It was the single largest symbol in a test object
// file - 21 KB of the 1.6 MB - and there are twenty-six of those.
//
// `seed` makes Math.random deterministic by default: the test story is
// byte-comparable goldens, and a page drawing with random cannot have one
// otherwise.
void install_builtins(context & cx, std::uint64_t seed = 0x2545F4914F6CDD1DULL);

} // namespace ctbrowser::script
