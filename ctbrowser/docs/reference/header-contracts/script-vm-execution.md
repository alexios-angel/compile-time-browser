# Script Vm Execution contracts

<a id="contract-1"></a>

`static constexpr std::size_t allocation_ceiling = 40'000'000;`

--- allocation -------------------------------------------------------
A RUNAWAY PAGE IS REFUSED, not left to exhaust the machine: a cap turns
std::bad_alloc into an ordinary fault with the JS stack attached. The
number is far above any real page - p5.js loading allocates a few hundred
thousand - so reaching it means a loop that does not terminate. Counted
SINCE THE LAST COLLECTION (collect() resets it): a loop that allocates
through a safepoint is bounded by the collector, not by this.

<a id="contract-2"></a>

`run_result run(const program & prog);`

--- execution ---------------------------------------------------------

THE PROGRAM MUST OUTLIVE THE CONTEXT. Closures hold `const function_proto *`
into it, and anything that calls back in later - a timer, an event
listener, requestAnimationFrame - dereferences them long after run()
returned. Passing a temporary works exactly until the first callback.

<a id="contract-3"></a>

`void instantiate_module(const program & prog, module_record & into);`

--- ES modules --------------------------------------------------------
Run `prog` AS a module: its exports are published into `into`, and its
imports are resolved through the registry the loader filled in.
BEFORE ANY OF THE GRAPH RUNS: creates this module's export cells without
evaluating it, so a cyclic importer finds an empty binding rather than a
missing one. See the definition.

<a id="contract-4"></a>

`// `a = the cell exported as `export_name` by the module at `specifier``.`

THE FOUR MODULE OPCODE BODIES, shared by the interpreter's VM_CASEs and
the ct_aot_module_* / ct_aot_dynamic_import helpers so the two tiers
cannot disagree. Nothing else calls them.

EACH KEEPS ITS OWN modules_ LOOKUP rather than sharing one that answers
a module_record *. flat_map is boost::unordered_flat_map, which is open
addressing with NO reference stability, and the dynamic-import loader
INSERTS into modules_ - so a record reference held across anything that
can load is a dangling one. Keeping the lookup inside each member is the
rule ct_aot_module_namespace's ABI row states, applied on both sides.

<a id="contract-5"></a>

`[[nodiscard]] value module_import_cell(const std::string & specifier,`

`a = the cell exported as `export_name` by the module at `specifier``.

A CELL, NOT A VALUE. An imported binding is LIVE, so the importer must
hold the very box the exporter writes through; copying the value is the
CommonJS behaviour and is observably wrong.

RAISE TIER on both misses - a module that was not loaded and a name it
does not export are uncatchable engine faults, not the ReferenceError a
spec-conformant engine would throw. It answers undefined on those paths
and the caller polls failed().

<a id="contract-6"></a>

`[[nodiscard]] value module_export_cell(const std::string & name, value current);`

Publish `name` as an export of the module being evaluated, and answer the
cell that has to land in the destination register.

IT ADOPTS THE RECORD'S CELL rather than publishing the register's.
instantiate_module creates every cell before ANY module in the graph
runs, so by the time this executes one may already be held by a cyclic
importer; overwriting the record would hand that importer a box nobody
ever writes to again.

`current` IS ANSWERED UNCHANGED when no module is being evaluated, and
that arm is the whole reason the parameter exists. The interpreter simply
does not write the register, and the register holds the local being
exported - so a second implementation that wrote undefined there would
DESTROY it. Returning what was already there is the same thing said in a
form a register-to-SSA lowering can express.

<a id="contract-7"></a>

`[[nodiscard]] value module_namespace_for(const std::string & specifier);`

`a = the namespace object of the module at `specifier``, live and cached.
Same resolve-then-find as module_import_cell and the same raise, but the
RESULT KIND differs: an ordinary object whose properties are accessors
over the cells, not a cell.

<a id="contract-8"></a>

`[[nodiscard]] value deferred_module_namespace(module_record & of);`

THE DEFERRED NAMESPACE (16.2.2, `import defer * as ns`): the module's
live namespace, except that the first read of an export EVALUATES the
module first, through the hook the loader installed (EvaluateSync) -
an evaluation that throws is that read's throw. A symbol key and
`then` never trigger it (IsSymbolLikeNamespaceKey). What the hidden
native import_defer_name answers; see the definition for what of the
exotic object this ordinary one does not do.

<a id="contract-9"></a>

`[[nodiscard]] value dynamic_import(value specifier, const std::string & referrer);`

`a = import(specifier)` - a promise for the namespace object.

THE REFERRER IS THE CALLER'S, not current_module_'s: a dynamic import is
usually called long after its module finished evaluating, from a
callback, where current_module_ is null. Both tiers read it off the
running frame's function_proto::module and pass it in.

to_string IS INSIDE THIS, not at the call sites: it can run a page's own
toString, and converting on one side only would let the two tiers
disagree about what was asked for.

<a id="contract-10"></a>

`static constexpr std::uint32_t reentry_ceiling = 512;`

--- THE CEILING THE C++ STACK NEVER HAD -------------------------------

`frames_` counts INTERPRETED frames, so the 512-frame ceiling in
`invoke` cannot see a cycle that never pushes one. A native `toString`
that asks the context to stringify its own receiver recurses

    to_string -> to_primitive_string -> invoke -> to_string -> ...

entirely on the C++ stack, and a self-referential array - `a[0] = a;
String(a)` - is the same cycle with no call in it at all, which is why
the counter is on the CONVERSIONS as well as on the native call.

A catchable RangeError, like every engine's stack exhaustion. The same
512 as the interpreted stack: one level is four C++ frames of a few
hundred bytes, so 512 is a few hundred KB of an 8 MB stack - far enough
below the fault to be a diagnosis, and far above any finite conversion.

<a id="contract-11"></a>

`value call(value callable, std::span<const value> args, value this_value = value::undefined());`

Call a JS function FROM C++ - an event listener, a timer, a
requestAnimationFrame callback. Re-entrant: it runs a nested interpreter
loop on the existing register stack, so a listener may itself call back
into script.
A THROW THE CALLEE DOES NOT CATCH IS HELD UNTIL THE NATIVE RETURNS.
Called DIRECTLY from a native (native_scope, and no interpreted frame
pushed since it began), `call` fences the callee (see call_fenced); a
throw that crosses it is parked in pending_throw_, this returns
undefined, every further `call` from the same native returns undefined
without calling, and the VM rethrows it at the native's own call site
(rethrow_pending, at the three places a native returns to it). From
interpreted code - a setter reached through op::set_index inside a test
body that `apply` is running - the throw unwinds at once to that code's
own handlers, as before. So `[1, 2].forEach(f)` with `f`
throwing on 1 never calls `f` for 2, and the page's `try` around the
forEach catches once - before, the first throw unwound to that `try`
while forEach kept going, and the second throw found the handler
already consumed and was an engine fault. A native that wants to see
the throw itself uses call_fenced.

<a id="contract-12"></a>

`value call_fenced(value callable, std::span<const value> args, value this_value, bool & threw,`

`call`, WITH THE THROW VISIBLE TO THE CALLER. A throw the callee does
not catch unwinds to the innermost `try` on the WHOLE stack - which,
from C++, is whatever page code happened to be running above the
native, or nothing (an engine fault). This puts a fence on the handler
stack first: the throw stops here, `threw` says so and `thrown` is the
value, and the caller decides - a promise reaction rejects its
promise, a callback-taking native rethrows. The listener trampoline in
events/dispatch.cpp did this with compiled JavaScript; this is the
same fence in the VM.

<a id="contract-13"></a>

`[[nodiscard]] value spread_values(value v);`

op::iterable - a SPREAD's source (`[...x]`, `f(...x)`), which is
iterable_values with the one thing a spread adds: null, undefined and a
primitive that is not a string are the TypeError of 13.2.5.1's
GetIterator, where a library constructor given null (`new Map(null)`)
is content with nothing. Both tiers call this one.

<a id="contract-14"></a>

`[[nodiscard]] value construct(value callee, std::span<const value> args,`

`new callee(...args)` where the argument count is only known at run time.
op::construct keeps its own inline path because it does not need a nested
interpreter loop; this is for the spread form and for `Reflect.construct`,
which both do.
`new_target` defaults to the callee (a plain `new`); a proxy's
[[Construct]] and Reflect.construct pass their own.

<a id="contract-15"></a>

`bool to_primitive_hint(value v, const char * hint, value & out);`

--- conversions (ECMA-262 shaped, and shared with the bindings) -------
ToPrimitive (7.1.1) with a hint - "default", "number" or "string": the
object's @@toPrimitive, then OrdinaryToPrimitive. False means a
TypeError is in flight. to_primitive, to_number_value and
to_primitive_string are this one walk.

<a id="contract-16"></a>

`[[nodiscard]] value proxy_trap(value proxy, const std::string & name, bool * failed = nullptr);`

The handler's trap of this name, or undefined when it has none. Public
for the standard library: `hasOwnProperty` must ask a proxy's handler the
same question `in` asks it, and `window` is a proxy.
GetMethod (7.3.10): null/undefined is "no trap" (undefined here); a
revoked proxy or a non-callable trap is the TypeError, and `failed`,
when given, is set for that and for a getter's throw - the caller must
not forward to the target after either (see the definition).
