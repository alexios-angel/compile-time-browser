// PHASE 54A'S TRANSFER FUNCTION, ONE JAVASCRIPT FACT AT A TIME.
//
// The oracle in TypeOracle.cpp answers "is the inference sound over a corpus",
// which is the question that matters and is also the question that cannot say
// WHICH rule is wrong. This file is the other half: a table of small functions
// whose right answer is a statement about JavaScript, checked individually, so
// a regression names the operator.
//
// THE NEGATIVE ROWS ARE THE POINT OF THE TABLE. An inference that answered
// `num<i32>` for every `|` would pass a table containing only the positive
// rows, and it would be WRONG, because `1n | 2n` is `3n`. So for each numeric
// operator there are two rows - one where the operands are known numbers and
// the claim is allowed, and one where they are function parameters and the
// claim must collapse to `boxed`. If a change makes the analysis unconditional,
// the negative rows go red and say which operator did it.
//
// Part 23 §1.4's ratio note: this file is a TEST, and tests are not counted
// against the ODS-first rule - there is no TableGen way to assert that
// `-0 | 0` is an int32 and `-0` alone is not.
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "OwnedGlobalMethodsFixtures.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/NativeMap.h"
#include "ctcompile/CTNative/Analysis/NativeObjectIdentity.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using ctcompile::ctnative::TypeInference;
using ctcompile::ctnative::TypeLattice;

int failures = 0;

// One row: a ctjs function, and what the operation marked `check` must infer.
// `body` OWNS ITS TEXT rather than pointing at it. Several rows build their
// body by concatenation, and a `const char *` taken from such a temporary
// dangles at the end of the initializer's full-expression - which reads as a
// parse failure in a row that is written correctly.
struct row {
    const char * what;     // the JavaScript fact, for the failure message
    std::string body;      // the operations, inside a ctjs.func
    const char * expected; // the printed ctnative type
    bool wholeModule = false;
    int fieldAssigned = -1; // -1 leaves historical rows unchanged; 0/1 checks live presence.
};

// THE FUNCTION HEADER EVERY ROW SHARES. ctjs.func takes three implicit
// arguments before the JavaScript ones - receiver, new_target, callee - and
// %p and %q are ordinary parameters, which is what makes them `boxed`: nothing
// is known about a caller, so nothing is known about an argument. That is
// precisely the state in which a BigInt claim cannot be ruled out.
constexpr const char * kPrologue =
    "ctjs.func @f(%receiver: !ctjs.value, %new_target: !ctjs.value, "
    "%callee: !ctjs.value, %p: !ctjs.value, %q: !ctjs.value) -> !ctjs.value "
    "attributes {upvalue_count = 0 : i32} {\n";

// 5.0, 1.5, -0.0 and 2**31 as the bit patterns ctjs.number carries. Spelled as
// bits because a NumberAttr is bits - see CTJSAttrs.td, which explains that a
// builtin FloatAttr would lose the difference between -0.0 and 0.0, and that
// difference is one of the rows below.
constexpr const char * kFive = "#ctjs.number<4617315517961601024>";         // 5.0
constexpr const char * kOneAndAHalf = "#ctjs.number<4609434218613702656>";  // 1.5
constexpr const char * kNegativeZero = "#ctjs.number<9223372036854775808>"; // -0.0
constexpr const char * kTwoToThe31 = "#ctjs.number<4746794007248502784>";   // 2147483648.0

std::string prologue() {
    return std::string{kPrologue};
}

void check(mlir::ModuleOp module, const char * what, const char * expected,
           const ctcompile::ctnative::OwnedGlobalRoots * owner = nullptr) {
    // DeadCodeAnalysis IS NOT OPTIONAL. The sparse framework gets its
    // predecessor information from it; without it a block argument never
    // receives what was branched into it and every answer is uninitialized.
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    // AND SparseConstantPropagation, WHICH IS NOT OPTIONAL EITHER, and the
    // reason is a trap worth naming. DeadCodeAnalysis decides which successor
    // of a branch is live by asking for every branch operand's ConstantValue
    // lattice; if nothing provides one, those lattices stay uninitialized, it
    // bails out, and NO successor is ever marked live. The sparse analysis
    // then skips every op in every non-entry block - measured: 109 unvisited
    // values and zero registers beating `boxed` on the fixture, while the
    // single-block unit rows all passed.
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<TypeInference>(owner);
    if (failed(solver.initializeAndRun(module))) {
        std::printf("FAIL %s: the solver did not converge\n", what);
        ++failures;
        return;
    }

    mlir::Operation * marked = nullptr;
    module.walk([&](mlir::Operation * op) {
        if (op->hasAttr("check")) { marked = op; }
    });
    if (marked == nullptr || marked->getNumResults() != 1) {
        std::printf("FAIL %s: no single-result operation carried `check`\n", what);
        ++failures;
        return;
    }

    const TypeLattice * lattice = solver.lookupState<TypeLattice>(marked->getResult(0));
    std::string got;
    if (lattice == nullptr) {
        got = "<no lattice>";
    } else {
        llvm::raw_string_ostream os{got};
        lattice->getValue().print(os);
    }
    if (got != expected) {
        std::printf("FAIL %s\n  expected %s\n  got      %s\n", what, expected, got.c_str());
        ++failures;
    }
}

void check(mlir::MLIRContext & context, const row & r) {
    std::string text = r.wholeModule ? r.body : prologue() + r.body + "  ctjs.return %p\n}\n";
    mlir::OwningOpRef<mlir::ModuleOp> module =
        mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    if (!module) {
        std::printf("FAIL %s: the module did not parse\n%s\n", r.what, text.c_str());
        ++failures;
        return;
    }
    check(*module, r.what, r.expected);
    if (r.fieldAssigned >= 0) {
        mlir::Operation * read = nullptr;
        module->walk([&](mlir::Operation * op) {
            if (op->hasAttr("check")) { read = op; }
        });
        const auto proof = ctcompile::ctnative::queryNativeObjectFieldPresence(read);
        if (proof.exhausted || proof.assigned != (r.fieldAssigned != 0)) {
            std::printf("FAIL %s: live field presence=%d exhausted=%d\n", r.what,
                        static_cast<int>(proof.assigned), static_cast<int>(proof.exhausted));
            ++failures;
        }
    }
}

std::string invokeModule(const std::string & helpers, llvm::StringRef observed = "%payload",
                         bool observeNormal = false) {
    const std::string observation = "    %observed = scf.execute_region -> !ctjs.value {\n"
                                    "      scf.yield " +
                                    observed.str() + " : !ctjs.value\n    } {check}\n";
    return R"mlir(
module {
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value, %condition: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %before = ctjs.constant #ctjs.string<"before call">
    %argument = ctjs.constant #ctjs.number<4617315517961601024>
    %result = ctjs.invoke {
      %called = ctjs.call_direct @helper(%nil, %nil, %nil, %condition, %argument)
      ctjs.invoke_exit %called state(%before)
    } normal {
    ^bb0(%returned: !ctjs.value):
)mlir" + (observeNormal ? observation : "") +
           R"mlir(
      ctjs.invoke_yield(%before)
    } unwind {
    ^bb0(%payload: !ctjs.value, %saved: !ctjs.value):
)mlir" + (!observeNormal ? observation : "") +
           R"mlir(
      ctjs.invoke_yield(%saved)
    } : !ctjs.value
    ctjs.return %result
  }
)mlir" + helpers +
           "}\n";
}

std::string throwingHelper(llvm::StringRef payload, llvm::StringRef prefix = "") {
    return R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
)mlir" + prefix.str() +
           "    %thrown = ctjs.constant " + payload.str() + "\n    ctjs.throw %thrown\n  }\n";
}

std::string conditionalHelper(llvm::StringRef result, llvm::StringRef payload,
                              llvm::StringRef prefix = "") {
    return R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32, ctnative.nothrow} {
)mlir" + prefix.str() +
           R"mlir(
    %bit = ctjs.truthy %condition
    cf.cond_br %bit, ^normal, ^throwing
  ^normal:
)mlir" + "    %normal = ctjs.constant " +
           result.str() + R"mlir(
    ctjs.return %normal
  ^throwing:
)mlir" + "    %thrown = ctjs.constant " +
           payload.str() + "\n    ctjs.throw %thrown\n  }\n";
}

std::string helperChain(unsigned depth) {
    std::string text;
    for (unsigned index = 0; index < depth; ++index) {
        const std::string name = index == 0 ? "helper" : "helper" + std::to_string(index);
        text += "  ctjs.func private @" + name + R"mlir((%receiver: !ctjs.value,
            %new_target: !ctjs.value, %callee: !ctjs.value,
            %condition: !ctjs.value, %argument: !ctjs.value) -> !ctjs.value
        attributes {upvalue_count = 0 : i32} {
)mlir";
        if (index + 1 == depth) {
            text += "    ctjs.throw %argument\n";
        } else {
            text += "    %r = ctjs.call_direct @helper" + std::to_string(index + 1) +
                    "(%receiver, %new_target, %callee, %condition, %argument)\n"
                    "    ctjs.return %r\n";
        }
        text += "  }\n";
    }
    return text;
}

// The identity field group is a storage schema, not an allocation identity.
// These rows deliberately give distinct objects the same schema. Presence is
// allowed to remove only implicit absence at this read; every explicit stored
// type, including Undefined on another object, still belongs to the join.
const std::string kIdentityFieldPrelude = R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %wide = ctjs.constant #ctjs.number<4609434218613702656>
  %nil = ctjs.constant #ctjs.undefined
  %yes = ctjs.constant #ctjs.boolean<true>
  %key = ctjs.constant #ctjs.string<"value">
  %different = ctjs.constant #ctjs.string<"different">
  %first = ctjs.create_object {ctnative.object_identity}
  %second = ctjs.create_object {ctnative.object_identity}
)mlir";

std::string identityFieldStore(llvm::StringRef object = "%first", llvm::StringRef value = "%one",
                               llvm::StringRef key = "%key") {
    return "  ctjs.set_property " + object.str() + "[" + key.str() + "], " + value.str() +
           " {ctnative.object_field_group = 7 : i64}\n";
}

std::string identityFieldRead(llvm::StringRef object = "%first") {
    return "  %observed = ctjs.get_property " + object.str() +
           "[%key] {check, ctnative.object_field_group = 7 : i64}\n";
}

void checkIdentityFieldRows(mlir::MLIRContext & context) {
    const std::string store = identityFieldStore();
    const std::string read = identityFieldRead();
    const std::vector<row> rows = {
        {"an identity schema drops implicit absence only after this object's store",
         kIdentityFieldPrelude + store + read, "!ctnative.num<i32>", false, 1},
        {"an identity read before its store retains implicit absence",
         kIdentityFieldPrelude + read + store, "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a different allocation's store cannot initialize this instance's field",
         kIdentityFieldPrelude + identityFieldStore("%second") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a different field cannot initialize this instance's queried field",
         kIdentityFieldPrelude + identityFieldStore("%first", "%one", "%different") +
             identityFieldStore("%second") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"an identity field absent from every object reads Undefined", kIdentityFieldPrelude + read,
         "!ctnative.opt<!ctnative.bottom>", false, 0},
        {"a present identity field keeps the full schema's wider number join",
         kIdentityFieldPrelude + store + identityFieldStore("%second", "%wide") + read,
         "!ctnative.num<f64>", false, 1},
        {"a present identity field keeps an explicit Undefined from the same schema",
         kIdentityFieldPrelude + store + identityFieldStore("%second", "%nil") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 1},
        {"a present identity field keeps its explicit Undefined write",
         kIdentityFieldPrelude + identityFieldStore("%first", "%nil") + read,
         "!ctnative.opt<!ctnative.bottom>", false, 1},
        {"an identity field keeps a later explicit Undefined in its value join",
         kIdentityFieldPrelude + store + read + identityFieldStore("%first", "%nil"),
         "!ctnative.opt<!ctnative.num<i32>>", false, 1},
        {"identity field presence does not collapse different primitive schema values",
         kIdentityFieldPrelude + store + identityFieldStore("%second", "%yes") + read,
         "!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>", false, 1},
        {"one conditional own-field store leaves an absent path",
         kIdentityFieldPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + store + "  }\n" +
             read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"both conditional own-field stores establish presence",
         kIdentityFieldPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + store +
             "  } else {\n" + identityFieldStore("%first", "%wide") + "  }\n" + read,
         "!ctnative.num<f64>", false, 1},
        {"stores to different allocations on two arms leave this field absent",
         kIdentityFieldPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + store +
             "  } else {\n" + identityFieldStore("%second") + "  }\n" + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a definite store survives an unrelated conditional field write",
         kIdentityFieldPrelude + store + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityFieldStore("%second", "%wide") + "  }\n" + read,
         "!ctnative.num<f64>", false, 1},
        {"an unknown incoming receiver cannot borrow another object's initialization",
         kIdentityFieldPrelude + store + identityFieldRead("%p"),
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a dynamic field mutation invalidates earlier instance initialization",
         kIdentityFieldPrelude + store + identityFieldStore("%second", "%one", "%p") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a loop-carried object origin remains conservative despite a schema store",
         kIdentityFieldPrelude + store + R"mlir(
  %bit = ctjs.truthy %p
  %carried = scf.while (%before = %first) : (!ctjs.value) -> !ctjs.value {
    scf.condition(%bit) %before : !ctjs.value
  } do {
  ^bb0(%after: !ctjs.value):
    scf.yield %after : !ctjs.value
  }
)mlir" + identityFieldRead("%carried"),
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
    };
    for (const row & r : rows) { check(context, r); }

    const std::string crossCall = R"mlir(
module {
  ctjs.func private @make(%receiver: !ctjs.value, %target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object {ctnative.object_identity}
    %key = ctjs.constant #ctjs.string<"value">
    %number = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %number {ctnative.object_field_group = 7 : i64}
    ctjs.return %object
  }
  ctjs.func @read(%receiver: !ctjs.value, %target: !ctjs.value,
                  %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %object = ctjs.call_direct @make(%nil, %nil, %nil)
    %key = ctjs.constant #ctjs.string<"value">
    %observed = ctjs.get_property %object[%key]
        {check, ctnative.object_field_group = 7 : i64}
    ctjs.return %observed
  }
}
)mlir";
    check(context, {"a cross-call object origin cannot borrow a schema initialization", crossCall,
                    "!ctnative.opt<!ctnative.num<i32>>", true, 0});

    // Keep all annotations while moving the store to another live allocation.
    // A newly constructed solver and a cloned source module must both observe
    // each change. The original schema group remains deliberately unchanged.
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + kIdentityFieldPrelude + store + read + "  ctjs.return %observed\n}\n",
        &context);
    if (!module) {
        std::printf("FAIL identity own-field live mutation fixture did not parse\n");
        ++failures;
        return;
    }
    ctcompile::ctjs::SetPropertyOp stored;
    ctcompile::ctjs::GetPropertyOp observed;
    llvm::SmallVector<ctcompile::ctjs::CreateObjectOp> objects;
    module->walk([&](ctcompile::ctjs::SetPropertyOp op) { stored = op; });
    module->walk([&](ctcompile::ctjs::GetPropertyOp op) { observed = op; });
    module->walk([&](ctcompile::ctjs::CreateObjectOp op) { objects.push_back(op); });
    if (!stored || !observed || objects.size() != 2) {
        std::printf("FAIL identity own-field live mutation fixture lost its exact operations\n");
        ++failures;
        return;
    }
    const auto liveAndFresh = [&](const char * what, const char * expected, bool assigned) {
        check(*module, what, expected);
        const auto proof = ctcompile::ctnative::queryNativeObjectFieldPresence(observed);
        if (proof.assigned != assigned || proof.exhausted) {
            std::printf("FAIL %s: stale source retained the wrong field presence\n", what);
            ++failures;
        }
        mlir::OwningOpRef<mlir::ModuleOp> fresh{llvm::cast<mlir::ModuleOp>(module->clone())};
        check(*fresh, what, expected);
        ctcompile::ctjs::GetPropertyOp freshRead;
        fresh->walk([&](ctcompile::ctjs::GetPropertyOp op) { freshRead = op; });
        const auto freshProof = ctcompile::ctnative::queryNativeObjectFieldPresence(freshRead);
        if (freshProof.assigned != assigned || freshProof.exhausted) {
            std::printf("FAIL %s: fresh source retained the wrong field presence\n", what);
            ++failures;
        }
    };
    liveAndFresh("identity field presence before live mutation", "!ctnative.num<i32>", true);
    stored->setOperand(0, objects[1].getResult());
    liveAndFresh("identity field rederives a changed store receiver",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    stored->setOperand(0, objects[0].getResult());
    stored->moveAfter(observed);
    liveAndFresh("identity field rederives a store moved after the read",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    stored->moveBefore(observed);
    liveAndFresh("identity field rebuilds presence after restoring its store", "!ctnative.num<i32>",
                 true);
    auto number = stored.getValue().getDefiningOp<ctcompile::ctjs::ConstantOp>();
    const auto original = number.getValue();
    number->setAttr("value", ctcompile::ctjs::UndefinedAttr::get(&context));
    liveAndFresh("identity field retains a newly explicit Undefined value",
                 "!ctnative.opt<!ctnative.bottom>", true);
    number->setAttr("value", original);
    liveAndFresh("identity field rebuilds its value join after restoring the source",
                 "!ctnative.num<i32>", true);
}

const std::string kIdentityMapPrelude = kIdentityFieldPrelude + R"mlir(
  %slot = ctjs.constant #ctjs.string<"slot">
  %missing = ctjs.constant #ctjs.string<"missing">
  %setName = ctjs.constant #ctjs.string<"set">
  %getName = ctjs.constant #ctjs.string<"get">
  %deleteName = ctjs.constant #ctjs.string<"delete">
  %clearName = ctjs.constant #ctjs.string<"clear">
  %constructor = ctjs.load_global "Map" {ctnative.map_constructor}
  %map = ctjs.construct %constructor(%constructor)
      {ctnative.map_group = 3 : i64, ctnative.map_site}
  %setter = ctjs.get_property %map[%setName] {ctnative.map_method}
  %getter = ctjs.get_property %map[%getName] {ctnative.map_method}
  %eraser = ctjs.get_property %map[%deleteName] {ctnative.map_method}
  %clearer = ctjs.get_property %map[%clearName] {ctnative.map_method}
)mlir";

std::string identityMapSet(llvm::StringRef result, llvm::StringRef object = "%first") {
    return "  " + result.str() + " = ctjs.call %setter(%map, %slot, " + object.str() +
           ") {ctnative.map_action = \"set\", ctnative.map_group = 3 : i64}\n";
}

std::string identityMapGet(llvm::StringRef result = "%saved", llvm::StringRef key = "%slot") {
    // Even refusals carry map_present. It is an old report, not proof that
    // this exact read still names an initialized object in the current source.
    return "  " + result.str() + " = ctjs.call %getter(%map, " + key.str() +
           ") {ctnative.map_action = \"get\", ctnative.map_present}\n";
}

ctcompile::ctjs::GetPropertyOp checkedField(mlir::ModuleOp module) {
    ctcompile::ctjs::GetPropertyOp read;
    module.walk([&](ctcompile::ctjs::GetPropertyOp op) {
        if (op->hasAttr("check")) { read = op; }
    });
    return read;
}

void checkFieldBudgets(mlir::ModuleOp module, const char * what, bool assigned) {
    const auto read = checkedField(module);
    const auto complete = ctcompile::ctnative::queryNativeObjectFieldPresence(read);
    if (complete.assigned != assigned || complete.exhausted || complete.work == 0) {
        std::printf("FAIL %s: complete field-presence budget fixture disagrees\n", what);
        ++failures;
        return;
    }
    for (uint64_t limit = 0; limit < complete.work; ++limit) {
        const auto bounded = ctcompile::ctnative::queryNativeObjectFieldPresence(read, limit);
        if (bounded.assigned || !bounded.exhausted || bounded.work > limit) {
            std::printf("FAIL %s: field presence survived work cutoff %llu of %llu\n", what,
                        static_cast<unsigned long long>(limit),
                        static_cast<unsigned long long>(complete.work));
            ++failures;
            return;
        }
    }
    const auto exact = ctcompile::ctnative::queryNativeObjectFieldPresence(read, complete.work);
    if (exact.assigned != assigned || exact.exhausted || exact.work != complete.work) {
        std::printf("FAIL %s: exact field-presence budget did not reproduce the proof\n", what);
        ++failures;
    }
    std::printf("field presence %s: %llu exhaustive work cutoffs\n", what,
                static_cast<unsigned long long>(complete.work));
}

void checkIdentityMapFieldRows(mlir::MLIRContext & context) {
    const std::string store = identityFieldStore();
    const std::string save = identityMapSet("%put") + identityMapGet();
    const std::string read = identityFieldRead("%saved");
    const std::string erase =
        "  %removed = ctjs.call %eraser(%map, %slot) {ctnative.map_action = \"delete\"}\n";
    const std::string clear =
        "  %cleared = ctjs.call %clearer(%map) {ctnative.map_action = \"clear\"}\n";
    const std::vector<row> rows = {
        {"Map.get retains the exact initialized object from the reaching set",
         kIdentityMapPrelude + store + save + read, "!ctnative.num<i32>", false, 1},
        {"Map.get of a missing key cannot borrow the stored object's fields",
         kIdentityMapPrelude + store + identityMapSet("%put") +
             identityMapGet("%saved", "%missing") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"Map.get follows an overwrite to an uninitialized object in the same schema",
         kIdentityMapPrelude + store + identityMapSet("%put") +
             identityMapSet("%overwrite", "%second") + identityMapGet() + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a saved Map.get alias retains its original object after overwrite",
         kIdentityMapPrelude + store + save + identityMapSet("%overwrite", "%second") + read,
         "!ctnative.num<i32>", false, 1},
        {"a saved Map.get alias retains its original object after deletion",
         kIdentityMapPrelude + store + save + erase + read, "!ctnative.num<i32>", false, 1},
        {"a saved Map.get alias retains its original object after clearing the Map",
         kIdentityMapPrelude + store + save + clear + read, "!ctnative.num<i32>", false, 1},
        {"a fresh Map.get after deletion has no initialized object origin",
         kIdentityMapPrelude + store + identityMapSet("%put") + erase + identityMapGet() + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a field write through a Map.get alias initializes the original object",
         kIdentityMapPrelude + save + identityFieldStore("%saved") + identityFieldRead(),
         "!ctnative.num<i32>", false, 1},
        {"a field write through the original object initializes its saved Map.get alias",
         kIdentityMapPrelude + save + store + read, "!ctnative.num<i32>", false, 1},
        {"a saved alias field write after deletion initializes its retained object",
         kIdentityMapPrelude + save + erase + identityFieldStore("%saved") + identityFieldRead(),
         "!ctnative.num<i32>", false, 1},
        {"a saved alias field read keeps an explicitly stored Undefined",
         kIdentityMapPrelude + store + save + identityFieldStore("%saved", "%nil") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 1},
        {"one branch writing through a saved alias leaves an absent path",
         kIdentityMapPrelude + save + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityFieldStore("%saved") + "  }\n" + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"both branches can initialize one object through different aliases",
         kIdentityMapPrelude + save + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityFieldStore("%saved") + "  } else {\n" + identityFieldStore("%first", "%wide") +
             "  }\n" + read,
         "!ctnative.num<f64>", false, 1},
        {"conditional Map writes preserve one independently known object origin",
         kIdentityMapPrelude + store + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityMapSet("%left") + "  } else {\n" + identityMapSet("%right") + "  }\n" +
             identityMapGet() + read,
         "!ctnative.num<i32>", false, 1},
        {"one conditional Map write cannot establish a reaching object",
         kIdentityMapPrelude + store + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityMapSet("%left") + "  }\n" + identityMapGet() + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"an unknown call invalidates saved object field facts",
         kIdentityMapPrelude + store + save + "  %effect = ctjs.call %p(%nil)\n" + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a same-schema second Map does not inherit the first Map's entry",
         kIdentityMapPrelude + store + identityMapSet("%put") + R"mlir(
  %otherMap = ctjs.construct %constructor(%constructor)
      {ctnative.map_group = 3 : i64, ctnative.map_site}
  %otherGetter = ctjs.get_property %otherMap[%getName] {ctnative.map_method}
  %saved = ctjs.call %otherGetter(%otherMap, %slot)
      {ctnative.map_action = "get", ctnative.map_present}
)mlir" + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"same-origin structured yields retain the saved object identity",
         kIdentityMapPrelude + store + save + R"mlir(
  %bit = ctjs.truthy %p
  %joined = scf.if %bit -> (!ctjs.value) {
    scf.yield %first : !ctjs.value
  } else {
    scf.yield %saved : !ctjs.value
  }
)mlir" + identityFieldRead("%joined"),
         "!ctnative.num<i32>", false, 1},
        {"different-origin structured yields cannot borrow one allocation's fields",
         kIdentityMapPrelude + store + save + R"mlir(
  %bit = ctjs.truthy %p
  %joined = scf.if %bit -> (!ctjs.value) {
    scf.yield %saved : !ctjs.value
  } else {
    scf.yield %second : !ctjs.value
  }
)mlir" + identityFieldRead("%joined"),
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
    };
    for (const row & r : rows) { check(context, r); }

    // The positive also passes actual preparation: raw annotations are not
    // required for this source to acquire the independently checked schema.
    const std::string text = prologue() + kIdentityMapPrelude + store + save +
                             identityMapSet("%overwrite", "%second") + read +
                             "  ctjs.return %observed\n}\n";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    if (!module) {
        std::printf("FAIL saved Map own-field preparation fixture did not parse\n");
        ++failures;
        return;
    }
    ctcompile::ctnative::prepareNativeMaps(*module);
    ctcompile::ctnative::prepareNativeObjectIdentities(*module);
    auto observed = checkedField(*module);
    if (!observed || ctcompile::ctnative::nativeObjectFieldGroup(observed) < 0) {
        std::printf("FAIL saved Map own-field fixture did not receive a live object schema\n");
        ++failures;
        return;
    }
    check(*module, "actual native preparation retains a saved object's initialized field",
          "!ctnative.num<i32>");
    checkFieldBudgets(*module, "prepared saved Map alias", true);

    // Retain prepared reports and alter the original reaching object. A saved
    // alias must never change identity just because a later Map set changes.
    llvm::SmallVector<ctcompile::ctjs::CallOp> puts;
    ctcompile::ctjs::CallOp saved;
    ctcompile::ctjs::SetPropertyOp fieldStore;
    module->walk([&](ctcompile::ctjs::CallOp call) {
        const auto action = ctcompile::ctnative::nativeMapAction(call);
        if (action == "set") { puts.push_back(call); }
        if (action == "get") { saved = call; }
    });
    module->walk([&](ctcompile::ctjs::SetPropertyOp op) { fieldStore = op; });
    if (puts.size() != 2 || !saved || !fieldStore) {
        std::printf("FAIL saved Map live mutation fixture lost exact source operations\n");
        ++failures;
        return;
    }
    const auto liveAndFresh = [&](const char * what, const char * expected, bool assigned) {
        check(*module, what, expected);
        const auto stale = ctcompile::ctnative::queryNativeObjectFieldPresence(observed);
        mlir::OwningOpRef<mlir::ModuleOp> fresh{llvm::cast<mlir::ModuleOp>(module->clone())};
        check(*fresh, what, expected);
        const auto rebuilt =
            ctcompile::ctnative::queryNativeObjectFieldPresence(checkedField(*fresh));
        if (stale.assigned != assigned || rebuilt.assigned != assigned || stale.exhausted ||
            rebuilt.exhausted) {
            std::printf("FAIL %s: live/fresh Map field presence disagrees\n", what);
            ++failures;
        }
    };
    const auto original = puts[0].getArgs()[1];
    puts[0]->setOperand(3, puts[1].getArgs()[1]);
    liveAndFresh("saved Map field rederives a changed reaching payload",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    puts[0]->setOperand(3, original);
    liveAndFresh("saved Map field restores its reaching payload", "!ctnative.num<i32>", true);
    const auto getKey = saved.getArgs()[0];
    auto missing = mlir::cast<ctcompile::ctjs::ConstantOp>(fieldStore.getKey().getDefiningOp());
    saved->setOperand(2, missing.getResult());
    liveAndFresh("saved Map field ignores stale presence after a lookup-key mutation",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    saved->setOperand(2, getKey);
    auto getter = saved.getCallee().getDefiningOp<ctcompile::ctjs::GetPropertyOp>();
    const auto originalCalleeKey = getter.getKey();
    getter->setOperand(1, missing.getResult());
    liveAndFresh("saved Map field rejects a stale action after a method-key mutation",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    getter->setOperand(1, originalCalleeKey);
    liveAndFresh("saved Map field restores its live method and lookup", "!ctnative.num<i32>", true);
    checkFieldBudgets(*module, "restored saved Map alias", true);

    // A live malformed source can retain valid reports. Query it directly,
    // without sending invalid SSA to the type solver: a then-only allocation
    // cannot become the receiver outside its region or in the sibling arm.
    for (bool sibling : {false, true}) {
        const std::string thenBody = R"mlir(
  %bit = ctjs.truthy %p
  scf.if %bit {
    %inside = ctjs.create_object {ctnative.object_identity, inside}
    ctjs.set_property %inside[%key], %one {ctnative.object_field_group = 7 : i64}
)mlir";
        const std::string body = kIdentityMapPrelude + store + save + thenBody +
                                 (sibling ? "  } else {\n" + read + "  }\n" : "  }\n" + read);
        auto scoped = mlir::parseSourceString<mlir::ModuleOp>(
            prologue() + body + "  ctjs.return %one\n}\n", &context);
        if (!scoped) {
            std::printf("FAIL own-field cross-scope fixture did not parse\n");
            ++failures;
            continue;
        }
        auto scopeRead = checkedField(*scoped);
        ctcompile::ctjs::CreateObjectOp inside;
        scoped->walk([&](ctcompile::ctjs::CreateObjectOp made) {
            if (made->hasAttr("inside")) { inside = made; }
        });
        if (!scopeRead || !inside ||
            !ctcompile::ctnative::queryNativeObjectFieldPresence(scopeRead).assigned) {
            std::printf("FAIL own-field cross-scope fixture lacks its initial live proof\n");
            ++failures;
            continue;
        }
        const auto receiver = scopeRead.getObject();
        scopeRead->setOperand(0, inside.getResult());
        mlir::OwningOpRef<mlir::ModuleOp> fresh{llvm::cast<mlir::ModuleOp>(scoped->clone())};
        if (ctcompile::ctnative::queryNativeObjectFieldPresence(scopeRead).assigned ||
            ctcompile::ctnative::queryNativeObjectFieldPresence(checkedField(*fresh)).assigned) {
            std::printf("FAIL own-field initialization crossed an invalid SSA scope\n");
            ++failures;
        }
        scopeRead->setOperand(0, receiver);
        if (!ctcompile::ctnative::queryNativeObjectFieldPresence(scopeRead).assigned) {
            std::printf("FAIL own-field initialization did not recover after a scope repair\n");
            ++failures;
        }
    }

    auto absentModule = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + kIdentityFieldPrelude + identityFieldStore("%second") + identityFieldRead() +
            "  ctjs.return %observed\n}\n",
        &context);
    if (!absentModule) {
        std::printf("FAIL absent own-field budget fixture did not parse\n");
        ++failures;
    } else {
        checkFieldBudgets(*absentModule, "different allocation refusal", false);
    }

    auto branchModule = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + rows[12].body + "  ctjs.return %observed\n}\n", &context);
    if (!branchModule) {
        std::printf("FAIL branch own-field budget fixture did not parse\n");
        ++failures;
    } else {
        checkFieldBudgets(*branchModule, "both-arm alias writes", true);
    }
}

void checkStaleFieldEffects(mlir::MLIRContext & context) {
    const auto checkBoth = [&](mlir::ModuleOp module, const char * what, bool assigned) {
        const char * expected =
            assigned ? "!ctnative.num<i32>" : "!ctnative.opt<!ctnative.num<i32>>";
        check(module, what, expected);
        const auto stale =
            ctcompile::ctnative::queryNativeObjectFieldPresence(checkedField(module));
        mlir::OwningOpRef<mlir::ModuleOp> fresh{module.clone()};
        check(*fresh, what, expected);
        const auto rebuilt =
            ctcompile::ctnative::queryNativeObjectFieldPresence(checkedField(*fresh));
        if (stale.assigned != assigned || rebuilt.assigned != assigned || stale.exhausted ||
            rebuilt.exhausted) {
            std::printf("FAIL %s: stale/fresh field effect proof disagrees\n", what);
            ++failures;
        }
    };

    // The unused lookup still executes. Changing its property name cannot
    // retain the old map_method authority just because no call follows it.
    const std::string body = kIdentityMapPrelude + identityFieldStore() + identityMapSet("%put") +
                             identityMapGet() + identityFieldRead("%saved");
    auto unused = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + body + "  ctjs.return %observed\n}\n", &context);
    if (!unused) {
        std::printf("FAIL unused Map method effect fixture did not parse\n");
        ++failures;
    } else {
        ctcompile::ctjs::GetPropertyOp lookup;
        ctcompile::ctjs::ConstantOp changedKey;
        unused->walk([&](ctcompile::ctjs::GetPropertyOp get) {
            auto key = get.getKey().getDefiningOp<ctcompile::ctjs::ConstantOp>();
            auto name = key ? llvm::dyn_cast<ctcompile::ctjs::StringAttr>(key.getValue())
                            : ctcompile::ctjs::StringAttr{};
            if (name && name.getValue() == "clear") {
                lookup = get;
                changedKey = key;
            }
        });
        if (!lookup || !lookup->use_empty() || !lookup->hasAttr("ctnative.map_method")) {
            std::printf("FAIL unused Map method effect fixture lost its marked lookup\n");
            ++failures;
        } else {
            checkBoth(*unused, "unused standard Map lookup before mutation", true);
            const auto name = changedKey.getValue();
            changedKey->setAttr("value", ctcompile::ctjs::StringAttr::get(&context, "unknown"));
            checkBoth(*unused, "unused Map lookup rederives its changed property name", false);
            changedKey->setAttr("value", name);
            checkBoth(*unused, "unused Map lookup restores its standard method", true);
        }
    }

    // A marked zero-argument constructor executes between initialization and
    // the read. Mutate both operands so new_target still equals callee: that
    // syntactic equality alone is not the identity of the standard Map.
    const std::string constructorBody = kIdentityMapPrelude + identityFieldStore() +
                                        identityMapSet("%put") + identityMapGet() +
                                        R"mlir(
  %effect = ctjs.construct %constructor(%constructor)
      {ctnative.map_group = 4 : i64, ctnative.map_site, changed_constructor}
)mlir" + identityFieldRead("%saved");
    auto constructor = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + constructorBody + "  ctjs.return %observed\n}\n", &context);
    if (!constructor) {
        std::printf("FAIL stale Map constructor effect fixture did not parse\n");
        ++failures;
    } else {
        ctcompile::ctjs::ConstructOp changed;
        constructor->walk([&](ctcompile::ctjs::ConstructOp made) {
            if (made->hasAttr("changed_constructor")) { changed = made; }
        });
        if (!changed || !changed.getArgs().empty() || !changed->hasAttr("ctnative.map_site")) {
            std::printf("FAIL stale Map constructor effect fixture lost its marked constructor\n");
            ++failures;
        } else {
            checkBoth(*constructor, "standard Map constructor before live mutation", true);
            const auto callee = changed.getCallee();
            const auto unknown =
                changed->getParentOfType<ctcompile::ctjs::FuncOp>().getBody().front().getArgument(
                    3);
            changed->setOperand(0, unknown);
            changed->setOperand(1, unknown);
            checkBoth(*constructor, "stale Map site rejects a changed unknown constructor", false);
            changed->setOperand(0, callee);
            changed->setOperand(1, callee);
            checkBoth(*constructor, "Map site restores its live standard constructor", true);
        }
    }

    // Prototype/accessor mutations elsewhere invalidate the closed scalar
    // environment even if a later allocation could rebuild a local fact.
    const std::string environmentBody = R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %nil = ctjs.constant #ctjs.undefined
  %key = ctjs.constant #ctjs.string<"value">
  %otherKey = ctjs.constant #ctjs.string<"ordinary"> {changed_key}
  %other = ctjs.create_object
  ctjs.set_property %other[%otherKey], %nil
  %fresh = ctjs.create_object {ctnative.object_identity}
  ctjs.set_property %fresh[%key], %one {ctnative.object_field_group = 7 : i64}
  %observed = ctjs.get_property %fresh[%key]
      {check, ctnative.object_field_group = 7 : i64}
)mlir";
    auto environment = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + environmentBody + "  ctjs.return %observed\n}\n", &context);
    if (!environment) {
        std::printf("FAIL scalar field environment mutation fixture did not parse\n");
        ++failures;
        return;
    }
    ctcompile::ctjs::ConstantOp changedKey;
    ctcompile::ctjs::SetPropertyOp ordinary;
    environment->walk([&](ctcompile::ctjs::ConstantOp key) {
        if (key->hasAttr("changed_key")) { changedKey = key; }
    });
    environment->walk([&](ctcompile::ctjs::SetPropertyOp set) {
        if (!set->hasAttr("ctnative.object_field_group")) { ordinary = set; }
    });
    if (!changedKey || !ordinary) {
        std::printf("FAIL scalar field environment fixture lost its exact mutations\n");
        ++failures;
        return;
    }
    checkBoth(*environment, "closed scalar environment before mutation", true);
    const auto originalKey = changedKey.getValue();
    changedKey->setAttr("value", ctcompile::ctjs::StringAttr::get(&context, "__proto__"));
    checkBoth(*environment, "scalar field environment rederives a prototype write", false);
    changedKey->setAttr("value", originalKey);
    checkBoth(*environment, "scalar field environment restores an ordinary field write", true);
    mlir::OpBuilder before(ordinary);
    auto accessor = ctcompile::ctjs::DefineAccessorOp::create(
        before, ordinary.getLoc(), ordinary.getObject(), "value", ordinary.getValue(),
        ordinary.getValue());
    checkBoth(*environment, "scalar field environment rejects a new accessor definition", false);
    accessor.erase();
    checkBoth(*environment, "scalar field environment restores after accessor removal", true);

    // Unknown code can install an inherited setter before a later object is
    // allocated. A fresh allocation does not restore that closed environment.
    const std::string beforeFresh = R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %nil = ctjs.constant #ctjs.undefined
  %key = ctjs.constant #ctjs.string<"value">
  %bit = ctjs.truthy %q
)mlir";
    const std::string freshField = R"mlir(
  %fresh = ctjs.create_object {ctnative.object_identity, mutation_target}
  ctjs.set_property %fresh[%key], %one {ctnative.object_field_group = 7 : i64}
)mlir";
    const std::string freshRead = R"mlir(
  %observed = ctjs.get_property %fresh[%key]
      {check, ctnative.object_field_group = 7 : i64}
)mlir";
    const std::string unknownCall = "  %unknown = ctjs.call %p(%nil) {unknown_effect}\n";
    const std::string unknownConstructor = "  %unknown = ctjs.construct %p(%p) {unknown_effect}\n";
    struct effectRow {
        const char * what;
        std::string body;
        bool intoThen = false;
    };
    const std::vector<effectRow> effectRows = {
        {"unknown call before fresh allocation",
         beforeFresh + freshField + freshRead + unknownCall},
        {"unknown constructor before fresh allocation",
         beforeFresh + freshField + freshRead + unknownConstructor},
        {"unknown branch effect before later fresh allocation",
         beforeFresh + "  scf.if %bit {\n  } {branch_target}\n" + freshField + freshRead +
             unknownCall,
         true},
        {"safe sibling after an unknown effect in the other arm",
         beforeFresh + freshField + "  scf.if %bit {\n" + unknownCall + "  } else {\n" + freshRead +
             "  } {branch_target}\n"},
    };
    for (const effectRow & r : effectRows) {
        auto source = mlir::parseSourceString<mlir::ModuleOp>(
            prologue() + r.body + "  ctjs.return %one\n}\n", &context);
        if (!source) {
            std::printf("FAIL %s: unknown-effect fixture did not parse\n", r.what);
            ++failures;
            continue;
        }
        mlir::Operation * effect = nullptr;
        mlir::Operation * target = nullptr;
        mlir::scf::IfOp branch;
        source->walk([&](mlir::Operation * op) {
            if (op->hasAttr("unknown_effect")) { effect = op; }
            if (op->hasAttr("mutation_target")) { target = op; }
            if (op->hasAttr("branch_target")) { branch = llvm::cast<mlir::scf::IfOp>(op); }
        });
        if (branch) {
            target =
                r.intoThen ? branch.getThenRegion().front().getTerminator() : branch.getOperation();
        }
        if (!effect || !target || !effect->getNextNode()) {
            std::printf("FAIL %s: unknown-effect fixture lost its source positions\n", r.what);
            ++failures;
            continue;
        }
        auto * restoreBefore = effect->getNextNode();
        checkBoth(*source, (std::string{r.what} + " before live mutation").c_str(), true);
        effect->moveBefore(target);
        checkBoth(*source, (std::string{r.what} + " after effect moved earlier").c_str(), false);
        effect->moveBefore(restoreBefore);
        checkBoth(*source, (std::string{r.what} + " after exact source restoration").c_str(), true);
    }
}

// Comparison-only identities need their own complete producer/use census.
// These expectations are attached to source allocations, never inferred from
// field schema numbers or another comparison operand's type. Preparation may
// add annotations but must preserve every allocation, scalar write and operand.
void checkComparisonIdentityPreparation(mlir::ModuleOp module, const char * what) {
    using namespace ctcompile;
    std::vector<mlir::Operation *> operations;
    std::vector<std::vector<mlir::Value>> operands;
    module.walk([&](mlir::Operation * op) {
        operations.push_back(op);
        operands.emplace_back(op->operand_begin(), op->operand_end());
    });
    ctnative::prepareNativeObjectIdentities(module);
    std::vector<mlir::Operation *> after;
    module.walk([&](mlir::Operation * op) { after.push_back(op); });
    if (after != operations) {
        std::printf("FAIL %s: identity preparation changed source operations\n", what);
        ++failures;
        return;
    }
    std::vector<std::pair<int64_t, int64_t>> groups;
    unsigned allocations = 0;
    bool typed = false;
    for (size_t index = 0; index < operations.size(); ++index) {
        auto * op = operations[index];
        if (!llvm::equal(op->getOperands(), operands[index])) {
            std::printf("FAIL %s: identity preparation changed a source operand\n", what);
            ++failures;
        }
        if (auto object = llvm::dyn_cast<ctjs::CreateObjectOp>(op)) {
            ++allocations;
            auto expected = op->getAttrOfType<mlir::BoolAttr>("test_identity");
            if (!expected || op->hasAttr(ctnative::kNativeObjectIdentity) != expected.getValue()) {
                std::printf("FAIL %s: fresh allocation %u has the wrong identity proof\n", what,
                            allocations);
                ++failures;
            }
            typed |= expected && expected.getValue() && op->hasAttr("check");
        } else if (op->hasAttr(ctnative::kNativeObjectIdentity)) {
            std::printf("FAIL %s: a non-allocation producer retained a forged identity\n", what);
            ++failures;
        }
        if (auto expected = op->getAttrOfType<mlir::IntegerAttr>("test_field_family")) {
            const int64_t group = ctnative::nativeObjectFieldGroup(op);
            if ((group >= 0) != (expected.getInt() >= 0)) {
                std::printf("FAIL %s: scalar field has the wrong family proof\n", what);
                ++failures;
            }
            if (expected.getInt() >= 0 && group >= 0) {
                for (const auto & [source, previous] : groups) {
                    if ((source == expected.getInt()) != (previous == group)) {
                        std::printf("FAIL %s: comparison changed scalar field family identity\n",
                                    what);
                        ++failures;
                    }
                }
                groups.emplace_back(expected.getInt(), group);
            }
        }
    }
    if (allocations == 0) {
        std::printf("FAIL %s: identity fixture contains no fresh allocation\n", what);
        ++failures;
    }
    if (typed) { check(module, what, "!ctnative.object_identity"); }
}

void forgeComparisonIdentityReports(mlir::ModuleOp module) {
    auto * context = module.getContext();
    module.walk([&](mlir::Operation * op) {
        op->setAttr(ctcompile::ctnative::kNativeObjectIdentity, mlir::UnitAttr::get(context));
        op->setAttr(ctcompile::ctnative::kNativeObjectFieldGroup,
                    mlir::IntegerAttr::get(mlir::IntegerType::get(context, 64), 777));
    });
}

void checkComparisonIdentityRows(mlir::MLIRContext & context) {
    const std::string prefix = R"mlir(
  %nil = ctjs.constant #ctjs.undefined
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %wide = ctjs.constant #ctjs.number<4609434218613702656>
  %field = ctjs.constant #ctjs.string<"value">
)mlir";
    const std::string fresh = "  %object = ctjs.create_object {test_identity = true, check}\n";
    const std::string refused = "  %object = ctjs.create_object {test_identity = false}\n";
    const std::string equal = "  %comparison = ctjs.compare strict_eq %object, %object\n";
    const std::string field =
        "  ctjs.set_property %object[%field], %one {test_field_family = 0 : i64}\n";
    const std::string refusedField =
        "  ctjs.set_property %object[%field], %one {test_field_family = -1 : i64}\n";
    const auto function = [&](const std::string & body, llvm::StringRef returned = "%comparison") {
        return prologue() + prefix + body + "  ctjs.return " + returned.str() + "\n}\n";
    };
    struct identityRow {
        const char * what;
        std::string source;
    };
    const std::vector<identityRow> rows{
        {"strict self-comparison proves a fresh identity", function(fresh + equal)},
        {"both strict comparison operands retain independent fresh identities",
         function(fresh + R"mlir(
  %other = ctjs.create_object {test_identity = true}
  %comparison = ctjs.compare strict_eq %object, %other
  %reversed = ctjs.compare strict_eq %other, %object
)mlir")},
        {"strict comparison does not merge equal field layouts or numeric writes",
         function(fresh + field + R"mlir(
  %other = ctjs.create_object {test_identity = true}
  ctjs.set_property %other[%field], %wide {test_field_family = 1 : i64}
  %first = ctjs.get_property %object[%field] {test_field_family = 0 : i64}
  %second = ctjs.get_property %other[%field] {test_field_family = 1 : i64}
  %comparison = ctjs.compare strict_eq %object, %other
)mlir")},
        {"a boxed comparison operand is not an origin for the independent fresh operand",
         function(fresh + "  %comparison = ctjs.compare strict_eq %object, %p\n")},
        {"root bookkeeping preserves the fresh strict-comparison proof",
         function("  %frame = ctjs.frame_enter 5\n" + fresh + "  ctjs.root %object in %frame\n" +
                  equal + "  ctjs.frame_exit %frame\n")},
        {"unrelated unknown code cannot change fieldless identity",
         function("  %ignored = ctjs.call %p(%nil)\n" + fresh + equal)},
        {"a strict-comparison field family rechecks unknown calls in its environment",
         function("  %ignored = ctjs.call %p(%nil)\n" + refused + refusedField + equal)},
        {"a strict-comparison field family rechecks unknown constructors in its environment",
         function("  %ignored = ctjs.construct %p(%p)\n" + refused + refusedField + equal)},
        {"an unobserved fresh object is not a comparison candidate", function(refused, "%nil")},
        {"ordinary fields alone do not invent a comparison candidate",
         function(refused + refusedField, "%nil")},
        {"loose equality alone does not prove comparison-only identity",
         function(refused + "  %comparison = ctjs.compare eq %object, %object\n")},
        {"relational comparison alone does not prove comparison-only identity",
         function(refused + "  %comparison = ctjs.compare lt %object, %object\n")},
        {"one strict use does not hide a loose equality use",
         function(refused + equal + "  %coercive = ctjs.compare eq %object, %p\n")},
        {"one strict use does not hide a relational use",
         function(refused + equal + "  %coercive = ctjs.compare ge %object, %p\n")},
        {"one strict use does not hide arithmetic coercion",
         function(refused + equal + "  %coercive = ctjs.binary add %object, %one\n")},
        {"one strict use does not hide truthiness outside the comparison census",
         function(refused + equal + "  %truth = ctjs.truthy %object\n")},
        {"one strict use does not hide unary coercion",
         function(refused + equal + "  %coercive = ctjs.unary plus %object\n")},
        {"unknown call arguments reject the whole comparison family",
         function(refused + equal + "  %escaped = ctjs.call %p(%nil, %object)\n")},
        {"unknown call receivers reject the whole comparison family",
         function(refused + equal + "  %escaped = ctjs.call %p(%object)\n")},
        {"using the fresh object as a callee is not identity observation",
         function(refused + equal + "  %escaped = ctjs.call %object(%nil)\n")},
        {"constructor arguments reject the whole comparison family",
         function(refused + equal + "  %escaped = ctjs.construct %p(%p, %object)\n")},
        {"public return publication rejects the whole comparison family",
         function(refused + equal, "%object")},
        {"storing an object in an array is an outgoing ownership edge",
         function(refused + equal + "  %escaped = ctjs.create_array [%object]\n")},
        {"storing an object in an ordinary property is an outgoing ownership edge",
         function(refused + equal + "  ctjs.set_property %p[%field], %object\n")},
        {"dynamic fields reject the whole comparison family",
         function(refused + equal +
                  "  ctjs.set_property %object[%p], %one {test_field_family = -1 : i64}\n")},
        {"prototype setters reject the whole comparison family", function(refused + equal + R"mlir(
  %prototype = ctjs.constant #ctjs.string<"__proto__">
  ctjs.set_property %object[%prototype], %nil {test_field_family = -1 : i64}
)mlir")},
        {"both structured alternatives keep their fresh allocation proofs", function(fresh + R"mlir(
  %other = ctjs.create_object {test_identity = true}
  %bit = ctjs.truthy %p
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %other : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"an execute-region yield cannot hide object publication from the use census",
         function(refused + equal + R"mlir(
  %escaped = scf.execute_region -> !ctjs.value {
    scf.yield %object : !ctjs.value
  }
  ctjs.store_global "escaped_identity", %escaped
)mlir")},
        {"an untracked execute-region result is not a proved comparison alias",
         function(refused + equal + R"mlir(
  %alias = scf.execute_region -> !ctjs.value {
    scf.yield %object : !ctjs.value
  }
  %observed = ctjs.compare strict_eq %alias, %object
)mlir")},
        {"both tracked if-yields preserve a same-origin strict-comparison alias",
         function(fresh + R"mlir(
  %bit = ctjs.truthy %p
  %alias = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %object : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %alias, %object
)mlir")},
        {"tracked if-yields expose publication to the complete object use census",
         function(refused + equal + R"mlir(
  %bit = ctjs.truthy %p
  %escaped = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %object : !ctjs.value
  }
  ctjs.store_global "escaped_identity", %escaped
)mlir")},
        {"unknown incoming parameters cannot acquire a fresh allocation's proof",
         function(refused + R"mlir(
  %bit = ctjs.truthy %q
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %p : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"a primitive incoming alternative does not become an object identity",
         function(refused + R"mlir(
  %bit = ctjs.truthy %p
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %nil : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"an unknown call result does not become an object identity", function(refused + R"mlir(
  %unknown = ctjs.call %p(%nil)
  %bit = ctjs.truthy %q
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %unknown : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"an unknown constructor result does not become an object identity",
         function(refused + R"mlir(
  %unknown = ctjs.construct %p(%p)
  %bit = ctjs.truthy %q
  %selected = scf.if %bit -> !ctjs.value {
    scf.yield %object : !ctjs.value
  } else {
    scf.yield %unknown : !ctjs.value
  }
  %comparison = ctjs.compare strict_eq %selected, %object
)mlir")},
        {"a closed exact direct call preserves an owning comparison alias", function(fresh + R"mlir(
  %alias = ctjs.call_direct @identity(%nil, %nil, %nil, %object)
  %comparison = ctjs.compare strict_eq %alias, %object
)mlir") + R"mlir(
ctjs.func private @identity(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %value: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  ctjs.return %value
}
)mlir"},
    };
    for (const auto & r : rows) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(r.source, &context);
        if (!module) {
            std::printf("FAIL %s: comparison identity fixture did not parse\n", r.what);
            ++failures;
            continue;
        }
        checkComparisonIdentityPreparation(*module, r.what);
        forgeComparisonIdentityReports(*module);
        checkComparisonIdentityPreparation(*module, r.what);
        auto freshModule = mlir::OwningOpRef<mlir::ModuleOp>(module->clone());
        forgeComparisonIdentityReports(*freshModule);
        checkComparisonIdentityPreparation(*freshModule, r.what);
    }
    std::printf("comparison identity: %zu source rows, clean/forged/fresh preparation\n",
                rows.size());
}

void checkComparisonIdentityMutations(mlir::MLIRContext & context) {
    using namespace ctcompile;
    const auto expect = [&](mlir::ModuleOp module, bool accepted) {
        module.walk([&](mlir::Operation * op) {
            if (op->hasAttr("test_identity") && !op->hasAttr("environment_only")) {
                op->setAttr("test_identity", mlir::BoolAttr::get(&context, accepted));
            }
            if (op->hasAttr("test_field_family")) {
                op->setAttr("test_field_family",
                            mlir::IntegerAttr::get(mlir::IntegerType::get(&context, 64),
                                                   accepted ? 0 : -1));
            }
        });
    };
    unsigned states = 0;
    const auto liveAndFresh = [&](mlir::ModuleOp module, const char * what, bool accepted) {
        ++states;
        expect(module, accepted);
        forgeComparisonIdentityReports(module);
        // Clone before the old module is rechecked: both queries see forged
        // prior reports, including deliberately invalid live SSA/arity below.
        auto fresh = mlir::OwningOpRef<mlir::ModuleOp>(module.clone());
        checkComparisonIdentityPreparation(module, what);
        checkComparisonIdentityPreparation(*fresh, what);
    };
    auto module = mlir::parseSourceString<mlir::ModuleOp>(R"mlir(
module {
  ctjs.func @caller(%receiver: !ctjs.value, %new_target: !ctjs.value,
                    %callee: !ctjs.value, %p: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %one = ctjs.constant #ctjs.number<4607182418800017408>
    %field = ctjs.constant #ctjs.string<"value">
    %object = ctjs.create_object {test_identity = true, check}
    ctjs.set_property %object[%field], %one {test_field_family = 0 : i64}
    %alias = ctjs.call_direct @identity(%nil, %nil, %nil, %object, %one)
    %comparison = ctjs.compare strict_eq %alias, %object
    ctjs.return %comparison
  }
  ctjs.func private @identity(%receiver: !ctjs.value, %new_target: !ctjs.value,
                              %callee: !ctjs.value, %value: !ctjs.value,
                              %unused: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.return %value
  }
}
)mlir",
                                                          &context);
    if (!module) {
        std::printf("FAIL comparison identity mutation fixture did not parse\n");
        ++failures;
        return;
    }
    auto caller = module->lookupSymbol<ctjs::FuncOp>("caller");
    auto helper = module->lookupSymbol<ctjs::FuncOp>("identity");
    ctjs::CallDirectOp call;
    ctjs::CompareOp comparison;
    ctjs::SetPropertyOp store;
    ctjs::CreateObjectOp object;
    caller.walk([&](ctjs::CallDirectOp found) { call = found; });
    caller.walk([&](ctjs::CompareOp found) { comparison = found; });
    caller.walk([&](ctjs::SetPropertyOp found) { store = found; });
    caller.walk([&](ctjs::CreateObjectOp found) { object = found; });
    if (!caller || !helper || !call || !comparison || !store || !object) {
        std::printf("FAIL comparison identity mutation fixture lost source operations\n");
        ++failures;
        return;
    }
    liveAndFresh(*module, "closed comparison alias before live mutations", true);

    const auto strict = comparison.getKindAttr();
    comparison.setKindAttr(ctjs::CompareKindAttr::get(&context, ctjs::CompareKind::Eq));
    liveAndFresh(*module, "changing the only strict use removes the candidate", false);
    comparison.setKindAttr(strict);
    liveAndFresh(*module, "restoring the strict comparison restores the proof", true);

    const std::vector<mlir::Value> actuals(call->operand_begin(), call->operand_end());
    call->eraseOperands(4, 1);
    liveAndFresh(*module, "missing later actual cannot preserve a compared object argument", false);
    call->setOperands(actuals);
    liveAndFresh(*module, "restoring missing actual restores exact closed flow", true);
    std::vector<mlir::Value> surplus = actuals;
    surplus.push_back(actuals.front());
    call->setOperands(surplus);
    liveAndFresh(*module, "surplus actual cannot preserve a compared object argument", false);
    call->setOperands(actuals);
    liveAndFresh(*module, "restoring surplus actual restores exact closed flow", true);

    const auto target = call.getCalleeAttr();
    call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(&context, "missing"));
    liveAndFresh(*module, "a changed unresolved direct target invalidates stale identity", false);
    call.setCalleeAttr(target);
    liveAndFresh(*module, "restoring the direct target rebuilds the identity proof", true);

    const auto visibility = helper->getAttr("sym_visibility");
    helper->setAttr("sym_visibility", mlir::StringAttr::get(&context, "public"));
    liveAndFresh(*module, "public helper visibility invalidates its old closed census", false);
    helper->setAttr("sym_visibility", visibility);
    liveAndFresh(*module, "restoring private visibility rebuilds the closed census", true);

    mlir::OpBuilder before(comparison);
    auto extra = llvm::cast<ctjs::CallDirectOp>(before.insert(call->clone()));
    extra->setOperand(3, caller.getBody().front().getArgument(3));
    liveAndFresh(*module, "one new unknown caller invalidates the complete producer census", false);
    extra.erase();
    liveAndFresh(*module, "removing the unknown caller restores all fresh producers", true);

    auto published = ctjs::StoreGlobalOp::create(before, comparison.getLoc(), "escaped_identity",
                                                 object.getResult());
    liveAndFresh(*module, "a new publication rejects an otherwise proved comparison family", false);
    published.erase();
    liveAndFresh(*module, "removing publication restores the family", true);

    const auto field = store.getKey();
    store->setOperand(1, caller.getBody().front().getArgument(3));
    liveAndFresh(*module, "a changed dynamic field key invalidates stale field groups", false);
    store->setOperand(1, field);
    liveAndFresh(*module, "restoring the ordinary field key rebuilds the family", true);

    auto key = field.getDefiningOp<ctjs::ConstantOp>();
    const auto ordinary = key.getValue();
    key.setValueAttr(ctjs::StringAttr::get(&context, "__proto__"));
    liveAndFresh(*module, "a prototype field spelling invalidates stale field groups", false);
    key.setValueAttr(ordinary);
    liveAndFresh(*module, "restoring the field spelling rebuilds the family", true);

    // A use moved before its allocation, or into a sibling region, has no
    // source origin. A clone of the invalid live IR must refuse just as the
    // already-prepared module does, without relying on parser verification.
    const std::string scoped = R"mlir(
ctjs.func @scoped(%receiver: !ctjs.value, %new_target: !ctjs.value,
                  %callee: !ctjs.value, %p: !ctjs.value) -> !ctjs.value
    attributes {upvalue_count = 0 : i32} {
  %nil = ctjs.constant #ctjs.undefined
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %field = ctjs.constant #ctjs.string<"value">
  %condition = ctjs.truthy %p
  scf.if %condition {
    %object = ctjs.create_object {test_identity = true, check}
    ctjs.set_property %object[%field], %one {test_field_family = 0 : i64}
    %comparison = ctjs.compare strict_eq %object, %object
  } else {
  }
  ctjs.return %nil
}
)mlir";
    for (bool sibling : {false, true}) {
        auto source = mlir::parseSourceString<mlir::ModuleOp>(scoped, &context);
        if (!source) {
            std::printf("FAIL comparison identity source-scope fixture did not parse\n");
            ++failures;
            continue;
        }
        ctjs::CreateObjectOp scopedObject;
        ctjs::CompareOp scopedComparison;
        mlir::scf::IfOp branch;
        source->walk([&](ctjs::CreateObjectOp found) { scopedObject = found; });
        source->walk([&](ctjs::CompareOp found) { scopedComparison = found; });
        source->walk([&](mlir::scf::IfOp found) { branch = found; });
        if (!scopedObject || !scopedComparison || !branch) {
            std::printf("FAIL comparison identity scope fixture lost source positions\n");
            ++failures;
            continue;
        }
        liveAndFresh(*source, "scoped comparison before live source mutation", true);
        auto * restoreBefore = scopedComparison->getNextNode();
        if (sibling) {
            scopedComparison->moveBefore(branch.getElseRegion().front().getTerminator());
        } else {
            scopedComparison->moveBefore(scopedObject);
        }
        liveAndFresh(*source,
                     sibling ? "comparison moved into a sibling region loses its source origin"
                             : "comparison moved before allocation loses its source origin",
                     false);
        scopedComparison->moveBefore(restoreBefore);
        liveAndFresh(*source, "restoring exact comparison source scope rebuilds the proof", true);
    }
    // The field environment is its own source proof. A property operation on
    // an unrelated allocation cannot use a same-schema origin from another
    // region. Fieldless identities do not depend on that environment at all.
    for (bool withField : {false, true}) {
        const std::string environment = prologue() + R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %field = ctjs.constant #ctjs.string<"value">
  %object = ctjs.create_object {test_identity = true, check}
)mlir" +
                                        (withField ? "  ctjs.set_property %object[%field], %one "
                                                     "{test_field_family = 0 : i64}\n"
                                                   : "") +
                                        R"mlir(
  %comparison = ctjs.compare strict_eq %object, %object
  %condition = ctjs.truthy %p
  scf.if %condition {
    %other = ctjs.create_object {test_identity = false, environment_only}
    %read = ctjs.get_property %other[%field] {environment_read}
  } else {
  }
  ctjs.return %comparison
}
)mlir";
        for (bool sibling : {false, true}) {
            auto source = mlir::parseSourceString<mlir::ModuleOp>(environment, &context);
            if (!source) {
                std::printf("FAIL comparison field-environment scope fixture did not parse\n");
                ++failures;
                continue;
            }
            mlir::Operation * other = nullptr;
            mlir::Operation * read = nullptr;
            mlir::scf::IfOp branch;
            source->walk([&](mlir::Operation * op) {
                if (op->hasAttr("environment_only")) { other = op; }
                if (op->hasAttr("environment_read")) { read = op; }
                if (auto found = llvm::dyn_cast<mlir::scf::IfOp>(op)) { branch = found; }
            });
            if (!other || !read || !branch) {
                std::printf("FAIL comparison field-environment fixture lost source positions\n");
                ++failures;
                continue;
            }
            liveAndFresh(*source, "independent field environment before source mutation", true);
            auto * restoreBefore = read->getNextNode();
            read->moveBefore(sibling ? branch.getElseRegion().front().getTerminator() : other);
            // Only the source proof is queried on malformed IR. The general
            // sparse type solver is entitled to require verified SSA.
            source->walk([](mlir::Operation * op) { op->removeAttr("check"); });
            liveAndFresh(*source,
                         withField ? "unrelated malformed property origin blocks field identity"
                                   : "fieldless identity needs no unrelated property-origin proof",
                         !withField);
            read->moveBefore(restoreBefore);
            source->walk([&](mlir::Operation * op) {
                if (op->hasAttr("test_identity") && !op->hasAttr("environment_only")) {
                    op->setAttr("check", mlir::UnitAttr::get(&context));
                }
            });
            liveAndFresh(*source, "restoring unrelated property scope rebuilds field environment",
                         true);
        }
    }
    std::printf("comparison identity: %u live/fresh mutation states\n", states);
}

// Exercise the dependency, rather than relying on a particular worklist order:
// the literal arrives only after the alias first waits, then the actual saved
// SSA lattice widens three times. These are sound overapproximations of the
// same immutable program. No host category supplies a native type here.
class ScheduledScalarInference final : public TypeInference {
public:
    ScheduledScalarInference(mlir::DataFlowSolver & solver,
                             const ctcompile::ctnative::OwnedGlobalRoots * owner,
                             mlir::Operation * literal, mlir::Value saved,
                             mlir::Operation * observed)
        : TypeInference(solver, owner), solver_(solver), literal_(literal), saved_(saved),
          observed_(observed) {}

    mlir::LogicalResult visitOperation(mlir::Operation * op,
                                       llvm::ArrayRef<const TypeLattice *> operands,
                                       llvm::ArrayRef<TypeLattice *> results) override {
        if (op == literal_ && stage_ == 0) { return mlir::success(); }
        if (failed(TypeInference::visitOperation(op, operands, results))) {
            return mlir::failure();
        }
        if (op != observed_) { return mlir::success(); }
        using namespace ctcompile::ctnative;
        const auto type = results.front()->getValue().getType();
        if (stage_ == 0 && !type) {
            ++stage_;
            solver_.enqueue({solver_.getProgramPointAfter(literal_), this});
        } else if (stage_ == 1 && type == NumType::get(op->getContext(), NumKind::I32)) {
            ++stage_;
            widen(NumType::get(op->getContext(), NumKind::F64));
        } else if (stage_ == 2 && type == NumType::get(op->getContext(), NumKind::F64)) {
            ++stage_;
            widen(OptType::get(op->getContext(), type));
        } else if (stage_ == 3 &&
                   type == OptType::get(op->getContext(),
                                        NumType::get(op->getContext(), NumKind::F64))) {
            ++stage_;
            widen(BoxedType::get(op->getContext()));
        } else if (stage_ == 4 && type && llvm::isa<BoxedType>(type)) {
            ++stage_;
        }
        return mlir::success();
    }

    [[nodiscard]] unsigned stages() const { return stage_; }

private:
    void widen(mlir::Type type) {
        auto * producer = solver_.getOrCreateState<TypeLattice>(saved_);
        propagateIfChanged(producer, producer->join(ctcompile::ctnative::TypeValue{type}));
    }

    mlir::DataFlowSolver & solver_;
    mlir::Operation * literal_;
    mlir::Value saved_;
    mlir::Operation * observed_;
    unsigned stage_ = 0;
};

void checkSavedScalarGlobalTypes(mlir::MLIRContext & context) {
    namespace fixtures = ctcompile::test::owned_global_methods;
    namespace ctjs = ctcompile::ctjs;
    using ctcompile::ctnative::HostContractAnalysis;
    using ctcompile::ctnative::OwnedGlobalRoots;
    using fixtures::replaced;
    const auto require = [](bool condition, const char * message) {
        if (condition) { return; }
        std::printf("FAIL scalar global inference: %s\n", message);
        ++failures;
    };
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = fixtures::contractFor(module);
        contract.initialIntrinsics = {"Map"};
        contract.observations = {"saved", "alias", "trace"};
        return contract;
    };
    const std::string store = "    ctjs.store_global \"saved\", %answer\n";
    const std::string read = "    %saved = ctjs.load_global \"saved\"\n";
    const std::string alias = "    ctjs.store_global \"alias\", %saved\n"
                              "    %aliasResult = ctjs.load_global \"alias\" {check}\n";
    auto source =
        replaced(fixtures::capturedFixture, "    ctjs.store_global \"trace\", %answer\n",
                 store + read + alias + "    ctjs.store_global \"trace\", %aliasResult\n");
    source = replaced(source, "    ctjs.return %size\n",
                      std::string("    %literal = ctjs.constant ") + kFive +
                          " {test_scalar_producer}\n    ctjs.return %literal\n");
    unsigned rows = 0;
    unsigned states = 0;
    for (const bool prepared : {false, true}) {
        auto program = source;
        if (prepared) {
            program = replaced(program, "    %cell = ctjs.create_cell %u\n", "");
            program = replaced(program, "    ctjs.cell_set %cell, %state\n", "");
            program = replaced(program, "captures %cell", "captures %state");
            program = replaced(program, "    %state = ctjs.load_upvalue %callee[0]\n", "");
            program = replaced(
                program, "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value)",
                "@get$3(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, "
                "%state: !ctjs.value)");
            program = replaced(program, "upvalue_count = 1 : i32", "upvalue_count = 0 : i32");
            program = replaced(program, "%answer = ctjs.call %getter(%owned)",
                               "%environment = ctjs.load_upvalue %getter[0]\n"
                               "    %answer = ctjs.call_direct @get$3(%owned, %u, %getter, "
                               "%environment)");
        }
        const char * exact = prepared ? "!ctnative.num<i32>" : "!ctnative.boxed";
        const char * optional = prepared ? "!ctnative.opt<!ctnative.num<i32>>" : "!ctnative.boxed";
        const auto variant = [&](const std::string & text, bool proved, unsigned reads,
                                 const char * expected, const char * message) {
            ++rows;
            auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
            require(static_cast<bool>(module), "source/prepared fixture parses");
            if (!module) { return; }
            const auto contract = requested(*module);
            HostContractAnalysis host(*module, contract);
            OwnedGlobalRoots owner(*module, contract);
            require(host.proved() == proved && owner.proved() == proved && !host.exhausted() &&
                        !owner.exhausted(),
                    message);
            require(host.scalarReads().size() == reads && owner.scalarReads().size() == reads,
                    "only complete live proofs supply the expected scalar edges");
            check(*module, message, expected, &owner);
            require(ctcompile::ctnative::hostContractFingerprint(*module) == contract.moduleSha256,
                    "solving leaves every source operation unchanged");
        };
        variant(program, true, 2, exact,
                "saved aliases join actual SSA types, even when a host category says Number");
        variant(replaced(program, kFive, kOneAndAHalf), true, 2,
                prepared ? "!ctnative.num<f64>" : "!ctnative.boxed",
                "a fractional producer cannot be narrowed to an integer by its category");
        variant(replaced(program, kFive, kNegativeZero), true, 2,
                prepared ? "!ctnative.num<f64>" : "!ctnative.boxed",
                "negative zero keeps the real producer's f64 lattice");
        auto repeated =
            replaced(program, read, read + "    %savedAgain = ctjs.load_global \"saved\"\n");
        repeated = replaced(repeated, "ctjs.store_global \"alias\", %saved",
                            "ctjs.store_global \"alias\", %savedAgain");
        variant(repeated, true, 3, exact, "each repeated load subscribes to the same sole store");
        variant(replaced(program, store, store + store), true, 0, optional,
                "two equal stores cannot borrow a previous single-store initialization proof");
        variant(replaced(program, store + read, read + store), false, 0, optional,
                "a load preceding its store retains implicit Undefined");
        variant(replaced(program, read, "    %saved = ctjs.load_global \"unknown\"\n"), false, 0,
                "!ctnative.boxed", "an undeclared global cannot borrow another binding's edge");
        for (const char * name : {"globalThis", "window"}) {
            variant(
                replaced(program, store,
                         std::string("    %dynamic = ctjs.load_global \"") + name + "\"\n" + store),
                false, 0, "!ctnative.boxed",
                "dynamic globals remain boxed despite the unchanged saved scalar program");
        }
        auto literal = replaced(program, store,
                                std::string("    %entryLiteral = ctjs.constant ") + kFive + "\n" +
                                    "    ctjs.store_global \"saved\", %entryLiteral\n");
        variant(literal, true, 2, "!ctnative.num<i32>",
                "constant-only aliases join the literal's actual initialized type");
        variant(replaced(literal, kFive, kOneAndAHalf), true, 2, "!ctnative.num<f64>",
                "constant-only fractional aliases preserve their original f64 producer");
        variant(replaced(literal, kFive, kNegativeZero), true, 2, "!ctnative.num<f64>",
                "constant-only negative zero cannot become an i32 through its category");
        const std::string literalStore = "    ctjs.store_global \"saved\", %entryLiteral\n";
        variant(replaced(literal, literalStore,
                         "    %sum = ctjs.binary add %entryLiteral, %entryLiteral\n"
                         "    ctjs.store_global \"saved\", %sum\n"),
                true, 2, "!ctnative.num<f64>",
                "constant Number arithmetic keeps the ordinary double arithmetic type");
        variant(replaced(literal, "    ctjs.store_global \"alias\", %saved\n",
                         "    %sum = ctjs.binary add %saved, %entryLiteral\n"
                         "    ctjs.store_global \"alias\", %sum\n"),
                true, 2, "!ctnative.num<f64>",
                "arithmetic after a constant alias retains the actual arithmetic lattice");
        variant(replaced(literal, read, read + "    %savedAgain = ctjs.load_global \"saved\"\n"),
                true, 3, "!ctnative.num<i32>",
                "repeated constant reads each subscribe to the one original literal");
        variant(replaced(literal, literalStore, literalStore + literalStore), true, 0,
                "!ctnative.opt<!ctnative.num<i32>>",
                "equal constant stores retain absence without a sole initialization edge");
        variant(replaced(literal, literalStore + read, read + literalStore), false, 0,
                "!ctnative.opt<!ctnative.num<i32>>",
                "a constant read before its initialization retains implicit Undefined");
        variant(replaced(literal, literalStore,
                         "    %unrelated = ctjs.call %this(%u)\n" + literalStore),
                false, 0, "!ctnative.opt<!ctnative.num<i32>>",
                "an unrelated unknown call blocks constant initialization authority");
        for (const char * name : {"globalThis", "window"}) {
            variant(replaced(literal, literalStore,
                             std::string("    %dynamic = ctjs.load_global \"") + name + "\"\n" +
                                 literalStore),
                    false, 0, "!ctnative.boxed",
                    "dynamic global access keeps constant-only observations boxed");
        }

        for (const bool constantOnly : {false, true}) {
            const char * actualType = constantOnly ? "!ctnative.num<i32>" : exact;
            const char * absentType = constantOnly ? "!ctnative.opt<!ctnative.num<i32>>" : optional;
            auto module =
                mlir::parseSourceString<mlir::ModuleOp>(constantOnly ? literal : program, &context);
            require(static_cast<bool>(module), "live-proof fixture parses");
            if (!module) { continue; }
            auto contract = requested(*module);
            OwnedGlobalRoots complete(*module, contract);
            require(complete.proved() && complete.scalarReads().size() == 2,
                    "the immutable fixture has complete independent initialization evidence");
            if (!complete.proved() || complete.scalarReads().size() != 2) { continue; }
            check(*module, "omitting the owner retains the ordinary global absence seed",
                  absentType);
            const unsigned completion = complete.steps();
            require(completion > 0, "owner proof has a nonzero complete-work bound");
            for (const unsigned budget : {0u, completion / 2, completion - 1}) {
                OwnedGlobalRoots limited(*module, contract, budget);
                require(!limited.proved() && limited.exhausted() && limited.scalarReads().empty(),
                        "an incomplete ownership proof supplies no partial initialization edges");
                check(*module, "exhausted ownership retains implicit global absence", absentType,
                      &limited);
            }
            OwnedGlobalRoots exactBudget(*module, contract, completion);
            require(exactBudget.proved() && exactBudget.steps() == completion,
                    "the exact completion budget supplies the complete proof");
            check(*module, "the exact owner budget enables only the actual stored-value lattice",
                  actualType, &exactBudget);

            if (prepared || constantOnly) {
                mlir::Operation * delayed = nullptr;
                mlir::Operation * observed = nullptr;
                module->walk([&](mlir::Operation * op) {
                    if (!constantOnly && op->hasAttr("test_scalar_producer")) { delayed = op; }
                    if (op->hasAttr("check")) { observed = op; }
                });
                if (constantOnly) {
                    delayed = complete.scalarReads().front().value.getDefiningOp();
                }
                require(delayed && observed, "the scheduled producer and alias observation exist");
                if (delayed && observed) {
                    mlir::DataFlowSolver solver;
                    solver.load<mlir::dataflow::DeadCodeAnalysis>();
                    solver.load<mlir::dataflow::SparseConstantPropagation>();
                    auto * inference = solver.load<ScheduledScalarInference>(
                        &complete, delayed, complete.scalarReads().front().value, observed);
                    require(succeeded(solver.initializeAndRun(*module)),
                            "the scheduled scalar producer converges");
                    require(inference->stages() == 5, "alias waits, receives i32, widens to f64 "
                                                      "and optional, then becomes boxed");
                }
            }

            // Rebuild the borrowed owner after every edit. An old fingerprint must
            // refuse before exposing any edges; a new one must prove the live IR.
            mlir::Builder attributes(&context);
            module->walk([&](mlir::Operation * op) {
                op->setAttr("ctnative.scalar_global", attributes.getStringAttr("number"));
                op->setAttr("ctnative.inferred_result", attributes.getStringAttr("number"));
                op->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
                op->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            });
            contract = requested(*module);
            OwnedGlobalRoots reported(*module, contract);
            require(reported.proved(),
                    "report attributes leave valid live initialization provable");
            check(*module, "reports do not change independently inferred scalar types", actualType,
                  &reported);
            auto initialization = complete.scalarReads().front().initialization;
            auto savedRead = complete.scalarReads().front().read;
            const auto checkChanged = [&](const char * expected, bool proves = false) {
                OwnedGlobalRoots stale(*module, contract);
                require(!stale.proved() && stale.reason().contains("fingerprint") &&
                            stale.scalarReads().empty(),
                        "a stale fingerprint withholds all scalar initialization edges");
                check(*module, "a stale contract cannot drop implicit absence", absentType, &stale);
                const auto freshContract = requested(*module);
                OwnedGlobalRoots fresh(*module, freshContract);
                require(fresh.proved() == proves && !fresh.exhausted() &&
                            fresh.scalarReads().empty(),
                        "fresh proof checks the actual edited source instead of Number reports");
                check(*module, "the edited live source controls the scalar load", expected, &fresh);
                mlir::OwningOpRef<mlir::ModuleOp> clone{
                    llvm::cast<mlir::ModuleOp>(module->clone())};
                OwnedGlobalRoots cloned(*clone, requested(*clone));
                require(cloned.proved() == proves && cloned.scalarReads().empty(),
                        "a fresh clone independently rechecks edited edges");
                check(*clone, "fresh cloned source preserves the edited scalar type", expected,
                      &cloned);
                ++states;
            };
            const auto restored = [&]() {
                OwnedGlobalRoots owner(*module, contract);
                require(owner.proved(),
                        "restored source independently regains initialization evidence");
                check(*module, "restored initialization joins its unchanged real SSA type",
                      actualType, &owner);
            };
            initialization->moveAfter(savedRead);
            checkChanged(absentType);
            initialization->moveBefore(savedRead);
            restored();
            mlir::OpBuilder duplicateBuilder(initialization);
            duplicateBuilder.setInsertionPointAfter(initialization);
            auto * duplicate = duplicateBuilder.clone(*initialization);
            checkChanged(absentType, true);
            duplicate->erase();
            restored();
            savedRead->setAttr("name", attributes.getStringAttr("trace"));
            // This creates an uninitialized cycle in the ordinary global lattice;
            // query the finite proof alone rather than interpreting that cycle as
            // a fresh Number fact or asking the solver to guess an initializer.
            OwnedGlobalRoots wrongName(*module, requested(*module));
            require(!wrongName.proved() && wrongName.scalarReads().empty(),
                    "a changed global name cannot consume the old edge");
            ++states;
            savedRead->setAttr("name", attributes.getStringAttr("saved"));
            restored();
            auto getter = module->lookupSymbol<ctjs::FuncOp>("get$3");
            auto returned = llvm::cast<ctjs::ReturnOp>(getter.getBody().front().getTerminator());
            const auto originalReturn = returned.getValue();
            returned->setOperand(0, getter.getBody().front().getArgument(0));
            // The changed method invalidates complete ownership. Its saved
            // result becomes boxed; an independent constant keeps its actual
            // literal type plus the ordinary global absence seed.
            OwnedGlobalRoots changedReturn(*module, requested(*module));
            require(!changedReturn.proved() && changedReturn.scalarReads().empty(),
                    "an unknown return cannot inherit the former Number category");
            check(*module, "a changed method cannot preserve initialization through Number reports",
                  constantOnly ? absentType : "!ctnative.boxed", &changedReturn);
            ++states;
            returned->setOperand(0, originalReturn);
            restored();
        }
    }
    require(rows == 40 && states == 16, "all source/prepared rows and live source edits ran");
    std::printf("scalar global inference: %u source/prepared rows, %u live edits, "
                "pending/i32/f64/optional/boxed subscription checked\n",
                rows, states);
}

} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctcompile::ctjs::CTJSDialect>();
    context.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::ub::UBDialect>();
    context.getOrLoadDialect<ctcompile::ctnative::CTNativeDialect>();

    const std::string five = std::string{"  %a = ctjs.constant "} + kFive + "\n" +
                             "  %b = ctjs.constant " + kFive + "\n";
    // The array rows need their own constant names: `five` spells %a and %b,
    // and %a would collide with the array in the rows below.
    const std::string ints = std::string{"  %i1 = ctjs.constant "} + kFive + "\n" +
                             "  %i2 = ctjs.constant " + kFive + "\n";

    const std::vector<row> rows = {
        // --- literals, where the bound proof is the literal itself ----------
        {"5 is an int32", std::string{"  %r = ctjs.constant "} + kFive + " {check}\n",
         "!ctnative.num<i32>"},
        {"1.5 is not an int32", std::string{"  %r = ctjs.constant "} + kOneAndAHalf + " {check}\n",
         "!ctnative.num<f64>"},
        // THE ROW MOST LIKELY TO BE GOT WRONG. -0 is integral and inside int32,
        // and `Object.is(-0, 0)` is false, so calling it an int32 loses a
        // difference JavaScript can see. type-oracle.py agrees by counting
        // NUM_NEGATIVE_ZERO as not-an-i32.
        {"-0 is integral and in range and is still NOT an int32",
         std::string{"  %r = ctjs.constant "} + kNegativeZero + " {check}\n", "!ctnative.num<f64>"},
        {"2**31 is one past the int32 maximum",
         std::string{"  %r = ctjs.constant "} + kTwoToThe31 + " {check}\n", "!ctnative.num<f64>"},
        {"undefined is an empty optional", "  %r = ctjs.constant #ctjs.undefined {check}\n",
         "!ctnative.opt<!ctnative.bottom>"},
        {"null is the same empty optional, which is a declared divergence",
         "  %r = ctjs.constant #ctjs.null {check}\n", "!ctnative.opt<!ctnative.bottom>"},
        {"a boolean literal", "  %r = ctjs.constant #ctjs.boolean<true> {check}\n",
         "!ctnative.bool"},
        {"a string literal", "  %r = ctjs.constant #ctjs.string<\"hi\"> {check}\n",
         "!ctnative.str<utf8>"},

        // --- the operators that need no operand proof -----------------------
        {"`>>>` has no BigInt form, so it is always a number",
         "  %r = ctjs.binary ushr %p, %q {check}\n", "!ctnative.num<f64>"},
        {"`typeof` is always a string", "  %r = ctjs.unary typeof %p {check}\n",
         "!ctnative.str<utf8>"},
        {"unary `+` is ToNumber, which throws on a BigInt", "  %r = ctjs.unary plus %p {check}\n",
         "!ctnative.num<f64>"},
        {"a comparison is always a boolean", "  %r = ctjs.compare lt %p, %q {check}\n",
         "!ctnative.bool"},
        {"concat never consults the BigInt arm", "  %r = ctjs.binary concat %p, %q {check}\n",
         "!ctnative.str<utf8>"},
        {"ToNumber is a double", "  %r = ctjs.convert to_number %p {check}\n",
         "!ctnative.num<f64>"},

        // --- THE NEGATIVE ROWS, which are why the table exists --------------
        {"`|` on unknown operands could be BigInt and must NOT claim i32",
         "  %r = ctjs.binary bitor %p, %q {check}\n", "!ctnative.boxed"},
        {"`-` on unknown operands could be BigInt", "  %r = ctjs.binary sub %p, %q {check}\n",
         "!ctnative.boxed"},
        {"`~` on an unknown operand could be BigInt", "  %r = ctjs.unary bitnot %p {check}\n",
         "!ctnative.boxed"},
        {"generic `+` on unknown operands could concatenate or be BigInt",
         "  %r = ctjs.binary add %p, %q {check}\n", "!ctnative.boxed"},
        {"generic `+` on one unknown operand stays boxed even beside a number",
         five + "  %r = ctjs.binary add %a, %p {check}\n", "!ctnative.boxed"},
        // --- and the two halves of `+` that ARE provable ---------------------
        {"generic `+` on two numbers is a double", five + "  %r = ctjs.binary add %a, %b {check}\n",
         "!ctnative.num<f64>"},
        {"generic `+` on a number and undefined is a double (NaN, but a number)",
         five + "  %u = ctjs.constant #ctjs.undefined\n"
                "  %r = ctjs.binary add %a, %u {check}\n",
         "!ctnative.num<f64>"},
        {"generic `+` with a proved string on either side is a string",
         five + "  %s = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.binary add %a, %s {check}\n",
         "!ctnative.str<utf8>"},
        {"generic `+` of a string and an unknown operand is still a string",
         "  %s = ctjs.constant #ctjs.string<\"x\">\n"
         "  %r = ctjs.binary add %s, %p {check}\n",
         "!ctnative.str<utf8>"},

        // --- A SECOND BLOCK, which the single-block rows above cannot test --
        //
        // Every other row lives in the entry block, and the entry block is
        // live by fiat. This one puts the checked op behind a branch so the
        // solver has to decide the successor is live - which it cannot do
        // without SparseConstantPropagation loaded. Without it this row reads
        // `<uninitialized>`.
        {"a value behind a branch is still visited",
         "  %t = ctjs.truthy %p\n"
         "  cf.cond_br %t, ^yes, ^no\n"
         "^yes:\n"
         "  %r = ctjs.unary typeof %p {check}\n"
         "  ctjs.return %r\n"
         "^no:\n",
         "!ctnative.str<utf8>"},

        // --- THE CLOSED WORLD FOR GLOBALS (part 24 Phase 62½-A) -------------
        //
        // A load of a global is the join of every store of that name in the
        // module. Three rows: a stored number is a number; a name nothing
        // stores is boxed (it is a builtin or undeclared); and the mere
        // presence of a `globalThis` load anywhere makes every global boxed,
        // because the table may then be written by a path this rule cannot
        // see.
        {"a global stored a number loads as a number OR undefined - nothing orders the load after "
         "the store",
         five + "  ctjs.store_global \"g\", %a\n"
                "  %r = ctjs.load_global \"g\" {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a global stored a number and a string loads as their join",
         five + "  ctjs.store_global \"g\", %a\n"
                "  %s = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.store_global \"g\", %s\n"
                "  %r = ctjs.load_global \"g\" {check}\n",
         "!ctnative.opt<!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>>"},
        {"a global nothing stores is boxed - it is a builtin or undeclared",
         "  %r = ctjs.load_global \"Math\" {check}\n", "!ctnative.boxed"},
        {"a globalThis load anywhere makes every global boxed",
         five + "  ctjs.store_global \"g\", %a\n"
                "  %w = ctjs.load_global \"globalThis\"\n"
                "  %r = ctjs.load_global \"g\" {check}\n",
         "!ctnative.boxed"},

        // --- THE LIFT'S POISON IS THE IDENTITY --------------------------------
        //
        // --ctjs-lift-to-scf yields ub.poison for a loop-carried value on the
        // path that leaves the loop. Joined with a number it must stay that
        // number; typed boxed it would absorb, and every `for` loop would be
        // refused by the native lowering - which is how this row was found.
        {"a number joined with the lift's poison is still that number",
         five + "  %t = ctjs.truthy %p\n"
                "  %z = ub.poison : !ctjs.value\n"
                "  %r = scf.if %t -> (!ctjs.value) {\n"
                "    scf.yield %a : !ctjs.value\n"
                "  } else {\n"
                "    scf.yield %z : !ctjs.value\n"
                "  } {check}\n",
         "!ctnative.num<i32>"},

        // --- THE CLOSED SHAPE (part 24 Phase 56A) ----------------------------
        //
        // An object literal used only through constant keys: a read of a key
        // is the join of its stores, from undefined. Any other use opens the
        // shape and every read is boxed.
        //
        // AND THE undefined SEED IS DROPPED WHERE A STORE DOMINATES THE READ -
        // part 24 Phase 59 slice 2 step 3, the field half. "Nothing orders the
        // read after a store" is a statement about fields in general and false
        // of a particular read that a `ctjs.set_property` of the same key, on
        // the same value, in the same `ctjs.func`, properly dominates. The next
        // three rows are that rule and its two negatives, and the negatives are
        // the load-bearing half: narrowing a value that CAN be undefined is a
        // WRONG ANSWER rather than a refusal, because a global is printed as
        // `%.17g` of a double and undefined-as-NaN spells `nan` there.
        {"a closed object's field reads as its store where the store dominates the read",
         five + "  %o = ctjs.create_object\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n"
                "  %k2 = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.get_property %o[%k2] {check}\n",
         "!ctnative.num<i32>"},
        {"a read BEFORE the store keeps the undefined the field started with",
         five + "  %o = ctjs.create_object\n"
                "  %k2 = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.get_property %o[%k2] {check}\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a store on one arm of an scf.if dominates nothing after it",
         five + "  %o = ctjs.create_object\n"
                "  %t = ctjs.truthy %p\n"
                "  scf.if %t {\n"
                "    %k = ctjs.constant #ctjs.string<\"x\">\n"
                "    ctjs.set_property %o[%k], %a\n"
                "  }\n"
                "  %k2 = ctjs.constant #ctjs.string<\"x\">\n"
                "  %r = ctjs.get_property %o[%k2] {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a key never stored reads as undefined",
         "  %o = ctjs.create_object\n"
         "  %k = ctjs.constant #ctjs.string<\"x\">\n"
         "  %r = ctjs.get_property %o[%k] {check}\n",
         "!ctnative.opt<!ctnative.bottom>"},
        // --- THE CARRIED CELL (part 24 Phase 59 slice 2, steps 2 and 3) ------
        //
        // The closure lift makes a shared binding a frame-scope variable and
        // marks the `ctjs.create_cell` `ctnative.carried`; its type is then the
        // join over the box's INITIAL and every value ever assigned to it,
        // because a read on a path that reached no assignment loads what the
        // variable was built with. `compiler_impl::predeclare_locals` builds it
        // with `undefined`, so a hoisted `var` is `opt<num>`.
        //
        // AND `ctnative.assigned_before_read` IS THE LIFT SAYING THERE IS NO
        // SUCH PATH. It is written only when one `ctjs.cell_set` properly
        // dominates every read of the binding - every `ctjs.cell_get` here, and
        // every CALL of every closure that captured it, which is a question
        // about operations the lift erases and so cannot be asked in this file.
        // These two rows are the contract between the two translation units:
        // the attribute is spelled once, in TypeInference.h, and its whole
        // observable effect is the difference between them.
        {"a carried cell holds its initial as well as its stores",
         five + "  %u = ctjs.constant #ctjs.undefined\n"
                "  %c = ctjs.create_cell %u {ctnative.carried}\n"
                "  ctjs.cell_set %c, %a\n"
                "  %r = ctjs.cell_get %c {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        {"a carried cell the lift proved assigned before every read holds only its stores",
         five + "  %u = ctjs.constant #ctjs.undefined\n"
                "  %c = ctjs.create_cell %u {ctnative.assigned_before_read, ctnative.carried}\n"
                "  ctjs.cell_set %c, %a\n"
                "  %r = ctjs.cell_get %c {check}\n",
         "!ctnative.num<i32>"},

        {"a dynamic key opens the shape: every read is boxed",
         five + "  %o = ctjs.create_object\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n"
                "  ctjs.set_property %o[%p], %a\n"
                "  %r = ctjs.get_property %o[%k] {check}\n",
         "!ctnative.boxed"},
        {"an object that reaches a call has an open shape",
         five + "  %o = ctjs.create_object\n"
                "  %k = ctjs.constant #ctjs.string<\"x\">\n"
                "  ctjs.set_property %o[%k], %a\n"
                "  %c = ctjs.call %p(%o)\n"
                "  %r = ctjs.get_property %o[%k] {check}\n",
         "!ctnative.boxed"},

        // --- THE DENSE ARRAY (part 24 Phase 57A) -----------------------------
        //
        // WHY THESE ROWS EXIST AT ALL. The emitter hardcodes `vector<double>`
        // and PrintDeduced::isDeducible excludes both `emitc.call_opaque` and
        // `emitc.variable`, so nothing downstream ever prints the element type
        // - a join that widened wrongly would still lower, still compile and
        // still agree with the interpreter on the fixture. The element type is
        // observable HERE and in the oracle corpus, and nowhere else.
        {"a dense array literal is a vector of the join of its appends, from undefined",
         ints + "  %arr = ctjs.create_array [] {check}\n"
                "  ctjs.append %i1 to %arr\n"
                "  ctjs.append %i2 to %arr\n",
         "!ctnative.vec<!ctnative.opt<!ctnative.num<i32>>>"},
        // TWO WIDTHS MERGE, THEY DO NOT UNION. Stage 53G normalises `num<i32>`
        // and `num<f64>` into the wider number, so `[1, 2.5]` is a
        // vector<double> and not a vector of a two-alternative variant.
        {"two numeric widths in one array are the wider number, not a union",
         ints + "  %h = ctjs.constant " + kOneAndAHalf +
             "\n"
             "  %arr = ctjs.create_array [] {check}\n"
             "  ctjs.append %i1 to %arr\n"
             "  ctjs.append %h to %arr\n",
         "!ctnative.vec<!ctnative.opt<!ctnative.num<f64>>>"},
        {"a literal's own inline elements count toward the element type too",
         ints + "  %arr = ctjs.create_array [%i1] {check}\n",
         "!ctnative.vec<!ctnative.opt<!ctnative.num<i32>>>"},
        {"an index read is the element type - undefined among it, because a[7] is undefined",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  %r = ctjs.get_property %arr[%i1] {check}\n",
         "!ctnative.opt<!ctnative.num<i32>>"},
        // `length` IS NEVER UNDEFINED and is never an i32 either: an array's
        // length is a uint32, which does not fit one, and nothing here proves
        // this array is short.
        {"`length` on a dense array is a number, and an f64 rather than an i32",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  %k = ctjs.constant #ctjs.string<\"length\">\n"
                "  %r = ctjs.get_property %arr[%k] {check}\n",
         "!ctnative.num<f64>"},

        // --- AND THE FOUR NEGATIVE ROWS, which are why the rule is a proof ---
        //
        // An index STORE is the sparsity route part 24 Stage 57A names by
        // hand: `a[100] = 1` gives `length` 101 with one element. It opens the
        // site, so the literal and every read of it are boxed.
        {"an index store opens the site: a[100] = 1 makes it sparse",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  ctjs.set_property %arr[%i1], %i2\n"
                "  %r = ctjs.get_property %arr[%i1] {check}\n",
         "!ctnative.boxed"},
        // A KEY NOTHING PROVED A NUMBER READS A PROPERTY, NOT AN ELEMENT:
        // `a["push"]` is a function. The site is still dense - a read is a
        // read - so only the key check stands between this and a claim of
        // `opt<num<i32>>` for a function.
        //
        // THIS ROW IS THE ONLY GATE ON THAT GUARD, and the oracle is not, which
        // was measured: a claim is per REGISTER, the bytecode puts the array
        // literal and the read in one slot, and the join over the two is
        // `boxed` whatever the read claims. Deleting the guard turns this row
        // red with `!ctnative.opt<!ctnative.num<i32>>` and leaves every corpus
        // in check-type-claims.cmake green.
        {"a key nothing proved a number reads a property, and is boxed",
         ints + "  %arr = ctjs.create_array []\n"
                "  ctjs.append %i1 to %arr\n"
                "  %r = ctjs.get_property %arr[%p] {check}\n",
         "!ctnative.boxed"},
        {"a read through a named key opens the site",
         ints + "  %arr = ctjs.create_array [] {check}\n"
                "  ctjs.append %i1 to %arr\n"
                "  %k = ctjs.constant #ctjs.string<\"foo\">\n"
                "  %r = ctjs.get_property %arr[%k]\n",
         "!ctnative.boxed"},
        {"an array that escapes into a global is not a vector",
         ints + "  %arr = ctjs.create_array [] {check}\n"
                "  ctjs.append %i1 to %arr\n"
                "  ctjs.store_global \"g\", %arr\n",
         "!ctnative.boxed"},

        // --- and the positive halves of the same operators ------------------
        {"`|` on two numbers is an int32", five + "  %r = ctjs.binary bitor %a, %b {check}\n",
         "!ctnative.num<i32>"},
        {"`-` on two numbers is a double", five + "  %r = ctjs.binary sub %a, %b {check}\n",
         "!ctnative.num<f64>"},
        {"`~` on a number is an int32", five + "  %r = ctjs.unary bitnot %a {check}\n",
         "!ctnative.num<i32>"},
        {"static `+` is ToNumber, so on two numbers it is a double",
         five + "  %r = ctjs.binary_static add %a, %b {check}\n", "!ctnative.num<f64>"},
    };

    for (const row & r : rows) { check(context, r); }
    checkIdentityFieldRows(context);
    checkIdentityMapFieldRows(context);
    checkComparisonIdentityRows(context);
    checkComparisonIdentityMutations(context);
    checkStaleFieldEffects(context);
    checkSavedScalarGlobalTypes(context);

    // The call result is not a thrown payload, and catch state is not a
    // post-call assignment. Query the actual continuation argument, keeping
    // unrelated normal-return typing out of the payload observations.
    const std::string joined = R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32, ctnative.nothrow} {
    %bit = ctjs.truthy %condition
    cf.cond_br %bit, ^left, ^right
  ^left:
    ctjs.throw %argument
  ^right:
    %other = ctjs.constant #ctjs.string<"other">
    ctjs.throw %other
  }
)mlir";
    const std::string returning = R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    ctjs.return %argument
  }
)mlir";
    std::string largeBody;
    for (unsigned index = 0; index < 4096; ++index) {
        largeBody += "    %unused" + std::to_string(index) + " = ctjs.constant #ctjs.undefined\n";
    }
    const std::vector<row> invocationRows = {
        {"invoke obtains a numeric throw from the live callee", invokeModule(throwingHelper(kFive)),
         "!ctnative.num<i32>", true},
        {"invoke preserves negative-zero payload precision",
         invokeModule(throwingHelper(kNegativeZero)), "!ctnative.num<f64>", true},
        {"invoke carries a boolean payload", invokeModule(throwingHelper("#ctjs.boolean<true>")),
         "!ctnative.bool", true},
        {"invoke carries an owning string payload",
         invokeModule(throwingHelper("#ctjs.string<\"payload\">")), "!ctnative.str<utf8>", true},
        {"invoke keeps pre-call state independent of payload and unavailable result",
         invokeModule(throwingHelper(kFive), "%saved"), "!ctnative.str<utf8>", true},
        {"invoke forwards an ordinary call result only to its normal continuation",
         invokeModule(returning, "%returned", true), "!ctnative.num<i32>", true},
        {"invoke joins every live throw and ignores a forged nonthrowing marker",
         invokeModule(joined), "!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>", true},
        {"invoke follows transitive throwing helpers", invokeModule(helperChain(3)),
         "!ctnative.num<i32>", true},
        {"invoke accepts its finite helper-depth boundary", invokeModule(helperChain(32)),
         "!ctnative.num<i32>", true},
        {"invoke refuses a helper-depth proof beyond the bound", invokeModule(helperChain(33)),
         "!ctnative.boxed", true},
        {"invoke refuses an exhausted work proof", invokeModule(throwingHelper(kFive, largeBody)),
         "!ctnative.boxed", true},
        {"invoke does not infer an explicit-only payload across unknown property effects",
         invokeModule(throwingHelper(kFive, "    %key = ctjs.constant #ctjs.string<\"x\">\n"
                                            "    %read = ctjs.get_property %receiver[%key]\n")),
         "!ctnative.boxed", true},
        {"invoke refuses recursive escaping-payload inference",
         invokeModule(throwingHelper(
             kFive, "    %recursive = ctjs.call_direct "
                    "@helper(%receiver, %new_target, %callee, %condition, %argument)\n")),
         "!ctnative.boxed", true},
    };
    for (const row & r : invocationRows) { check(context, r); }

    const std::string numericCompletion = conditionalHelper(kFive, "#ctjs.string<\"failure\">");
    std::string parameterCompletion = numericCompletion;
    parameterCompletion.replace(parameterCompletion.find("ctjs.return %normal"), 19,
                                "ctjs.return %argument");
    const std::string joinedParameterCompletion = R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %bit = ctjs.truthy %condition
    cf.cond_br %bit, ^first, ^second
  ^first:
    cf.br ^normal(%argument : !ctjs.value)
  ^second:
    %other = ctjs.constant #ctjs.number<9223372036854775808>
    cf.cond_br %bit, ^throwing, ^normal(%other : !ctjs.value)
  ^normal(%joined: !ctjs.value):
    ctjs.return %joined
  ^throwing:
    %thrown = ctjs.constant #ctjs.string<"failure">
    ctjs.throw %thrown
  }
)mlir";
    std::string ordinaryCompletion = invokeModule(numericCompletion, "%returned", true);
    const auto normalObservation = ordinaryCompletion.find("    %observed = scf.execute_region");
    ordinaryCompletion.insert(normalObservation,
                              "    %ordinary = ctjs.call_direct "
                              "@helper(%nil, %nil, %nil, %condition, %argument) {check}\n");
    ordinaryCompletion.replace(ordinaryCompletion.find("} {check}"), 9, "}");

    std::string twoReturns = numericCompletion;
    const auto firstReturn = twoReturns.find("    ctjs.return %normal");
    twoReturns.replace(firstReturn, std::string("    ctjs.return %normal").size(), R"mlir(
    cf.cond_br %bit, ^first, ^second
  ^first:
    ctjs.return %normal
  ^second:
    %other = ctjs.constant #ctjs.boolean<true>
    ctjs.return %other
)mlir");

    std::string transitive = numericCompletion;
    transitive.replace(transitive.find("@helper"), 7, "@leaf");
    transitive += R"mlir(
  ctjs.func private @helper(%receiver: !ctjs.value, %new_target: !ctjs.value,
                            %callee: !ctjs.value, %condition: !ctjs.value,
                            %argument: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %nested = ctjs.call_direct @leaf(%receiver, %new_target, %callee, %condition, %argument)
    %normal = ctjs.constant #ctjs.boolean<true>
    ctjs.return %normal
  }
)mlir";

    std::string publicHelper = numericCompletion;
    publicHelper.erase(publicHelper.find("private "), 8);
    std::string capturedHelper = numericCompletion;
    capturedHelper.replace(capturedHelper.find("upvalue_count = 0"), 17, "upvalue_count = 1");
    const std::string propertyEffect = "    %key = ctjs.constant #ctjs.string<\"x\">\n"
                                       "    %read = ctjs.get_property %receiver[%key]\n";
    const std::string recursiveCall =
        "    %recursive = ctjs.call_direct "
        "@helper(%receiver, %new_target, %callee, %condition, %argument)\n";
    const std::vector<row> normalInvocationRows = {
        {"invoke joins normal returns independently from string throws",
         invokeModule(numericCompletion, "%returned", true), "!ctnative.num<i32>", true},
        {"invoke forwards a passed parameter across a helper with throw exits",
         invokeModule(parameterCompletion, "%returned", true), "!ctnative.num<i32>", true},
        {"invoke subscribes a passed parameter and SSA joins before normal return",
         invokeModule(joinedParameterCompletion, "%returned", true), "!ctnative.num<f64>", true},
        {"invoke preserves negative zero on normal completion",
         invokeModule(conditionalHelper(kNegativeZero, kFive), "%returned", true),
         "!ctnative.num<f64>", true},
        {"invoke preserves boolean normal completion independently from number throws",
         invokeModule(conditionalHelper("#ctjs.boolean<true>", kFive), "%returned", true),
         "!ctnative.bool", true},
        {"invoke preserves owning string normal completion independently from boolean throws",
         invokeModule(conditionalHelper("#ctjs.string<\"normal\">", "#ctjs.boolean<false>"),
                      "%returned", true),
         "!ctnative.str<utf8>", true},
        {"invoke joins all normal return alternatives", invokeModule(twoReturns, "%returned", true),
         "!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>", true},
        {"invoke takes normal returns from its own helper, not transitive callees",
         invokeModule(transitive, "%returned", true), "!ctnative.bool", true},
        {"invoke leaves an ordinary call in its normal continuation conservative",
         ordinaryCompletion, "!ctnative.boxed", true},
        {"invoke does not invent a normal result for an unconditional throw",
         invokeModule(throwingHelper(kFive), "%returned", true), "!ctnative.boxed", true},
        {"invoke refuses checked normal flow through unknown property effects",
         invokeModule(conditionalHelper(kFive, kFive, propertyEffect), "%returned", true),
         "!ctnative.boxed", true},
        {"invoke refuses checked normal flow through a recursive helper",
         invokeModule(conditionalHelper(kFive, kFive, recursiveCall), "%returned", true),
         "!ctnative.boxed", true},
        {"invoke refuses checked normal flow from an open public helper",
         invokeModule(publicHelper, "%returned", true), "!ctnative.boxed", true},
        {"invoke refuses checked normal flow from a captured helper",
         invokeModule(capturedHelper, "%returned", true), "!ctnative.boxed", true},
        {"invoke refuses checked normal flow after exhausting the work bound",
         invokeModule(conditionalHelper(kFive, kFive, largeBody), "%returned", true),
         "!ctnative.boxed", true},
    };
    for (const row & r : normalInvocationRows) { check(context, r); }

    // Every solve must inspect the same live helper again. Retain a forged
    // nothrow marker while changing its return and adding a global mutation;
    // neither the old completion query nor the marker may authorize the next.
    {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(
            invokeModule(numericCompletion, "%returned", true), &context);
        if (!module) {
            std::printf("FAIL invocation normal-flow mutation fixture did not parse\n");
            ++failures;
        } else {
            check(*module, "invoke normal flow before live mutation", "!ctnative.num<i32>");
            auto helper = module->lookupSymbol<ctcompile::ctjs::FuncOp>("helper");
            ctcompile::ctjs::ReturnOp returned;
            helper.walk([&](ctcompile::ctjs::ReturnOp found) { returned = found; });
            auto constant = returned.getValue().getDefiningOp<ctcompile::ctjs::ConstantOp>();
            constant->setAttr("value", ctcompile::ctjs::StringAttr::get(&context, "changed"));
            check(*module, "invoke rederives a changed normal return", "!ctnative.str<utf8>");
            mlir::OpBuilder before(returned);
            auto effect = ctcompile::ctjs::StoreGlobalOp::create(before, returned.getLoc(),
                                                                 "published", returned.getValue());
            check(*module, "invoke refuses a newly effectful live helper", "!ctnative.boxed");
            check(*module, "invoke rerun retains the changed helper refusal", "!ctnative.boxed");
            effect.erase();
            check(*module, "invoke rebuilds completion flow after removing the effect",
                  "!ctnative.str<utf8>");
        }
    }

    // AND ONE MODULE THAT MUST NOT PARSE. `ctjs.binary_static sub` names a
    // kind context::binary_op_static has no arm for - it answers undefined -
    // so an inference claiming f64 for it would be wrong, and the fix is not
    // a narrower claim but a verifier that refuses the IR. This is the
    // refutation Phase 54A's adversarial review raised, kept as a test.
    {
        const std::string text =
            prologue() + "  %r = ctjs.binary_static sub %p, %q\n" + "  ctjs.return %r\n}\n";
        mlir::ScopedDiagnosticHandler quiet{&context,
                                            [](mlir::Diagnostic &) { return mlir::success(); }};
        mlir::OwningOpRef<mlir::ModuleOp> module =
            mlir::parseSourceString<mlir::ModuleOp>(text, &context);
        if (module) {
            std::printf("FAIL ctjs.binary_static sub verified, and the helper has no arm for it\n");
            ++failures;
        }
    }

    if (failures != 0) {
        std::printf("\n%d row(s) failed\n", failures);
        return 1;
    }
    std::printf("type inference: every row agrees with JavaScript\n");
    return 0;
}
