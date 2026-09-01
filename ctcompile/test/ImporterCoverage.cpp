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
// test exists to prevent. The build passes the path in as
// CTCOMPILE_IMPORTER_SOURCE.
//
// IT IS A RATCHET, NOT A GATE. Eighteen non-suspending opcodes are unhandled
// today and the Phase 13 gate is that none are, so a test demanding zero would
// simply be red. Instead the pending list below must match EXACTLY: an opcode
// that stops being handled fails, a newly added opcode fails, and an opcode
// that BECOMES handled fails until its line is deleted. The list can only
// shrink, and the failure message is the work list.
#include <ctbrowser/script/bytecode.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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

// WHAT IS NOT IMPORTED YET, WITH WHY - and the why matters, because these are
// not one backlog. Three of them are other phases' work and would be wrong to
// pull forward; the rest are Phase 13's own.
struct pending {
    std::string_view opcode;
    std::string_view why;
};

constexpr pending not_yet[] = {
    // ---- Phase 13's own work list, in Bootstrap's order of cost -----------
    {"gather_rest", "a rest parameter, f(...xs) - 3 Bootstrap functions"},
    {"make_arguments", "the `arguments` object, which gather_rest's ABI row says it READS"},

    // ---- NOT Phase 13. Listed so the gap is visible, not so it is worked. --
    // wrap_promise WAS HERE, and its line said it was "only non-suspending
    // because the WRAP is". That turned out to be the whole reason it could be
    // landed on its own: an async function containing no `await` carries this
    // opcode and no await_value at all, so it is fully AOT-eligible. It is
    // Phase 14's, and it is done.
    {"load_import", "ES modules - Phases 15-16"},
    {"bind_export", "ES modules - Phases 15-16"},
    {"dyn_import", "dynamic import() - Phases 15-16"},
    {"load_namespace", "ES modules - Phases 15-16"},
};

int failures = 0;

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
    std::ifstream in{CTCOMPILE_IMPORTER_SOURCE};
    if (!in) {
        std::printf("could not read the importer's source at %s\n", CTCOMPILE_IMPORTER_SOURCE);
        return 1;
    }
    std::ostringstream text;
    text << in.rdbuf();
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

    std::vector<std::string_view> missing;
    for (const row & each : table) {
        if (each.may_suspend) { continue; }
        if (!dispatches(source, each.name)) { missing.push_back(each.name); }
    }

    const auto listed = [](std::string_view name) {
        return std::any_of(std::begin(not_yet), std::end(not_yet),
                           [&](const pending & p) { return p.opcode == name; });
    };

    // A GAP NOBODY DECLARED. Either an opcode was added to the table with no
    // importer case, or one stopped being dispatched.
    for (const std::string_view name : missing) {
        if (!listed(name)) {
            std::printf("FAIL %.*s is a non-suspending opcode the importer does not dispatch, and "
                        "it is not in the pending list. Add a case for it, or add a line saying "
                        "why not.\n",
                        static_cast<int>(name.size()), name.data());
            ++failures;
        }
    }

    // AND A LINE THAT IS NO LONGER TRUE, which matters just as much: a stale
    // entry makes the list stop being a measurement.
    for (const pending & each : not_yet) {
        const bool still_missing =
            std::find(missing.begin(), missing.end(), each.opcode) != missing.end();
        if (!still_missing) {
            std::printf("FAIL %.*s IS dispatched now - delete its line from not_yet, or this list "
                        "stops being a measurement of anything.\n",
                        static_cast<int>(each.opcode.size()), each.opcode.data());
            ++failures;
        }
    }

    const auto suspending = static_cast<std::size_t>(std::count_if(
        std::begin(table), std::end(table), [](const row & r) { return r.may_suspend; }));
    std::printf("%zu opcodes: %zu suspending, %zu imported, %zu pending\n", std::size(table),
                suspending, std::size(table) - suspending - missing.size(), missing.size());

    if (failures == 0 && !missing.empty()) {
        std::printf("\nPhase 13 is complete when this list is empty:\n");
        for (const pending & each : not_yet) {
            std::printf("  %-16.*s %.*s\n", static_cast<int>(each.opcode.size()),
                        each.opcode.data(), static_cast<int>(each.why.size()), each.why.data());
        }
    }
    return failures == 0 ? 0 : 1;
}
