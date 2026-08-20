#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <ctbrowser/script/value.hpp>

// A register-based instruction set.
//
// Register-based rather than stack-based, and the reason is inline caches:
// `GET_PROP dst, obj, name` names its operands, so a cache keyed on that
// instruction's address sees the same object shape every time it executes. A
// stack machine's `getprop` has no stable identity for what it is operating
// on, which is why the engines that care about property-access speed are all
// register machines.
//
// Registers are frame slots, allocated by a high-water mark as the compiler
// walks expressions - Lua's design. It is not graph-colouring register
// allocation and does not try to be: it costs one integer per expression
// node, produces no spills because the frame is as large as it needs to be,
// and removes every push/pop from the dispatch loop.
//
// An instruction is 8 bytes: an opcode and three 16-bit operands, the odd byte
// going to alignment padding. Ops that need a wider operand (constant indices, jump offsets)
// read b and c as one 32-bit field, which is why `bx()` exists.
//
// It was 4 bytes with three BYTE operands, and every operand that did not fit
// was truncated without a word: the 257th distinct property name in a function
// read a different property, a function wanting more than 256 registers
// aliased its own locals, and a jump further than 32,767 instructions branched
// to an address that was never a target. p5.js hits all three. Eight bytes
// costs about 8 MB on a 4.5 MB bundle and removes the entire class of problem.

namespace ctbrowser::script {

enum class op : std::uint8_t {
    // --- moves and constants
    load_const,  // a = k[bx]          (number/bool - an immediate, no heap)
    load_string, // a = intern(strings[bx])
    // a = the BigInt whose LITERAL TEXT is strings[bx] (without the trailing
    // `n`). The text rather than a parsed constant, because a bigint has no
    // fixed width to put in the constant pool - and the VM caches the parse per
    // slot, so the digits are read once however hot the site is.
    load_bigint,
    load_undef, // a = undefined
    load_null,  // a = null
    load_true,  // a = true
    load_false, // a = false
    move,       // a = b

    // --- globals and locals
    get_global, // a = globals[k[bx]]
    set_global, // globals[k[bx]] = a

    // --- captured variables. A local that some nested function refers to is
    // stored in a heap CELL rather than directly in its register, and every
    // access goes through the cell. That is what makes mutation through a
    // closure visible to the enclosing scope - the alternative, copying the
    // value into the closure, silently gets the commonest closure idiom
    // (a counter) wrong.
    new_cell,    // a = cell(a)          (box the value already in a)
    cell_get,    // a = *b
    cell_set,    // *a = b
    get_upvalue, // a = *upvalues[b]     (upvalues are always cells)
    set_upvalue, // *upvalues[a] = b

    // --- arithmetic. JS `+` is add_or_concat: it is the one operator whose
    // meaning depends on its operand types, so it gets its own opcode rather
    // than a runtime branch inside a generic `add`.
    add,         // a = b + c   (numeric)
    concat,      // a = b + c   (string)
    add_generic, // a = b + c   (either, decided at runtime)
    sub,
    mul,
    div,
    mod,
    pow,
    negate,      // a = -b
    to_number,   // a = +b  - a CONVERSION, not a copy; see compile_unary
    logical_not, // a = !b

    // --- comparison
    equal,           // a = b === c
    not_equal,       // a = b !== c
    loose_equal,     // a = b == c
    loose_not_equal, // a = b != c
    less,
    less_equal,
    greater,
    greater_equal,
    instance_of,  // a = b instanceof c   (walks b's prototype chain)
    has_property, // a = b in c

    // --- bitwise. JavaScript's operate on ToInt32/ToUint32 of a double, so
    // they are not just the C operators on the stored number.
    bit_and,
    bit_or,
    bit_xor,
    shl,
    shr,
    ushr,
    bit_not, // a = ~b

    // --- control flow
    jump,          // ip += sbx
    jump_if_false, // if (!truthy(a)) ip += sbx
    jump_if_true,  // if (truthy(a)) ip += sbx
    // `??` and `??=` ask whether a is null or undefined - NOT whether it is
    // falsy. They are different questions and the difference is the entire
    // point of the operator: `0 ?? 5` is 0, and `"" ?? "x"` is "". Compiling
    // them as `||` gets both wrong, silently.
    jump_if_not_nullish, // if (!nullish(a)) ip += sbx
    // A default parameter applies when the argument is UNDEFINED and only
    // then - `f(null)` against `function f(a = 1)` leaves a null. So this is
    // not the nullish test, and it is not the truthy one either.
    jump_if_defined, // if (a is not undefined) ip += sbx
    // b..argc, as an array: the tail a rest parameter binds. The frame knows
    // how many arguments actually arrived; the proto only knows how many were
    // declared, which is the wrong number here.
    gather_rest, // a = [...arguments].slice(b)

    // `f(...args)`. Every other call form passes its arguments in consecutive
    // registers and puts the COUNT in an operand, which cannot work when the
    // count is not known until the spread is evaluated. These take the
    // arguments as an array instead, so one form covers plain calls, method
    // calls and computed calls - the receiver is simply a register.
    apply,           // a = a.call(c, ...b)
    construct_apply, // a = new a(...b)

    // `get x()` / `set x(v)`. A property that runs code when it is read or
    // written, which is a different thing from a property that HOLDS a
    // function - installing a getter as a data property made `obj.x` be the
    // function rather than call it.
    define_getter, // a.<b> gets the getter in c
    define_setter, // a.<b> gets the setter in c

    // --- objects and arrays
    new_object, // a = {}
    new_array,  // a = []
    get_prop,   // a = b[k[c]]        (named; the inline-cache site)
    set_prop,   // a[k[b]] = c
    get_index,  // a = b[c]           (computed)
    set_index,  // a[b] = c
    append,     // a.push(b)          (array literals)

    // --- calls
    closure,       // a = new closure of functions[bx]
    call,          // a = call(a, args a+1 .. a+b)
    call_method,   // a = call(a[k[c]], this=a, args a+1 .. a+b)
    call_computed, // a = call(a[reg c], this=a, args a+1 .. a+b)
    construct,     // a = new a(args a+1 .. a+b)
    call_receiver, // a = call(a, this=c, args a+1 .. a+b) - `super.m(...)`
    copy_props,    // a gets every own property of b   (`{...o}`, Object.assign)
    delete_prop,   // a[k[b]] = gone
    delete_index,  // a[b] = gone
    own_keys,      // a = the own property names of b, as an array (for..in)
    // a = b as an ARRAY OF VALUES, for anything a page can iterate. for-of is an
    // index loop over `length`, so a Map or a Set - which has neither - ran zero
    // times and said nothing. See context::iterable_values.
    iterable,
    set_proto,   // a.__proto__ = b, for `class X extends Y`
    get_proto,   // a = b's prototype, for `super`
    load_home,   // a = the home object of the running method (its class's
                 // prototype); `super` starts its lookup at the home's proto
    await_value, // a = the settled value of b (a promise, or b itself)
    // `yield b`. Suspends the frame into its generator and hands b out to
    // whoever called `.next()`; a is where the value passed to the NEXT
    // `.next(v)` lands, which is what makes `var x = yield y` work.
    //
    // It is the same machinery `await` uses - the frame is lifted into a
    // coroutine_object and put back later - and deliberately so. The difference
    // is only WHO resumes it: a settling promise for await, an explicit
    // `.next()` for a generator.
    yield_value,
    wrap_promise, // a = a as a SETTLED promise (what an `async` function returns)
    ret,          // return a
    ret_undef,    // return undefined

    // --- misc
    type_of,   // a = typeof b
    load_this, // a = the receiver of the call that entered this frame
    // a = `new.target`: the constructor this frame was invoked with, or
    // undefined when it was an ordinary call. The frame already knows - it
    // carries `constructing` so that `new C()` can evaluate to the new object -
    // so this reads state that was there rather than adding any.
    load_new_target,
    // `super(...)` PASSES new.target ALONG. The spec propagates it down the
    // constructor chain: inside a base constructor reached through super(),
    // `new.target` is the class `new` was written against, not the base. That
    // cannot be inferred at run time - a super() call and an ordinary method
    // call look identical by then - so the compiler marks the call and the VM
    // hands the value to the next frame it pushes.
    pass_new_target,
    // --- ES modules ------------------------------------------------------
    // `a = the cell exported as names[b] by the module at specifier
    // names[c]`. A cell, NOT a value: an imported binding is LIVE, so the
    // importer must hold the very box the exporter writes through. Copying the
    // value here is the CommonJS behaviour and is observably wrong - it is the
    // shortcut docs/plans/modules.md names in advance.
    load_import,
    // Publish the cell in register a as this module's export named names[bx].
    bind_export,
    // `a = import(b)` - a PROMISE for the namespace object of the module whose
    // specifier is the string in register b. The specifier is a runtime value,
    // so unlike a static import there is nothing the loader could have resolved
    // in advance: the VM hands it and the calling function's own module to the
    // embedder's loader and gets back a settled promise.
    dyn_import,
    // `a = the namespace object of the module at names[b]`, for `import * as
    // ns`. LIVE, like every other imported binding: each property is an
    // accessor reading the exporter's cell, not a value copied at link time.
    load_namespace,

    // a = the CLOSURE running this frame. `var f = function me() { ... me() }`
    // binds `me` inside its own body and nowhere else, and there is no other
    // way to reach it: the enclosing scope has no such name, and the closure
    // does not exist yet when the body is compiled. Without it a recursive
    // function expression called an undefined `me` - which is silent when the
    // call is a callback, as `(function pump() { raf(pump); })()` is.
    load_callee,
    // a = `arguments`: every value this call actually received, as an array.
    //
    // It is a real Array rather than the spec's array-LIKE object, which is a
    // deliberate and visible deviation: `Array.isArray(arguments)` is true here
    // and false in a browser. The alternative - a plain object with numeric
    // keys - loses `Array.prototype.slice.call(arguments)`, which is the single
    // commonest thing done with it, so this is the more useful of two wrong
    // answers. `length` and indexing, which is what dispatch code reads, are
    // right either way.
    make_arguments,

    // --- exceptions. `try` pushes a handler with the address to jump to;
    // `throw` unwinds call frames until it finds one. Unwinding is what makes
    // this a VM change rather than a compiler one: a handler in a caller has to
    // be reachable from a throw several frames deep.
    push_handler, // remember (catch address = sbx, this frame); a = catch register
    pop_handler,  // leave the try block normally
    throw_value,  // raise a
    halt,
};

// THE TABLE THAT DESCRIBES THEM, and the assert that keeps it honest.
//
// bytecode_opcodes.def carries one CT_OPCODE line per enumerator above, with
// the operand encoding and the behaviour an ahead-of-time compiler needs -
// whether an operation allocates, throws, can run user JavaScript in the
// middle of itself, or can suspend the frame. The VM builds its own dispatch
// table from it, and ctcompile reads the same file, so there is ONE list.
//
// Counting the ENTRIES rather than sizing an array is deliberate and was
// learned the hard way: a table built with array designators has no gap check,
// and two opcodes went missing from the VM's private list for as long as
// modules existed. A hole is a null entry and a jump to address zero in the
// computed-goto build; here it is a build failure.
#define CT_OPCODE(name, ...) +1
inline constexpr std::size_t opcode_count = 0
#include <ctbrowser/script/bytecode_opcodes.def>
    ;
#undef CT_OPCODE
static_assert(opcode_count == static_cast<std::size_t>(op::halt) + 1,
              "bytecode_opcodes.def must list every opcode in `enum class op` exactly once");

struct instruction {
    op code = op::halt;
    std::uint16_t a = 0, b = 0, c = 0;

    [[nodiscard]] constexpr std::uint32_t bx() const noexcept {
        return (static_cast<std::uint32_t>(b) << 16) | c;
    }
    [[nodiscard]] constexpr std::int32_t sbx() const noexcept {
        return static_cast<std::int32_t>(bx());
    }
    static constexpr instruction with_bx(op o, std::uint16_t reg, std::uint32_t wide) noexcept {
        return instruction{o, reg, static_cast<std::uint16_t>(wide >> 16),
                           static_cast<std::uint16_t>(wide & 0xFFFF)};
    }
};

static_assert(sizeof(instruction) == 8, "an instruction is one 64-bit word");

// Where one of a function's upvalues comes from, resolved at compile time.
// A closure is built by walking this list: each entry either grabs a cell out
// of the ENCLOSING FRAME's register, or re-shares a cell the enclosing
// closure already holds. The second case is what makes capture work through
// more than one level of nesting.
struct upvalue_desc {
    bool from_parent_local = true;
    std::uint16_t index = 0;
};

// One compiled function. `constants` holds every literal and every property
// name the body mentions, so the dispatch loop never touches a std::string
// except through an index.
struct function_proto {
    // THE MODULE THIS FUNCTION WAS COMPILED IN, empty for a classic script. The
    // compiler cannot know it - a specifier is the LOADER's name for a file -
    // so the loader stamps every proto in a program after compiling it.
    //
    // It is here rather than on `program` because that is where it can be
    // reached: a running frame holds a `function_proto *` and nothing else, and
    // `import('./x.js')` called from a callback long after evaluation still has
    // to resolve `./x.js` against the module that WROTE it.
    std::string module;
    std::string name;
    std::uint16_t param_count = 0;
    std::uint16_t frame_size = 1; // registers this body needs
    // An arrow does not get its own `this`; it sees the one where it was
    // WRITTEN. The VM cannot tell an arrow from a function at run time, and
    // reading the frame's own receiver made `this` undefined inside every arrow
    // inside a method - which is exactly where arrows are usually written.
    bool is_arrow = false;
    // `function*`. Calling one does NOT run the body: it builds a generator
    // object over a suspended frame and hands that back, so the first
    // instruction runs on the first `.next()`.
    bool is_generator = false;
    // WHERE IT WAS WRITTEN, as byte offsets into the program's source.
    //
    // `f.toString()` has to hand back the text, and an engine with no answer
    // cannot run a library that reads its own source - which p5.js's error
    // system does. Two integers, and the parser already knew both.
    std::uint32_t source_begin = 0, source_end = 0;
    std::vector<instruction> code;
    // Immediates only. A string literal cannot live here: `value` for a string
    // is a pointer into a VM heap that does not exist at compile time, so the
    // TEXT is kept instead and the VM materializes it. That also keeps a
    // compiled program independent of any one VM instance, which matters once
    // there is an agent per thread.
    std::vector<value> constants;
    std::vector<std::string> strings;
    std::vector<std::string> names; // for get_global/get_prop operands
    std::vector<upvalue_desc> upvalues;
    std::vector<std::uint32_t> nested; // indices into program::functions

    [[nodiscard]] std::uint32_t add_constant(value v) {
        constants.push_back(v);
        return static_cast<std::uint32_t>(constants.size() - 1);
    }
    [[nodiscard]] std::uint32_t add_string(std::string s) {
        for (std::size_t i = 0; i < strings.size(); ++i) {
            if (strings[i] == s) { return static_cast<std::uint32_t>(i); }
        }
        strings.push_back(std::move(s));
        return static_cast<std::uint32_t>(strings.size() - 1);
    }
    [[nodiscard]] std::uint32_t add_name(std::string n) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == n) { return static_cast<std::uint32_t>(i); } // names repeat constantly
        }
        names.push_back(std::move(n));
        return static_cast<std::uint32_t>(names.size() - 1);
    }
    std::size_t emit(instruction i) {
        code.push_back(i);
        return code.size() - 1;
    }
};

struct program {
    // WHAT THIS MODULE IMPORTS, in source order, so a loader can fetch the
    // graph without re-parsing. Empty for a classic script. The specifiers are
    // exactly as written - resolving them against the importing module's URL is
    // the loader's job, not the compiler's.
    std::vector<std::string> imports;
    // AND WHAT IT EXPORTS, likewise statically known. The loader needs these
    // BEFORE the module runs: every binding in a cyclic graph has to exist
    // before any of the graph is evaluated, or the module that imports first
    // asks for a name whose exporter has not reached its own first statement.
    // That is the specification's instantiate-then-evaluate split, and this
    // vector is the half of it the compiler can answer.
    std::vector<std::string> exports;
    // AND WHAT IT RE-EXPORTS: `export { a } from './m.js'` and `export * from
    // './m.js'`, which every ES library's index file is made of and which bind
    // NO local name at all. They are edges in the graph rather than
    // instructions, and they are resolved where the graph is known - the loader,
    // at instantiate time, before anything evaluates. Doing it at run time
    // instead would hand a cyclic importer a cell that gets replaced later.
    //
    // `source` empty means `export *`: every name the other module exports,
    // except ones this module declares itself.
    struct reexport {
        std::string exported;
        std::string source;
        std::string from;
    };
    std::vector<reexport> reexports;
    // THE SOURCE THIS WAS COMPILED FROM, kept so a function can be printed.
    // A 4.5 MB bundle costs 4.5 MB, which it already cost to compile.
    std::string source;
    std::vector<function_proto> functions; // [0] is the top-level script
    bool ok = true;
    std::string error;
};

} // namespace ctbrowser::script
