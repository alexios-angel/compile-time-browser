//===- CTNativeAttrNames.h - Shared ctnative attribute names ----*- C++ -*-===//
//
// The discardable `ctnative.*` attribute names one pass writes and another
// (or the C++ emitter) reads. A typo in a bare string is a silent miss (the
// reader falls back and nothing fails), so both sides must spell them from
// here.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"

namespace ctcompile::ctnative {

/// Written by closure lifting (lib/CTNative/Lowering/ClosureLifting) and read
/// by the admission check.
inline constexpr llvm::StringLiteral kNativeReceiver = "ctnative.receiver";
inline constexpr llvm::StringLiteral kNativeObjectArgs = "ctnative.object_args";
inline constexpr llvm::StringLiteral kNativeCellArgs = "ctnative.cell_args";
inline constexpr llvm::StringLiteral kNativeCaptures = "ctnative.captures";
inline constexpr llvm::StringLiteral kNativeLifted = "ctnative.lifted";
inline constexpr llvm::StringLiteral kNativeUnboxed = "ctnative.unboxed";
inline constexpr llvm::StringLiteral kNativeCarried = "ctnative.carried";
/// On an arrow whose lexical `this` became a capture (ArrowThis.cpp), the
/// capture slot that holds it; on the cell made for it, a unit marker.
inline constexpr llvm::StringLiteral kNativeLexicalThis = "ctnative.lexical_this";
/// On a carried `ctjs.create_cell`: a callback the runtime keeps (a timer
/// handler, a listener, a promise handler) captures it, so the binding lives
/// in a std::shared_ptr<ctnative::cell<T>> (Runtime/Cell.hpp) rather than a
/// frame-local variable. On a ctjs.func or a call site, the entry-block
/// indices among `ctnative.cell_args` that receive such a handle rather than
/// a pointer.
inline constexpr llvm::StringLiteral kNativeSharedCell = "ctnative.shared_cell";
inline constexpr llvm::StringLiteral kNativeSharedCellArgs = "ctnative.shared_cell_args";
inline constexpr llvm::StringLiteral kNativeClosureReason = "ctnative.closure_reason";
inline constexpr llvm::StringLiteral kNativeCellReason = "ctnative.cell_reason";
inline constexpr llvm::StringLiteral kNativeObjectReason = "ctnative.object_reason";
inline constexpr llvm::StringLiteral kNativeMethod = "ctnative.method";
inline constexpr llvm::StringLiteral kNativeMethodRefusal = "ctnative.method_refusal";
/// A carried cell of a function that runs at most once per program run, stored
/// in a namespace-scope variable whose name is the attribute's value. Read by
/// type inference (which joins every cell naming the same variable) and by the
/// lowering (which declares the variable).
inline constexpr llvm::StringLiteral kNativeStaticCell = "ctnative.static_cell";
/// On a static cell: this op is not the binding's box but a name for it in
/// another function, created where a captured upvalue used to be read.
inline constexpr llvm::StringLiteral kNativeStaticReference = "ctnative.static_reference";
/// On a ctjs.func whose boxed bindings could not become static: why its body
/// is not proved to run at most once.
inline constexpr llvm::StringLiteral kNativeNotRunOnce = "ctnative.not_run_once";
/// On the operations that set up a proved JavaScript class (its prototype,
/// constructor and `__home` edges, member definitions and the class helper
/// calls): they lower to nothing. Written by the class census
/// (ClosureLifting/Classes.cpp).
inline constexpr llvm::StringLiteral kNativeClassSetup = "ctnative.class_setup";
/// On a `new this(...)` in a static member of a proved class: the name of the
/// constructor's ctjs.func, which it constructs (a string, so that it is not
/// a symbol use). The static-member rewrite
/// (ClosureLifting/ClassStatics.cpp) proved that member's `this` is that class;
/// the constructor lift builds the instance there.
inline constexpr llvm::StringLiteral kNativeClassConstruct = "ctnative.class_construct";
/// On a ctjs.cell_get of a run-once static cell written only with a class's
/// closure, before every read, and on its ctjs.cell_set writes: the name of
/// that class constructor's ctjs.func (a string, as above). A `new` of the
/// read constructs the class there. Written by markStaticClassReads
/// (ClosureLifting/Singletons.cpp).
inline constexpr llvm::StringLiteral kNativeStaticClass = "ctnative.static_class";
/// On the owning ctjs.create_cell of such a static cell whose reads are all
/// `new`s but which markStaticClassReads did not mark because a read can run
/// before the class is written.
inline constexpr llvm::StringLiteral kNativeStaticClassReason = "ctnative.static_class_reason";
/// On a class constructor's closure: why the class census did not prove it.
inline constexpr llvm::StringLiteral kNativeClassReason = "ctnative.class_reason";
/// On the object literal that stands for an instance of a JavaScript class
/// (the constructor lift's `new K(...)`): the class's name, which the lowering
/// gives the C++ class it emits for that shape.
inline constexpr llvm::StringLiteral kNativeClassName = "ctnative.class_name";
/// On a ctjs.constant that stands for a class held as an object field (the
/// class's name): its value has the class's tag type, which carries no
/// runtime data (ClosureLifting/ClassStatics.cpp).
inline constexpr llvm::StringLiteral kNativeClassTag = "ctnative.class_tag";
/// On a read of a published object's field that holds a class, and on each
/// direct call made through such a read: the class it resolves to.
inline constexpr llvm::StringLiteral kNativeExportClass = "ctnative.export_class";
/// On the static box of a binding that holds an object literal: why calls of
/// its methods stay calls through the literal (ClosureLifting/Singletons.cpp).
inline constexpr llvm::StringLiteral kNativeSingletonReason = "ctnative.singleton_reason";
/// A callback of an Array method (forEach, map, filter, some, every, find or
/// reduce) that closure lifting specialised for its one call. On the
/// `ctjs.call` it is a symbol reference to the callback, whose captures are
/// the call's leading arguments; on the callback's `ctjs.func` it is a unit
/// attribute. Written by ClosureLifting/ArrayCallbacks.cpp and read by type
/// inference, admission and the lowering.
inline constexpr llvm::StringLiteral kNativeArrayCallback = "ctnative.array_callback";
/// A callback handed to the host (`el.addEventListener(type, f)` or
/// `setTimeout(f, delay)`) that closure lifting specialised for its one call.
/// On the `ctjs.call` it is a symbol reference to the callback, whose captures
/// follow the event type or the delay among the call's arguments; on the
/// callback's `ctjs.func` it is a unit attribute.
/// Written by ClosureLifting/HostCallbacks.cpp and read by type inference,
/// admission and the lowering.
inline constexpr llvm::StringLiteral kNativeHostCallback = "ctnative.host_callback";
/// Marks such a call as `setTimeout(f, delay)` or `setInterval(f, delay)`,
/// naming which, since the lowering replaces the load the call was recognised
/// by before the call itself.
inline constexpr llvm::StringLiteral kNativeHostTimer = "ctnative.host_timer";
/// A callback a promise operation keeps: the executor of `new Promise(f)` or
/// the handler of `p.then(f)`, which closure lifting specialised for that one
/// construct or call. On the `ctjs.construct` or `ctjs.call` it is a symbol
/// reference to the callback, whose captures are the operation's arguments.
/// Written by ClosureLifting/Promises.cpp and read by type inference,
/// admission and the lowering.
inline constexpr llvm::StringLiteral kNativePromiseCallback = "ctnative.promise_callback";
/// On such a callback's `ctjs.func`, the entry-block index of its JavaScript
/// parameter (the executor's resolve function, or the handler's value),
/// which follows the captures; a handler that declares none has no
/// parameter at that index.
inline constexpr llvm::StringLiteral kNativePromiseParameter = "ctnative.promise_parameter";
/// On a specialised `addEventListener` call whose receiver is the realm's own
/// `document` or `window`: that name, since the lowering replaces the load.
inline constexpr llvm::StringLiteral kNativeHostTarget = "ctnative.host_target";
/// On a specialised `addEventListener` call that passes a capture flag: the
/// flag is its last argument, after the captures.
inline constexpr llvm::StringLiteral kNativeHostCapture = "ctnative.host_capture";
/// On the `ctjs.create_closure` of a listener that is also passed to
/// removeEventListener, or added more than once: the lowering makes it the
/// closure's callable_identity (Runtime/Handlers.hpp), and each of its
/// specialised calls (marked ctnative.host_identified) passes that value as
/// its second argument, before the captures.
inline constexpr llvm::StringLiteral kNativeHostIdentity = "ctnative.host_identity";
inline constexpr llvm::StringLiteral kNativeHostIdentified = "ctnative.host_identified";
/// On a specialised `removeEventListener` call: its arguments are the type,
/// the listener's identity and the capture flag, with no captures.
inline constexpr llvm::StringLiteral kNativeHostRemove = "ctnative.host_remove";
/// A listener that removes itself through its own binding. On its
/// `ctjs.create_closure`: the capture slot that held the binding, now a
/// placeholder in which the lift passes the closure's identity. On the lifted
/// listener: the entry argument that receives the identity, a
/// ctnative::callable_identity (ClosureLifting/HostCallbacks.cpp).
inline constexpr llvm::StringLiteral kNativeListenerSelf = "ctnative.listener_self";
/// On an addEventListener or removeEventListener call whose listener is the
/// value of a closed top-level function binding: that function. Written by
/// ResolveGlobals (CTJS), read by ClosureLifting/HostCallbacks.cpp.
inline constexpr llvm::StringLiteral kHostListener = "ctjs.host_listener";
/// On such a call's load of the binding: the load becomes the function's
/// declared identity (ctnative::declared_identity).
inline constexpr llvm::StringLiteral kNativeHostDeclared = "ctnative.host_declared";

/// Written by the lowering and read by the C++ emitter (lib/Target/Cpp).
inline constexpr llvm::StringLiteral kNativeCallableBody = "ctnative.callable_body";
inline constexpr llvm::StringLiteral kNativeCallableCreate = "ctnative.callable_create";
inline constexpr llvm::StringLiteral kNativeProvenance = "ctnative.provenance";
inline constexpr llvm::StringLiteral kNativeDeduced = "ctnative.deduced";
inline constexpr llvm::StringLiteral kNativeStatement = "ctnative.statement";
inline constexpr llvm::StringLiteral kNativePinned = "ctnative.pinned";
inline constexpr llvm::StringLiteral kNativeConstBindings = "ctnative.const_bindings";
inline constexpr llvm::StringLiteral kNativeReadableLiterals = "ctnative.readable_literals";
inline constexpr llvm::StringLiteral kNativeNumericAlias = "ctnative.numeric_alias";
inline constexpr llvm::StringLiteral kNativeParameterSuppression = "ctnative.parameter_suppression";
inline constexpr llvm::StringLiteral kNativeConstOperands = "ctnative.const_operands";
inline constexpr llvm::StringLiteral kNativeTemplateParams = "ctnative.template_params";

} // namespace ctcompile::ctnative
