//===- properties.cpp - AOT helpers for objects and properties ------------===//
//
// The AOT runtime helpers for objects, properties, cells and globals:
// allocation, indexed and named access, the interned name pool, deletion, own
// keys, accessors and prototypes.
//
// Each function implements one CT_AOT_HELPER row declared in
// ctbrowser/include/ctbrowser/aot/helpers/*.def; shared declarations are in
// internal.hpp. A row's three flags are (may_throw, may_reenter,
// is_safepoint), written below as e.g. (0, 0, 0). "Raise tier" means the
// helper reports failure only by setting the context's failure flag (the
// allocation ceiling), which a JS `try` cannot catch and callers poll with
// ct_aot_failed; helpers that can throw a catchable JS error return an
// int32_t status instead.
//
//===----------------------------------------------------------------------===//

#include "internal.hpp"

namespace ctbrowser::aot {

namespace {

/// The interned name records. A deque, because the handles are pointers a
/// compiled body keeps for the life of the process and a vector would move
/// them; name_index()'s keys are views into these records, which is sound
/// only because their addresses are stable.
///
/// Single-threaded, like the rest of this VM: script runs on one thread. If
/// that ever changes this needs a lock, and so does most of the engine.
std::deque<aot_name_record> & name_records() {
    static std::deque<aot_name_record> records;
    return records;
}

std::unordered_map<std::string_view, const aot_name_record *> & name_index() {
    static std::unordered_map<std::string_view, const aot_name_record *> index;
    return index;
}

} // namespace

extern "C" {

std::uint64_t ct_aot_cell_new(ct_aot_frame * f, std::uint64_t init) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return value::object(cx.allocate<cell_object>(value::from_bits(init))).bits();
}

std::uint64_t ct_aot_new_object(ct_aot_frame * f) {
    return aot_bridge::frame_of(f).ctx->make_object().bits();
}

/// `reserve_hint` is only a hint: the row keeps it so a backend can size a
/// literal's backing store, and an array that ignores it is merely slower.
std::uint64_t ct_aot_new_array(ct_aot_frame * f, std::uint32_t reserve_hint) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const value made = cx.make_array();
    if (reserve_hint != 0) {
        static_cast<array_object *>(made.as_heap())->items.reserve(reserve_hint);
    }
    return made.bits();
}

/// ct_aot_iterable_values. VM_CASE(iterable) is one line,
/// `reg(in.a) = spread_values(reg(in.b))`, and spread_values is already a named
/// member (iterable_values plus the spread's own TypeError for a source with
/// nothing to iterate), so there is nothing to extract and no way for the
/// interpreter and compiled code to drift.
///
/// Delegated wholesale, including a known interpreter defect: the array-like
/// path calls lookup_property up to 2^24 times without checking failed_, so a
/// throw part-way through still runs the remaining lookups. Checking here but
/// not in the interpreter would make compiled code fail earlier than
/// interpreted code on a program that can observe the difference; the fix
/// belongs in the VM, for both.
std::int32_t ct_aot_iterable_values(ct_aot_frame * f, std::uint64_t source, std::uint64_t * out) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const value produced = cx.spread_values(value::from_bits(source));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

/// ct_aot_has_property, ct_aot_instance_of and ct_aot_delete_index: the three
/// shared members lifted out of run_loop so that `key in obj` cannot mean one
/// thing compiled and another interpreted.
///
/// Their failure modes differ, and the signatures show it. has_property and
/// delete_index return an int32_t status, so a caller tests it. instance_of
/// (in operators.cpp) returns its boolean: it is raise tier, so on failure the
/// uint32_t is meaningless and the caller polls ct_aot_failed at a back edge.
std::int32_t ct_aot_has_property(ct_aot_frame * f, std::uint64_t target, std::uint64_t key,
                                 std::uint32_t * out) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const bool present = cx.has_property(value::from_bits(target), value::from_bits(key));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = present ? 1u : 0u; }
    return status;
}

std::int32_t ct_aot_delete_index(ct_aot_frame * f, std::uint64_t target, std::uint64_t key) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.delete_index(value::from_bits(target), value::from_bits(key));
    return aot_bridge::check(f);
}

/// ct_aot_append. VM_CASE(append), through the shared context::array_append.
///
/// (0, 0, 0): the push_back can grow a std::vector, which is malloc rather than
/// allocate(). No GC object is created, so there is no safepoint and no
/// ceiling to raise against.
void ct_aot_append(ct_aot_frame * f, std::uint64_t array, std::uint64_t v) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.array_append(value::from_bits(array), value::from_bits(v));
}

/// ct_aot_new_string. Calls context::interned_string, the interpreter's
/// memoised string-literal path.
///
/// The memo is required for correctness, not speed. `allocations_` counts
/// allocations over the process lifetime and is never reset, so the
/// 40,000,000 ceiling is a lifetime budget: interpreted, a string literal in a
/// per-pixel loop allocates one object for the whole run, while the same loop
/// compiled without the memo would allocate one per iteration and hit the
/// ceiling in about a second, failing a program the interpreter runs forever.
///
/// Raise tier only, so there is no status: the sole failure is allocate()'s
/// ceiling, which sets failed_ without entering unwind_to_handler and is
/// invisible to a JS try. The returned string is well-formed even then.
std::uint64_t ct_aot_new_string(ct_aot_frame * f, const ct_aot_site * site, std::uint32_t slot,
                                const char * utf8, std::uint32_t len) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return cx.interned_string(static_cast<const void *>(site), slot, std::string_view{utf8, len})
        .bits();
}

/// ct_aot_set_index. `target[key] = v`, through the same context::store_index
/// the interpreter calls.
///
/// There is no out-parameter, by the row's design: the bytecode performs the
/// write and evaluates the expression separately, exactly as delete does.
/// `site` is reserved for a future inline cache; it is accepted and ignored so
/// the ABI signature both code generators target need not change.
std::int32_t ct_aot_set_index(ct_aot_frame * f, std::uint64_t obj, std::uint64_t key,
                              std::uint64_t v, ct_aot_ic * site) {
    (void)site;
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.clear_store_rejected();
    cx.store_index(value::from_bits(obj), value::from_bits(key), value::from_bits(v));
    // The same strict-mode TypeError VM_CASE(set_index) throws, decided by the
    // frame's own proto - the image keeps the real one.
    if (aot_bridge::strict_frame(f)) { cx.strict_store_check(cx.to_string(value::from_bits(key))); }
    return aot_bridge::check(f);
}

/// ct_aot_define_data. A literal's own property, through the member
/// VM_CASE(define_data) calls. The property is defined, not assigned, so there
/// is no strict-mode arm.
std::int32_t ct_aot_define_data(ct_aot_frame * f, std::uint64_t obj, std::uint64_t key,
                                std::uint64_t v) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.define_data_property(value::from_bits(obj), value::from_bits(key), value::from_bits(v),
                            true);
    return aot_bridge::check(f);
}

/// ct_aot_get_proto and ct_aot_set_proto, through the shared members the rows
/// name. Both are (0, 0, 0): the link is a plain field, so nothing allocates,
/// nothing throws and no accessor can run.
///
/// Taking the value in and returning the result removes an aliasing question
/// the interpreter has: get_proto is emitted as `{get_proto, dst, dst}` right
/// after load_home, so a and b are the same register there.
std::uint64_t ct_aot_get_proto(ct_aot_frame * f, std::uint64_t target) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return cx.super_base(value::from_bits(target)).bits();
}

void ct_aot_set_proto(ct_aot_frame * f, std::uint64_t target, std::uint64_t proto) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.set_prototype(value::from_bits(target), value::from_bits(proto));
}

/// ct_aot_new_bigint_literal. A BigInt literal, parsed once per site.
///
/// Raise tier: it answers the value rather than a status, and the row is
/// explicit that a literal the lexer accepted but that is not an integer does
/// not throw (1.5n substitutes 0n). The only failure is the allocation
/// ceiling, so a well-formed value comes back on every path.
///
/// `site` is the caller's marker, not a function_proto, which is why the cache
/// key is a void pointer. A compiled body numbers its slots in walk order and
/// the interpreter numbers them by constant-pool index; sharing a key would let
/// one read a slot the other filled with a different literal.
std::uint64_t ct_aot_new_bigint_literal(ct_aot_frame * f, const ct_aot_site * site,
                                        std::uint32_t slot, const char * text, std::uint32_t len) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return cx
        .interned_bigint_literal(static_cast<const void *>(site), slot, std::string_view{text, len})
        .bits();
}

/// ct_aot_delete_prop. `delete o.k`, the named form.
///
/// (0, 0, 0): erasing from a hash map is malloc's business, not the
/// collector's, and a name cannot run a to_string the way delete_index's value
/// key can, which is the reason the two are separate opcodes.
void ct_aot_delete_prop(ct_aot_frame * f, std::uint64_t target, const ct_aot_name * name) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.delete_named(value::from_bits(target),
                    reinterpret_cast<const aot_name_record *>(name)->text);
}

/// ct_aot_own_keys. for-in, which compiles to a for-of over this array.
///
/// Raise tier: it returns the array rather than a status, so on failure the
/// value is meaningless and the caller polls ct_aot_failed at a back edge. It
/// allocates (make_array plus one string per key), which is why it is a
/// safepoint even though it cannot run user code.
std::uint64_t ct_aot_own_keys(ct_aot_frame * f, std::uint64_t source) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return cx.own_keys(value::from_bits(source)).bits();
}

/// ct_aot_define_accessor. `get x()` and `set x(v)`, both opcodes.
///
/// (0, 0, 0). may_reenter is false even though every later read or write of
/// that property will re-enter: installing an accessor only stores the
/// function and never invokes it. The cost moves to the access site, which is
/// where the barrier and the cache belong.
void ct_aot_define_accessor(ct_aot_frame * f, std::uint64_t target, const ct_aot_name * name,
                            std::uint64_t getter, std::uint64_t setter) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.define_accessor(value::from_bits(target),
                       reinterpret_cast<const aot_name_record *>(name)->text,
                       value::from_bits(getter), value::from_bits(setter));
}

/// ct_aot_copy_props. Object spread, through the shared member.
///
/// (0, 0, 0), verified rather than copied from a look-alike: object_object::set
/// grows a std::vector and a hash map (malloc, not allocate()), so no GC
/// object is created, nothing can throw and no accessor runs. The lowering
/// shape matches ct_aot_append's, but that alone would not justify its flags.
void ct_aot_copy_props(ct_aot_frame * f, std::uint64_t target, std::uint64_t source) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.copy_own_properties(value::from_bits(target), value::from_bits(source));
}

/// ct_aot_cell_get. VM_CASE(cell_get) verbatim. It takes no frame and cannot
/// fail: its row (0, 0, 0) guarantees that a non-cell argument yields undefined
/// silently, so callers need no extra type check.
std::uint64_t ct_aot_cell_get(std::uint64_t cell) {
    const value held = value::from_bits(cell);
    return held.is_kind(heap_kind::cell) ? static_cast<cell_object *>(held.as_heap())->slot.bits()
                                         : value::undefined().bits();
}

/// ct_aot_cell_set. VM_CASE(cell_set) verbatim, and silent on a non-cell for
/// the same reason.
void ct_aot_cell_set(std::uint64_t cell, std::uint64_t v) {
    const value held = value::from_bits(cell);
    if (held.is_kind(heap_kind::cell)) {
        static_cast<cell_object *>(held.as_heap())->slot = value::from_bits(v);
    }
}

/// ct_aot_global_get. A name that is neither a global nor a property of the
/// global object is an unresolvable reference and throws ReferenceError, which
/// is catchable, so this returns a status and an out-slot like every other
/// throwing row. The embedder's undeclared-name hook still answers first,
/// which is how an element with an `id` answers to a bare identifier.
/// `context::global_or_named` is exactly what VM_CASE(get_global) runs, so the
/// interpreter and compiled code cannot drift.
std::int32_t ct_aot_global_get(ct_aot_frame * f, const char * name, std::uint32_t name_len,
                               std::uint64_t * out) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    const value produced = cx.global_or_named(std::string_view{name, name_len});
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

/// ct_aot_global_get_soft: the global read for source `typeof x`. An unresolved
/// name yields undefined; a getter or proxy trap can still throw, so check the
/// frame status before consuming this value, as the helper's row requires.
std::uint64_t ct_aot_global_get_soft(ct_aot_frame * f, const char * name, std::uint32_t name_len) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    return cx.global_or_named(std::string_view{name, name_len}, true).bits();
}

/// InitializeBinding of a global lexical declaration, after its initializer ran.
void ct_aot_global_init(ct_aot_frame * f, const char * name, std::uint32_t name_len,
                        std::uint64_t v) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.initialize_global(cx.current_realm(), std::string{name, name_len}, value::from_bits(v));
}

/// ct_aot_global_set. VM_CASE(set_global) is context::assign_global, and so is
/// this, so a global written by compiled code and one written by the
/// interpreter land in the same place: the table's binding, a data property
/// the global object owns, or a new table entry. The store is sloppy (`strict`
/// false), so a read-only global refuses silently. An uninitialized lexical
/// throws ReferenceError; generated code checks ct_aot_check after this call.
///
/// The name is copied because the map owns its keys. The caller's characters
/// point into a generated translation unit's rodata and would outlive
/// everything here, but borrowing would be a second rule to remember.
void ct_aot_global_set(ct_aot_frame * f, const char * name, std::uint32_t name_len,
                       std::uint64_t v) {
    context & cx = *aot_bridge::frame_of(f).ctx;
    cx.assign_global(cx.current_realm(), std::string{name, name_len}, value::from_bits(v), false);
}

std::int32_t ct_aot_get_index(ct_aot_frame * f, std::uint64_t obj, std::uint64_t key,
                              ct_aot_ic * site, std::uint64_t * out) {
    (void)site;
    context & cx = *aot_bridge::frame_of(f).ctx;
    const value produced = cx.lookup_index(value::from_bits(obj), value::from_bits(key));
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

/// ct_aot_intern_name. Idempotent: the same characters always give the same
/// pointer, which lets a backend compare names by address.
///
/// The handle is an opaque pointer on the ABI's side and an aot_name_record on
/// this one, which is why `struct ct_aot_name` is declared and never defined.
const ct_aot_name * ct_aot_intern_name(const char * utf8, std::uint32_t len) {
    const std::string_view text{utf8, len};
    auto & index = name_index();
    if (const auto found = index.find(text); found != index.end()) {
        return reinterpret_cast<const ct_aot_name *>(found->second);
    }
    auto & records = name_records();
    records.push_back(aot_name_record{std::string{text}, hash_name(text)});
    const aot_name_record * record = &records.back();
    // Keyed by a view into the record's own string, not the caller's
    // characters, which the caller is free to release.
    index.emplace(std::string_view{record->text}, record);
    return reinterpret_cast<const ct_aot_name *>(record);
}

std::int32_t ct_aot_get_prop(ct_aot_frame * f, std::uint64_t obj, const ct_aot_name * name,
                             ct_aot_ic * site, std::uint64_t * out) {
    (void)site; // Reserved for a future inline cache; keeps the ABI stable.
    context & cx = *aot_bridge::frame_of(f).ctx;
    // Uses the interpreter's own lookup_property, which hashes the name again.
    // Reusing the interned hash across the prototype chain would need
    // lookup_property to accept a prehashed_name; that is a separate
    // optimisation.
    const value produced = cx.lookup_property(
        value::from_bits(obj), reinterpret_cast<const aot_name_record *>(name)->text);
    const std::int32_t status = aot_bridge::check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

std::int32_t ct_aot_set_prop(ct_aot_frame * f, std::uint64_t obj, const ct_aot_name * name,
                             std::uint64_t v, ct_aot_ic * site) {
    (void)site;
    context & cx = *aot_bridge::frame_of(f).ctx;
    const std::string & text = reinterpret_cast<const aot_name_record *>(name)->text;
    cx.clear_store_rejected();
    cx.store_property(value::from_bits(obj), text, value::from_bits(v));
    if (aot_bridge::strict_frame(f)) { cx.strict_store_check(text); }
    return aot_bridge::check(f);
}

} // extern "C"

} // namespace ctbrowser::aot
