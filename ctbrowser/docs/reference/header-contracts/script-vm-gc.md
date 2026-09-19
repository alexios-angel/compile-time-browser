# Script Vm Gc contracts

<a id="contract-1"></a>

`class rooted {`

A VALUE THE COLLECTOR CAN SEE, FOR AS LONG AS A C++ SCOPE HOLDS IT.

The precise collector walks exactly the roots of each_root, and a value
in a C++ local is in none of them. `construct` allocates the instance,
then runs field initialisers, then calls the constructor body, with the
instance in a C++ local across both - under gc_stress that is a
use-after-free. A STACK OF TEMPORARIES rather than another special-cased
field like `current_this_` and `pending_new_target_`.

<a id="contract-2"></a>

`class rooted_values {`

THE SAME THING FOR A WHOLE RANGE - a std::vector<value> a builtin is
holding across a call back into the VM.

`Array.prototype.sort` is why. It snapshots the array so a comparator
cannot make the merge index out of bounds, and that snapshot is a bare
std::vector<value>: a comparator that empties the array leaves the
snapshot holding the only reference to every element that is not
currently an argument, and a collection there frees them under the merge
that is still reading them. One `rooted` per element cannot be spelled -
`rooted` is deliberately immovable, so it does not go in a container.

A COPY AT CONSTRUCTION IS SUFFICIENT AND EXACT for a sort, because the
work vector is PERMUTED and never gains a value the snapshot did not
have. Anything that can grow its range mid-call wants a heap object it
can root once instead.

<a id="contract-3"></a>

`class native_scope {`

WHAT A NATIVE IN PROGRESS MAY BE HOLDING. A native allocates an object,
calls back into JavaScript (`to_string` on an argument, a callback, a
getter) and then uses the object - `new Blob([part])` stringifies each
part with the Blob it is building in a C++ local. The precise collector
has no inventory of C++ locals, so every such native was a use-after-free
once collection could run inside a turn; the first one found was
`loadBlob`'s result no longer being `instanceof Blob`. Rather than audit
~1,300 natives for `rooted`, the collector PINS EVERYTHING ALLOCATED
SINCE THE OUTERMOST NATIVE WAS ENTERED: the heap is a newest-first list,
so that is the prefix down to the object that was the head at entry.
Bounded, because a native's own allocations are bounded; what is NOT
pinned is what testharness.js's per-subtest `apply` was called on, so a
synchronous script of ten thousand subtests still collects. A collector
policy, not a root: it is applied in collect(), outside each_root, so the
escape oracle's notion of "reachable" does not learn it.

<a id="contract-4"></a>

`void record_step(instruction in);`

--- the type oracle ----------------------------------------------------

ANOTHER TEST MODE, and the same shape as the one above: off by default,
one predictable not-taken branch when it is off, and enormously more
expensive when it is on. A context picks up whatever
`set_active_type_recorder` last installed when it is CONSTRUCTED, which
is what lets a whole page be recorded - a `shell::browser` builds its own
context and never hands it out.

ONE INTERPRETER STEP, called from the dispatch loop and defined in
type_record.cpp. Public only because the macro in run_loop.cpp is
clearer than a friend declaration; nothing else should call it. The
escape oracle's four hooks are declared with the collector's root walk
further down, after call_frame.

<a id="contract-5"></a>

`enum class resume_mode {`

A SUSPENDED FRAME, saved whole.

`await` on a pending promise cannot block - there is one stack and the
event loop is above it - so the frame is lifted out of the register stack
and put here, the caller is handed a promise, and the frame goes back when
the awaited promise settles.

Only the TOP frame can suspend, which is sufficient: `registers_` is one
flat vector indexed by base, so lifting a frame out from the middle would
move every frame above it. And there is never a reason to - every frame
below is either already suspended or a synchronous caller that must
itself unwind before it can wait for anything.

The register window is COPIED rather than referenced. It has to be: the
stack is truncated the moment the frame leaves, and whatever runs next
reuses those slots.
What `.next(v)` / `.throw(e)` / `.return(v)` do to a generator. Declared
ahead of coroutine_object because an async generator queues them.

<a id="contract-6"></a>

`bool generator = false;`

--- generators ------------------------------------------------
A generator is the SAME suspended frame with a different resumer: an
explicit `.next()` rather than a settling promise. `started` exists
because the first `.next(v)` must NOT deliver v anywhere - there is
no yield waiting for it yet - and `done` because a finished generator
must keep answering `{value: undefined, done: true}` for ever rather
than running its body again.

<a id="contract-7"></a>

`bool async_gen = false;`

--- async generators ------------------------------------------
`async function*`: the SAME frame again, resumed by `.next()` and
suspended by BOTH `yield` and `await`. Each `.next(v)` hands back a
promise of the `{value, done}` record and joins a queue
(AsyncGeneratorEnqueue, 27.6.3.5); requests run one at a time, the
next one starting when the body yields or finishes. `awaiting` is
the body parked on an `await` between two requests - `promise` is
then the request that the eventual yield settles - and `self` is
the generator object, which the drain needs and the frame does not
carry.

<a id="contract-8"></a>

`std::uint8_t return_pending = 0;`

A `.return(v)` whose v is being AWAITED (27.6.3.8 / 27.6.3.9)
before the body sees it: 1 = the frame is at a yield and resumes
with a return completion of the awaited value, 2 = the generator
is finished and the request settles with it directly.

<a id="contract-9"></a>

`value delegate;`

THE ITERATOR RECORD OF A `yield*` IN PROGRESS (see yield_delegate_open_name),
undefined otherwise. While it is set, `.next()` on a sync generator
hands the inner result object out as it is, and `.throw()` /
`.return()` are forwarded to the inner iterator by generator_resume
instead of resuming the frame (27.5.3.7 / 14.4.14).

<a id="contract-10"></a>

`void await_for(coroutine_object * saved, value v);`

27.7.5.3 Await steps 1-2 for a frame ALREADY parked: PromiseResolve
(%Promise%, v), then resume() when it settles - from the promise's
handler list, or from a job when it already has. A `constructor`
getter that throws rejects on the spot, as PromiseResolve's `?` says.

<a id="contract-11"></a>

`void suspend_frame(coroutine_object * saved, std::uint16_t await_reg);`

LIFT THE TOP FRAME INTO A COROUTINE (await and yield are the same
suspension): its register window is copied out, its handlers travel
with it with reg_top made RELATIVE - it comes back somewhere else in
the stack - and the frame is popped. `await_reg` is where the resuming
value lands.

<a id="contract-12"></a>

`[[nodiscard]] value generator_resume(value generator, value sent, resume_mode how);`

What `.next(v)` / `.throw(e)` / `.return(v)` do. Runs the body until it
yields or finishes, and answers the `{value, done}` record the iterator
protocol is made of. For an ASYNC generator the body may also park on an
`await`, in which case the answer is undefined and `awaiting` is set:
resume() finishes that request when the awaited promise settles.

<a id="contract-13"></a>

`void settle_async_generator(coroutine_object * saved, value outcome, bool raw_return);`

What one request's outcome does to its promise: a rejected settled
promise rejects it (the body threw - see the compiler's async fence),
anything else fulfils it with `{value, done}`. `raw_return` says the
outcome is the body's return value, still to be wrapped in a done record.

<a id="contract-14"></a>

`struct upvalue_source {`

WHERE THE PARENT'S HALF OF AN UPVALUE COMES FROM, and the two tiers
genuinely differ - which is why this is a parameter and not an
assumption.

A descriptor marked from_parent_local names a REGISTER of the enclosing
frame. The interpreter has that window and indexes it directly. A
compiled body does not: its registers are the backend's own slots and its
numbering is not the bytecode's, so the ABI has it pass an array indexed
IN PARALLEL with the descriptors instead - ct_aot_make_closure's row
spells that out, and packed is the plausible wrong reading.

Exactly one pointer is set. Passing both, or neither, is a caller bug.

<a id="contract-15"></a>

`const std::uint64_t * by_descriptor = nullptr;`

RAW BITS, because that is what the ABI passes and a value is not
layout-punnable: script::value keeps its bits private and offers
from_bits/bits() as the only route in and out. Reinterpreting an
array of one as an array of the other would be exactly the type
punning this project refuses, and copying it would allocate once per
closure a compiled body builds.

<a id="contract-16"></a>

`std::uint32_t descriptor_count = 0;`

HOW LONG by_descriptor IS, and only that form has a length to state.
The register window is the enclosing frame's and is as long as that
frame; the parallel array is the CALLER's, and reading past its end
is a closure that captured whatever was next in memory.

<a id="contract-17"></a>

`[[nodiscard]] value make_closure(closure_object * enclosing, std::uint32_t function_index,`

ONE COPY OF op::closure's BODY, shared with the compiled tier.

Factored so run_loop and ct_aot_make_closure cannot drift, which is the
.def's instruction for this row. It also GUARDS three things the inline
version dereferences unguarded, because a compiled caller can reach it in
configurations the interpreter cannot: no program with no enclosing
closure, a function index past the end, and an upvalue count that
disagrees with the target's. Each raises; the row is raise-tier only.

<a id="contract-18"></a>

`value invoke(value callable, std::span<const value> args, value this_value, bool constructing);`

THE AOT BRIDGE REACHES IN HERE - ct_aot_enter pushes a real call_frame,
ct_aot_leave truncates handlers_ exactly as op::ret does, and
ct_aot_check classifies against frames_ and failed_. See
lib/Script/aot_bridge/.
The one implementation of "enter this callable", which `call` and
`construct` are the two public spellings of. Private because
`constructing` is not a thing an embedder should be choosing.

<a id="contract-19"></a>

`friend class executing_as;`

The dispatch layer maintains `executing_` and reads it to attribute a
transition. It is not a second implementation of anything - it is the
ONE place that decides interpreted or native, and these are the two
members it needs.

<a id="contract-20"></a>

`std::uint16_t landed_slot = 0;`

WHERE THE THROWN VALUE WAS JUST PUT, recorded by unwind_to_handler at
the moment it writes, for compiled frames: a compiled body has no
bytecode to resume into, so it asks ct_aot_catch_land where the value
went - and the handler that knew has already been POPPED by the
search. The interpreter's catch block has the register baked into
the instruction at `address`.

<a id="contract-21"></a>

`value new_target = value::undefined();`

`new.target`: the constructor this frame was entered with, or
undefined for an ordinary call. It is a VALUE rather than a flag
because it PROPAGATES - a base constructor reached through super()
reports the derived class `new` was written against, not itself.

<a id="contract-22"></a>

`coroutine_object * generator = nullptr;`

WHICH GENERATOR THIS FRAME IS THE BODY OF, or null for an ordinary
call. `yield` needs it to know where to save itself, and it cannot be
found any other way: the coroutine is reached from the generator
object, not from the frame.

<a id="contract-23"></a>

`value arguments_object = value::undefined();`

The `arguments` object, when this body built one.

Kept on the FRAME as well as in a register because it is built before
the parameter prologue - it has to be, or a default or a destructured
pattern has already overwritten the register it would read - and
building it claims a register that an EXTRA argument may be sitting
in. Anything after that point which still needs the raw arguments,
which is the rest parameter, reads them from here instead.

<a id="contract-24"></a>

`value async_promise = value::undefined();`

The promise this frame's eventual return settles, once it has
suspended at least once. Undefined on a frame that has not - a
function that never awaits anything pending returns normally and
needs no promise of its own beyond the one `wrap_promise` makes.

<a id="contract-25"></a>

`std::uint64_t serial = 0;`

THE ESCAPE ORACLE'S FRAME IDENTITY. Zero until this frame's first
tracked allocation, when note_allocation assigns the next serial.
Keyed on a serial rather than on `base` or `proto` because neither
is unique across a run.

TRAILING, WITH A DEFAULT, AND IT MUST STAY THAT WAY. Five sites
build a call_frame with a positional aggregate initializer that
lists the first eight members (run_loop.cpp's VM_CASE(call) and
VM_CASE(construct); vm/call/'s invoke, run_module and execute);
a member added anywhere but the end shifts every one of them and
compiles cleanly.
