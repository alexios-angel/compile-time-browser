# Script Vm Frames contracts

<a id="contract-1"></a>

`[[nodiscard]] value make_arguments_object(call_frame & fr, const value * slots,`

`arguments` AND A REST PARAMETER, which are one problem: both need the
arguments PAST the declared parameters, and both take the window and the
count as arguments rather than reading the frame - because a compiled
frame's own `base` is above the caller's window, not at it.

THE SIDE-SLOT WRITE IS PART OF make_arguments_object, not of its caller.
It is the half a fusion of these two would drop: gather_rest READS
call_frame::arguments_object on one of its two paths, because building
the arguments object claims a register an extra argument may be sitting
in, so after it runs the frame's copy is the one that still has them.

<a id="contract-2"></a>

`void run_field_initialisers(value constructor, value self);`

Run the `__fields` initialiser of `constructor` and of every class it
extends, BASE FIRST, against a freshly made instance. The chain is walked
here rather than threaded through the compiler because the compiler does
not know what `extends` will evaluate to.

<a id="contract-3"></a>

`void new_callee_type_error(const function_proto & fn, std::string_view origin, value callee);`

THE `new` FORM OF THE CALLEE TYPE ERROR, factored so the two tiers
cannot spell it differently.

`origin` IS THE BACKWARDS SCAN, PASSED IN. callee_origin walks emitted
bytecode from an ip and a register index, and an AOT frame has neither,
so the interpreter supplies the scan's result and a compiled frame
supplies nothing - which describe_callee renders as "the value".

<a id="contract-4"></a>

`[[nodiscard]] std::vector<value> spread_arguments(value arg_array);`

THE SPREAD FORMS OF A CALL AND A `new`, which are one VM_CASE because
they differ only in which member they end in. THE ARGUMENTS ARRIVED AS AN
ARRAY because their count was not known until the spread was evaluated -
so there is no argc and no contiguous window. A non-array yields NO
arguments rather than one, which is the interpreter's behaviour and is
why the unpack is shared rather than written twice.

<a id="contract-5"></a>

`[[nodiscard]] value construct_new(value callee, std::span<const value> args,`

op::construct's OWN DISPATCH, AS A VALUE - a different function from
`construct` above rather than a wrapper round it, because VM_CASE
(construct) ends in a frame PUSH and computes nothing. Every branch is
the same member the interpreter calls. `from` is the function the `new`
was written in, used only to name it in the TypeError.

<a id="contract-6"></a>

`[[nodiscard]] value run_loop(std::size_t stop_depth);`

THE DISPATCH LOOP, IN TWO INSTANTIATIONS - see run_loop.cpp. `Record`
is the type oracle's hook and it is `if constexpr`: a per-instruction
`if (recorder_)` measured +0.48% on phaser_invaders, and two
instantiations cost ~15 KB of COLD object code instead.

<a id="contract-7"></a>

`std::vector<heap_object *> mark_worklist_;`

THE GREY SET, and why the mark phase is a loop rather than a recursion:
a recursive mark needs a C++ stack as deep as the object graph is LONG,
and `head = { next: head }` 200,000 times is a plain page - a list, a
parser's node chain - that segfaults an 8 MiB stack.

Tri-colour in the classic sense: `marked` is the black/grey bit, set the
moment an object is discovered so a cycle terminates, and this vector is
the grey set waiting to have its edges walked. `push_mark` greys;
`trace_object` blackens one object by greying its children. Held as a
MEMBER rather than a local so its capacity survives between collections -
a long-lived page collects many times and would otherwise regrow it every
time.

<a id="contract-8"></a>

`void note_allocation(heap_object * p);`

--- THE ESCAPE ORACLE'S HOOKS - type_record.cpp ------------------------

Beside record_step in spirit; declared here because two of them take a
call_frame, which the class has not declared at that point. All four are
reached only through `recorder_ != nullptr` and under the Record
instantiation of the dispatch loop, so a shipped build runs none of them.

<a id="contract-9"></a>

`executing_kind executing_ = executing_kind::cxx;`

WHAT IS RUNNING RIGHT NOW - the interpreter, a compiled body, or C++.

The half of a mixed-mode transition that cannot be read off the call
site: `enter_compiled` is reached from `op::call` and from `context::call`
alike, and only this says whether the code doing the calling was itself
compiled. Maintained by `executing_as`, which is one byte saved and
restored on the C++ stack of whatever entered a body.

<a id="contract-10"></a>

`std::vector<std::unique_ptr<program>> owned_programs_;`

PROGRAMS THE CONTEXT COMPILED ITSELF, for `new Function(body)`.

A closure holds a `const function_proto *` into the program it came from
and `closure_object::owner` records which program that is, so a function
compiled at run time works everywhere - as long as the program OUTLIVES
it. Nothing else owns one, so the context does. Same reasoning as
browser::run_script keeping its own.

<a id="contract-11"></a>

`flat_map<const void *, flat_map<std::uint32_t, value>> string_cache_;`

Keyed by the FUNCTION rather than by its index in the current program,
because a context runs more than one program. KEYED BY const void *,
NOT const function_proto *: the interpreter keys it by the proto it is
running; a compiled body keys it by a marker address of its own, because
the backend numbers its slots in walk order and the interpreter by
constant-pool index, so sharing a key would let a compiled body read a
slot the interpreter filled with a DIFFERENT literal.

<a id="contract-12"></a>

`std::function<double()> clock_;`

Making a PENDING promise and settling one. The VM can read a promise's
state - `await` already did - but creating and settling run the standard
library's own logic, including queueing the handlers. Two hooks rather
than reaching into the object's properties, so there is one definition of
what settling means.

<a id="contract-13"></a>

`value pending_closure_ = value::undefined();`

THE CLOSURE A COMPILED BODY IS ABOUT TO BE ENTERED WITH. The entry ABI
delivers `site` - the function_proto - and not the closure, and upvalues
live on the closure INSTANCE. It travels the same way new.target does
rather than through the ABI's signature: set by enter_compiled_body,
consumed and cleared by ct_aot_enter, and a GC root in the window
between.

<a id="contract-14"></a>

`std::size_t pending_argv_base_ = 0;`

WHERE THE ARRIVING ARGUMENTS ARE, AS AN INDEX, and how many.

A compiled frame's own `base` is ABOVE the caller's window - ct_aot_enter
resizes registers_ and starts the frame at the new end - so a compiled
body has no way to reach the arguments past its declared parameters. The
entry ABI delivers `argv` and `argc`, but argv is a POINTER into
registers_ and ct_aot_enter's resize may reallocate it.

AN INDEX SURVIVES WHAT A POINTER DOES NOT, which is the whole design.
registers_ only grows during a call and the caller's window is not
truncated until the compiled body returns, so the index stays valid and
the VALUES stay rooted - collect() marks registers_ in full.

NOT IN each_root, deliberately, unlike pending_new_target_ and
pending_closure_ beside them: these root an integer. There is nothing
here for the collector to trace.

<a id="contract-15"></a>

`struct microtask {`

The MICROTASK QUEUE. A promise handler runs at the end of the turn, not
the moment the promise settles: `p.then(f); after();` must run `after`
FIRST.

A job is a callable plus its arguments, held as values so the collector
traces them - a std::function capturing a value would be a root nothing
knows about, which is how a queued handler's argument gets freed before
it runs.

<a id="contract-16"></a>

`bool store_rejected_ = false;`

WHETHER THE LAST [[Set]] WAS REJECTED - a non-writable or inherited
non-writable property, a non-extensible receiver, a getter with no
setter, a primitive receiver. store_property/store_index set it and
carry on silently, which is sloppy mode; the run loop and the AOT
bridge read it after a store from STRICT code and throw the TypeError
(10.1.9.2 / 13.15.2 PutValue step 6.b).

<a id="contract-17"></a>

`std::size_t unwinds_ = 0;`

How many throws have unwound, ever. A handler that called into
something that may throw compares it before and after, which is the
only way to know a throw crossed the call: the landing already cleared
thrown_.
