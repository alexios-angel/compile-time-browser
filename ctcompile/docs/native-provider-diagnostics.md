# Provider diagnostics and callback effects

The optional host-prefix analysis now follows Bootstrap's conflict diagnostic
through its private key snapshot, message construction and installed recorder
callback. The unchanged CommonJS, browser and script-this-fallback programs
advance from **13 to 20 resolved entry calls** and from **11 to 18 completed
provider summaries**. All reusable methods and callbacks remain runtime code.

Two options keep the increments independently selectable:

```text
follow-publication=true follow-provider-reads=true follow-provider-mutations=true
follow-provider-diagnostics=true follow-provider-callbacks=true
```

Diagnostics require mutation following; callbacks additionally require
diagnostics. Both new options default to false. With diagnostics alone, the
exact probes still complete eleven summaries and stop at the actual callback
call, after proving its arguments. An incomplete attempt publishes no new
provider, snapshot or callback summary.

## Measured Bootstrap increment

Each mode preserves its seven source functions, one factory Map, three capture
edges, one publication and two distinct nested Map allocations. The UMD prefix
still selects 2/5/6 branches respectively. Eighteen summaries record **52 reads**
(including the three snapshot operations), **five sets**, **three deletes**,
and **one callback with two global writes**. One delete is unsuccessful.
The recorder increments `traceErrorCount` and sets `traceErrorMessage` to 1
after checking this exact message:

```text
Bootstrap doesn't allow more than one instance per element. Bound instance: bs.alert.
```

The source snippets and their program hashes are unchanged. The vendor
fragment is 1,101 bytes, SHA-256
`3eebf5b59691eff3c32478e06fb44a5c9eb554bc045d2ff2e20901813a9f99c8`.
All **19/19/24 observations** agree under Node, the interpreter and compiled
boxed script/wrapper execution, including GC stress.

Native admission remains **0/7 in every mode**. Analysis now stops at the later
`set(element, "bs.collapse", instance)` ordinary object payload. This increment
does not establish ownership or call proofs for arbitrary exported users.

## Proof and transaction boundaries

`ProviderDiagnostics` copies current prefix globals alongside a disposable copy
of the provider Maps. Ordinary source-global and own-field lookups use those
actual identities; the names `console` and `error` confer no effect authority.
Standard Map and Array identities still require the fingerprinted host contract
and existing source replacement/escape checks.

A private Map.keys iterator must be consumed once by Array.from in the same
block, with no intervening mutation or callback. The snapshot owns independent,
insertion-ordered key records. Replacement, deletion and reinsertion preserve
Map order. Only an in-range integer index selecting a string key is followed.
Concatenation requires two proved strings. Snapshot escape, mutation, reuse,
customized iteration and object coercion withhold a normal-result proof.

`ProviderCallbacks` resolves the actual source closure for each invocation.
It requires a capture-free body, exact primitive arguments and unused implicit
receiver, new.target and callee parameters. The bounded path reader supports
constants, primitive equality, numeric add/subtract/multiply/divide, boolean
control, selected structured branches and initialized scalar global slots.
Numeric arithmetic uses binary64 APFloat with round-to-nearest-even.

A write requires an already-known primitive destination disjoint from declared
host roots, intrinsic bindings and publication slots. Its value must also be
primitive. Calls, reentry, property effects, allocations, captures, identity
inspection, throws and unsupported control stop the attempt. Differential
callbacks use `var` globals; this work does not repair the existing frontend's
missing top-level lexical-const assignment checks.

Reads after writes see current transaction values. Subsequent invocations use
fresh call contexts and updated state. Maps, scalar globals and reports commit
together only after the enclosing provider method returns normally and passes
resource retention. Later failure discards tentative callback writes and Map
changes. Budgets charge traversal, snapshot copying, text and global-name work.
Exhaustion prevents specialization; diagnostic annotations are never proof inputs.

No callback body or provider operation is folded away. Entry observation
branches after provider summaries remain runtime too. Reports list callback
targets, source call/write ordinals, results and writes under the enclosing
provider invocation. Internal callbacks do not inflate resolved entry-call counts.

## Validation and implementation

The final devbox gate passes **464/464 CTests**, including **152/152 lit cases**,
in **547.27 seconds**. All **557 C++ files** pass formatting and whitespace
checks pass. Default and disabled optimization coverage retains Bootstrap
**19/574**, p5 **39/4754** and Phaser **45/7725**.

The three exact callback differentials and ProviderState unit test pass. The
28-case source regression covers insertion order, full messages, repeated/replaced
callbacks, reads after writes, current post-state selection, effect refusals,
stale/forged contracts and exhausted transactions.

Five refusal-only cases record existing interpreter differences separately:
fractional array indexing, eager Map.keys mutation/reuse/override behavior,
and the generic Map object string tag. Supported snapshots and callbacks
require exact Node/interpreter agreement. Throwing controls use Node exception
observations; the reference printer does not expose globals after a throw.

| Area | Implementation |
|---|---|
| Ordered snapshot storage and atomic budget failure | `lib/CTNative/HostContract/ProviderState.{h,cpp}` |
| Checked lookups, iterator consumption and messages | `lib/CTNative/HostContract/ProviderDiagnostics.{h,cpp}` |
| Primitive callback paths and global effects | `lib/CTNative/HostContract/ProviderCallbacks.{h,cpp}` |
| Enclosing transaction commit | `lib/CTNative/HostContract/PrefixMutations.cpp` |
| Source regression | `test/CTNative/host-provider-diagnostics.{py,test}` |
| Exact Bootstrap oracle and boxed differential | `tools/check/bootstrap-host-prefix.py`, `test/cmake/BootstrapData.cmake` |

See [the next provider proof](bootstrap-provider-next.md) for the ordinary
object payload and native export boundaries that remain.
