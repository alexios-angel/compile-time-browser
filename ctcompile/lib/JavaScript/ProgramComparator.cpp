#include <ctcompile/JavaScript/ProgramComparator.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace ctcompile::js {

using ctbrowser::script::function_proto;
using ctbrowser::script::program;
using ctbrowser::script::script_kind;

namespace {

struct walker {
    std::optional<difference> found;

    bool differ(std::string where, std::string what) {
        found = difference{std::move(where), std::move(what)};
        return false;
    }
    template <typename T>
    bool same(const T & a, const T & b, const std::string & where, const char * what) {
        if (a == b) { return true; }
        return differ(where, what);
    }
    bool count(std::size_t a, std::size_t b, const std::string & where, const char * what) {
        if (a == b) { return true; }
        return differ(where,
                      std::string{what} + " " + std::to_string(a) + " vs " + std::to_string(b));
    }
    bool texts(const std::vector<std::string> & a, const std::vector<std::string> & b,
               const std::string & where, const char * what) {
        if (!count(a.size(), b.size(), where, what)) { return false; }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                return differ(where, std::string{what} + " " + std::to_string(i) + ": \"" + a[i] +
                                         "\" vs \"" + b[i] + "\"");
            }
        }
        return true;
    }
};

} // namespace

std::optional<difference> compare(const program & expected, const program & actual) {
    walker w;

    if (!w.same(expected.ok, actual.ok, "", "one program compiled and the other did not")) {
        return w.found;
    }
    if (!w.same(expected.error, actual.error, "", "the compile error differs")) { return w.found; }

    // ORDER MATTERS FOR TWO OF THESE THREE. Import order fixes depth-first
    // instantiation and therefore the order of modules' top-level side effects;
    // the FIRST claim on a re-exported name wins, so swapping two re-exports
    // changes which cell a name resolves to. Export order is not observable -
    // a namespace object's keys come from a hash map - but it is compared
    // anyway, because an image has no reason to reorder it and a change there
    // means something else moved.
    // THE KIND FIRST, because everything below it means something different
    // depending on the answer: a module's top-level declarations are its own
    // scope's and a classic script's are the global object's, so two programs
    // that differ here are not two versions of one thing.
    if (expected.kind != actual.kind) {
        return w.differ("kind", expected.kind == script_kind::module_
                                    ? "expected a module, got a classic script"
                                    : "expected a classic script, got a module"),
               w.found;
    }
    if (!w.texts(expected.imports, actual.imports, "", "imports")) { return w.found; }
    if (!w.texts(expected.exports, actual.exports, "", "exports")) { return w.found; }
    if (!w.count(expected.reexports.size(), actual.reexports.size(), "", "re-export count")) {
        return w.found;
    }
    for (std::size_t i = 0; i < expected.reexports.size(); ++i) {
        const auto & a = expected.reexports[i];
        const auto & b = actual.reexports[i];
        const std::string where = "re-export " + std::to_string(i);
        if (a.exported != b.exported) { return w.differ(where, "exported name"), w.found; }
        if (a.source != b.source) { return w.differ(where, "source name"), w.found; }
        if (a.from != b.from) { return w.differ(where, "specifier"), w.found; }
    }

    if (expected.source != actual.source) {
        // Reported as a length rather than a diff: these are megabytes.
        return w.differ("source", "retained source differs (" +
                                      std::to_string(expected.source.size()) + " vs " +
                                      std::to_string(actual.source.size()) + " bytes)"),
               w.found;
    }

    if (!w.count(expected.functions.size(), actual.functions.size(), "", "function count")) {
        return w.found;
    }
    for (std::size_t fi = 0; fi < expected.functions.size(); ++fi) {
        const function_proto & a = expected.functions[fi];
        const function_proto & b = actual.functions[fi];
        const std::string where = "function " + std::to_string(fi);

        if (!w.same(a.module, b.module, where, "module")) { return w.found; }
        if (!w.same(a.name, b.name, where, "name")) { return w.found; }
        if (!w.same(a.param_count, b.param_count, where, "param_count")) { return w.found; }
        if (!w.same(a.frame_size, b.frame_size, where, "frame_size")) { return w.found; }
        if (!w.same(a.is_arrow, b.is_arrow, where, "is_arrow")) { return w.found; }
        if (!w.same(a.is_generator, b.is_generator, where, "is_generator")) { return w.found; }
        if (!w.same(a.source_begin, b.source_begin, where, "source_begin")) { return w.found; }
        if (!w.same(a.source_end, b.source_end, where, "source_end")) { return w.found; }

        if (!w.count(a.code.size(), b.code.size(), where, "instruction count")) { return w.found; }
        for (std::size_t ip = 0; ip < a.code.size(); ++ip) {
            const auto & x = a.code[ip];
            const auto & y = b.code[ip];
            // FIELD BY FIELD, NOT AS BYTES. `instruction` has a padding byte at
            // offset 1 whose contents depend on the optimisation level of the
            // code that built it, so a memcmp here would report differences
            // that do not exist - and would do it intermittently.
            if (x.code != y.code || x.a != y.a || x.b != y.b || x.c != y.c) {
                return w.differ(where + ", instruction " + std::to_string(ip),
                                "opcode or operands"),
                       w.found;
            }
        }

        if (!w.count(a.constants.size(), b.constants.size(), where, "constant count")) {
            return w.found;
        }
        for (std::size_t i = 0; i < a.constants.size(); ++i) {
            // BY BITS. See the header: as doubles, NaN would differ from itself
            // and -0 would compare equal to +0.
            if (a.constants[i].bits() != b.constants[i].bits()) {
                return w.differ(where + ", constant " + std::to_string(i), "bit pattern"), w.found;
            }
        }

        if (!w.texts(a.strings, b.strings, where, "strings")) { return w.found; }
        if (!w.texts(a.names, b.names, where, "names")) { return w.found; }

        if (!w.count(a.upvalues.size(), b.upvalues.size(), where, "upvalue count")) {
            return w.found;
        }
        for (std::size_t i = 0; i < a.upvalues.size(); ++i) {
            if (a.upvalues[i].from_parent_local != b.upvalues[i].from_parent_local ||
                a.upvalues[i].index != b.upvalues[i].index) {
                return w.differ(where + ", upvalue " + std::to_string(i),
                                "capture source or index"),
                       w.found;
            }
        }

        // THE DEBUG SIDE TABLES, compared for the same reason everything above
        // is: a round trip that silently drops them looks exactly like a build
        // with CTBROWSER_SCRIPT_DEBUG_NAMES off, and the round-trip test is the
        // only thing that can tell the two apart.
        if (!w.count(a.locals.size(), b.locals.size(), where, "local count")) { return w.found; }
        for (std::size_t i = 0; i < a.locals.size(); ++i) {
            if (a.locals[i].name != b.locals[i].name || a.locals[i].reg != b.locals[i].reg ||
                a.locals[i].first_pc != b.locals[i].first_pc ||
                a.locals[i].last_pc != b.locals[i].last_pc ||
                a.locals[i].boxed != b.locals[i].boxed) {
                return w.differ(where + ", local " + std::to_string(i),
                                "name, register, live range or boxedness"),
                       w.found;
            }
        }

        if (!w.count(a.code_offsets.size(), b.code_offsets.size(), where, "source-offset count")) {
            return w.found;
        }
        for (std::size_t i = 0; i < a.code_offsets.size(); ++i) {
            if (a.code_offsets[i] != b.code_offsets[i]) {
                return w.differ(where + ", instruction " + std::to_string(i), "source offset"),
                       w.found;
            }
        }
    }
    return std::nullopt;
}

} // namespace ctcompile::js
