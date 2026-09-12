// WHICH BYTECODE OPERATIONS THE IMPORTER UNDERSTANDS, MEASURED RATHER THAN
// LISTED.
//
// Phase 13's tracking mechanism, and the plan is specific about why it is
// shaped this way: "Coverage is measured against the bytecode_opcodes.def
// table, not against a hand-kept checklist that will fall out of date... so a
// newly added opcode fails the test until it is handled or explicitly listed as
// suspending."
//
// BOTH SIDES ARE DERIVED, WHICH IS THE WHOLE POINT. The opcode list comes from
// the X-macro, so it cannot disagree with the engine. What the importer handles
// comes from the importer's own SOURCE, so it cannot disagree with the
// importer. Nothing here is a transcription of either, and a transcription is
// what this project has already watched drift twice - once in a fixture that
// was duplicated in two files, once in an ABI table whose line numbers rotted.
//
// WHY IT READS SOURCE INSTEAD OF CALLING SOMETHING. The importer dispatches
// from a `switch (in.code)` and from two dispatch TABLES, and there is no
// runtime predicate that answers "do you handle this opcode" - adding one would
// be a third place to keep in step with the switch, which is the drift this
// test exists to prevent. The build passes the driver, instruction-dispatch
// and operator-table paths, so moving dispatch into a helper keeps it visible.
//
// IT IS A GATE: every non-suspending opcode in the table must be dispatched.
// A new opcode fails here until the importer handles it or the .def marks it
// `may_suspend`; an opcode that stops being handled fails the same way.
#include <ctbrowser/script/bytecode.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

namespace {

struct row {
    std::string_view name;
    bool may_suspend;
};

#define CT_OPCODE(name_, a_kind_, b_kind_, c_kind_, writes_a_, allocates_, may_throw_,             \
                  may_reenter_, is_safepoint_, may_suspend_, resumable_, impl_)                    \
    row{#name_, (may_suspend_) != 0},

constexpr row table[] = {
#include <ctbrowser/script/bytecode_opcodes.def>
};

#undef CT_OPCODE

// A DISPATCH SITE, NOT A MENTION. The importer reaches an opcode two ways - a
// `case op::x:` label in its switch, and a `{op::x, ...}` row in one of its two
// dispatch tables - and it also TALKS about opcodes in comments. Counting a
// comment as coverage would let a prose mention hide a missing case, which is
// exactly the failure this test is for.
bool dispatches(const std::string & source, std::string_view opcode) {
    const std::string needle = "op::" + std::string{opcode};
    for (std::size_t at = source.find(needle); at != std::string::npos;
         at = source.find(needle, at + 1)) {
        const std::size_t after = at + needle.size();
        // The name must END here: `op::add` must not match inside
        // `op::add_generic`.
        if (after < source.size() &&
            (std::isalnum(static_cast<unsigned char>(source[after])) != 0 ||
             source[after] == '_')) {
            continue;
        }
        std::size_t tail = after;
        while (tail < source.size() &&
               std::isspace(static_cast<unsigned char>(source[tail])) != 0) {
            ++tail;
        }
        if (tail < source.size() && source[tail] == ':') { return true; } // a case label
        std::size_t head = at;
        while (head > 0 && std::isspace(static_cast<unsigned char>(source[head - 1])) != 0) {
            --head;
        }
        if (head > 0 && source[head - 1] == '{') { return true; } // a table row
    }
    return false;
}

} // namespace

int main() {
    std::ostringstream text;
    for (const char * path :
         {CTCOMPILE_IMPORTER_SOURCE, CTCOMPILE_IMPORTER_DISPATCH, CTCOMPILE_IMPORTER_TABLES}) {
        std::ifstream in{path};
        if (!in) {
            std::printf("could not read the importer's source at %s\n", path);
            return 1;
        }
        text << in.rdbuf() << '\n';
    }
    const std::string source = text.str();

    // A SANITY CHECK ON THE MATCHER ITSELF, because a matcher that finds
    // nothing would report every opcode as missing, and a matcher that finds
    // everything would report none - and both would look like a result.
    // op::add is dispatched from a table and op::iterable from a case label,
    // so one of each is pinned.
    if (!dispatches(source, "add") || !dispatches(source, "iterable")) {
        std::printf("the matcher found neither a table row nor a case label it should have - "
                    "the importer's dispatch has been rewritten and this test cannot read it\n");
        return 1;
    }
    if (dispatches(source, "await_value")) {
        std::printf("the matcher claims op::await_value is dispatched, which would mean it is "
                    "matching prose - every mention would then count as coverage\n");
        return 1;
    }

    std::size_t missing = 0;
    for (const row & each : table) {
        if (each.may_suspend || dispatches(source, each.name)) { continue; }
        std::printf("FAIL %.*s is a non-suspending opcode the importer does not dispatch. Add a "
                    "case for it, or mark it may_suspend in bytecode_opcodes.def.\n",
                    static_cast<int>(each.name.size()), each.name.data());
        ++missing;
    }

    const auto suspending = static_cast<std::size_t>(std::count_if(
        std::begin(table), std::end(table), [](const row & r) { return r.may_suspend; }));
    std::printf("%zu opcodes: %zu suspending, %zu imported, %zu missing\n", std::size(table),
                suspending, std::size(table) - suspending - missing, missing);

    return missing == 0 ? 0 : 1;
}
