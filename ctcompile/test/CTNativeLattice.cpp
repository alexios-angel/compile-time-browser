// THE ctnative TYPE LATTICE, AND THE FIRST DECLARED DIVERGENCE.
//
// Part 24's gate for Phase 53: "the lattice has a unit test with a hand-written
// meet table". The table is `kMeetTable` below and it is written by hand on
// purpose - it is the one place in this phase where the expected answer is
// stated independently of the code that computes it.
//
// A TABLE ALONE IS NOT ENOUGH, and the reason is the shape of the bug part 24
// is worried about: "two places computing 'what type is this' differently is
// how a transpiler produces a program that is right on Tuesday." A table checks
// the cases somebody thought of. What makes `meet` usable as the confluence
// operator of a dataflow analysis is that it is a LATTICE JOIN - idempotent,
// commutative, associative, with `bottom` as its identity and `boxed`
// absorbing - and those are properties, not cases. So the table is checked, and
// then all four laws are checked exhaustively over a sample of the type
// universe; associativity alone is 21^3 = 9261 triples.
//
// THE TWO HALVES CATCH DIFFERENT THINGS, and that was measured rather than
// hoped for. Three guards in CTNativeLattice.cpp were removed one at a time and
// the suite re-run:
//
//   the `opt` hoist in collect()   -> SIX TABLE ROWS red, 0 of 9261 triples.
//                                     An opaque `opt` alternative satisfies
//                                     every law; what it loses is the canonical
//                                     form, which only the table states.
//   the re-sort after the merge    -> 4 of 9261 triples red, TABLE ALL GREEN.
//                                     Two spellings of one type, reachable only
//                                     through a three-way bracketing.
//   the variant cap                -> one cap check red, both halves green.
//
// Neither half would have been enough on its own.
//
// AND THE DIVERGENCE IS PINNED AGAINST THE RUNNING INTERPRETER, never against a
// number written down here. `stringLengthPinnedToInterpreter` compiles and runs
// three lines of JavaScript in the real VM and asks IT what `"\u{1F600}".length`
// is. Part 24 §A.2: "Every phase's gate is a comparison against the
// interpreter."
#include <ctcompile/CTNative/IR/CTNativeDialect.h>
#include <ctcompile/CTNative/IR/CTNativeInterfaces.h>
#include <ctcompile/CTNative/IR/CTNativeLattice.h>
#include <ctcompile/CTNative/IR/CTNativeTypes.h>

#include <ctbrowser/script/builtins.hpp>
#include <ctbrowser/script/compile.hpp>
#include <ctbrowser/script/vm.hpp>

#include "mlir/AsmParser/AsmParser.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using ctcompile::ctnative::CTNativeDialect;
using ctcompile::ctnative::kDefaultStringEncoding;
using ctcompile::ctnative::kMaxVariantAlternatives;
using ctcompile::ctnative::meet;
using ctcompile::ctnative::StaticTypedOpInterface;
using ctcompile::ctnative::StrEncoding;
using mlir::Type;

namespace {

int checks = 0;
int failures = 0;

void check(const char * what, bool ok) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAILED  %s\n", what);
    }
}

std::string show(Type type) {
    if (!type) { return "<null>"; }
    std::string buffer;
    llvm::raw_string_ostream stream(buffer);
    stream << type;
    return buffer;
}

// THE TYPE UNIVERSE, AS TEXT. Spelled the way a person writes it in a .mlir
// file rather than built with get() calls, so the table below reads as a table
// and so that every line of it also exercises the generated parser.
Type parse(mlir::MLIRContext & context, std::string_view text) {
    Type parsed = mlir::parseType(llvm::StringRef(text.data(), text.size()), &context);
    if (!parsed) {
        std::printf("FAILED  could not parse the type `%.*s`\n", int(text.size()), text.data());
    }
    return parsed;
}

//===--------------------------------------------------------------------===//
// The hand-written meet table
//===--------------------------------------------------------------------===//
//
// Read as: meeting `a` with `b` gives `expected`. Every row is an assertion
// about the LATTICE, not about the implementation, and each carries the reason
// it is that answer and not another.
struct MeetRow {
    const char * a;
    const char * b;
    const char * expected;
    const char * why;
};

const MeetRow kMeetTable[] = {
    // --- the identity and the absorbing element -----------------------------
    {"!ctnative.bottom", "!ctnative.num<f64>", "!ctnative.num<f64>",
     "bottom is the identity: no value reached here, so the other side stands"},
    {"!ctnative.bottom", "!ctnative.boxed", "!ctnative.boxed",
     "the identity holds at the top too, or it is not an identity"},
    {"!ctnative.bottom", "!ctnative.bottom", "!ctnative.bottom", "and it is idempotent"},
    {"!ctnative.boxed", "!ctnative.num<i32>", "!ctnative.boxed",
     "boxed absorbs: once a value needs the runtime, proving something about "
     "one path does not un-need it"},
    {"!ctnative.boxed", "!ctnative.json", "!ctnative.boxed",
     "boxed is above json - json is a C++ representation, boxed is a refusal"},

    // --- the numeric chain --------------------------------------------------
    {"!ctnative.num<i32>", "!ctnative.num<i32>", "!ctnative.num<i32>", "idempotent"},
    {"!ctnative.num<i32>", "!ctnative.num<i64>", "!ctnative.num<i64>",
     "i32 -> i64: the wider bound proof is the one that holds on both paths"},
    {"!ctnative.num<i32>", "!ctnative.num<f64>", "!ctnative.num<f64>",
     "a proof that a value fits an int32 is not a proof about a path that did not"},
    {"!ctnative.num<i64>", "!ctnative.num<f64>", "!ctnative.num<f64>",
     "f64 is what a JavaScript number IS; i64 was only ever a narrowing of it"},

    // --- booleans do not join the numeric chain -----------------------------
    {"!ctnative.bool", "!ctnative.bool", "!ctnative.bool", "idempotent"},
    {"!ctnative.bool", "!ctnative.num<f64>",
     "!ctnative.variant<!ctnative.bool, !ctnative.num<f64>>",
     "`true + 1` is 2 by COERCION, not because the two share a representation; "
     "folding bool into the numbers would make typeof wrong"},

    // --- strings ------------------------------------------------------------
    {"!ctnative.str<utf8>", "!ctnative.str<utf16>", "!ctnative.str<utf16>",
     "utf16 is the wider REPRESENTATION - it holds lone surrogates and utf8 "
     "cannot encode them at all"},
    {"!ctnative.strview<utf8>", "!ctnative.strview<utf16>", "!ctnative.strview<utf16>",
     "the same widening, and a view stays a view"},
    {"!ctnative.strview<utf8>", "!ctnative.str<utf8>", "!ctnative.str<utf8>",
     "a view widens into an owning string; the reverse needs a lifetime proof "
     "this phase does not have"},
    {"!ctnative.strview<utf16>", "!ctnative.str<utf8>", "!ctnative.str<utf16>",
     "both widenings at once, and they are independent"},

    // --- opt is a lift, and it is hoisted outermost -------------------------
    {"!ctnative.opt<!ctnative.num<i32>>", "!ctnative.num<f64>", "!ctnative.opt<!ctnative.num<f64>>",
     "meet(opt<T>, U) is opt<meet(T, U)> - nullability travels through the "
     "lattice instead of doubling it"},
    {"!ctnative.opt<!ctnative.num<i32>>", "!ctnative.opt<!ctnative.num<i64>>",
     "!ctnative.opt<!ctnative.num<i64>>", "two lifts stay one lift"},
    {"!ctnative.opt<!ctnative.bool>", "!ctnative.num<f64>",
     "!ctnative.opt<!ctnative.variant<!ctnative.bool, !ctnative.num<f64>>>",
     "the CANONICAL FORM: opt outside, variant inside, never the other way"},
    {"!ctnative.opt<!ctnative.bool>", "!ctnative.boxed", "!ctnative.boxed",
     "boxed already holds undefined, so lifting it would be a second spelling"},
    {"!ctnative.opt<!ctnative.bool>", "!ctnative.json", "!ctnative.json",
     "and so does boost::json::value, which has a null kind"},

    // --- unions -------------------------------------------------------------
    {"!ctnative.bool", "!ctnative.str<utf8>",
     "!ctnative.variant<!ctnative.bool, !ctnative.str<utf8>>",
     "two unrelated static types are genuinely both, and std::variant says so"},
    {"!ctnative.variant<!ctnative.bool, !ctnative.str<utf8>>", "!ctnative.bool",
     "!ctnative.variant<!ctnative.bool, !ctnative.str<utf8>>",
     "a variant absorbs an alternative it already has"},
    {"!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>", "!ctnative.num<f64>",
     "!ctnative.variant<!ctnative.bool, !ctnative.num<f64>>",
     "and MERGES one it can: two numbers inside a union are not a union"},
    {"!ctnative.json", "!ctnative.bool", "!ctnative.json",
     "json absorbs everything below it - a value it could not type does not "
     "become typeable because one path was a bool"},

    // --- containers ---------------------------------------------------------
    {"!ctnative.vec<!ctnative.num<i32>>", "!ctnative.vec<!ctnative.num<f64>>",
     "!ctnative.vec<!ctnative.num<f64>>", "containers meet elementwise"},
    {"!ctnative.vec<!ctnative.bool>", "!ctnative.set<!ctnative.bool>",
     "!ctnative.variant<!ctnative.set<!ctnative.bool>, !ctnative.vec<!ctnative.bool>>",
     "different container kinds are not a family - and the alternatives come "
     "back in PRINTED order, which is what makes meet(a,b) == meet(b,a)"},
    {"!ctnative.map<!ctnative.str<utf8>, !ctnative.num<i32>>",
     "!ctnative.map<!ctnative.str<utf16>, !ctnative.num<f64>>",
     "!ctnative.map<!ctnative.str<utf16>, !ctnative.num<f64>>", "both parameters, independently"},

    // --- the three pointers -------------------------------------------------
    {"!ctnative.owned<!ctnative.bool>", "!ctnative.shared<!ctnative.bool>",
     "!ctnative.shared<!ctnative.bool>",
     "a unique owner can always be made shared; the reverse would be the proof "
     "that failed"},
    {"!ctnative.owned<!ctnative.num<i32>>", "!ctnative.owned<!ctnative.num<f64>>",
     "!ctnative.owned<!ctnative.num<f64>>", "two unique owners stay unique"},
    {"!ctnative.weak<!ctnative.bool>", "!ctnative.shared<!ctnative.bool>",
     "!ctnative.variant<!ctnative.shared<!ctnative.bool>, !ctnative.weak<!ctnative.bool>>",
     "STAGE 55C: silently unifying a strong and a weak reference is the "
     "miscompile the whole weak rule exists to prevent, so they are unordered"},
};

//===--------------------------------------------------------------------===//
// The sample of the type universe the lattice laws are checked over
//===--------------------------------------------------------------------===//
const char * const kSampleTypes[] = {
    "!ctnative.bottom",
    "!ctnative.boxed",
    "!ctnative.json",
    "!ctnative.bool",
    "!ctnative.num<i32>",
    "!ctnative.num<i64>",
    "!ctnative.num<f64>",
    "!ctnative.str<utf8>",
    "!ctnative.str<utf16>",
    "!ctnative.strview<utf8>",
    "!ctnative.strview<utf16>",
    "!ctnative.opt<!ctnative.num<i32>>",
    "!ctnative.opt<!ctnative.bool>",
    "!ctnative.vec<!ctnative.num<i32>>",
    "!ctnative.vec<!ctnative.bool>",
    "!ctnative.set<!ctnative.bool>",
    "!ctnative.map<!ctnative.str<utf8>, !ctnative.num<f64>>",
    "!ctnative.owned<!ctnative.bool>",
    "!ctnative.shared<!ctnative.bool>",
    "!ctnative.weak<!ctnative.bool>",
    "!ctnative.variant<!ctnative.bool, !ctnative.num<f64>>",
};

//===--------------------------------------------------------------------===//
// The StaticTyped interface, attached to an operation this dialect does not own
//===--------------------------------------------------------------------===//
//
// PHASE 53 HAS NO OPERATIONS - that is the phase - so the interface is proved
// against `builtin.unrealized_conversion_cast`, which can carry any result type
// at all. An ExternalModel with no overrides uses the DEFAULT implementation
// declared in CTNativeInterfaces.td, which is exactly the thing under test:
// that the ODS default reads the single result's type and that the shared
// `hasProvedNativeType()` can tell a proved type from an unproved one.
struct CastIsStaticTyped
    : public StaticTypedOpInterface::ExternalModel<CastIsStaticTyped,
                                                   mlir::UnrealizedConversionCastOp> {};

//===--------------------------------------------------------------------===//
// The divergence pin
//===--------------------------------------------------------------------===//
//
// How many code units the UTF-8 bytes `utf8` occupy under `encoding` - which is
// what `.length` answers for a string in that representation.
std::size_t codeUnitsIn(StrEncoding encoding, std::string_view utf8) {
    if (encoding == StrEncoding::UTF8) { return utf8.size(); }
    std::size_t units = 0;
    for (std::size_t at = 0; at < utf8.size();) {
        const unsigned lead = static_cast<unsigned char>(utf8[at]);
        std::size_t width = 1;
        if (lead >= 0xF0) {
            width = 4;
        } else if (lead >= 0xE0) {
            width = 3;
        } else if (lead >= 0xC0) {
            width = 2;
        }
        // A supplementary code point - one that needs four UTF-8 bytes - is a
        // SURROGATE PAIR in UTF-16 and therefore two code units. That is the
        // whole of the "\u{1F600}".length disagreement, in one line.
        units += (width == 4) ? 2u : 1u;
        at += width;
    }
    return units;
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.loadDialect<CTNativeDialect>();

    // --- the hand-written meet table ----------------------------------------
    for (const MeetRow & row : kMeetTable) {
        const Type a = parse(context, row.a);
        const Type b = parse(context, row.b);
        const Type expected = parse(context, row.expected);
        if (!a || !b || !expected) {
            ++failures;
            continue;
        }
        const Type got = meet(a, b);
        ++checks;
        if (got != expected) {
            ++failures;
            std::printf("FAILED  meet(%s, %s)\n            expected %s\n            got      %s\n"
                        "            because  %s\n",
                        row.a, row.b, row.expected, show(got).c_str(), row.why);
        }
        // AND THE OTHER WAY ROUND, on every row, because a table checked in one
        // direction is a table that would not have noticed an unstable
        // alternative order.
        ++checks;
        if (meet(b, a) != expected) {
            ++failures;
            std::printf("FAILED  meet(%s, %s) is not meet(%s, %s)\n", row.b, row.a, row.a, row.b);
        }
    }

    // --- the four lattice laws, over the sample ------------------------------
    std::vector<Type> sample;
    for (const char * text : kSampleTypes) {
        const Type parsed = parse(context, text);
        if (!parsed) {
            ++failures;
            continue;
        }
        sample.push_back(parsed);
    }
    check("every sample type parsed", sample.size() == std::size(kSampleTypes));

    const Type bottom = ctcompile::ctnative::BottomType::get(&context);
    const Type boxed = ctcompile::ctnative::BoxedType::get(&context);

    int lawFailures = 0;
    for (const Type a : sample) {
        // Idempotence and the identity, which are the two the analysis relies on
        // for its initial state.
        if (meet(a, a) != a) { ++lawFailures; }
        if (meet(bottom, a) != a) { ++lawFailures; }
        if (meet(a, bottom) != a) { ++lawFailures; }
        if (meet(boxed, a) != boxed) { ++lawFailures; }
        if (meet(a, boxed) != boxed) { ++lawFailures; }
        // A null Type is ABSENT, not bottom - the documented seedless fold.
        if (meet(Type(), a) != a) { ++lawFailures; }
    }
    check("idempotent, bottom is the identity, boxed absorbs, null is absent", lawFailures == 0);
    if (lawFailures != 0) {
        std::printf("        %d of the unary/identity laws failed\n", lawFailures);
    }

    int commuteFailures = 0;
    for (const Type a : sample) {
        for (const Type b : sample) {
            if (meet(a, b) != meet(b, a)) {
                ++commuteFailures;
                if (commuteFailures <= 5) {
                    std::printf("        meet(%s, %s) = %s but meet(%s, %s) = %s\n",
                                show(a).c_str(), show(b).c_str(), show(meet(a, b)).c_str(),
                                show(b).c_str(), show(a).c_str(), show(meet(b, a)).c_str());
                }
            }
        }
    }
    check("commutative over the whole sample", commuteFailures == 0);

    // ASSOCIATIVITY IS THE EXPENSIVE ONE AND THE ONE WORTH HAVING, and what it
    // actually guards is the SECOND sortAndUnique in CTNativeLattice.cpp's
    // finishUnion - the one that looks redundant. The merge loop replaces
    // members in place, so a list sorted before it is not sorted after it;
    // delete that call and `meet(set<bool>, meet(owned<bool>, shared<bool>))`
    // answers `variant<set, shared>` where the other bracketing answers
    // `variant<shared, set>`. Four of the 9261 triples go red and every row of
    // the hand-written table above stays green, which is why both exist.
    int associateFailures = 0;
    int triples = 0;
    for (const Type a : sample) {
        for (const Type b : sample) {
            for (const Type c : sample) {
                ++triples;
                const Type left = meet(meet(a, b), c);
                const Type right = meet(a, meet(b, c));
                if (left == right) { continue; }
                ++associateFailures;
                if (associateFailures <= 5) {
                    std::printf("        (%s ^ %s) ^ %s = %s\n         %s ^ (%s ^ %s) = %s\n",
                                show(a).c_str(), show(b).c_str(), show(c).c_str(),
                                show(left).c_str(), show(a).c_str(), show(b).c_str(),
                                show(c).c_str(), show(right).c_str());
                }
            }
        }
    }
    // ASSERT THE COUNTER, NEVER THE OUTPUT: a loop that ran zero triples would
    // report zero failures and look identical to a passing one.
    check("the associativity sweep covered every triple",
          triples == int(sample.size() * sample.size() * sample.size()));
    check("associative over the whole sample", associateFailures == 0);
    std::printf("associativity: %d triples, %d disagreements\n", triples, associateFailures);

    // --- the variant cap ----------------------------------------------------
    //
    // FIVE MUTUALLY UNMERGEABLE TYPES. bool, a number, a string, a vector and a
    // set share no family with each other, so nothing collapses them and the
    // union really does reach five. Past the cap the answer is json, which is
    // what keeps the lattice finite-height and Phase 54's analysis terminating.
    {
        const Type five[] = {
            parse(context, "!ctnative.bool"),
            parse(context, "!ctnative.num<f64>"),
            parse(context, "!ctnative.str<utf8>"),
            parse(context, "!ctnative.vec<!ctnative.bool>"),
            parse(context, "!ctnative.set<!ctnative.bool>"),
        };
        Type four = bottom;
        for (std::size_t i = 0; i < kMaxVariantAlternatives; ++i) { four = meet(four, five[i]); }
        const auto asVariant = llvm::dyn_cast<ctcompile::ctnative::VariantType>(four);
        check("four unmergeable types are a variant", static_cast<bool>(asVariant));
        if (asVariant) {
            check("with exactly four alternatives",
                  asVariant.getAlternatives().size() == kMaxVariantAlternatives);
        }
        const Type fifth = meet(four, five[kMaxVariantAlternatives]);
        check("the fifth falls off the cap into json",
              llvm::isa<ctcompile::ctnative::JsonType>(fifth));
    }

    // --- the StaticTyped interface ------------------------------------------
    {
        mlir::UnrealizedConversionCastOp::attachInterface<CastIsStaticTyped>(context);
        mlir::OpBuilder builder(&context);
        const mlir::Location loc = builder.getUnknownLoc();
        mlir::OwningOpRef<mlir::ModuleOp> module(mlir::ModuleOp::create(builder, loc));
        builder.setInsertionPointToStart(module->getBody());

        const Type proved = parse(context, "!ctnative.num<f64>");
        auto native = mlir::UnrealizedConversionCastOp::create(
            builder, loc, mlir::TypeRange{proved}, mlir::ValueRange{});
        auto nativeIface = mlir::cast<StaticTypedOpInterface>(native.getOperation());
        check("the ODS default returns the single result's type",
              nativeIface.getStaticType() == proved);
        check("a ctnative result is a proved type", nativeIface.hasProvedNativeType());

        auto builtin = mlir::UnrealizedConversionCastOp::create(
            builder, loc, mlir::TypeRange{builder.getI32Type()}, mlir::ValueRange{});
        auto builtinIface = mlir::cast<StaticTypedOpInterface>(builtin.getOperation());
        check("a builtin result is NOT a proved type - an operation that claimed "
              "the interface without an inference behind it",
              !builtinIface.hasProvedNativeType());

        auto twoResults = mlir::UnrealizedConversionCastOp::create(
            builder, loc, mlir::TypeRange{proved, proved}, mlir::ValueRange{});
        auto twoIface = mlir::cast<StaticTypedOpInterface>(twoResults.getOperation());
        check("two results have no single static type, and that is not an error",
              !twoIface.getStaticType());
        check("and therefore nothing was proved about them", !twoIface.hasProvedNativeType());
    }

    // --- ND-1: the first declared divergence, pinned to the interpreter ------
    //
    // NOTHING HERE IS WRITTEN DOWN TWICE. The fixture hands back the string's
    // BYTES and the engine's answer for `.length`, and the assertions are
    // between those two and `kDefaultStringEncoding`. A number in this block
    // would be a constant that could go stale silently, which is the failure
    // GCRoots.cpp records at the other end of the pipeline.
    {
        const std::string fixture = "var S = \"\\uD83D\\uDE00\";\n"
                                    "var L = S.length;\n";
        ctbrowser::script::program compiled =
            ctbrowser::script::compiler::compile(std::string(fixture));
        check("the ND-1 fixture compiled", compiled.ok);
        if (compiled.ok) {
            ctbrowser::script::context cx;
            ctbrowser::script::install_builtins(cx);
            const bool ran = cx.run(compiled).ok;
            check("the ND-1 fixture ran", ran);
            if (ran) {
                const std::string bytes = cx.to_string(cx.global("S"));
                const std::string length = cx.to_string(cx.global("L"));

                // THE PIN. Whatever `.length` answers in this engine, the
                // default C++ representation this backend picks has to answer
                // the SAME - otherwise every native-compiled `.length` on a
                // non-ASCII string disagrees with the differential harness's
                // own oracle. Part 24 says to default to utf16; flip
                // kDefaultStringEncoding and this line goes red.
                check("kDefaultStringEncoding answers .length the way the "
                      "interpreter does",
                      std::to_string(codeUnitsIn(kDefaultStringEncoding, bytes)) == length);

                // AND THE DIVERGENCE ITSELF, so that the entry in
                // docs/native-divergences.md is a tested claim and not a note.
                // ECMA-262 counts UTF-16 code units and answers 2; this engine
                // stores UTF-8 bytes and answers 4.
                check("ECMA-262's answer for \"\\u{1F600}\".length is 2 code units",
                      codeUnitsIn(StrEncoding::UTF16, bytes) == 2);
                check("this engine's answer is 4, and the two really do differ",
                      length == "4" && codeUnitsIn(StrEncoding::UTF16, bytes) !=
                                           codeUnitsIn(StrEncoding::UTF8, bytes));
                check("so the default is utf8, which is the engine's own",
                      kDefaultStringEncoding == StrEncoding::UTF8);
            }
        }
    }

    // ASSERT THE COUNTER. A test that silently stopped running most of itself
    // exits 0 and looks exactly like a passing one; this is the number to
    // update when a case is added, and the reason it is here rather than a
    // lower bound is that a lower bound would not notice a case being deleted.
    const int expectedChecks = 79;
    if (checks != expectedChecks) {
        std::printf("FAILED  ran %d checks, expected %d - a case was added or lost\n", checks,
                    expectedChecks);
        ++failures;
    }

    std::printf("%s: %d checks, %d failures\n", failures == 0 ? "ok" : "FAILED", checks, failures);
    return failures == 0 ? 0 : 1;
}
