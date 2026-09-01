# ES modules — `load_import`, `bind_export`, `load_namespace`, `dyn_import`

The four opcodes `ImporterCoverage.cpp` labels "ES modules - Phases 15-16".
They are the last four non-suspending opcodes the importer does not dispatch
that belong to a phase rather than to a blocker, and unlike Phase 13's list
they share one problem that is not a missing helper:

**NOTHING IN `ctcompile/test/` COMPILES A MODULE.** `ctjs-translate` has two
translations and both call `compiler::compile(text)` with the default
`script_kind::classic`; `Differential.cpp` runs one classic script through
`cx.run`; `linkable.js` and `gc-roots.js` are classic. A module opcode is
emitted only by `compile_program`'s `if (module_scope_)` arm, so today there is
no path by which any of these four could reach the importer at all — and
therefore no path by which a lowering for one could be shown to compute
anything.

Everything below was read rather than recalled. Where a fact cited by an
existing comment turned out to be false, that is recorded as a finding instead
of being quietly worked around.

**ALL FOUR LANDED.** The sections down to *The step list* are the reading, in
the present tense of the tree BEFORE the work, and are kept that way because
they are the argument for the shape that was built. *The differential fixture,
as built* onwards is what actually happened, including the five falsifications
and the list of things still unverified. Importer coverage went 84 to 88 of the
91 non-suspending opcodes; the corpus counts did not move, and could not, for a
reason measured rather than assumed at the bottom of this file.

## What was measured

* **All four helpers are declared and none has a body.** `ct_aot_module_import_cell`,
  `ct_aot_module_export_cell`, `ct_aot_module_namespace` and
  `ct_aot_dynamic_import` appear in `aot_helpers.def` and in no `.cpp` anywhere
  — verified by searching `ctbrowser/lib`, `ctbrowser/unittests`, `ctbrowser/tools`
  and `ctcompile`. `runtime_defines()` in `CTJSToEmitC.cpp` will therefore have
  to name them until the bridge lands, or the backend emits four link errors.
* **`CT_AOT_NO_WRITE` DOES NOT EXIST.** `ct_aot_module_export_cell`'s row calls
  itself "THE ONLY HELPER IN THE TABLE THAT PRODUCES CT_AOT_NO_WRITE", and
  `ct_aot_status` in `aot.hpp` has exactly four members — `unwound`, `failed`,
  `caught`, `ok`. The token `NO_WRITE` occurs twice in the tree and both are in
  that one paragraph. This is `context::callee_type_error` again: a name cited
  by a row that was never anywhere. See the design section — it is solvable
  without adding a status, and adding one would be actively unsafe, because
  every existing `status_call` treats "not `ok`" as the shared failure path and
  would return out of the function.
* **`flat_map<std::string, V>::find` has no heterogeneous overload.**
  `core/containers.hpp` says so at length and `string_flat_map` exists because
  of it. `modules_`, `module_record::resolved` and `module_record::exports` are
  all plain `flat_map`s, so a helper taking `(const char *, uint32_t)` must
  build a `std::string` to look anything up. That costs one allocation per
  executed module opcode, and every one of these runs once per binding in a
  module prologue — never in a loop.
* **`aot_bridge` is a `friend` of `context`** (`vm.hpp:1168`), so it can read
  `current_module_` and `modules_` directly. The row for
  `ct_aot_module_export_cell` worries about this ("private at vm.hpp:1009 with
  no accessor"); the friendship is the answer it already had, and the line
  number is wrong — see the citations section.
* **A compiled top level IS entered, on all three paths.** `context::execute`
  asks `enter_compiled` before pushing a frame, and so does
  `context::run_reentrant`; `ctbrowser/unittests/unit/aot_dispatch.cpp` already
  asserts both, the second arm through `cx.run_module`. That matters because
  `load_import`, `bind_export` and `load_namespace` are emitted ONLY into
  `functions[0]` of a module program — `compile_program`'s module arm — so a
  test that could not enter a compiled top level could not reach three of the
  four.
* **`run_module` does not instantiate.** `context::run_module` sets
  `into.compiled` and evaluates; it never creates a cell.
  `context::instantiate_module` is called from exactly one place in the whole
  tree, `browser::instantiate_module`. So a host that evaluates a module
  without instantiating it first reaches `bind_export`'s allocating fallback,
  and that arm is live rather than theoretical — the row says this and it is
  still true.
* **`module_namespace` publishes the half-built object before the accessor
  loop.** `of.namespace_object = value::object(ns);` precedes the `for` that
  allocates one `native_object` per export, so the allocations inside the loop
  cannot lose it. The row is right about the mechanism and wrong about the
  ordinal — see the citations section.
* **`dyn_import` does not pre-write its destination.** The other three module
  handlers begin `reg(in.a) = value::undefined();`. `VM_CASE(dyn_import)` does
  not, so on the no-loader `raise` register `a` keeps whatever it held. The
  opcode row says this and it is the one claim in that paragraph that is still
  exactly true.

## The four ABI rows, verbatim

From `ctbrowser/include/ctbrowser/aot/aot_helpers.def`. Reproduced in the
file's order, which is import, export, namespace, dynamic.

```
/* What lands in the result is a CELL, not a value - every later read must
 * go through ct_aot_cell_get, because compile/frames.cpp:143-149 marks the
 * local boxed and deliberately emits NO new_cell, so boxing a box would
 * leave the importer reading a cell containing a cell. Both names are
 * compile-time constants and both are genuinely available: the specifier is
 * decode_string_literal(n.text) at frames.cpp:89 and the export name is
 * spec.text/'default' at frames.cpp:136-139, i.e. text the compiler read
 * out of the source. That asymmetry is what justifies string-literal
 * parameters HERE and forbids them for the dynamic-import referrer. The
 * encoding trap - b is the EXPORT name and c the SPECIFIER, the only opcode
 * reading c as a standalone index - disappears by construction with named
 * parameters. Not a safepoint: it reads two flat_maps and, on failure,
 * builds a std::string, which is a C++ heap allocation and not a GC one.
 * DELEGATES TO: run_loop.cpp's VM_CASE(load_import), inline:
 *   current_module_->resolved lookup (as-written then as-keyed), then
 *   modules_ lookup, then module_record::exports lookup. Nothing new is
 *   written.
 * FAILURE: RAISE TIER ONLY for both misses - 'module X was not loaded' and
 *   'X has no export named Y', both raised there - uncatchable engine
 *   faults, NOT the ReferenceError a spec-conformant backend would emit.
 *   Returns undefined on those paths; poll ct_aot_failed. The interpreter's
 *   pre-write at :1297 is DEAD on the failure path (raise sets failed_,
 *   VM_NEXT goes straight to vm_done, nothing reads the register), so the
 *   lowering need not reproduce a store the interpreter never observes. */
CT_AOT_HELPER(/* name */ ct_aot_module_import_cell, /* ret */ uint64_t,
              /* params */ (struct ct_aot_frame *fr, const char *specifier, uint32_t specifier_len, const char *export_name, uint32_t export_name_len),
              /* may_throw */ 1, /* may_reenter */ 0, /* is_safepoint */ 0)
CT_AOT_COVERS(/* helper */ ct_aot_module_import_cell, /* opcode */ load_import)
```

```
/* Data flows record -> register, backwards from the name and from
 * bytecode.hpp:201: it ADOPTS the pre-existing cell so a cyclic importer
 * that already took the box keeps a live binding. Cannot merge with
 * ct_aot_module_import_cell: different registry (this module's record vs
 * another's), different failure tier (silent no-op vs uncatchable raise),
 * and it can CREATE the cell where load_import can only find one. A
 * safepoint solely because of that fallback allocation. It needs
 * current_module_, private at vm.hpp:1009 with no accessor - but that is an
 * implementation detail of ctbrowser's own DEFINE expansion, not an ABI
 * question, since the helper is DEFINED in ctbrowser under Principle 11.
 * DELEGATES TO: run_loop.cpp:1270-1292 inline:
 *   current_module_->exports[name], creating a cell_object when absent
 *   (:1287). instantiate_module (call.cpp:110-119) normally created them
 *   all, but browser::instantiate_module is what calls it (browser.cpp:975)
 *   - context::run_module (call.cpp:151-162) does NOT, so a host running a
 *   module without instantiating it first reaches the fallback and the
 *   allocation is live.
 * FAILURE: CT_AOT_OK with *out = the record's cell; CT_AOT_NO_WRITE when
 *   current_module_ is null; CT_AOT_FAILED on the ceiling. THE ONLY HELPER
 *   IN THE TABLE THAT PRODUCES CT_AOT_NO_WRITE, and that is the status's
 *   whole justification: the handler at :1284 simply breaks with no raise
 *   and no thrown_, so execution CONTINUES with the destination register
 *   untouched. writes_a is CONDITIONAL, and if AOT wrote undefined there it
 *   would DESTROY the local being exported, because the register holds that
 *   local on entry (entry.cpp:44-48 passes find_local(local_name) as the
 *   destination). */
CT_AOT_HELPER(/* name */ ct_aot_module_export_cell, /* ret */ int32_t,
              /* params */ (struct ct_aot_frame *fr, const char *name, uint32_t name_len, uint64_t *out),
              /* may_throw */ 1, /* may_reenter */ 0, /* is_safepoint */ 1)
CT_AOT_COVERS(/* helper */ ct_aot_module_export_cell, /* opcode */ bind_export)
```

```
/* Kept separate from ct_aot_module_import_cell for a reason stronger than
 * 'the bytecode made the distinction': the two RESULT KINDS have different
 * downstream lowering - a cell every read must funnel through
 * ct_aot_cell_get, versus an ordinary object whose reads are get_prop and
 * therefore accessor RE-ENTRIES, since module_namespace installs a native
 * GETTER per export (call.cpp:137-146). One helper with a nullable name
 * would turn a static property of the lowering into a run-time one. The
 * tempting third option - factoring the shared resolve+lookup into a helper
 * returning module_record* - is UNSAFE: flat_map is
 * boost::unordered_flat_map (core/containers.hpp:30), open-addressing with
 * NO pointer or reference stability, and the installed loader INSERTS into
 * it (browser.cpp:1220-1226), which is why the loader itself re-does find()
 * at :1233 after loading. Any module_record* held across a module-loading
 * helper dangles, so the lookup stays inside each helper. The allocation
 * here is safe under a real collector because the half-built object is
 * stored into of.namespace_object BEFORE the accessor loop
 * (call.cpp:134-135) and that field is GC root 12 - the exact
 * counterexample to ct_aot_iterable_values's unrooted C++ local.
 * DELEGATES TO: the same resolved/modules_ lookup as load_import -
 *   run_loop.cpp's VM_CASE(load_import) - then context::module_namespace.
 * FAILURE: RAISE TIER ONLY - 'module X was not loaded', raised by
 *   VM_CASE(load_namespace) itself, or the
 *   ceiling. Same correction as ct_aot_module_import_cell: the pre-
 *   write of undefined is dead on the raise path and need not be
 *   reproduced. */
CT_AOT_HELPER(/* name */ ct_aot_module_namespace, /* ret */ uint64_t,
              /* params */ (struct ct_aot_frame *fr, const char *specifier, uint32_t specifier_len),
              /* may_throw */ 1, /* may_reenter */ 0, /* is_safepoint */ 1)
CT_AOT_COVERS(/* helper */ ct_aot_module_namespace, /* opcode */ load_namespace)
```

```
/* THE REFERRER IS A PARAMETER NO COMPILER CAN FILL, and the tree says so in
 * words: function_proto::module is stamped by the LOADER - `for
 * (script::function_proto & fn : compiled->functions) fn.module =
 * specifier;` at browser.cpp:976, under the comment 'The compiler cannot do
 * this: a specifier is the loader's name for a file.' It is also absent
 * from engine_contract::proto_header, so Phase 15 does not serialize it
 * today. Baking a literal would make the image's referrer the compiler's
 * GUESS at what the loader will call the file, and resolve_specifier
 * (browser.cpp:917-918) rewrites only './' and '../' - so a wrong referrer
 * leaves bare and absolute specifiers working and breaks exactly the
 * relative ones, silently, the worst possible failure distribution. Reading
 * it from fr costs nothing new because the frame must carry the owning
 * proto ANYWAY for current_stack. The heaviest safepoint in the area: TWO
 * user-JS re-entries before it returns (to_primitive_string's
 * lookup_property then call, coerce.cpp:172-177; then a whole module graph
 * through run_reentrant, call.cpp:169-200), and the loader INSERTS into
 * modules_, which is the reference-invalidation hazard above. It may also
 * set suspended_ via the imported module's top-level frame, a flag AOT must
 * not read.
 * DELEGATES TO: run_loop.cpp's VM_CASE(dyn_import) inline: context::to_string(spec)
 *   (coerce.cpp:78 -> to_primitive_string at :171 for an object), then the
 *   embedder's module_loader_ (installed at Shell/browser.cpp:1216-1239),
 *   which reaches the VM through run_reentrant. The REFERRER is read by the
 *   helper from fr, not passed in.
 * FAILURE: Full status; *out = the promise on OK. NO LOADER INSTALLED IS
 *   CT_AOT_FAILED, not a no-write: VM_CASE(dyn_import) raise()s 'dynamic
 *   import() has no loader installed', which sets failed_, and VM_NEXT then
 *   goes to vm_done - the interpreter does NOT keep going, so a status
 *   meaning 'completed, keep going, do not write' would make AOT code
 *   execute past an uncatchable engine fault. A MISSING MODULE, by
 *   contrast, is CT_AOT_OK carrying an already-REJECTED settled promise
 *   (browser.cpp:1221-1223, 1231-1233) - the lowering must not treat 'the
 *   module was not found' as a control-flow event. */
CT_AOT_HELPER(/* name */ ct_aot_dynamic_import, /* ret */ int32_t,
              /* params */ (struct ct_aot_frame *fr, uint64_t specifier, uint64_t *out),
              /* may_throw */ 1, /* may_reenter */ 1, /* is_safepoint */ 1)
CT_AOT_COVERS(/* helper */ ct_aot_dynamic_import, /* opcode */ dyn_import)
```

### Tiers, derived rather than asserted

`classify_return` in `RuntimeHelpers.hpp` maps `uint64_t` to
`return_role::value` and `int32_t`-with-a-frame-parameter to
`return_role::status`. So:

| helper | tier | shape |
|---|---|---|
| `ct_aot_module_import_cell` | RAISE | plain value return, poll `ct_aot_failed` at a back edge |
| `ct_aot_module_export_cell` | STATUS | `status_call`, one `uint64_t *` out-parameter |
| `ct_aot_module_namespace` | RAISE | plain value return |
| `ct_aot_dynamic_import` | STATUS | `status_call`, one `uint64_t *` out-parameter |

Two RAISE-tier rows follow `ct_aot_cell_new`'s body exactly: return the value
plainly, never a status, and let the caller's back-edge poll notice. Two STATUS
rows are `status_call`'s existing shape — with one modification for
`bind_export`, below.

## The four VM handlers, verbatim

All four from `ctbrowser/lib/Script/vm/run_loop.cpp`. In scope there, `base` is
`vm_frame->base`, `reg(r)` is `registers_[base + r]` and `vm_proto` is the
running `function_proto`.

```cpp
        VM_CASE(bind_export) do {
            // ADOPT THE RECORD'S CELL, do not publish this register's. The cell
            // is created before ANY module in the graph runs - see
            // instantiate_module - so by the time this executes it already
            // exists and something in a cycle may already be holding it.
            // Overwriting the record here would hand that importer a box
            // nobody ever writes to again.
            //
            // THE CELL, NOT THE VALUE, either way: an importer holds the box,
            // which is what makes the binding LIVE. Handing over the value
            // passes "an importer sees an export" and fails "an imported
            // binding is live" - the shortcut docs/plans/modules.md names in
            // advance.
            if (current_module_ != nullptr) {
                value & slot = current_module_->exports[vm_proto->names[in.bx()]];
                if (!slot.is_kind(heap_kind::cell)) {
                    slot = value::object(allocate<cell_object>(value::undefined()));
                }
                reg(in.a) = slot;
            }
            break;
        }
        while (0);
        VM_NEXT;
```

```cpp
        VM_CASE(load_import) do {
            // The exporter has been evaluated already - the loader walks the
            // graph depth-first - so its cell is there to be taken.
            reg(in.a) = value::undefined();
            const std::string & written = vm_proto->names[in.c];
            const std::string & what = vm_proto->names[in.b];
            // AS WRITTEN IS NOT AS KEYED: `./dep.js` in one module and in
            // another are two different files. The loader left the translation
            // in the record - see module_record::resolved.
            const std::string & from = [&]() -> const std::string & {
                if (current_module_ == nullptr) { return written; }
                const auto mapped = current_module_->resolved.find(written);
                return mapped == current_module_->resolved.end() ? written : mapped->second;
            }();
            const auto found = modules_.find(from);
            if (found == modules_.end()) {
                raise("module `" + from + "` was not loaded");
                break;
            }
            const auto cell = found->second.exports.find(what);
            if (cell == found->second.exports.end()) {
                raise("`" + from + "` has no export named `" + what + "`");
                break;
            }
            reg(in.a) = cell->second;
            break;
        }
        while (0);
        VM_NEXT;
```

```cpp
        VM_CASE(load_namespace) do {
            reg(in.a) = value::undefined();
            const std::string & written = vm_proto->names[in.b];
            const std::string & from = [&]() -> const std::string & {
                if (current_module_ == nullptr) { return written; }
                const auto mapped = current_module_->resolved.find(written);
                return mapped == current_module_->resolved.end() ? written : mapped->second;
            }();
            const auto found = modules_.find(from);
            if (found == modules_.end()) {
                raise("module `" + from + "` was not loaded");
                break;
            }
            reg(in.a) = module_namespace(found->second);
            break;
        }
        while (0);
        VM_NEXT;
```

```cpp
        VM_CASE(dyn_import) do {
            // THE REFERRER COMES FROM THE RUNNING FUNCTION, not from
            // current_module_, and the difference is the whole point: a
            // dynamic import is usually called long after its module finished
            // evaluating, from a callback, where current_module_ is null. The
            // proto knows which module it was compiled in - see
            // function_proto::module.
            if (!module_loader_) {
                raise("dynamic import() has no loader installed");
                break;
            }
            const value spec = reg(in.b);
            // THE RESULT INTO A LOCAL FIRST, and it is not a style choice. The
            // loader evaluates the module it fetches, which RE-ENTERS this VM
            // and grows `registers_` - so a reference to reg(in.a) taken before
            // the call points into a freed buffer by the time the value comes
            // back. Written that way it stored the promise into memory nobody
            // owned and `import(...)` read undefined, with no error anywhere.
            const value loaded = module_loader_(
                *this, to_string(spec),
                vm_frame->proto == nullptr ? std::string{} : vm_frame->proto->module);
            reg(in.a) = loaded;
            break;
        }
        while (0);
        VM_NEXT;
```

### Exactly which registers and operands each touches

| | reads | writes |
|---|---|---|
| `VM_CASE(load_import)` | `names[in.b]` = the **EXPORT NAME**; `names[in.c]` = the **SPECIFIER AS WRITTEN**. No register is a source. | `reg(in.a)` — `undefined` first, then the cell |
| `VM_CASE(bind_export)` | `names[in.bx()]` = the export name, where `bx()` is `(b << 16) \| c` | `current_module_->exports[name]`, and `reg(in.a)` — **only when `current_module_ != nullptr`** |
| `VM_CASE(load_namespace)` | `names[in.b]` = the specifier as written; `in.c` unused | `reg(in.a)` — `undefined` first, then the namespace object |
| `VM_CASE(dyn_import)` | `reg(in.b)` = the specifier **VALUE**; `vm_frame->proto->module` = the referrer; `in.c` unused | `reg(in.a)`, and **NOT** pre-written |

**`load_import` is the one to get backwards, and the ABI row says so: "b is the
EXPORT name and c the SPECIFIER, the only opcode reading c as a standalone
index".** `bind_imports` emits `instruction{op::load_import, r, what, from}`,
where `what` is the exporter's name for the binding and `from` is the specifier
— and `bytecode.hpp` states the same thing from the other side: "`a` = the cell
exported as `names[b]` by the module at specifier `names[c]`". Swapping the two
is a mistake that COMPILES, produces a plausible message ("`x` has no export
named `./dep.js`") and would pass any fixture whose specifier and export name
happened to be equal. Every fixture below keeps them distinct for that reason.

**`bind_export`'s `in.b` is the bx HIGH half and is always 0 in practice**, so a
decoder that reads `b` alone gets name index 0 and exports the wrong binding
under a name it did not mean. The opcode row calls this out. Both the importer
case and the fixture below are shaped so that this mistake shows.

**`in.c` is unused by `bind_export` (as a standalone operand), `load_namespace`
and `dyn_import`**, and `bytecode_opcodes.def` agrees: `load_namespace` and
`dyn_import` carry `c_kind unused`, `bind_export` carries `c_kind bx_hi` in the
file's usual convention — `b_kind` names what the pair indexes and `c_kind` marks
it as a pair. Thirteen rows are written that way; this one is not an anomaly.

## What `current_module_`, `modules_` and `module_record` actually are

```cpp
struct module_record {
    std::string specifier;
    const program * compiled = nullptr;
    flat_map<std::string, value> exports;
    flat_map<std::string, std::string> resolved;
    value namespace_object = value::undefined();
    bool evaluated = false;
};
```

* **`exports` maps a name to the CELL**, not to a value — that is what makes an
  imported binding live, and `vm.hpp` says handing over the value "passes a test
  that two modules can see each other and fails the one that matters".
* **`resolved` maps the specifier AS WRITTEN to the registry key.** `./dep.js`
  means a different file depending on which module wrote it, and the bytecode
  can only carry what was written. `browser::instantiate_module` fills it:
  `record.resolved[written] = resolve_specifier(specifier, written)`. A record
  with an empty `resolved` map is not an error — the lambda in both handlers
  falls back to the written specifier.
* **`namespace_object` is created on demand and cached**, so two `import * as`
  of the same module are the same object and `===` between them is true.
* `context::modules_` is `flat_map<std::string, module_record>`, reachable
  publicly through `context::modules()`. `context::current_module_` is a
  `module_record *`, private, `nullptr` except inside `run_module`.

Both maps are `boost::unordered_flat_map`, which is open-addressing with **no
pointer or reference stability**. `ct_aot_module_namespace`'s row is right that
a `module_record *` must not be held across anything that can insert into
`modules_`, and the loader does insert. Nothing proposed here holds one across a
call.

## What `instantiate_module` does, and who calls it

```cpp
void context::instantiate_module(const program & prog, module_record & into) {
    into.compiled = &prog;
    for (const std::string & name : prog.exports) {
        value & slot = into.exports[name];
        if (!slot.is_kind(heap_kind::cell)) {
            slot = value::object(allocate<cell_object>(value::undefined()));
        }
    }
}
```

It creates every export cell **without running anything**. That is the
instantiate half of the two-phase split, and the reason it exists is a cycle: A
imports B imports A, so B evaluates first and asks A for a binding A has not
reached the declaration of. With every cell created up front B gets a real box
that is merely empty, and A's later write is a write B reads.

**Exactly one caller in the tree**: `browser::instantiate_module`
(`ctbrowser/lib/Shell/browser.cpp`), which stamps `fn.module = specifier` on
every proto, calls it, records `resolved`, and recurses over the dependencies.
`context::run_module` does **not** call it, and neither does anything in
`unittests/` or `ctcompile/`. So `bind_export`'s creating fallback is reachable
by any host that evaluates a module directly — which is what the harness below
deliberately does in one arm and deliberately does not do in another.

## Candidate designs, per opcode

### `load_import` and `load_namespace` — one shared question

Both begin with the identical five lines: resolve the written specifier through
`current_module_->resolved`, then `modules_.find`, then raise
``"module `X` was not loaded"``. They then diverge completely: one looks the
export up and answers a **cell**, the other calls `module_namespace` and answers
an **object whose properties are accessors**.

**(a) Two `context` members, each self-contained, sharing a private
`resolve_module_specifier`.** The shared part is the resolution lambda, lifted
to a member returning `const std::string &`; each handler keeps its own
`modules_.find`, its own raise, and its own tail.

*Cost.* Three members, four call sites in `run_loop.cpp`, two bridge statics.
The two `modules_.find` calls stay where they are, so nothing holds a
`module_record *` across anything.

**(b) One member answering `module_record *`, with the two tails at the call
sites.** Fewer lines.

*It should not be written, and the ABI row already refused it for the runtime
half of the same question:* "Any `module_record*` held across a module-loading
helper dangles, so the lookup stays inside each helper." Within `run_loop` the
pointer would not be held across anything, so it is safe *today* — which is
exactly the shape of invariant this project keeps discovering it did not have.
It also splits the raise from the lookup, and the raise message is one of the
two things a differential fixture can observe.

**Recommendation: (a).**

### `bind_export` — the conditional write, and the status that does not exist

The row asks for `CT_AOT_NO_WRITE`. There is no such status. Three ways out:

**(a) Add `no_write` to `ct_aot_status`.**

*Cost.* One enumerator — and then every `status_call` in `CTJSToEmitC.cpp`
compares the answer against `ok` and branches to the shared failure path on
anything else. A `no_write` returned to an existing call site would **return out
of the compiled function** carrying a status the caller reads as a failure. So
this is not one enumerator; it is a new arm in `status_call`, in
`caught_or_failure`, and in `failure_path`, plus a rule about which helpers may
produce it. It also changes the ABI's status vocabulary for the LLVM backend
that has not been written yet.

**(b) Seed the out-parameter with the destination's current value, and leave the
ABI signature exactly as declared.** The op carries `$current` as an operand;
the lowering declares the `uint64_t` slot **initialised to `current`** rather
than uninitialised, calls the helper, and maps the operation's result to the
loaded slot. The bridge writes `*out` only when `current_module_ != nullptr`,
and returns `ok` either way. On the null-module path the slot still holds
`current`, so the SSA value the register is rebound to is the value it already
had — which is what "the destination register untouched" means once registers
are SSA values rather than memory.

*Cost.* One extra operand on the operation, one extra `ec::VariableOp`
initialiser, and a correction to the row's FAILURE paragraph. No new status, no
change to `status_call`'s contract for anything else.

*The property that matters:* it cannot destroy the local being exported, which
is the row's stated reason for wanting a status. It writes back the same bits.

**(c) Make it RAISE tier: `uint64_t ct_aot_module_export_cell(fr, name, len, uint64_t current)`.**
Cheapest to lower — it becomes `CreateCellOp`'s shape exactly — but it changes a
declared signature that two code generators are written against, and the row's
`may_throw 1` exists for the allocation ceiling, which `allocate` survives
anyway. Changing a signature to make a lowering easier is the drift
`CTJSABIShape.cpp` exists to catch, in the one direction the trait cannot see.

**Recommendation: (b).** It honours the declared signature literally, needs no
new vocabulary, and the FAILURE paragraph's correction is one sentence: the
no-write is expressed by leaving a caller-seeded `*out` alone, because the
status enum has four members and none of them is `NO_WRITE`.

### `dyn_import` — the referrer, and re-entrancy

The row is emphatic and correct: the referrer cannot be a literal. `fn.module`
is stamped by the loader after compilation, under the comment "The compiler
cannot do this: a specifier is the loader's name for a file", and it is absent
from `engine_contract::proto_header`, so an image does not carry it either.

**(a) Read it from the frame, exactly as the row says.** `frames_[held.frame_index].proto->module`,
with the same `proto == nullptr` guard the handler has. The frame already
carries the proto for `current_stack`, so this costs nothing new.

**(b) Bake the referrer as a fourth string parameter.** Rejected by the row, and
the reason is the failure distribution rather than purity: `resolve_specifier`
rewrites only `./` and `../`, so a wrong referrer leaves bare and absolute
specifiers working and breaks exactly the relative ones.

**Recommendation: (a).** The one thing it adds over the handler is that
`aot_bridge::enter` must have set `entered.proto` — it does, from `site`.

The re-entrancy is `ct_aot_call`'s existing pattern: call, then `check(f)`,
write `*out` only on `ok`. The loader can evaluate a whole module graph, which
can raise, which `check` reports as `failed`; it can also unwind past this
frame, which `check` reports as `unwound` by comparing `frames_.size()` against
the recorded index. Both are already handled by `status_call`'s branch to the
shared failure path.

**`suspended_` must not be read**, and nothing proposed reads it.

## The step list

The order is the project's established one — the shared member first, so the two
tiers cannot spell the semantics differently, and both halves of the EmitC
lowering together, because one without the other has happened four times.

**1 — `ctbrowser/include/ctbrowser/script/vm.hpp` and
`ctbrowser/lib/Script/vm/run_loop.cpp`: lift all four handlers into shared
`context` members.** The move `make_closure`, `construct_new`,
`iterable_values`, `has_property`, `instance_of`, `delete_index`, `own_keys`,
`delete_named` and `interned_bigint_literal` have all already made.

* `const std::string & resolve_module_specifier(const std::string & written) const`
  — private; the lambda both handlers spell twice, once. Returns a reference into
  `resolved` or into `written`, so the caller must keep `written` alive, which
  every caller does.
* `value module_import_cell(const std::string & specifier, const std::string & export_name)`
  — resolve, `modules_.find`, `exports.find`, both raises, return the cell or
  `undefined`.
* `value module_export_cell(const std::string & name, value current)` — the
  whole `if (current_module_ != nullptr)` body, answering `current` when there is
  no module. **The null-module arm is part of the member**, because it is the
  half a lowering would otherwise have to reinvent and get wrong in one tier.
* `value module_namespace_for(const std::string & specifier)` — resolve,
  `modules_.find`, the raise, then `module_namespace(found->second)`.
* `value dynamic_import(value spec, const std::string & referrer)` — the
  no-loader raise, `to_string`, the loader call. **`to_string` is inside the
  member**: it can run user JavaScript, and doing it on one side only would make
  the two tiers convert differently.
* The four `VM_CASE`s become one or two lines each. `VM_CASE(bind_export)`
  becomes `reg(in.a) = module_export_cell(vm_proto->names[in.bx()], reg(in.a));`
  — which reads the destination as a source, and is the shape that makes the
  conditional write expressible without a status on both sides at once.

**2 — `ctbrowser/include/ctbrowser/aot/aot_helpers.def`: repair the citations,
and correct the two paragraphs that are false.** No row's signature changes. See
the citations section for the list. `helper_count` is derived and does not move.

**3 — `ctbrowser/lib/Script/aot_bridge.cpp`: four statics and four `extern "C"`
wrappers.** Each delegates to its Step 1 member against `frame_of(f).ctx`.

* `module_import_cell` and `module_namespace` return the value plainly, with no
  status test — raise tier, following `ct_aot_own_keys`' body exactly.
* `module_export_cell` writes `*out` and returns `ok`; the caller seeded `*out`.
* `dynamic_import` reads the referrer off `cx.frames_[held.frame_index]`, calls
  the member, then `check(f)`, and writes `*out` only on `ok` — `ct_aot_call`'s
  body exactly.
* Every one builds a `std::string` from its `(ptr, len)` pair, because
  `flat_map<std::string, V>::find` takes the key type and `core/containers.hpp`
  says why.

**4 — `ctcompile/include/ctcompile/CTJS/IR/CTJSOps.td`: four operations.**

* `CTJS_ModuleImportCellOp : CTJS_RuntimeOp<"module_import_cell", "ct_aot_module_import_cell", [CTJS_MayThrow]>`
  — `StrAttr:$specifier`, `StrAttr:$export_name`, one `CTJS_ValueType` result.
* `CTJS_ModuleExportCellOp : CTJS_RuntimeOp<"module_export_cell", "ct_aot_module_export_cell", [CTJS_MayThrow, CTJS_Safepoint]>`
  — `StrAttr:$name`, `CTJS_ValueType:$current`, one `CTJS_ValueType` result.
* `CTJS_ModuleNamespaceOp : CTJS_RuntimeOp<"module_namespace", "ct_aot_module_namespace", [CTJS_MayThrow, CTJS_Safepoint]>`
  — `StrAttr:$specifier`, one `CTJS_ValueType` result.
* `CTJS_DynamicImportOp : CTJS_RuntimeOp<"dynamic_import", "ct_aot_dynamic_import", CTJS_GenericEffects>`
  — `CTJS_ValueType:$specifier`, one `CTJS_ValueType` result. This is the only
  one of the four that is `may_reenter 1`, so it is the only one for which
  `CTJS_GenericEffects` is honest.

All four pass `verifyABIShape`, checked against `census_of`:

| op | roles | out | value return | results expected | attributes allowed |
|---|---|---|---|---|---|
| `module_import_cell` | frame, text, count, text, count | 0 | yes | 1 | 4 (carries 2) |
| `module_export_cell` | frame, text, count, out_value | 1 | no (status) | 1 | 2 (carries 1) |
| `module_namespace` | frame, text, count | 0 | yes | 1 | 2 (carries 1) |
| `dynamic_import` | frame, operand, out_value | 1 | no (status) | 1 | 0 (carries 0) |

`module_export_cell`'s `$current` operand is an operand the helper has no
parameter for — and that is legal, because the file states there is no
operand-count rule and why: "an operation supplying FEWER arguments than the ABI
takes is the normal case", and the excess rule is on ATTRIBUTES only. `$current`
is not dropped: it becomes the out-slot's initialiser, which is a materialisation
the lowering performs, exactly as `ctjs.get_property`'s inline cache is.

**5 — `ctcompile/lib/CTJS/Import/BytecodeImport.cpp`: four cases.**

```cpp
case op::load_import:
    // b IS THE EXPORT NAME AND c IS THE SPECIFIER, which is the reverse of
    // the reading order and the only opcode that reads c as a standalone
    // index. bytecode.hpp: "a = the cell exported as names[b] by the module
    // at specifier names[c]".
    set(in.a, ctjs::ModuleImportCellOp::create(into, where, value_type,
                                              into.getStringAttr(proto.names[in.c]),
                                              into.getStringAttr(proto.names[in.b])));
    break;
case op::bind_export:
    // bx(), NOT b. b is the HIGH half and is 0 for every name index below
    // 65536, so a decoder that reads b alone exports name 0 under this
    // name.
    //
    // AND reg(in.a) IS A SOURCE HERE. The write is conditional on there
    // being a module, and the register holds the local being exported on
    // entry - so the operation answers `current` when there is none.
    set(in.a, ctjs::ModuleExportCellOp::create(into, where, value_type,
                                              into.getStringAttr(proto.names[in.bx()]),
                                              reg(in.a)));
    break;
case op::load_namespace:
    set(in.a, ctjs::ModuleNamespaceOp::create(into, where, value_type,
                                              into.getStringAttr(proto.names[in.b])));
    break;
case op::dyn_import:
    // b IS THE SPECIFIER VALUE, not a name index - this is the only module
    // opcode whose specifier is computed.
    set(in.a, ctjs::DynamicImportOp::create(into, where, value_type, reg(in.b)));
    break;
```

Every one needs its name index bounds-checked against `proto.names.size()` the
way `op::load_bigint` checks `proto.strings.size()`, or a malformed image indexes
out of range — `state.give_up` is the existing spelling.

**6 — `ctcompile/lib/CTJS/Lowering/CTJSToEmitC.cpp`: BOTH halves.**

* `body_is_supported`'s `mlir::isa<CompareOp, ...>` allow-list gains
  `ModuleImportCellOp, ModuleNamespaceOp, DynamicImportOp`, and
  `ModuleExportCellOp` gains a `runtime_defines` guard beside `LoadGlobalOp`'s
  until Step 3 lands. Adding one half without the other has happened four times
  and now reaches `llvm::report_fatal_error`, not `llvm_unreachable`, so it is
  loud in release — **but only if an operation actually arrives at `convert()`**,
  which is what Step 8's fixture is for.
* `convert()` gains four branches. Two are `ec::CallOpaqueOp` with the
  `c_string_literal(name)` plus `std::to_string(name.size())` pair `LoadGlobalOp`
  already uses — the length travels beside the text because a specifier is BYTES.
  `DynamicImportOp` is `status_call`. `ModuleExportCellOp` needs a seeded variant
  of `status_call`, below.
* The entry emitter needs no change.

**7 — a seeded `status_call`.** `status_call` creates its out-slot with
`ec::VariableOp ... ec::OpaqueAttr::get(ctx, "")`, which is an uninitialised
local. `bind_export` needs it to start at `$current`. The narrowest change is one
extra overload taking an optional initial `mlir::Value`; when present the
variable is emitted with that initialiser rather than an empty one. It is the
only caller, and the comment says so, so that a second one is a decision rather
than an accident.

**8 — the tests, in one commit with the code.**

* `ctcompile/test/ImporterCoverage.cpp` — delete the four `not_yet` rows. The
  ratchet fails until they go.
* `ctcompile/test/CTJS/Import/unsupported.mlir` — **must be repointed**. It uses
  `import(u)` — `op::dyn_import` — as its example of a genuinely unsupported
  opcode, and the file explains that it has already been rewritten twice for
  exactly this reason. The remaining honest choices are `op::yield_value` and
  `op::await_value` (Phase 14, and `ct_aot_suspend_unsupported` exists precisely
  because they have no tier-1 lowering) and `op::wrap_promise`, which is on the
  `not_yet` list as Phase 14's. `function* g() { yield 1; }` is the shape that
  will outlast this phase, and its skip reason is a different one, so the
  `CHECK-SAME` lines change with it. A generator's opcode is `may_suspend`, so it
  is not on `ImporterCoverage`'s missing list at all and the ratchet cannot make
  it stale.
* `ctcompile/tools/ctjs-translate/ctjs-translate.cpp` — a third registration,
  `--ctbrowser-module-to-ctjs`, three lines beside `--ctbrowser-js-to-ctjs`,
  differing only in `script_kind::module`. Without it no fixture in
  `ctcompile/test/` can contain a module opcode.
* `ctcompile/test/compile-js-to-cpp.cmake` — an optional `-DKIND=module` that
  selects the flag. Defaulting to the classic one keeps every existing caller
  unchanged.
* `ctcompile/test/ModuleDifferential.cpp` + `module-main.js` + `module-dep.js` —
  the new harness, below.
* `ctcompile/test/linkable.js` — **cannot carry these**, and that is worth saying
  out loud rather than discovering: it is compiled as a classic script, and a
  module opcode in it would be a syntax error. The link check for the four new
  symbols is the module harness's own translation unit, which links against
  `ctbrowser::script` exactly as `ctcompile-test-linkable` does.

## The differential fixture, as built

The harness could not be `Differential.cpp`, for the reason at the top of this
file: it compiles ONE classic script and runs it with `cx.run`. Three of the
four opcodes cannot appear in a classic script at all. So
`ctcompile/test/ModuleDifferential.cpp` is a new binary that builds a
THREE-MODULE GRAPH the way `browser.cpp` does — register the records, fill
`resolved`, instantiate every module, then evaluate in dependency order — and
installs a compiled entry on the importing module's TOP LEVEL. That is the
first time the backend has been run over `functions[0]` rather than over a
named function.

Two things about the shape are load-bearing rather than convenient.

**`module-dep.js` and `module-user.js` stay interpreted in every arm.** dep is
where the live binding is written; user is the only thing in the world that can
see what `op::bind_export` decided. Reading `mine` from inside `module-main.js`
proves its local works whatever cell it holds — what bind_export decides is
whether the cell in main's RECORD is that same box, and only a second module
taking that box can tell.

**`resolved` is deliberately not the identity map.** `module-main.js` writes
`./dep.js` and the registry is keyed by `dep`, so a lowering that skipped the
resolution step raises ``module `./dep.js` was not loaded`` instead of
answering. A one-directory fixture produces an identity map by accident, and
under one the mistake is invisible.

```js
// module-dep.js — the exporter
export let count = 1;
export const tag = "D";
export default 7;
export function bump() { count = count + 1; }
```

```js
// module-main.js — the compiled body, abridged
import def, { count, tag, bump } from "./dep.js";
import * as ns from "./dep.js";
import * as ns2 from "./dep.js";
export let mine = 10;
export function raise2() { mine = mine + 2; }
function loadTwo() {
  LOADER_SAW = "";
  var found = import("./dep.js");
  var missing = import("./nowhere.js");
  return LOADER_SAW + "/" + typeof found + "/" + typeof missing + "/" + (found === missing);
}
```

```js
// module-user.js — the only view of what bind_export decided
import { mine, raise2 } from "./main.js";
READ_MINE = function () { raise2(); return "" + mine; };
```

| arm | anchor | what it separates |
|---|---|---|
| named import | `2/D/7` | a CELL from a copied value — `bump()` writes dep's own local, and a copy answers 1 — and the b/c swap, which raises rather than answering |
| namespace | `2/D/7/true` | the accessor over the cell from a snapshot, and the per-record identity cache: a namespace built per opcode answers `false` for `ns === ns2` |
| one record twice | `3/3/true` | whether the named import and the namespace came from the SAME record |
| dynamic import | `main>./dep.js,main>./nowhere.js/object/object/false` | the REFERRER, which no operand carries; and a missing module answering an already-rejected promise rather than a failure |
| own export | `12` | the adopted cell being writable from inside the module |
| adopted cell | `12` | publishing the register's own cell against adopting the record's — read from `module-user.js` |
| no module | `12` | the CONDITIONAL write, which is what `$current` is for |

Every arm runs twice: once with only the top level patched, and once with every
entry the build produced, which is `Differential.cpp`'s third mode and exists
for its reason — a compiled caller reaching a compiled callee goes through paths
a single patch never exercises. That second pass is not decoration here: see the
referrer falsification below, which is invisible in the first.

**The referrer is only observable because the test loader records it.** Nothing
in the tree stamps `function_proto::module` except `browser::instantiate_module`,
so the harness stamps it too, exactly as the browser does. Without that every
referrer is the empty string and the arm would be checking nothing.

## What was falsified, and what each mutation did

Each mutation was applied, rebuilt on the devbox, and the binary run. Every one
asserts its own edit landed — a falsification that silently edits nothing looks
exactly like a guard that fired.

| mutation | where | result |
|---|---|---|
| fill `ctjs.module_import_cell` in READING order — specifier from `b`, export name from `c` | `BytecodeImport.cpp` | **RED, all 12 checks**: ``module `default` was not loaded`` |
| read `in.b` instead of `in.bx()` for `bind_export` | `BytecodeImport.cpp` | **RED on 3**: `own export` both modes, and `adopted cell` answers `undefined` — the predicted symptom exactly. `no module` stays green, correctly: with no record the name is irrelevant |
| hand `ct_aot_module_namespace` `specifier.size() - 1` | `CTJSToEmitC.cpp` | **RED, all 12**: ``module `./dep.j` was not loaded`` |
| drop the referrer the bridge reads off the frame | `aot_bridge.cpp` | **RED on 1**: `dynamic import` in the ALL-entries mode only — `>./dep.js` against `main>./dep.js` |
| stop seeding `ct_aot_module_export_cell`'s out-slot with `$current` | `CTJSToEmitC.cpp` | **RED on 1**: `no module` answers `undefined` |

**The last two are the interesting ones.** The referrer mutation is invisible
with only the top level patched, because `loadTwo` — the function containing
`import()` — is interpreted there; it takes the ALL-entries pass to see it. And
the seed mutation is caught by the `no module` arm and by nothing else, which is
the entire justification for that arm existing.

## What could not be verified

**`CT_AOT_NO_WRITE` does not exist and never did.** `ct_aot_status` has four
members — `unwound`, `failed`, `caught`, `ok`. The row that named it was the
only occurrence in the tree besides its own second sentence. This is
`context::callee_type_error` again: cited by a row, never anywhere, found by
looking for it. The row now says so, and says why adding a fifth status would
not have been one enumerator: `status_call` compares against `ok` and branches
to the shared failure path on everything else, so a compiled body would RETURN
on the ordinary no-module path.

**Every line citation in three of the four rows was rotted, and
`ctcompile_def_citations` passed the whole time.** It only catches a citation
past the end of the file it names, and its regex requires a `basename.ext:NNN`
form — so a bare `:1287` is not checked at all and a range's second number never
is. What each pointed at when this work started:

| citation | what was actually there | what it meant |
|---|---|---|
| `run_loop.cpp:1270-1292` (`module_export_cell`) | the `vm_done:` label, the `#pragma` pop and the closing namespace — 1292 is **past the end** of a 1285-line file | `VM_CASE(bind_export)`, ~330 lines earlier |
| `:1287`, `:1297` | **past the end of the file** | inside `VM_CASE(bind_export)` and `VM_CASE(load_import)` |
| `:1284` | a blank line | inside `VM_CASE(bind_export)` |
| `instantiate_module (call.cpp:110-119)` | the middle of `context::call` — the `enter_compiled` dispatch and the 512-frame guard | `context::instantiate_module`, at 154 |
| `context::run_module (call.cpp:151-162)` | `instantiate_module`'s comment tail and its whole body | `context::run_module`, at 197 |
| `module_namespace (call.cpp:137-146)` | the tail of `context::call` | the accessor loop, at ~180-192 |
| `call.cpp:134-135` | `pending_new_target_` being cleared in `context::call` | `of.namespace_object = value::object(ns);` at 177 |
| `run_reentrant, call.cpp:169-200` | the tail of `module_namespace`'s comment and the head of `run_module` | `context::run_reentrant`, at 217 |
| `browser.cpp:975` / `:976` | nowhere near the `fn.module` stamp | `browser::instantiate_module`, at 1165 |
| `resolve_specifier (browser.cpp:917-918)` | not `resolve_specifier` | at 1106 |
| `Shell/browser.cpp:1216-1239` | inside `browser::instantiate_module`'s recursion | the `set_module_loader` lambda, 1405-1429 |
| `browser.cpp:1220-1226`, `:1231-1233` | the same recursion | the rejected-promise arms and the re-`find`, ~1409-1426 |
| `vm.hpp:1009` (`current_module_` "private with no accessor") | `context::collect_if_due` | `current_module_` is at 1436 — and `aot_bridge` is a `friend`, so the worry was moot |
| `frames.cpp:89` | the `imports` de-duplication `if` | `decode_string_literal(n.text)` at 88 |
| `frames.cpp:136-139` | the `hoisted < 0` guard | the `exported` ternary at 131-133 |
| `"GC root 12"` for `namespace_object` | `GCRoots.def` lists `module_exports` twelfth | `module_namespace` is **thirteenth** |

All of them are now cited by NAME, which is what the `.def`'s own header
prescribes and what the previous three batches did.

**Two citations in those rows were correct**, and that is recorded because the
rot was not uniform: `coerce.cpp:78` really is `context::to_string`,
`coerce.cpp:171` really is `to_primitive_string`, `172-177` really is its
`lookup_property`-then-`call` pair, `core/containers.hpp:30` really is the
`flat_map` alias, and `compile/frames.cpp:143-149` really is the boxing block.

**`bytecode_opcodes.def`'s module rows were worse, and nothing checks them at
all.** They cited SIX bare line numbers with no filename — `1405`, `1395`,
`1471`, `1459`, `1433`, `1309` — every one past the end of the 1285-line
`run_loop.cpp`. The handlers they meant were at 964, 954, 1030, 1018, 992 and
868. They also cited `call.cpp:111-119` for `instantiate_module` (154),
`call.cpp:133-135` for the namespace identity cache (175-178), `call.cpp:704`
for `context::resume` (947) and `Shell/browser.cpp:1216-1240` for the loader
(1405-1429). The bare-number form is invisible to `check-def-citations.cmake` by
construction, since its regex begins with a basename. **That is a gap the
checker could close** — a bare `:NNN` inside a `DELEGATES TO` paragraph is
findable — and this work did not close it.

**`unsupported.mlir`'s `CHECK-NOT` is far narrower than its comment claimed.**
A `CHECK-NOT` before the first `CHECK` constrains only the text before that
`CHECK` matches, and `ctjs.skipped` is a MODULE ATTRIBUTE that matches on line
one — so it covered the module header and nothing else. The top level IS
imported and prints below it, in the old fixture and the new one alike. The file
now says what it actually checks. It was repointed at `op::yield_value`, chosen
because the ABI has no suspend helper at all and because a `may_suspend` opcode
is invisible to `ImporterCoverage`'s ratchet, so nothing can make it stale by
accident. When Phase 14 lands a suspend ABI it needs a new example again.

**A stale comment in `compile/frames.cpp` that is not this track's to fix.**
`bind_imports` says of `spec.c == 2`: "A namespace import wants the whole module
object, which is stage 4 work — it is refused by name rather than bound to
nothing." The next fifteen lines implement it, including the boxed arm.
`ctbrowser/lib/Script/compile/` belongs to another track.

**`aot_contract.cpp` says "sixty-eight `extern "C"` prototypes"** and
`aot_helpers.def` has 69 `CT_AOT_HELPER` rows and 84 `CT_AOT_COVERS` rows. The
prose count is stale; the derived one is right. This work added no rows, so it
did not make it worse.

**Not verified: any of this under the collector.** `ctcompile_gc_roots` runs one
body with `set_gc_stress` on, and no module fixture was added to it.
`ct_aot_module_namespace` and `ct_aot_module_export_cell` are both safepoints
that allocate — the namespace object plus one `native_object` per export, and
the fallback cell — and the arguments for why each is safe are structural
(`module_record::namespace_object` is a GC root, and the cell goes straight into
`exports`, which is another). Structural arguments are what the stress test
exists to check, and this did not check them.

**Not verified: a cycle.** `instantiate_module` exists for cycles and the plan's
Phase 16 asks explicitly for "a cycle where one module reads an imported binding
before the exporter has evaluated it". No arm here is cyclic. The graph is
`user -> main -> dep`, acyclic, and the compiled body is `main`.

**Not verified: a re-export chain, or `export * from`.** `browser::wire_reexports`
is a loader-side mechanism that never reaches these four opcodes, but Phase 16's
gate names reexports and nothing here covers them.

**Not verified: anything about a real page.** No corpus is an ES module — all
four vendored bundles are UMD, one file each — so the corpus counts are
UNCHANGED by construction and were measured to confirm it rather than assumed:

```
                    compiled / total     before this work
  bootstrap            570 / 574          570 / 574
  p5                  4410 / 4754         4410 / 4754
  phaser              7702 / 7725         7702 / 7725
```

The baseline row was taken by stashing this work, rebuilding `ctjs-translate`
and re-measuring, rather than by copying a number out of an earlier commit
message.

**Not verified: `dyn_import` reached from a compiled body whose module is being
loaded re-entrantly.** The test loader is synchronous and settles immediately;
it never calls back into `run_reentrant`, so the "whole module graph" half of
`ct_aot_dynamic_import`'s two re-entries is exercised by nothing. What IS
exercised is `to_string` on the specifier, the referrer, the status edge, and
the rejected-promise arm.
