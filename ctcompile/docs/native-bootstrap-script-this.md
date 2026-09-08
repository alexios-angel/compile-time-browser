# Bootstrap script receiver

The exact Bootstrap host audit needs a real classic-script receiver when the
`globalThis` binding is undefined. The wrapper then chooses the object passed
as top-level `this`, publishes `bootstrap` there, and leaves a separate `self`
object unchanged. The previous reference entered every program with an
undefined receiver and therefore selected `self`.

## Runtime representation

[`context`](../../ctbrowser/include/ctbrowser/script/vm.hpp) owns a stable
`global_this_` JavaScript value. A bare context creates a global view whose
ordinary reads, writes and `in` checks forward to the existing `globals_`
binding table. There is one storage location for a supported global value:
`var answer = 42`, `this.answer`, and an assignment through the view share it.
The view also reaches the ordinary object prototype and any own properties
defined on its target, matching the existing Shell Window view model.

The writable `globalThis` binding initially points to this value. Replacing
that binding does not replace the realm receiver. Shell explicitly calls
`set_global_this(window_view)` before scripts run, so a browser's classic
script receives the same Window proxy as `window` and `self`. The embedding
hook selects the host identity; no lookup of either alias selects it later.

JavaScript `this` is a heap value here, not the C++ `context *` used to access
VM services. A future native method can use a C++ class receiver when its
typed instance and call convention are proved. Script entry still needs an
explicit realm value, and arrows still inherit their lexical receiver.

[`execute` and `run_reentrant`](../../ctbrowser/lib/Script/vm/call/run.cpp) select
the receiver from the program's recorded script kind:

| Entry | Receiver |
|---|---|
| Classic script, including a strict classic script | The realm's stable global value |
| ES module, including evaluation inside another frame | `undefined` |

Both interpreted frames and compiled entry calls receive that same value.
The AOT ABI passes it explicitly; `ct_aot_this` and bytecode `load_this` share
`effective_this`, including its existing arrow rule. Neither path substitutes
the current value of the `globalThis` binding.

The realm value is visited under the existing `globals` GC root category,
independently of the binding table. It therefore survives between turns even
after `globalThis` and every captured alias have been overwritten. A retained
arrow also keeps its captured receiver through the existing closure trace.

## Environment boundaries

This change establishes script receiver identity and entry behavior. It keeps
the existing own-data global environment contract:

- Classic `let` and `const` still use the shared global table. A separate
  global declarative environment, lexical redeclaration checks, and complete
  declaration attributes remain outside this implementation.
- The global view forwards ordinary data reads, writes and `in`. Reflection
  and deletion still follow the existing Proxy target behavior; they do not
  provide a complete reflected view of the global binding table.
- An accessor installed on the view's target is a property operation. Bare
  global operations remain data-table operations and do not acquire getter
  or setter effects. In particular, the pure, nonreentrant AOT global helpers
  have not silently gained JavaScript callbacks.
- Bare function calls retain the runtime's existing lack of sloppy-mode
  global substitution and primitive receiver boxing. Strict calls and arrow
  receivers remain distinct from classic-script entry.

These limitations are not native admission permissions. Host accessors,
unknown mutations, and native callable escape still need their separate
proofs. Passing all host oracle scenarios does not establish that Bootstrap
has been admitted to native lowering.

The opt-in [host prefix proof](native-host-prefix.md) can now use an explicit
embedding promise for this classic-script receiver and a finite writable
own-data publication slot. It follows Bootstrap's fallback argument even after
`globalThis` is undefined, while leaving ordinary-call effective `this`
unknown. The selected factory and publication remain runtime operations;
this advances a wrapper proof without admitting the exact Data probe natively.

## Verification

[`script_this`](../../ctbrowser/unittests/unit/script_this.cpp) checks default
identity, shared data bindings, overwritten aliases, strict-script and module
scope, lexical arrows, separate contexts, and receiver lifetime after all
visible aliases and the previous register window have been removed. Its
collection checks assert that collections actually ran. The entry tests cover
direct and reentrant execution with interpreted and compiled bodies, checking
both the AOT receiver argument and the `ct_aot_this` result.

[`dom_interfaces`](../../ctbrowser/unittests/unit/dom_interfaces.cpp) checks
that Shell supplies its Window identity and that changing `globalThis` does
not change a script's receiver. The six standalone script/module fixtures
agree with Node. Five focused devbox runtime tests pass. The unchanged 11-scenario
[host audit](native-bootstrap-host-contract.md#source-derived-host-audit-and-known-differences)
now passes **11/11** in both Node and ctbrowser with no mismatches. Its negative
control still rejects the exact `1234` to `1235` observation mutation. The audit
retains its exact vendor fragment and expected observations.
