module;
#include <cstdint>
#include <string>
#include <vector>

export module ctbrowser.script:bytecode;

import :value;

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
// An instruction is 4 bytes: an opcode and three byte operands. Ops that need
// a wider operand (constant indices, jump offsets) read b and c as one 16-bit
// field, which is why `bx()` exists.

export namespace ctbrowser::script {

enum class op : std::uint8_t {
    // --- moves and constants
    load_const,  // a = k[bx]          (number/bool - an immediate, no heap)
    load_string, // a = intern(strings[bx])
    load_undef,  // a = undefined
    load_null,   // a = null
    load_true,   // a = true
    load_false,  // a = false
    move,        // a = b

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
    set_proto,     // a.__proto__ = b, for `class X extends Y`
    get_proto,     // a = b's prototype, for `super`
    load_home,     // a = the home object of the running method (its class's
                   // prototype); `super` starts its lookup at the home's proto
    await_value,   // a = the settled value of b (a promise, or b itself)
    wrap_promise,  // a = a as a SETTLED promise (what an `async` function returns)
    ret,           // return a
    ret_undef,     // return undefined

    // --- misc
    type_of,   // a = typeof b
    load_this, // a = the receiver of the call that entered this frame

    // --- exceptions. `try` pushes a handler with the address to jump to;
    // `throw` unwinds call frames until it finds one. Unwinding is what makes
    // this a VM change rather than a compiler one: a handler in a caller has to
    // be reachable from a throw several frames deep.
    push_handler, // remember (catch address = sbx, this frame); a = catch register
    pop_handler,  // leave the try block normally
    throw_value,  // raise a
    halt,
};

struct instruction {
    op code = op::halt;
    std::uint8_t a = 0, b = 0, c = 0;

    [[nodiscard]] constexpr std::uint16_t bx() const noexcept {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b) << 8) | c);
    }
    [[nodiscard]] constexpr std::int16_t sbx() const noexcept {
        return static_cast<std::int16_t>(bx());
    }
    static constexpr instruction with_bx(op o, std::uint8_t reg, std::uint16_t wide) noexcept {
        return instruction{o, reg, static_cast<std::uint8_t>(wide >> 8),
                           static_cast<std::uint8_t>(wide & 0xFF)};
    }
};

static_assert(sizeof(instruction) == 4, "instructions should stay one word");

// Where one of a function's upvalues comes from, resolved at compile time.
// A closure is built by walking this list: each entry either grabs a cell out
// of the ENCLOSING FRAME's register, or re-shares a cell the enclosing
// closure already holds. The second case is what makes capture work through
// more than one level of nesting.
struct upvalue_desc {
    bool from_parent_local = true;
    std::uint8_t index = 0;
};

// One compiled function. `constants` holds every literal and every property
// name the body mentions, so the dispatch loop never touches a std::string
// except through an index.
struct function_proto {
    std::string name;
    std::uint8_t param_count = 0;
    std::uint8_t frame_size = 1; // registers this body needs
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

    [[nodiscard]] std::uint16_t add_constant(value v) {
        constants.push_back(v);
        return static_cast<std::uint16_t>(constants.size() - 1);
    }
    [[nodiscard]] std::uint16_t add_string(std::string s) {
        for (std::size_t i = 0; i < strings.size(); ++i) {
            if (strings[i] == s) { return static_cast<std::uint16_t>(i); }
        }
        strings.push_back(std::move(s));
        return static_cast<std::uint16_t>(strings.size() - 1);
    }
    [[nodiscard]] std::uint16_t add_name(std::string n) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == n) { return static_cast<std::uint16_t>(i); } // names repeat constantly
        }
        names.push_back(std::move(n));
        return static_cast<std::uint16_t>(names.size() - 1);
    }
    std::size_t emit(instruction i) {
        code.push_back(i);
        return code.size() - 1;
    }
};

struct program {
    std::vector<function_proto> functions; // [0] is the top-level script
    bool ok = true;
    std::string error;
};

} // namespace ctbrowser::script
