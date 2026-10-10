// The module the differential (Differential.cpp) compiles; its top level is
// the body under test.
//
// op::load_import, op::bind_export and op::load_namespace are emitted by
// compile_program's `if (module_scope_)` arm and by nowhere else, so all three
// live in functions[0] of a module program. That is why this file is compiled
// with script_kind::module and why the entry the driver installs is
// `_script_` - the name the compiler gives functions[0].
//
// Every specifier is different from every export name. op::load_import reads b
// as the export name and c as the specifier, which is the reverse of the
// reading order, and an importer that fills the operation in reading order
// compiles. With `./dep.js` against `count` the swap raises "module `count`
// was not loaded"; with a fixture whose specifier and export name happened to
// match it would pass.

// Four bindings from one specifier: a default, a mutable one, a constant, and a
// function. `def` and `count` differ in value and in exported name, so reading
// the wrong name index moves the answer rather than leaving it alone.
import def, {
    count,
    tag,
    bump
} from "./dep.js";

// Two namespaces of the same module, which is the only way to see the identity
// cache. `ns === ns2` is required to be true, and a lowering that built a
// namespace per op::load_namespace answers false while every property read off
// either one still works.
import * as ns from "./dep.js";
import * as ns2 from "./dep.js";

// And this module exports, so op::bind_export runs in the compiled body.
// `mine` is not the first name in this module's pool, which is what makes the
// bx() decode observable: b is the pair's high half and is 0 here, so an
// importer reading b alone publishes name 0 under this name.
export let mine = 10;
export function raise2() {
    mine = mine + 2;
}

// A dynamic import, the only module opcode an ordinary function can contain.
// Two calls: one the loader resolves and one it does not. A module that was not
// found answers an already-rejected settled promise and is not a failure, so a
// lowering that treated it as a control-flow event would return early and the
// second half of the answer would never appear.
function loadTwo() {
    // The loader records what it was asked into a global the harness installs
    // it around. That is the only way to see the referrer, which the helper
    // reads off the frame rather than taking as a parameter - a lowering that
    // dropped it would still return two promises and still answer "object".
    LOADER_SAW = "";
    var found = import("./dep.js");
    var missing = import("./nowhere.js");
    // Two distinct promises. A lowering that reused one out-slot would alias
    // them, and `typeof` alone could not tell.
    return LOADER_SAW + "/" + typeof found + "/" + typeof missing + "/" + (found === missing);
}

// Undeclared assignments, which are global writes even here. A module's top
// level is a scope of its own, so `function drive()` would be a local and the
// driver could not reach it; assigning to an undeclared name is an explicit
// write to the global object and is how a module talks to its host.
OUT = "<the module did not run>";

DRIVE = function (which) {
    // A sentinel, so an arm that throws or never matches is visible. Without it
    // OUT keeps the previous arm's answer, and the interpreter and the compiled
    // code read the same stale value and agree - a broken case reporting success.
    OUT = "<the arm did not run>";
    // A live binding, read through the named import. bump() runs in the
    // exporter and writes its own local; a `count` that was copied rather than
    // boxed answers 1.
    if (which === 0) {
        bump();
        OUT = "" + count + "/" + tag + "/" + def;
    }
    // The same write seen through the namespace, which is a different mechanism
    // - every namespace property is a native getter over the cell, not a copied
    // value - plus the identity the cache exists for.
    if (which === 1) {
        bump();
        OUT = "" + ns.count + "/" + ns.tag + "/" + ns.default+"/" + (ns === ns2);
    }
    // Both mechanisms against one write, so a lowering that made the namespace
    // out of a different record than the named import - the resolved-specifier
    // lookup skipped on one path - answers two different numbers.
    if (which === 2) {
        bump();
        bump();
        OUT = "" + count + "/" + ns.count + "/" + (count === ns.count);
    }
    if (which === 3) {
        OUT = loadTwo();
    }
    // This module's own export, written through the adopted cell. Reading it
    // here only proves the local works; user.js (embedded as module-user.js) is
    // what reads it from the other side of the record, which is the half op::bind_export decides.
    if (which === 4) {
        raise2();
        OUT = "" + mine;
    }
};
