// The AOT ABI - objects, properties, cells and globals: allocation, indexed
// and named access, the interned name pool, deletion, own keys, accessors,
// prototypes.
//
// One of four files carved out of a 1,586-line aot_bridge.cpp on 2026-09-08.
// `struct aot_bridge` is declared in internal.hpp beside this; each of its
// bodies is defined in the file that also holds the extern "C" row calling
// it, so a wrapper and the member behind it are still one translation unit
// and the call still inlines. The four members every file calls - ctx_of,
// frame_of, frame_record and check - stay in-class in the header for the
// same reason.

#include "internal.hpp"

namespace ctbrowser::script {

namespace {

// A DEQUE FOR THE RECORDS, because the handles are pointers a compiled body
// keeps for the life of the process and a vector would move them. The index's
// keys are views INTO those records, which is only sound because their
// addresses are stable.
//
// SINGLE-THREADED, like everything else in this VM. Script runs on one thread;
// if that ever changes this needs a lock, and so does most of the engine.
std::deque<aot_name_record> & name_records() {
    static std::deque<aot_name_record> records;
    return records;
}

std::unordered_map<std::string_view, const aot_name_record *> & name_index() {
    static std::unordered_map<std::string_view, const aot_name_record *> index;
    return index;
}

} // namespace

std::uint64_t aot_bridge::cell_new(aot::ct_aot_frame * f, std::uint64_t init) {
    context & cx = *frame_of(f).ctx;
    return value::object(cx.allocate<cell_object>(value::from_bits(init))).bits();
}

std::uint64_t aot_bridge::new_object(aot::ct_aot_frame * f) {
    return frame_of(f).ctx->make_object().bits();
}

// `reserve_hint` is a HINT: the row keeps it so a backend can size a
// literal's backing store, and an array that ignores it is merely slower.
std::uint64_t aot_bridge::new_array(aot::ct_aot_frame * f, std::uint32_t reserve_hint) {
    context & cx = *frame_of(f).ctx;
    const value made = cx.make_array();
    if (reserve_hint != 0) {
        static_cast<array_object *>(made.as_heap())->items.reserve(reserve_hint);
    }
    return made.bits();
}

// ct_aot_iterable_values. VM_CASE(iterable) is one line -
// `reg(in.a) = iterable_values(reg(in.b))` - and iterable_values is
// ALREADY a named member, so there is nothing to extract and no way for
// the two tiers to drift.
//
// DELEGATED WHOLESALE, INCLUDING THE ROW'S CORRECTION (1). That correction
// describes a real defect - the array-like arm calls lookup_property up to
// 2^24 times with no failed_ test, so a throw part-way through still runs
// millions of lookups - but it is the INTERPRETER's defect, and re-testing
// here and not there would make the compiled tier fail EARLIER than the
// interpreted one on a program that can observe the difference. Fixing it
// is a VM change with its own before/after test, in one place, for both
// tiers.
std::int32_t aot_bridge::iterable_values(aot::ct_aot_frame * f, std::uint64_t source,
                                         std::uint64_t * out) {
    context & cx = *frame_of(f).ctx;
    const value produced = cx.iterable_values(value::from_bits(source));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// ct_aot_has_property, ct_aot_instance_of, ct_aot_delete_index - the three
// shared members lifted out of run_loop so `key in obj` cannot mean one
// thing compiled and another interpreted.
//
// THEIR TIERS DIFFER AND THE SIGNATURES SAY SO. has_property and
// delete_index answer an int32_t status, so a caller tests. instance_of
// returns its BOOLEAN - it is raise tier, so on failure the uint32_t is
// meaningless and a caller polls ct_aot_failed at a back edge instead.
std::int32_t aot_bridge::has_property(aot::ct_aot_frame * f, std::uint64_t target,
                                      std::uint64_t key, std::uint32_t * out) {
    context & cx = *frame_of(f).ctx;
    const bool present = cx.has_property(value::from_bits(target), value::from_bits(key));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = present ? 1u : 0u; }
    return status;
}

std::int32_t aot_bridge::delete_index(aot::ct_aot_frame * f, std::uint64_t target,
                                      std::uint64_t key) {
    context & cx = *frame_of(f).ctx;
    cx.delete_index(value::from_bits(target), value::from_bits(key));
    return check(f);
}

// ct_aot_append. VM_CASE(append) through the shared context::array_append.
//
// (0, 0, 0): the push_back can grow a std::vector, which is malloc rather
// than allocate() - no GC object is created, so there is no safepoint and
// no ceiling to raise against.
void aot_bridge::append(aot::ct_aot_frame * f, std::uint64_t array, std::uint64_t v) {
    context & cx = *frame_of(f).ctx;
    cx.array_append(value::from_bits(array), value::from_bits(v));
}

// ct_aot_new_string. The same context::interned_string the interpreter
// now calls, memo and all.
//
// THE MEMO IS NOT AN OPTIMISATION HERE. `allocations_` counts TOTAL
// allocations for the process lifetime and is never reset, so the
// 40,000,000 ceiling is a lifetime budget: interpreted, a string literal
// in a per-pixel loop allocates ONE object for the whole run, while the
// same loop compiled without the memo allocates one per iteration and
// reaches the ceiling in about a second. That is a divergence in the raise
// tier, on a program the interpreter runs forever.
//
// RAISE TIER ONLY, so there is no status: the sole failure is allocate()'s
// ceiling, which sets failed_ without entering unwind_to_handler and is
// invisible to a JS try. The returned string is well-formed even after it.
std::uint64_t aot_bridge::new_string(aot::ct_aot_frame * f, const aot::ct_aot_site * site,
                                     std::uint32_t slot, const char * utf8, std::uint32_t len) {
    context & cx = *frame_of(f).ctx;
    return cx.interned_string(static_cast<const void *>(site), slot, std::string_view{utf8, len})
        .bits();
}

// ct_aot_set_index. `target[key] = v` through the same
// context::store_index the interpreter now calls.
//
// NO OUT-PARAMETER, and that is the row rather than an omission: the
// bytecode performs the write and evaluates the expression separately,
// exactly as delete does. `site` is where Phase 26 attaches an inline
// cache; it is taken and ignored, for the same reason ct_aot_get_index's
// is - the signature is what two code generators are written against.
std::int32_t aot_bridge::set_index(aot::ct_aot_frame * f, std::uint64_t obj, std::uint64_t key,
                                   std::uint64_t v) {
    context & cx = *frame_of(f).ctx;
    cx.store_index(value::from_bits(obj), value::from_bits(key), value::from_bits(v));
    return check(f);
}

// ct_aot_get_proto and ct_aot_set_proto, through the shared members the
// rows themselves named. Both all-zero: the link is a plain field, so
// nothing allocates, nothing throws and no accessor can run.
//
// BY VALUE IN, RESULT OUT, which removes an aliasing question the
// interpreter has: get_proto is emitted as `{get_proto, dst, dst}` right
// after load_home, so a and b are the same register there.
std::uint64_t aot_bridge::get_proto(aot::ct_aot_frame * f, std::uint64_t target) {
    context & cx = *frame_of(f).ctx;
    return cx.get_prototype(value::from_bits(target)).bits();
}

void aot_bridge::set_proto(aot::ct_aot_frame * f, std::uint64_t target, std::uint64_t proto) {
    context & cx = *frame_of(f).ctx;
    cx.set_prototype(value::from_bits(target), value::from_bits(proto));
}

// ct_aot_new_bigint_literal. A BigInt literal, parsed once per site.
//
// RAISE TIER: it answers the VALUE rather than a status, and the row is
// explicit that a literal the lexer accepted but that is not an integer
// does not throw - 1.5n substitutes 0n. The only failure is the allocation
// ceiling, so a well-formed value comes back on every path.
//
// THE SITE IS THE CALLER'S MARKER, not a function_proto, which is why the
// cache key is a void pointer. A compiled body numbers its slots in walk
// order and the interpreter numbers them by constant-pool index; sharing a
// key would let one read a slot the other filled with a different literal.
std::uint64_t aot_bridge::new_bigint_literal(aot::ct_aot_frame * f, const aot::ct_aot_site * site,
                                             std::uint32_t slot, const char * text,
                                             std::uint32_t len) {
    context & cx = *frame_of(f).ctx;
    return cx
        .interned_bigint_literal(static_cast<const void *>(site), slot, std::string_view{text, len})
        .bits();
}

// ct_aot_delete_prop. `delete o.k`, the named form.
//
// (0, 0, 0): erasing from a hash map is malloc's business, not the
// collector's, and a NAME cannot run a to_string the way delete_index's
// value key can - which is the entire reason the two are separate opcodes.
void aot_bridge::delete_prop(aot::ct_aot_frame * f, std::uint64_t target,
                             const aot_name_record * name) {
    context & cx = *frame_of(f).ctx;
    cx.delete_named(value::from_bits(target), name->text);
}

// ct_aot_own_keys. for-in, which compiles to a for-of over this array.
//
// RAISE TIER: it returns the ARRAY rather than a status, so on failure the
// value is meaningless and a caller polls ct_aot_failed at a back edge. It
// allocates - make_array plus one string per key - which is why it is a
// safepoint even though it cannot run user code.
std::uint64_t aot_bridge::own_keys(aot::ct_aot_frame * f, std::uint64_t source) {
    context & cx = *frame_of(f).ctx;
    return cx.own_keys(value::from_bits(source)).bits();
}

// ct_aot_define_accessor. `get x()` and `set x(v)`, both opcodes.
//
// (0, 0, 0) AND THE ROW SPELLS OUT WHY may_reenter IS FALSE HERE even
// though every later read or write of that property WILL re-enter:
// installing an accessor only STORES the function, it never invokes it.
// The cost moves to the access site, which is where the barrier and the
// cache both belong.
void aot_bridge::define_accessor(aot::ct_aot_frame * f, std::uint64_t target,
                                 const aot_name_record * name, std::uint64_t getter,
                                 std::uint64_t setter) {
    context & cx = *frame_of(f).ctx;
    cx.define_accessor(value::from_bits(target), name->text, value::from_bits(getter),
                       value::from_bits(setter));
}

// ct_aot_copy_props. Object spread, through the shared member.
//
// (0, 0, 0) AND THAT IS VERIFIED RATHER THAN COPIED. object_object::set
// grows a std::vector and a hash map - malloc, not allocate() - so no GC
// object is created, nothing can throw and no accessor runs. Taking
// ct_aot_append's TRAITS because the shape looks alike would overstate the
// row; it is the lowering shape that is alike, not the effects.
void aot_bridge::copy_props(aot::ct_aot_frame * f, std::uint64_t target, std::uint64_t source) {
    context & cx = *frame_of(f).ctx;
    cx.copy_own_properties(value::from_bits(target), value::from_bits(source));
}

// ct_aot_cell_get. VM_CASE(cell_get) verbatim.
//
// NO FRAME AND NO FAILURE: the row is (0, 0, 0) and takes no handle at all,
// and its FAILURE line calls the silence a semantic guarantee - "a non-cell
// argument yields undefined silently", which is what lets the pair above
// compose without a second test.
std::uint64_t aot_bridge::cell_get(std::uint64_t cell) {
    const value held = value::from_bits(cell);
    return held.is_kind(heap_kind::cell) ? static_cast<cell_object *>(held.as_heap())->slot.bits()
                                         : value::undefined().bits();
}

// ct_aot_cell_set. VM_CASE(cell_set) verbatim, and silent on a non-cell for
// the same reason.
void aot_bridge::cell_set(std::uint64_t cell, std::uint64_t v) {
    const value held = value::from_bits(cell);
    if (held.is_kind(heap_kind::cell)) {
        static_cast<cell_object *>(held.as_heap())->slot = value::from_bits(v);
    }
}

// ct_aot_global_get. THE ABSENCE IS LOAD-BEARING and the row says so: an
// undeclared global does NOT throw a ReferenceError in this runtime, it
// reads `undefined` - or whatever the embedder's undeclared-name hook says,
// which is how an element with an `id` answers to a bare identifier.
// `context::global_or_named` is exactly what VM_CASE(get_global) runs, so
// the two tiers cannot drift.
//
// THE CONTEXT IS NO LONGER const, because the hook may allocate a wrapper.
// It still touches no frame beyond finding the context.
std::uint64_t aot_bridge::global_get(aot::ct_aot_frame * f, const char * name,
                                     std::uint32_t name_len) {
    context & cx = *frame_of(f).ctx;
    return cx.global_or_named(std::string_view{name, name_len}).bits();
}

// ct_aot_global_set. VM_CASE(set_global) is
// `globals_[names[in.bx()]] = reg(in.a)` (run_loop.cpp:235), and
// context::define_global is that assignment - so a global created by
// compiled code and one created by the interpreter are the same entry.
//
// THE NAME IS COPIED because the map owns its keys and the caller's
// characters are a `const char *` pointing into a generated translation
// unit's rodata - which outlives everything here, but the map's contract is
// ownership and borrowing from it would be a second rule to remember.
void aot_bridge::global_set(aot::ct_aot_frame * f, const char * name, std::uint32_t name_len,
                            std::uint64_t v) {
    context & cx = *frame_of(f).ctx;
    cx.define_global(std::string{name, name_len}, value::from_bits(v));
}

std::int32_t aot_bridge::get_index(aot::ct_aot_frame * f, std::uint64_t obj, std::uint64_t key,
                                   std::uint64_t * out) {
    context & cx = *frame_of(f).ctx;
    const value produced = cx.lookup_index(value::from_bits(obj), value::from_bits(key));
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

// ct_aot_intern_name. Idempotent: the same characters always give the same
// pointer, which is what lets a backend compare names by address.
const aot_name_record * aot_bridge::intern(const char * utf8, std::uint32_t len) {
    const std::string_view text{utf8, len};
    auto & index = name_index();
    if (const auto found = index.find(text); found != index.end()) { return found->second; }
    auto & records = name_records();
    records.push_back(aot_name_record{std::string{text}, hash_name(text)});
    const aot_name_record * record = &records.back();
    // KEYED BY A VIEW INTO THE RECORD'S OWN STRING, not by the caller's
    // characters - which are a `const char *` the caller is free to free.
    index.emplace(std::string_view{record->text}, record);
    return record;
}

std::int32_t aot_bridge::get_prop(aot::ct_aot_frame * f, std::uint64_t obj,
                                  const aot_name_record * name, std::uint64_t * out) {
    context & cx = *frame_of(f).ctx;
    // THROUGH THE INTERPRETER'S OWN lookup_property, hashing the name again
    // as it does. The row's payoff - reusing the interned hash across the
    // prototype chain - needs lookup_property to take a prehashed_name, and
    // that is a refactor of a long function with two dozen `name == "..."`
    // arms. It is an OPTIMISATION, worth its own change, and doing it here
    // would mean an extraction and a new ABI row in one step.
    const value produced = cx.lookup_property(value::from_bits(obj), name->text);
    const std::int32_t status = check(f);
    if (status == static_cast<std::int32_t>(aot::ct_aot_status::ok)) { *out = produced.bits(); }
    return status;
}

std::int32_t aot_bridge::set_prop(aot::ct_aot_frame * f, std::uint64_t obj,
                                  const aot_name_record * name, std::uint64_t v) {
    context & cx = *frame_of(f).ctx;
    cx.store_property(value::from_bits(obj), name->text, value::from_bits(v));
    return check(f);
}

} // namespace ctbrowser::script

namespace ctbrowser::aot {

extern "C" {

// THE HANDLE IS AN OPAQUE POINTER on the ABI's side and a record on this one,
// which is what `struct ct_aot_name` being declared and never defined is for.
const ct_aot_name * ct_aot_intern_name(const char * utf8, std::uint32_t len) {
    return reinterpret_cast<const ct_aot_name *>(script::aot_bridge::intern(utf8, len));
}

std::int32_t ct_aot_get_prop(ct_aot_frame * fr, std::uint64_t obj, const ct_aot_name * name,
                             ct_aot_ic * site, std::uint64_t * out) {
    (void)site; // Phase 26 attaches an inline cache here without an ABI break
    return script::aot_bridge::get_prop(
        fr, obj, reinterpret_cast<const script::aot_name_record *>(name), out);
}

std::int32_t ct_aot_set_prop(ct_aot_frame * fr, std::uint64_t obj, const ct_aot_name * name,
                             std::uint64_t v, ct_aot_ic * site) {
    (void)site;
    return script::aot_bridge::set_prop(fr, obj,
                                        reinterpret_cast<const script::aot_name_record *>(name), v);
}

std::uint64_t ct_aot_cell_new(ct_aot_frame * fr, std::uint64_t init) {
    return script::aot_bridge::cell_new(fr, init);
}

std::uint64_t ct_aot_new_object(ct_aot_frame * fr) {
    return script::aot_bridge::new_object(fr);
}

std::uint64_t ct_aot_new_array(ct_aot_frame * fr, std::uint32_t reserve_hint) {
    return script::aot_bridge::new_array(fr, reserve_hint);
}

void ct_aot_append(ct_aot_frame * fr, std::uint64_t array, std::uint64_t v) {
    script::aot_bridge::append(fr, array, v);
}

std::uint64_t ct_aot_new_string(ct_aot_frame * fr, const ct_aot_site * site, std::uint32_t slot,
                                const char * utf8, std::uint32_t len) {
    return script::aot_bridge::new_string(fr, site, slot, utf8, len);
}

std::int32_t ct_aot_set_index(ct_aot_frame * fr, std::uint64_t obj, std::uint64_t key,
                              std::uint64_t v, ct_aot_ic * site) {
    (void)site;
    return script::aot_bridge::set_index(fr, obj, key, v);
}

std::uint64_t ct_aot_cell_get(std::uint64_t cell) {
    return script::aot_bridge::cell_get(cell);
}

void ct_aot_cell_set(std::uint64_t cell, std::uint64_t v) {
    script::aot_bridge::cell_set(cell, v);
}

std::uint64_t ct_aot_global_get(ct_aot_frame * fr, const char * name, std::uint32_t name_len) {
    return script::aot_bridge::global_get(fr, name, name_len);
}

void ct_aot_global_set(ct_aot_frame * fr, const char * name, std::uint32_t name_len,
                       std::uint64_t v) {
    script::aot_bridge::global_set(fr, name, name_len, v);
}

std::int32_t ct_aot_get_index(ct_aot_frame * fr, std::uint64_t obj, std::uint64_t key,
                              ct_aot_ic * site, std::uint64_t * out) {
    (void)site;
    return script::aot_bridge::get_index(fr, obj, key, out);
}

std::int32_t ct_aot_iterable_values(ct_aot_frame * fr, std::uint64_t source, std::uint64_t * out) {
    return script::aot_bridge::iterable_values(fr, source, out);
}

std::int32_t ct_aot_has_property(ct_aot_frame * fr, std::uint64_t target, std::uint64_t key,
                                 std::uint32_t * out) {
    return script::aot_bridge::has_property(fr, target, key, out);
}

std::int32_t ct_aot_delete_index(ct_aot_frame * fr, std::uint64_t target, std::uint64_t key) {
    return script::aot_bridge::delete_index(fr, target, key);
}

std::uint64_t ct_aot_new_bigint_literal(ct_aot_frame * fr, const ct_aot_site * site,
                                        std::uint32_t slot, const char * literal,
                                        std::uint32_t len) {
    return script::aot_bridge::new_bigint_literal(fr, site, slot, literal, len);
}

void ct_aot_delete_prop(ct_aot_frame * fr, std::uint64_t target, const ct_aot_name * name) {
    script::aot_bridge::delete_prop(fr, target,
                                    reinterpret_cast<const script::aot_name_record *>(name));
}

std::uint64_t ct_aot_own_keys(ct_aot_frame * fr, std::uint64_t source) {
    return script::aot_bridge::own_keys(fr, source);
}

void ct_aot_define_accessor(ct_aot_frame * fr, std::uint64_t target, const ct_aot_name * name,
                            std::uint64_t getter, std::uint64_t setter) {
    script::aot_bridge::define_accessor(
        fr, target, reinterpret_cast<const script::aot_name_record *>(name), getter, setter);
}

void ct_aot_copy_props(ct_aot_frame * fr, std::uint64_t target, std::uint64_t source) {
    script::aot_bridge::copy_props(fr, target, source);
}

std::uint64_t ct_aot_get_proto(ct_aot_frame * fr, std::uint64_t target) {
    return script::aot_bridge::get_proto(fr, target);
}

void ct_aot_set_proto(ct_aot_frame * fr, std::uint64_t target, std::uint64_t proto) {
    script::aot_bridge::set_proto(fr, target, proto);
}

} // extern "C"

} // namespace ctbrowser::aot
