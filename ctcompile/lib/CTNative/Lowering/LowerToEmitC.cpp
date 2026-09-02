// THE FIRST NATIVE ARTEFACT - part 24 Phase 62½-C.
//
// Everything before this file is an analysis. This is the lowering: a
// `ctjs.func` whose every value has a PROVED type the native tier can carry
// becomes an `emitc.func` over `double` and `bool`, and a function that does
// not is refused with a diagnostic naming the first value or operation that
// failed. Part 24 §1.2: the output links neither the interpreter nor its
// collector, and there is no boxed fallback - a refusal is a reason on the
// function, and the compilation-unit gate says whether the program is native.
//
// HOW A VALUE IS REPRESENTED, and where the representation is exact:
//
//   bool               bool     exactly
//   num<i32|i64|f64>   double   exactly - every JavaScript number is one
//   opt<num<...>>      double   with undefined as NaN. EXACT in arithmetic
//   opt<bottom>                 (undefined + 1 is NaN), relational comparison
//                               (undefined < 1 is false, as NaN < 1 is), and
//                               truthiness (both are falsy). NOT exact for
//                               equality with undefined/null, `typeof`, or
//                               printing - so every use in which the
//                               difference is observable is refused.
//
// The two `opt` rows exist because of the closed-world global rule: a global
// is undefined until its first store runs and nothing orders a load after
// one, so a numeric global is `opt<num>` (TypeInference.h). Numbers stay
// `double` even when proved `i32`: an int32_t representation is a Phase 63
// measurement, not a Phase 62½ obligation, and `double` is always correct.
//
// NOT A DIALECT CONVERSION, deliberately. After --ctjs-lift-to-scf the body is
// structured, and after the inference every value's type is known; retyping
// each value in place from the lattice and replacing each ctjs operation with
// its EmitC form leaves the whole of `scf` untouched for upstream's
// --convert-scf-to-emitc, which already handles `scf.if`, `scf.for` and
// `scf.while`. A TypeConverter converts by TYPE, and every JavaScript value
// has the same type; the lattice is per VALUE.
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"
#include "ctcompile/CTNative/Transforms/Passes.h"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <cctype>
#include <cmath>
#include <limits>
#include <string>

namespace ctcompile::ctnative {

#define GEN_PASS_DEF_CTNATIVELOWERTOEMITC
#include "ctcompile/CTNative/Transforms/Passes.h.inc"

namespace {

namespace ec = mlir::emitc;

// --- representation -----------------------------------------------------------

enum class carrier {
    none,
    boolean,
    number
};

// What C++ type carries a value of this ctnative type, per the table above.
// `none` is "not representable here", and is the reason for a refusal.
carrier carrierOf(mlir::Type type) {
    if (type == nullptr) { return carrier::none; }
    if (llvm::isa<BoolType>(type)) { return carrier::boolean; }
    if (llvm::isa<NumType>(type)) { return carrier::number; }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        if (llvm::isa<BottomType, NumType>(opt.getElementType())) { return carrier::number; }
    }
    return carrier::none;
}

// Can this value's carrier be undefined? True for the two `opt` rows, whose
// NaN representation is exact only in arithmetic, comparison and truthiness.
bool mayBeUndefined(mlir::Type type) {
    return llvm::isa<OptType>(type);
}

mlir::Type carrierType(mlir::MLIRContext * c, carrier which) {
    return which == carrier::boolean ? mlir::Type(mlir::IntegerType::get(c, 1))
                                     : mlir::Type(mlir::Float64Type::get(c));
}

std::string printed(mlir::Type type) {
    std::string out;
    llvm::raw_string_ostream os{out};
    if (type == nullptr) {
        os << "<unvisited>";
    } else {
        os << type;
    }
    return out;
}

// --- the admission check --------------------------------------------------------

struct admission {
    mlir::DataFlowSolver & solver;
    std::string why;
    // The carrier every `return` in the function agrees on; `none` until the
    // first return is seen. A function with no return at all returns NaN -
    // undefined's carrier - which lower() picks when this stays `none`.
    carrier returns = carrier::none;

    [[nodiscard]] mlir::Type typeOf(mlir::Value v) const {
        const TypeLattice * lattice = solver.lookupState<TypeLattice>(v);
        return lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
    }

    bool refuse(std::string reason) {
        if (why.empty()) { why = std::move(reason); }
        return false;
    }

    // A value used as a NUMBER: num, or an opt row (NaN is exact here).
    bool numeric(mlir::Value v, llvm::StringRef where) {
        if (carrierOf(typeOf(v)) != carrier::number) {
            return refuse((where + " operand is " + printed(typeOf(v)) + ", not a number").str());
        }
        return true;
    }
    // A value used as a BOOLEAN.
    bool boolean(mlir::Value v, llvm::StringRef where) {
        if (carrierOf(typeOf(v)) != carrier::boolean) {
            return refuse((where + " operand is " + printed(typeOf(v)) + ", not a boolean").str());
        }
        return true;
    }
    // A value that must not be undefined: equality is observable.
    bool defined(mlir::Value v, llvm::StringRef where) {
        if (mayBeUndefined(typeOf(v))) {
            return refuse((where + " on a value that may be undefined - NaN would not "
                                   "compare the way undefined does")
                              .str());
        }
        return true;
    }

    // A FUNCTION DECLARATION IS A BINDING, NOT A VALUE. `function f() {}` at
    // any level is `create_closure` whose only use is `store_global "f"`; in
    // the closed world the function exists as an emitc.func and calls of it
    // are direct, so the pair lowers to nothing. A closure used as a VALUE -
    // passed, returned, stored anywhere else - is not native yet (Phase 59).
    static bool isDeclarationClosure(mlir::Operation * o) {
        auto closure = llvm::dyn_cast_or_null<ctjs::CreateClosureOp>(o);
        if (!closure || !closure.getResult().hasOneUse()) { return false; }
        return llvm::isa<ctjs::StoreGlobalOp>(*closure.getResult().getUsers().begin());
    }
    static bool isDeclarationStore(mlir::Operation * o) {
        auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(o);
        return store && isDeclarationClosure(store.getValue().getDefiningOp());
    }

    // THE CALLEE VALUE OF A DIRECT CALL lowers to nothing: the call names its
    // function by symbol, and the boxed closure the interpreter would have
    // called through is only carried so the boxed tier can still lower the
    // same op. A load_global whose every use is that operand is exempt from
    // the carrier check for exactly that reason.
    static bool feedsOnlyDirectCallees(mlir::Value v) {
        if (v.use_empty()) { return false; }
        for (mlir::OpOperand & use : v.getUses()) {
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            if (!call || use.getOperandNumber() != 2) { return false; }
        }
        return true;
    }

    bool op(mlir::Operation * o) {
        using namespace ctjs;
        if (llvm::isa<FrameEnterOp, FrameExitOp, RootOp>(o)) { return true; }
        if (auto load = llvm::dyn_cast<LoadGlobalOp>(o);
            load && feedsOnlyDirectCallees(load.getResult())) {
            return true;
        }
        if (auto call = llvm::dyn_cast<CallDirectOp>(o)) {
            // receiver, new.target and the callee value are dropped; every
            // argument is a number. Whether the CALLEE is native is the
            // fixpoint in runOnOperation, not a question for one function.
            const auto operands = call.getArgOperands();
            for (unsigned i = 3; i < operands.size(); ++i) {
                if (!numeric(operands[i], "argument")) { return false; }
            }
            return true;
        }
        if (isDeclarationClosure(o) || isDeclarationStore(o)) { return true; }
        // THE LIFT'S UNDEFINED VALUE for a block argument no predecessor sets:
        // never read on any executed path, and carried as NaN - the double
        // that is also undefined's carrier - so it needs no proof.
        if (o->getName().getStringRef() == "ub.poison") { return true; }
        // THE STRUCTURING PASS'S MULTIPLEXERS: --ctjs-lift-to-scf encodes which
        // edge a merged block came from as i32 flags, in arith. They carry no
        // JavaScript value and --convert-arith-to-emitc lowers them.
        if (o->getDialect() != nullptr && o->getDialect()->getNamespace() == "arith") {
            for (mlir::Value v : o->getOperands()) {
                if (llvm::isa<ctjs::ValueType>(v.getType())) {
                    return refuse("an arith op on a JavaScript value");
                }
            }
            return true;
        }
        if (auto k = llvm::dyn_cast<ConstantOp>(o)) {
            if (llvm::isa<NumberAttr, BooleanAttr, UndefinedAttr>(k.getValue())) { return true; }
            return refuse("a constant that is not a number, a boolean or undefined");
        }
        if (auto b = llvm::dyn_cast<BinaryOp>(o)) {
            switch (b.getKind()) {
            case BinaryKind::Add:
            case BinaryKind::Sub:
            case BinaryKind::Mul:
            case BinaryKind::Div:
            case BinaryKind::Mod:
            case BinaryKind::Pow:
                return numeric(b.getLhs(), "binary") && numeric(b.getRhs(), "binary");
            default: return refuse("a bitwise or string operator is not native yet");
            }
        }
        if (auto b = llvm::dyn_cast<BinaryStaticOp>(o)) {
            if (b.getKind() != BinaryKind::Add) {
                return refuse("a static bitwise operator is not native yet");
            }
            return numeric(b.getLhs(), "++") && numeric(b.getRhs(), "++");
        }
        if (auto u = llvm::dyn_cast<UnaryOp>(o)) {
            switch (u.getKind()) {
            case UnaryKind::Neg:
            case UnaryKind::Plus: return numeric(u.getOperand(), "unary");
            case UnaryKind::Not:
                // `!x` is ToBoolean then negation, on ANY carrier: a number's
                // truthiness is exact under the NaN representation (undefined
                // and NaN are both falsy), so `!` on a number is admitted too.
                if (carrierOf(typeOf(u.getOperand())) == carrier::none) {
                    return refuse("! of " + printed(typeOf(u.getOperand())));
                }
                return true;
            default: return refuse("typeof, void and ~ are not native yet");
            }
        }
        if (auto cmp = llvm::dyn_cast<CompareOp>(o)) {
            switch (cmp.getKind()) {
            case CompareKind::Lt:
            case CompareKind::Le:
            case CompareKind::Gt:
            case CompareKind::Ge:
                return numeric(cmp.getLhs(), "compare") && numeric(cmp.getRhs(), "compare");
            case CompareKind::Eq:
            case CompareKind::StrictEq:
                return numeric(cmp.getLhs(), "equality") && numeric(cmp.getRhs(), "equality") &&
                       defined(cmp.getLhs(), "equality") && defined(cmp.getRhs(), "equality");
            }
            return refuse("an unknown comparison");
        }
        if (auto t = llvm::dyn_cast<TruthyOp>(o)) {
            const carrier c = carrierOf(typeOf(t.getValue()));
            if (c == carrier::none) {
                return refuse("truthiness of " + printed(typeOf(t.getValue())));
            }
            return true;
        }
        if (auto load = llvm::dyn_cast<LoadGlobalOp>(o)) {
            if (carrierOf(typeOf(load.getResult())) != carrier::number) {
                return refuse(("global `" + load.getName() + "` is " +
                               printed(typeOf(load.getResult())) + ", not a number")
                                  .str());
            }
            return true;
        }
        if (auto store = llvm::dyn_cast<StoreGlobalOp>(o)) {
            return numeric(store.getValue(), ("store to global `" + store.getName() + "`").str());
        }
        if (auto ret = llvm::dyn_cast<ReturnOp>(o)) {
            const carrier c = carrierOf(typeOf(ret.getValue()));
            if (c == carrier::none) { return refuse("returns " + printed(typeOf(ret.getValue()))); }
            if (returns == carrier::none) { returns = c; }
            if (returns != c) {
                return refuse("returns a number on one path and a boolean on another");
            }
            return true;
        }
        if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::ForOp, mlir::scf::ConditionOp,
                      mlir::scf::YieldOp>(o)) {
            return true;
        }
        if (llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(o)) {
            return refuse("unstructured control flow - run --ctjs-lift-to-scf first");
        }
        return refuse(("`" + o->getName().getStringRef() + "` is not native yet").str());
    }

    bool function(ctjs::FuncOp fn) {
        mlir::Block & entry = fn.getBody().front();
        // THE THREE IMPLICIT ARGUMENTS - receiver, new.target, callee - have no
        // native carrier and must be unused.
        for (unsigned i = 0; i < 3 && i < entry.getNumArguments(); ++i) {
            for (mlir::Operation * user : entry.getArgument(i).getUsers()) {
                if (isDeclarationClosure(user)) { continue; } // lowers to nothing
                return refuse(i == 0   ? "uses `this`"
                              : i == 1 ? "uses new.target"
                                       : "uses its own closure");
            }
        }
        for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
            const mlir::Type t = typeOf(entry.getArgument(i));
            if (carrierOf(t) == carrier::none) {
                return refuse("parameter " + std::to_string(i - 3) + " is " + printed(t) +
                              " - no caller proves it (a closed-world call is Phase 62½-A)");
            }
        }
        bool ok = true;
        fn.getBody().walk([&](mlir::Operation * o) {
            if (!ok) { return; }
            if (o == fn.getOperation()) { return; }
            ok = op(o);
            if (!ok) { return; }
            // A declaration closure's result is dropped with its store, and
            // so is a load_global that only names a direct call's callee;
            // neither has a carrier and neither needs one.
            if (isDeclarationClosure(o)) { return; }
            if (o->getName().getStringRef() == "ub.poison") { return; }
            if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(o);
                load && feedsOnlyDirectCallees(load.getResult())) {
                return;
            }
            // EVERY JAVASCRIPT VALUE THIS OPERATION DEFINES OR CARRIES has a
            // carrier - including scf results and region arguments.
            for (mlir::Value r : o->getResults()) {
                if (llvm::isa<ctjs::ValueType>(r.getType()) &&
                    carrierOf(typeOf(r)) == carrier::none) {
                    ok = refuse(("a value of type " + printed(typeOf(r)) + " from `" +
                                 o->getName().getStringRef() + "`")
                                    .str());
                    return;
                }
            }
            for (mlir::Region & region : o->getRegions()) {
                for (mlir::Block & block : region) {
                    for (mlir::BlockArgument a : block.getArguments()) {
                        if (llvm::isa<ctjs::ValueType>(a.getType()) &&
                            carrierOf(typeOf(a)) == carrier::none) {
                            ok = refuse("a loop-carried value of type " + printed(typeOf(a)));
                            return;
                        }
                    }
                }
            }
        });
        return ok;
    }
};

// --- the lowering ---------------------------------------------------------------

// The C identifier a lowered function gets: the importer's `name$index` with
// the `$` made a `_`, which keeps the index (two nested `helper`s stay apart)
// and keeps clear of <cmath>'s `nan`, `remainder` and friends.
std::string cIdentifier(llvm::StringRef symbol) {
    std::string name = symbol.str();
    for (char & ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) { ch = '_'; }
    }
    return name;
}

struct lowering {
    mlir::DataFlowSolver & solver;
    mlir::MLIRContext * context;
    mlir::ModuleOp module;
    llvm::StringSet<> globals; // numeric globals the emitted unit declares
    // ctjs symbol -> emitc symbol, decided for EVERY accepted function before
    // any is lowered, so a call lowered before its callee already names the
    // callee's new symbol and no symbol use is ever rewritten in place. A
    // rewrite would also reach a call_direct in a REFUSED caller, which must
    // keep naming a ctjs.func to verify.
    llvm::StringMap<std::string> names;
    // The hollowed ctjs.funcs, erased together in finish().
    llvm::SmallVector<ctjs::FuncOp> shells;

    void finish() {
        // PROTOTYPES FIRST. main is the importer's function 0 and is emitted
        // first, and C++ needs a declaration before a use; one
        // emitc.declare_func per lowered function, at the top of the module
        // after the includes, is what the emitter prints as a prototype.
        {
            mlir::OpBuilder b(context);
            b.setInsertionPointToStart(module.getBody());
            llvm::SmallVector<ec::FuncOp> lowered;
            module.walk([&](ec::FuncOp f) {
                if (f.getSymName() != "main") { lowered.push_back(f); }
            });
            // After the includes, which declareGlobals put first.
            for (mlir::Operation & op : module.getBody()->getOperations()) {
                if (!llvm::isa<ec::IncludeOp>(op)) {
                    b.setInsertionPoint(&op);
                    break;
                }
            }
            for (ec::FuncOp f : lowered) {
                ec::DeclareFuncOp::create(b, f.getLoc(),
                                          mlir::FlatSymbolRefAttr::get(context, f.getSymName()));
            }
        }
        for (ctjs::FuncOp fn : shells) {
            if (!mlir::SymbolTable::symbolKnownUseEmpty(fn.getOperation(), module)) {
                llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") + fn.getSymName() +
                                         "` still has symbol uses after every accepted function "
                                         "was lowered - the call-graph closure should have "
                                         "refused its caller");
            }
            fn.erase();
        }
        shells.clear();
    }

    [[nodiscard]] mlir::Type typeOf(mlir::Value v) const {
        const TypeLattice * lattice = solver.lookupState<TypeLattice>(v);
        return lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
    }

    mlir::Value f64Constant(mlir::OpBuilder & b, mlir::Location where, double d) {
        return ec::ConstantOp::create(b, where, mlir::Float64Type::get(context),
                                      b.getF64FloatAttr(d));
    }
    mlir::Value boolConstant(mlir::OpBuilder & b, mlir::Location where, bool v) {
        return ec::ConstantOp::create(
            b, where, mlir::IntegerType::get(context, 1),
            b.getIntegerAttr(mlir::IntegerType::get(context, 1), v ? 1 : 0));
    }
    mlir::Value lvalueOfGlobal(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef name) {
        globals.insert(name);
        return ec::GetGlobalOp::create(b, where,
                                       ec::LValueType::get(mlir::Float64Type::get(context)),
                                       mlir::FlatSymbolRefAttr::get(context, ("g_" + name).str()));
    }
    // A number's truthiness, exactly: not zero AND not NaN. `x == x` is the
    // NaN test, and NaN carries undefined too, which is also falsy.
    mlir::Value truthyNumber(mlir::OpBuilder & b, mlir::Location where, mlir::Value x) {
        const auto i1 = mlir::IntegerType::get(context, 1);
        mlir::Value nonzero =
            ec::CmpOp::create(b, where, i1, ec::CmpPredicate::ne, x, f64Constant(b, where, 0.0));
        mlir::Value notNaN = ec::CmpOp::create(b, where, i1, ec::CmpPredicate::eq, x, x);
        return ec::LogicalAndOp::create(b, where, i1, nonzero, notNaN);
    }
    mlir::Value libmCall(mlir::OpBuilder & b, mlir::Location where, llvm::StringRef fn,
                         mlir::ValueRange args) {
        return ec::CallOpaqueOp::create(b, where, mlir::TypeRange{mlir::Float64Type::get(context)},
                                        b.getStringAttr(fn), args)
            .getResult(0);
    }

    // Retype every JavaScript value in the function from the lattice. Done
    // BEFORE any operation is replaced, so the replacements see carriers.
    void retype(ctjs::FuncOp fn) {
        const auto retypeValue = [&](mlir::Value v) {
            if (!llvm::isa<ctjs::ValueType>(v.getType())) { return; }
            v.setType(carrierType(context, carrierOf(typeOf(v))));
        };
        for (mlir::Block & block : fn.getBody()) {
            for (mlir::BlockArgument a : block.getArguments()) { retypeValue(a); }
        }
        fn.getBody().walk([&](mlir::Operation * o) {
            for (mlir::Value r : o->getResults()) { retypeValue(r); }
            for (mlir::Region & region : o->getRegions()) {
                for (mlir::Block & block : region) {
                    for (mlir::BlockArgument a : block.getArguments()) { retypeValue(a); }
                }
            }
        });
    }

    // THE ONLY ERASE. An operation with uses is never erased: in a release
    // build that is a use-after-free with no diagnostic - it surfaced as a
    // crash at context teardown that came and went with the heap layout.
    static void eraseIfUnused(mlir::Operation * o) {
        if (!o->use_empty()) {
            llvm::report_fatal_error(llvm::Twine("ctnative lowering: erasing `") +
                                     o->getName().getStringRef() + "` while it still has uses");
        }
        o->erase();
    }

    void replace(mlir::Operation * o, bool isEntry, mlir::Type returnType) {
        using namespace ctjs;
        mlir::OpBuilder b(o);
        const mlir::Location where = o->getLoc();
        const auto f64 = mlir::Float64Type::get(context);
        const auto i1 = mlir::IntegerType::get(context, 1);
        const auto swap = [&](mlir::Value with) {
            o->getResult(0).replaceAllUsesWith(with);
            eraseIfUnused(o);
        };

        // FRAME BOOKKEEPING LOWERS TO NOTHING - but frame_enter's result is
        // used by every frame_exit and root after it, and walk order visits
        // it first, so its users go now and it goes in the sweep at the end.
        if (llvm::isa<FrameExitOp, RootOp>(o)) {
            eraseIfUnused(o);
            return;
        }
        if (llvm::isa<FrameEnterOp>(o)) { return; }
        if (o->getName().getStringRef() == "ub.poison") {
            swap(f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()));
            return;
        }
        if (admission::isDeclarationStore(o)) {
            mlir::Operation * closure = llvm::cast<StoreGlobalOp>(o).getValue().getDefiningOp();
            eraseIfUnused(o);
            eraseIfUnused(closure);
            return;
        }
        if (auto k = llvm::dyn_cast<ConstantOp>(o)) {
            if (auto n = llvm::dyn_cast<NumberAttr>(k.getValue())) {
                swap(f64Constant(b, where, n.getDouble()));
            } else if (auto bo = llvm::dyn_cast<BooleanAttr>(k.getValue())) {
                swap(boolConstant(b, where, bo.getValue()));
            } else {
                // undefined, as the NaN the representation table says it is.
                swap(f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()));
            }
            return;
        }
        if (auto bin = llvm::dyn_cast<BinaryOp>(o)) {
            const mlir::Value l = bin.getLhs(), r = bin.getRhs();
            switch (bin.getKind()) {
            case BinaryKind::Add: swap(ec::AddOp::create(b, where, f64, l, r)); return;
            case BinaryKind::Sub: swap(ec::SubOp::create(b, where, f64, l, r)); return;
            case BinaryKind::Mul: swap(ec::MulOp::create(b, where, f64, l, r)); return;
            case BinaryKind::Div: swap(ec::DivOp::create(b, where, f64, l, r)); return;
            case BinaryKind::Mod: swap(libmCall(b, where, "std::fmod", {l, r})); return;
            case BinaryKind::Pow: swap(libmCall(b, where, "std::pow", {l, r})); return;
            default: llvm_unreachable("admission refused it");
            }
        }
        if (auto bin = llvm::dyn_cast<BinaryStaticOp>(o)) {
            swap(ec::AddOp::create(b, where, f64, bin.getLhs(), bin.getRhs()));
            return;
        }
        if (auto u = llvm::dyn_cast<UnaryOp>(o)) {
            switch (u.getKind()) {
            case UnaryKind::Neg:
                swap(ec::UnaryMinusOp::create(b, where, f64, u.getOperand()));
                return;
            case UnaryKind::Plus: swap(u.getOperand()); return;
            case UnaryKind::Not: {
                mlir::Value v = u.getOperand();
                if (!llvm::isa<mlir::IntegerType>(v.getType())) { v = truthyNumber(b, where, v); }
                swap(ec::LogicalNotOp::create(b, where, i1, v));
                return;
            }
            default: llvm_unreachable("admission refused it");
            }
        }
        if (auto cmp = llvm::dyn_cast<CompareOp>(o)) {
            ec::CmpPredicate p = ec::CmpPredicate::eq;
            switch (cmp.getKind()) {
            case CompareKind::Lt: p = ec::CmpPredicate::lt; break;
            case CompareKind::Le: p = ec::CmpPredicate::le; break;
            case CompareKind::Gt: p = ec::CmpPredicate::gt; break;
            case CompareKind::Ge: p = ec::CmpPredicate::ge; break;
            case CompareKind::Eq:
            case CompareKind::StrictEq: p = ec::CmpPredicate::eq; break;
            }
            swap(ec::CmpOp::create(b, where, i1, p, cmp.getLhs(), cmp.getRhs()));
            return;
        }
        if (auto t = llvm::dyn_cast<TruthyOp>(o)) {
            mlir::Value v = t.getValue();
            swap(llvm::isa<mlir::IntegerType>(v.getType()) ? v : truthyNumber(b, where, v));
            return;
        }
        if (auto load = llvm::dyn_cast<LoadGlobalOp>(o)) {
            if (admission::feedsOnlyDirectCallees(load.getResult())) {
                // Every use is a call_direct's callee-value operand, and the
                // call is rewritten below without it; by the sweep it is dead.
                return;
            }
            swap(ec::LoadOp::create(b, where, f64, lvalueOfGlobal(b, where, load.getName())));
            return;
        }
        if (auto call = llvm::dyn_cast<CallDirectOp>(o)) {
            // The three implicit operands go; the rest are the parameters,
            // in the callee's own order. The callee symbol still names the
            // ctjs.func here; SymbolTable::replaceAllSymbolUses renames it
            // to the emitc.func when that function is lowered, whichever
            // order the two are visited in.
            const auto operands = call.getArgOperands();
            llvm::SmallVector<mlir::Value> args(operands.begin() + 3, operands.end());
            const auto named = names.find(call.getCallee());
            const std::string target =
                named == names.end() ? cIdentifier(call.getCallee()) : named->second;
            auto made = ec::CallOp::create(b, where, mlir::SymbolRefAttr::get(context, target),
                                           mlir::TypeRange{o->getResult(0).getType()}, args);
            swap(made.getResult(0));
            return;
        }
        if (auto store = llvm::dyn_cast<StoreGlobalOp>(o)) {
            ec::AssignOp::create(b, where, lvalueOfGlobal(b, where, store.getName()),
                                 store.getValue());
            eraseIfUnused(o);
            return;
        }
        if (auto ret = llvm::dyn_cast<ReturnOp>(o)) {
            if (isEntry) {
                // main: print the globals, return 0. The convention the gate
                // reads: `name=%.17g`, one per line, sorted by name.
                llvm::SmallVector<llvm::StringRef> names(globals.keys().begin(),
                                                         globals.keys().end());
                llvm::sort(names);
                for (llvm::StringRef name : names) {
                    mlir::Value current =
                        ec::LoadOp::create(b, where, f64, lvalueOfGlobal(b, where, name));
                    mlir::Value format = ec::LiteralOp::create(
                        b, where, ec::PointerType::get(ec::OpaqueType::get(context, "const char")),
                        b.getStringAttr(("\"" + name + "=%.17g\\n\"").str()));
                    ec::CallOpaqueOp::create(b, where, mlir::TypeRange{}, b.getStringAttr("printf"),
                                             mlir::ValueRange{format, current});
                }
                mlir::Value zero = ec::ConstantOp::create(
                    b, where, mlir::IntegerType::get(context, 32), b.getI32IntegerAttr(0));
                ec::ReturnOp::create(b, where, zero);
            } else {
                (void)returnType;
                ec::ReturnOp::create(b, where, ret.getValue());
            }
            eraseIfUnused(o);
            return;
        }
        // scf ops stay for --convert-scf-to-emitc.
    }

    void lower(ctjs::FuncOp fn) {
        const bool isEntry = fn.getSymName().starts_with("_script_$");
        mlir::Block & entry = fn.getBody().front();
        retype(fn);

        // The signature: the parameters after the three implicit arguments,
        // returning double (every native function returns a number; a
        // function that returns nothing returns NaN, which is undefined's
        // carrier).
        llvm::SmallVector<mlir::Type> params;
        for (unsigned i = 3; i < entry.getNumArguments(); ++i) {
            params.push_back(entry.getArgument(i).getType());
        }
        const mlir::Type f64 = mlir::Float64Type::get(context);
        const mlir::Type i32 = mlir::IntegerType::get(context, 32);
        // THE RETURN TYPE IS WHAT THE RETURNS CARRY - retyped already, so any
        // ctjs.return's operand type is the answer; a function that never
        // returns a value returns undefined, carried as a NaN double.
        mlir::Type returnType = isEntry ? i32 : f64;
        if (!isEntry) {
            fn.getBody().walk([&](ctjs::ReturnOp ret) { returnType = ret.getValue().getType(); });
        }
        if (isEntry) { params.clear(); }

        mlir::OpBuilder b(fn);
        const auto named = names.find(fn.getSymName());
        const std::string symbol = isEntry                ? std::string{"main"}
                                   : named != names.end() ? named->second
                                                          : cIdentifier(fn.getSymName());
        auto made =
            ec::FuncOp::create(b, fn.getLoc(), symbol, b.getFunctionType(params, {returnType}));
        made.getBody().takeBody(fn.getBody());
        mlir::Block & body = made.getBody().front();

        llvm::SmallVector<mlir::Operation *> ops;
        made.getBody().walk([&](mlir::Operation * o) {
            if (o->getDialect() == fn->getDialect() || o->getName().getStringRef() == "ub.poison") {
                ops.push_back(o);
            }
        });
        for (mlir::Operation * o : ops) { replace(o, isEntry, returnType); }

        // SWEEP THE CONSTANTS THE REWRITE ORPHANED - the NaN for an `undefined`
        // that main no longer returns, a literal folded into nothing. The
        // canonicalizer will not: emitc.constant carries no memory-effect
        // interface, so dead-code elimination keeps it, and the C++ it prints
        // is an unused variable that -Werror rejects. Reverse order, so a
        // constant whose only user was another dead constant goes too.
        llvm::SmallVector<mlir::Operation *> dead;
        made.getBody().walk([&](mlir::Operation * o) {
            if (llvm::isa<ec::ConstantOp, ec::LiteralOp, ctjs::FrameEnterOp, ctjs::LoadGlobalOp>(
                    o)) {
                dead.push_back(o);
            }
        });
        for (mlir::Operation * o : llvm::reverse(dead)) {
            if (o->use_empty()) { eraseIfUnused(o); }
        }

        // THE IMPLICIT ARGUMENTS GO LAST, once the declaration closures that
        // named `callee` and `this` have been erased with their stores - not
        // before, as they once did: erasing a block argument that still has
        // uses is the same silent use-after-free as erasing an operation
        // with uses, and it surfaced as a crash three passes later in a fold
        // of an operation that did not exist. Same invariant, same fatal.
        const unsigned drop = isEntry ? body.getNumArguments() : 3u;
        for (unsigned i = 0; i < drop; ++i) {
            if (!body.getArgument(0).use_empty()) {
                llvm::report_fatal_error(
                    llvm::Twine("ctnative lowering: implicit argument of `") + fn.getSymName() +
                    "` still has uses after lowering - admission should have refused it");
            }
            body.eraseArgument(0);
        }

        // THE OLD ctjs.func STAYS FOR NOW, hollow: an accepted caller lowered
        // after this one still holds a call_direct naming it, and erasing it
        // here left `is_between` with a live symbol use once - the invariant
        // in finish() caught it. finish() runs after every accepted function
        // has been rewritten, when no call_direct names any of them.
        shells.push_back(fn);
    }

    void declareGlobals() {
        mlir::OpBuilder b(context);
        b.setInsertionPointToStart(module.getBody());
        // INCLUDES FIRST: the builder advances past each op it creates, so
        // creation order is file order, and a global initialised to NAN
        // needs <cmath> above it.
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("cmath"), b.getUnitAttr());
        ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("cstdio"), b.getUnitAttr());
        llvm::SmallVector<llvm::StringRef> names(globals.keys().begin(), globals.keys().end());
        llvm::sort(names);
        for (llvm::StringRef name : names) {
            // Undefined until the first store: NaN.
            ec::GlobalOp::create(b, module.getLoc(), ("g_" + name).str(),
                                 mlir::Float64Type::get(context),
                                 b.getF64FloatAttr(std::numeric_limits<double>::quiet_NaN()),
                                 /*extern_specifier=*/false, /*static_specifier=*/true,
                                 /*const_specifier=*/false);
        }
    }
};

struct CTNativeLowerToEmitCPass : impl::CTNativeLowerToEmitCBase<CTNativeLowerToEmitCPass> {
    using CTNativeLowerToEmitCBase::CTNativeLowerToEmitCBase;

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();

        // All three, and none optional - TypeInference.h says why.
        mlir::DataFlowSolver solver;
        solver.load<mlir::dataflow::DeadCodeAnalysis>();
        solver.load<mlir::dataflow::SparseConstantPropagation>();
        solver.load<TypeInference>();
        if (failed(solver.initializeAndRun(module))) {
            module.emitError("the type inference did not converge");
            return signalPassFailure();
        }

        llvm::SmallVector<ctjs::FuncOp> functions;
        module.walk([&](ctjs::FuncOp fn) { functions.push_back(fn); });

        llvm::SmallVector<ctjs::FuncOp> accepted;
        for (ctjs::FuncOp fn : functions) {
            if (fn->hasAttr("ctjs.not_structured")) {
                fn->setAttr("ctnative.not_native",
                            mlir::StringAttr::get(&getContext(), "unstructured control flow"));
                continue;
            }
            admission check{solver, {}};
            if (check.function(fn)) {
                accepted.push_back(fn);
            } else {
                fn->setAttr("ctnative.not_native", mlir::StringAttr::get(&getContext(), check.why));
            }
        }

        // THE FIXPOINT: a function is native only if every function it calls
        // directly is. Drop any accepted function that calls a refused one,
        // name the callee in its diagnostic, and repeat until nothing moves -
        // a refusal anywhere in a call chain reaches every caller.
        llvm::DenseSet<mlir::Operation *> nativeSet;
        for (ctjs::FuncOp fn : accepted) { nativeSet.insert(fn.getOperation()); }
        mlir::SymbolTable symbols(module);
        // CLOSED IN BOTH DIRECTIONS. A native caller needs a native callee to
        // emit an emitc.call to; and a native CALLEE needs every caller native
        // too, because a refused caller keeps a ctjs.call_direct that must
        // name a ctjs.func with a body - which a lowered function no longer
        // is. So a refusal propagates along the call graph both ways, each
        // step naming the function that caused it, until nothing moves.
        for (bool changed = true; changed;) {
            changed = false;
            for (ctjs::FuncOp fn : functions) {
                const bool native = nativeSet.contains(fn.getOperation());
                fn.getBody().walk([&](ctjs::CallDirectOp call) {
                    mlir::Operation * callee = symbols.lookup(call.getCallee());
                    const bool calleeNative = callee != nullptr && nativeSet.contains(callee);
                    if (native && !calleeNative) {
                        nativeSet.erase(fn.getOperation());
                        fn->setAttr(
                            "ctnative.not_native",
                            mlir::StringAttr::get(
                                &getContext(),
                                ("calls `" + call.getCallee() + "`, which is not native").str()));
                        changed = true;
                    } else if (!native && calleeNative) {
                        nativeSet.erase(callee);
                        callee->setAttr(
                            "ctnative.not_native",
                            mlir::StringAttr::get(&getContext(), ("called by `" + fn.getSymName() +
                                                                  "`, which is not native")
                                                                     .str()));
                        changed = true;
                    }
                });
            }
        }
        llvm::erase_if(accepted,
                       [&](ctjs::FuncOp fn) { return !nativeSet.contains(fn.getOperation()); });

        lowering lower{solver, &getContext(), module, {}, {}, {}};
        for (ctjs::FuncOp fn : accepted) {
            lower.names[fn.getSymName()] =
                fn.getSymName().starts_with("_script_$") ? "main" : cIdentifier(fn.getSymName());
        }
        for (ctjs::FuncOp fn : accepted) { lower.lower(fn); }
        if (!accepted.empty()) { lower.declareGlobals(); }
        lower.finish();
    }
};

} // namespace

} // namespace ctcompile::ctnative
