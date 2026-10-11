//===- Instances.h - Class instances owned by a registry Map ----*- C++ -*-===//
//
// The proof that lets native code own a JavaScript class instance through a
// `std::shared_ptr<C>`: the class's constructor stores `this` into one Map
// (`registry.set(key, this)`), the Map holds nothing but instances of that
// class, every instance read back from it is checked for null or undefined
// before use, and no instance can reach the Map or another instance through
// its fields.
//
// With that proof the shared ownership is plain RAII. The Map holds one owner
// per entry, a local read back from it holds another, and the last owner to
// go destroys the instance at a point C++ fixes, where JavaScript would make
// it unreachable. No instance owns its way back to itself, so nothing needs a
// collector or a cycle collector. Bootstrap's BaseComponent is the program
// this is for: its constructor registers `this`, `getInstance` reads it back
// with `|| null`, and `dispose` removes the entry and goes on using `this`,
// which unique ownership could not express.
//
// The proof runs on the importer's IR after --ctjs-lift-to-scf and before
// the constructor lift, because that lift needs its answer to let `this`
// reach the Map. It runs after the closure lifter's binding steps, so a call
// through a run-once table of methods is already a direct call. The Map must
// live in a function-local binding, as Bootstrap's module scope is the body
// of its wrapper function.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"

#include <string>

namespace ctcompile::ctnative {

/// One Map that holds class instances, the class, and whether the registry
/// proof holds for them.
struct InstanceRegistry {
    /// The constructor whose `this` the Map stores.
    ctjs::FuncOp constructor;
    /// The `new Map()` that holds the instances, or for a Map of Maps the
    /// outer one, keyed by element in Bootstrap.
    ctjs::ConstructOp map;
    /// For a Map of Maps, the `new Map()` of each inner Map, which holds the
    /// instances and which the outer Map owns; empty otherwise.
    llvm::SmallVector<ctjs::ConstructOp> innerMaps;
    /// Why the proof fails, or empty when it holds.
    std::string refusal;
    /// The parameters proved never null or undefined: every call passes them
    /// an instance it has already checked.
    llvm::DenseSet<mlir::Value> nonNull;
};

/// Proves or refuses every Map in `module` that stores the `this` of a
/// function: one entry per such Map.
///
/// The proof holds when:
///   * every value stored into the Map is the `this` of one constructor,
///     directly or through parameters of functions whose callers are all
///     known (Bootstrap's `Data.set(element, key, this)` once the table call
///     is direct); the constructor uses `this` only through its fields and
///     methods and to pass it on, and those parameters only pass it on;
///   * the Map is a local `new Map()` held in a binding written once and read
///     directly, through closures that capture it, or by name once the
///     binding is a static variable, and is used only through `get`, `set`,
///     `has`, `delete`, `size` and `keys` (`values`, `entries` and `forEach`
///     would hand instances to code the proof does not follow);
///   * it is not iterated while `delete` can remove entries;
///   * every instance read back from it flows only through branches, returns
///     and calls whose callers are all known, is joined only with null or
///     undefined, and is dereferenced only under a check (`if (x)`,
///     `x !== null` once `|| null` has replaced undefined, `x != null`), or
///     in a parameter every caller passes a checked instance;
///   * the class's methods use `this` only through its fields and methods, a
///     method read from an instance is called on it at once, and the class is
///     read through `constructor` only when none of its functions captures
///     the Map;
///   * every function that captures the Map's binding is only called,
///     constructed or kept as a class method.
///
/// Together these make ownership acyclic without asking what a field holds:
/// no value that can hold an instance is stored into any object, and neither
/// the Map nor a function that captures it is held as data, so no field of an
/// instance can reach the Map or an instance. Elements, configuration objects
/// and strings in fields are all fine.
llvm::SmallVector<InstanceRegistry> proveInstanceRegistries(mlir::ModuleOp module);

} // namespace ctcompile::ctnative
