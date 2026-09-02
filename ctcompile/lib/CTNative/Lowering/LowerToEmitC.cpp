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
    number,
    structure
};

// What C++ type carries a value of this ctnative type, per the table above.
// `none` is "not representable here", and is the reason for a refusal.
carrier carrierOf(mlir::Type type) {
    if (type == nullptr) { return carrier::none; }
    if (llvm::isa<BoolType>(type)) { return carrier::boolean; }
    if (llvm::isa<NumType>(type)) { return carrier::number; }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        if (llvm::isa<BottomType, NumType>(opt.getElementType())) { return carrier::number; }
        // A boolean-or-undefined, as a bool whose undefined is false: exact
        // in a branch, under `!` and as truthiness (both are falsy), refused
        // where the difference shows (equality) - the number rows' shape.
        if (llvm::isa<BoolType>(opt.getElementType())) { return carrier::boolean; }
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

    // PHASE 56: A CLOSED SHAPE IS A STRUCT BY VALUE. TypeInference::hasClosedShape
    // is the proof - every use is a get or set through a constant key, so the
    // object never reaches anything that could add or remove a field, and never
    // leaves the frame (a return, a store, a call would all be uses that open
    // it). Each key must be a C identifier, each field a number or a boolean.
    static bool isClosedObject(mlir::Value v) { return TypeInference::hasClosedShape(v); }
    static llvm::StringRef keyOf(mlir::Value key) {
        auto constant = key.getDefiningOp<ctjs::ConstantOp>();
        if (!constant) { return {}; }
        auto str = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
        return str ? str.getValue() : llvm::StringRef{};
    }
    static bool isCIdentifier(llvm::StringRef key) {
        if (key.empty() || std::isdigit(static_cast<unsigned char>(key.front()))) { return false; }
        return llvm::all_of(
            key, [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; });
    }
    // A KEY THAT IS A C IDENTIFIER AND STILL CANNOT BE A FIELD NAME. Field
    // names are emitted verbatim - `cIdentifier()` sanitises symbols, not
    // members - so `o.class = 3` emitted `double class;` and `o.NAN = 5`
    // emitted `double NAN;` under this file's own `#include <cmath>`. Both
    // were admitted with no refusal and both are hard -Werror build failures
    // on a program the tier had declared native. Refused rather than mangled:
    // a generated field keeps the JavaScript name a reader is looking for, and
    // mangling every field to buy this rare case is a trade Phase 56C should
    // make deliberately, not this fix.
    static bool isReservedInCpp(llvm::StringRef key) {
        static constexpr llvm::StringLiteral kReserved[] = {
            // keywords a member may not be named
            "alignas",
            "alignof",
            "and",
            "and_eq",
            "asm",
            "auto",
            "bitand",
            "bitor",
            "bool",
            "break",
            "case",
            "catch",
            "char",
            "char8_t",
            "char16_t",
            "char32_t",
            "class",
            "compl",
            "concept",
            "const",
            "consteval",
            "constexpr",
            "constinit",
            "const_cast",
            "continue",
            "co_await",
            "co_return",
            "co_yield",
            "decltype",
            "default",
            "delete",
            "do",
            "double",
            "dynamic_cast",
            "else",
            "enum",
            "explicit",
            "export",
            "extern",
            "false",
            "float",
            "for",
            "friend",
            "goto",
            "if",
            "inline",
            "int",
            "long",
            "mutable",
            "namespace",
            "new",
            "noexcept",
            "not",
            "not_eq",
            "nullptr",
            "operator",
            "or",
            "or_eq",
            "private",
            "protected",
            "public",
            "register",
            "reinterpret_cast",
            "requires",
            "return",
            "short",
            "signed",
            "sizeof",
            "static",
            "static_assert",
            "static_cast",
            "struct",
            "switch",
            "template",
            "this",
            "thread_local",
            "throw",
            "true",
            "try",
            "typedef",
            "typeid",
            "typename",
            "union",
            "unsigned",
            "using",
            "virtual",
            "void",
            "volatile",
            "wchar_t",
            "while",
            "xor",
            "xor_eq",
            // macros the two headers this file emits are allowed to define
            "NAN",
            "INFINITY",
            "HUGE_VAL",
            "HUGE_VALF",
            "HUGE_VALL",
            "EOF",
            "NULL",
            "BUFSIZ",
            "FILENAME_MAX",
            "FOPEN_MAX",
            "L_tmpnam",
            "TMP_MAX",
            "SEEK_SET",
            "SEEK_CUR",
            "SEEK_END",
            "stdin",
            "stdout",
            "stderr",
            "errno",
            "MATH_ERRNO",
            "MATH_ERREXCEPT",
            "FP_FAST_FMA",
            "FP_INFINITE",
            "FP_NAN",
            "FP_NORMAL",
            "FP_SUBNORMAL",
            "FP_ZERO",
            "FP_ILOGB0",
            "FP_ILOGBNAN",
        };
        return llvm::is_contained(kReserved, key);
    }
    // NAMES OBJECT.PROTOTYPE ANSWERS FOR. A field that is only ever READ is
    // `undefined` for a plain key, which this tier carries as NaN - but these
    // names are not undefined: the literal's prototype answers them, and the
    // interpreter finds a function where the generated struct finds NaN. So
    // `if (o.constructor)` took the else branch natively and the then branch
    // in the interpreter, with no refusal anywhere. A key that IS stored
    // shadows the inherited one and is fine; only a read-only key is refused.
    static bool namesObjectPrototypeMember(llvm::StringRef key) {
        static constexpr llvm::StringLiteral kInherited[] = {
            "constructor",      "hasOwnProperty",   "isPrototypeOf",    "propertyIsEnumerable",
            "toLocaleString",   "toString",         "valueOf",          "__proto__",
            "__defineGetter__", "__defineSetter__", "__lookupGetter__", "__lookupSetter__",
        };
        return llvm::is_contained(kInherited, key);
    }
    // A string constant whose every use is a property key of a closed object
    // lowers to nothing: the key becomes a member name.
    static bool isKeyOnlyString(mlir::Operation * o) {
        auto constant = llvm::dyn_cast<ctjs::ConstantOp>(o);
        if (!constant || !llvm::isa<ctjs::StringAttr>(constant.getValue()) ||
            constant.getResult().use_empty()) {
            return false;
        }
        for (mlir::OpOperand & use : constant.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            mlir::Value object;
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                object = get.getObject();
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                object = set.getObject();
            }
            if (!object || use.getOperandNumber() != 1 || !isClosedObject(object)) { return false; }
        }
        return true;
    }

    // WHY A LITERAL'S SHAPE IS OPEN: the first use that is not a get or a set
    // through a constant key on the literal itself, named by what it is. A
    // refusal that lists every route there is tells the reader nothing about
    // which one this program took; this one names it.
    //
    // THE LOOP-CARRIED ROW IS THE ONE OBLIGATION O-3 LEAVES. --ctjs-lift-to-scf
    // now replaces a loop header's argument for a variable assigned once
    // before the loop by the variable (a trivial phi), so a literal updated
    // inside a loop is one SSA value and one stack slot. What still reaches
    // the scf.while is a REAL phi: the variable is assigned again inside the
    // loop, or on only one path before it, and two literals - two shapes,
    // two slots - would have to become one value, which is a pointer.
    static std::string whyOpen(mlir::Value object) {
        for (mlir::OpOperand & use : object.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0) {
                    if (!keyOf(get.getKey()).empty()) { continue; }
                    return "an object literal reached through a dynamic key";
                }
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                if (use.getOperandNumber() == 0) {
                    if (!keyOf(set.getKey()).empty()) { continue; }
                    return "an object literal reached through a dynamic key";
                }
                if (use.getOperandNumber() == 2) {
                    return "an object literal that escapes - it is stored into another object";
                }
            }
            if (llvm::isa<mlir::scf::WhileOp, mlir::scf::YieldOp, mlir::scf::ConditionOp>(user)) {
                return "an object literal that is loop-carried - more than one value reaches "
                       "the variable that holds it (assigned again inside a loop, or on only "
                       "one path before it)";
            }
            if (llvm::isa<ctjs::ReturnOp>(user)) {
                return "an object literal that escapes - it is returned";
            }
            return ("an object literal that escapes - it reaches `" +
                    user->getName().getStringRef() + "`")
                .str();
        }
        return "an object literal whose shape is not closed";
    }

    // A VALUE THAT LOWERS TO NOTHING NEEDS NO CARRIER, and these are the only
    // ones: the three implicit arguments (erased once their declaration
    // closures are gone), a declaration closure's result, the lift's poison
    // (replaced by NaN), a key constant (a member name) and a load_global
    // that only names a direct call's callee. function() exempts exactly this
    // list from the carrier check; retype() asks the same question.
    static bool lowersToNothing(mlir::Value v) {
        if (auto arg = llvm::dyn_cast<mlir::BlockArgument>(v)) {
            return arg.getOwner()->isEntryBlock() &&
                   llvm::isa<ctjs::FuncOp>(arg.getOwner()->getParentOp()) && arg.getArgNumber() < 3;
        }
        mlir::Operation * o = v.getDefiningOp();
        if (isDeclarationClosure(o) || isKeyOnlyString(o)) { return true; }
        if (o->getName().getStringRef() == "ub.poison") { return true; }
        return llvm::isa<ctjs::LoadGlobalOp>(o) && feedsOnlyDirectCallees(v);
    }

    bool op(mlir::Operation * o) {
        using namespace ctjs;
        if (llvm::isa<FrameEnterOp, FrameExitOp, RootOp>(o)) { return true; }
        if (auto object = llvm::dyn_cast<CreateObjectOp>(o)) {
            if (!isClosedObject(object.getResult())) { return refuse(whyOpen(object.getResult())); }
            // The keys this literal is ever WRITTEN with. A read of one of
            // them is an own property; a read of anything else falls through
            // to the prototype, which is what makes an inherited name wrong.
            llvm::StringSet<> written;
            for (mlir::Operation * user : object.getResult().getUsers()) {
                if (auto set = llvm::dyn_cast<SetPropertyOp>(user)) {
                    written.insert(keyOf(set.getKey()));
                }
            }
            for (mlir::Operation * user : object.getResult().getUsers()) {
                const llvm::StringRef key = llvm::isa<GetPropertyOp>(user)
                                                ? keyOf(llvm::cast<GetPropertyOp>(user).getKey())
                                                : keyOf(llvm::cast<SetPropertyOp>(user).getKey());
                if (!isCIdentifier(key)) {
                    return refuse(("field `" + key + "` is not a C identifier").str());
                }
                if (isReservedInCpp(key)) {
                    return refuse(("field `" + key +
                                   "` is a C++ keyword or a macro of <cmath>/<cstdio>, so the "
                                   "generated struct would not compile")
                                      .str());
                }
                if (!written.contains(key) && namesObjectPrototypeMember(key)) {
                    return refuse(("field `" + key +
                                   "` is read but never written, and Object.prototype answers "
                                   "that name - the interpreter finds a function where this "
                                   "would find undefined")
                                      .str());
                }
                if (auto set = llvm::dyn_cast<SetPropertyOp>(user)) {
                    const carrier c = carrierOf(typeOf(set.getValue()));
                    if (c != carrier::number && c != carrier::boolean) {
                        return refuse(("field `" + key + "` is stored a " +
                                       printed(typeOf(set.getValue())) +
                                       ", not a number or a boolean")
                                          .str());
                    }
                }
            }
            return true;
        }
        if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) {
            if (!isClosedObject(get.getObject())) {
                return refuse("a property read on an object that is not a closed-shape literal");
            }
            return true; // its result's carrier is checked with every other value
        }
        if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
            if (!isClosedObject(set.getObject())) {
                return refuse("a property write on an object that is not a closed-shape literal");
            }
            return true; // the value's carrier was checked at the object
        }
        if (isKeyOnlyString(o)) { return true; }
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
            // (`**` is not std::pow; exponentiate() below is why.)
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
            if (llvm::isa<ctjs::CreateObjectOp>(o) || isKeyOnlyString(o)) { return; }
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
// PART 24 PHASE 63 STEP 7: a provenance comment above every generated
// definition, so a C++ diagnostic on generated code maps back to the
// JavaScript site. The importer fuses a NameLoc with the FileLineColLoc of
// the site; the first file location found, at any depth, is the site.
static std::string siteOf(mlir::Location loc) {
    std::string site;
    loc->walk([&](mlir::Location l) {
        if (auto file = llvm::dyn_cast<mlir::FileLineColLoc>(l)) {
            site = file.getFilename().str() + ":" + std::to_string(file.getLine()) + ":" +
                   std::to_string(file.getColumn());
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return site.empty() ? std::string{"<no source location>"} : site;
}
// The importer gives a ctjs.func no location of its own; its first located
// operation is the function's site.
static std::string siteOfFunction(ctjs::FuncOp fn) {
    std::string site = siteOf(fn.getLoc());
    if (site != "<no source location>") { return site; }
    fn.getBody().walk([&](mlir::Operation * o) {
        const std::string here = siteOf(o->getLoc());
        if (here == "<no source location>") { return mlir::WalkResult::advance(); }
        site = here;
        return mlir::WalkResult::interrupt();
    });
    return site;
}

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
    lowering(mlir::DataFlowSolver & s, mlir::MLIRContext * c, mlir::ModuleOp m)
        : solver(s), context(c), module(m) {}
    llvm::StringSet<> globals; // numeric globals the emitted unit declares
    // ctjs symbol -> emitc symbol, decided for EVERY accepted function before
    // any is lowered, so a call lowered before its callee already names the
    // callee's new symbol and no symbol use is ever rewritten in place. A
    // rewrite would also reach a call_direct in a REFUSED caller, which must
    // keep naming a ctjs.func to verify.
    llvm::StringMap<std::string> names;
    // The hollowed ctjs.funcs, erased together in finish().
    llvm::SmallVector<ctjs::FuncOp> shells;
    // ONE CLASS PER CLOSED-SHAPE SITE (Phase 56C's one-definition-per-shape is
    // the next step): the class name, and its fields in name order with their
    // carrier types, declared at the top of the module by finish().
    struct shape {
        std::string name;
        std::string site; // the object literal, for the provenance comment
        llvm::SmallVector<std::pair<std::string, mlir::Type>> fields;
    };
    llvm::SmallVector<shape> shapes;
    llvm::DenseMap<mlir::Value, unsigned> shapeOf; // create_object result -> index into shapes
    // Decided while the IR is still ctjs: by the time a key constant or an
    // access is replaced, the object it keys is already an emitc.variable and
    // no longer reads as a closed create_object.
    llvm::DenseMap<mlir::Operation *, std::string> accessKey; // get/set -> member name
    llvm::DenseSet<mlir::Operation *> keyConstants;           // constants that lower to nothing

    mlir::Type classType(const shape & sh) {
        return ec::LValueType::get(ec::OpaqueType::get(context, sh.name));
    }
    // The fields of a closed object: every key read or written, with the
    // carrier of the field's inferred type (the join of its stores, which
    // every read carries); a key only ever read is undefined, carried as NaN.
    void collectShape(ctjs::CreateObjectOp object) {
        shape sh;
        sh.name = "ctn_shape_" + std::to_string(shapes.size());
        sh.site = siteOf(object.getLoc());
        llvm::StringMap<mlir::Type> fields;
        for (mlir::Operation * user : object.getResult().getUsers()) {
            mlir::Value key;
            if (auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                key = get.getKey();
                fields.try_emplace(admission::keyOf(key), get.getResult().getType());
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                key = set.getKey();
                fields.try_emplace(admission::keyOf(key), set.getValue().getType());
            }
            if (key) {
                accessKey[user] = admission::keyOf(key).str();
                keyConstants.insert(key.getDefiningOp());
            }
        }
        for (const auto & entry : fields) {
            sh.fields.emplace_back(entry.getKey().str(), entry.getValue());
        }
        llvm::sort(sh.fields, [](const auto & a, const auto & b) { return a.first < b.first; });
        shapeOf[object.getResult()] = static_cast<unsigned>(shapes.size());
        shapes.push_back(std::move(sh));
    }

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
            // THE CLASSES FIRST, one per closed-shape site, public fields
            // only: emitc.class prints exactly that.
            for (const shape & sh : shapes) {
                auto cls = ec::ClassOp::create(b, module.getLoc(), sh.name);
                cls->setAttr("ctnative.provenance",
                             b.getStringAttr("object literal at " + sh.site));
                mlir::Block & body = cls.getBody().emplaceBlock();
                mlir::OpBuilder inside = mlir::OpBuilder::atBlockEnd(&body);
                for (const auto & [name, type] : sh.fields) {
                    ec::FieldOp::create(inside, module.getLoc(), name, type, mlir::Attribute{});
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

    // JAVASCRIPT'S `**`, WHICH IS NOT C++'s std::pow.
    //
    // Number::exponentiate answers NaN when the base has magnitude one and the
    // exponent is NaN or infinite; C++ answers 1 for pow(1, NaN) and
    // pow(1, INFINITY). Everywhere else the two agree, including pow(NaN, 0)
    // == 1. So the whole difference is one guard, and emitting it is better
    // than refusing the operator: `2 ** 31` keeps working and `1 ** undefined`
    // stops being wrong. Undefined IS this tier's NaN, which is what made the
    // difference reachable from ordinary JavaScript rather than only from a
    // literal NaN.
    //
    // StdLibMap.td already classifies the library spelling `Math.pow` as
    // Divergent with this exact witness; this is the operator path catching up.
    mlir::Value exponentiate(mlir::OpBuilder & b, mlir::Location where, mlir::Value base,
                             mlir::Value exponent) {
        const auto i1 = mlir::IntegerType::get(context, 1);
        mlir::Value magnitude = libmCall(b, where, "std::fabs", {base});
        mlir::Value isOne = ec::CmpOp::create(b, where, i1, ec::CmpPredicate::eq, magnitude,
                                              f64Constant(b, where, 1.0));
        mlir::Value finite =
            ec::CallOpaqueOp::create(b, where, mlir::TypeRange{i1},
                                     b.getStringAttr("std::isfinite"), mlir::ValueRange{exponent})
                .getResult(0);
        mlir::Value notFinite = ec::LogicalNotOp::create(b, where, i1, finite);
        mlir::Value diverges = ec::LogicalAndOp::create(b, where, i1, isOne, notFinite);
        return ec::ConditionalOp::create(
            b, where, mlir::Float64Type::get(context), diverges,
            f64Constant(b, where, std::numeric_limits<double>::quiet_NaN()),
            libmCall(b, where, "std::pow", {base, exponent}));
    }

    // Retype every JavaScript value in the function from the lattice. Done
    // BEFORE any operation is replaced, so the replacements see carriers.
    void retype(ctjs::FuncOp fn) {
        const auto retypeValue = [&](mlir::Value v) {
            if (!llvm::isa<ctjs::ValueType>(v.getType())) { return; }
            // A closed object keeps its ctjs type until its shape is known
            // below; everything else takes its carrier now.
            if (admission::isClosedObject(v)) { return; }
            const carrier c = carrierOf(typeOf(v));
            // NO CARRIER IS FATAL, NOT A DOUBLE. This fell through to f64
            // for anything that was not a boolean, so a value admission
            // never looked at - a boxed object threaded through a loop, say
            // - would have been retyped to a number and lowered as one, and
            // the miscompile would have surfaced as a wrong answer at the
            // gate rather than here. Admission refuses every such function;
            // reaching this line is a bug in admission, and says so.
            if (c == carrier::none && !admission::lowersToNothing(v)) {
                llvm::report_fatal_error(llvm::Twine("ctnative lowering: `") + fn.getSymName() +
                                         "` holds a value of type " + printed(typeOf(v)) +
                                         " that has no native carrier - admission should "
                                         "have refused it (a literal that reaches a loop is "
                                         "obligation O-3, and whyOpen names it)");
            }
            v.setType(carrierType(context, c));
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
        // NOW THE SHAPES, from the carriers the fields' values just took, and
        // the objects' own types last.
        fn.getBody().walk([&](ctjs::CreateObjectOp object) {
            collectShape(object);
            mlir::Value(object.getResult()).setType(classType(shapes[shapeOf[object.getResult()]]));
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
        if (auto object = llvm::dyn_cast<CreateObjectOp>(o)) {
            // The struct, by value, in this frame; every field set to its
            // undefined - NaN for a number, false for a boolean - before the
            // first store, so a read before a write is exact.
            const shape & sh = shapes[shapeOf[object.getResult()]];
            mlir::Value local =
                ec::VariableOp::create(b, where, classType(sh), ec::OpaqueAttr::get(context, ""));
            for (const auto & [name, type] : sh.fields) {
                mlir::Value member =
                    ec::MemberOp::create(b, where, ec::LValueType::get(type), name, local);
                mlir::Value init =
                    llvm::isa<mlir::IntegerType>(type)
                        ? boolConstant(b, where, false)
                        : f64Constant(b, where, std::numeric_limits<double>::quiet_NaN());
                ec::AssignOp::create(b, where, member, init);
            }
            swap(local);
            return;
        }
        if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) {
            const mlir::Type type = get.getResult().getType();
            mlir::Value member = ec::MemberOp::create(b, where, ec::LValueType::get(type),
                                                      accessKey.at(o), get.getObject());
            swap(ec::LoadOp::create(b, where, type, member));
            return;
        }
        if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
            mlir::Value member =
                ec::MemberOp::create(b, where, ec::LValueType::get(set.getValue().getType()),
                                     accessKey.at(o), set.getObject());
            ec::AssignOp::create(b, where, member, set.getValue());
            eraseIfUnused(o);
            return;
        }
        if (keyConstants.contains(o)) { return; } // dead after its get/set; swept
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
            case BinaryKind::Pow: swap(exponentiate(b, where, l, r)); return;
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
        // The JavaScript name is the symbol before the importer's `$index`.
        const llvm::StringRef jsName = fn.getSymName().split('$').first;
        made->setAttr("ctnative.provenance",
                      b.getStringAttr((isEntry ? "the top level" : "function " + jsName.str()) +
                                      ", " + siteOfFunction(fn)));
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
            if (llvm::isa<ec::ConstantOp, ec::LiteralOp, ctjs::FrameEnterOp, ctjs::LoadGlobalOp,
                          ctjs::ConstantOp>(o)) {
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
            // NO INITIALISER, so static zero-initialisation gives 0. Not NaN,
            // which is what this used to emit: every global then started at
            // the same bytes the gate prints for a NaN a program computed, so
            // "never written" and "computed NaN" compared EQUAL and any global
            // whose right answer is NaN was un-failable. Deleting the whole
            // body of the fixture function that exists to prove undefined-field
            // semantics kept the gate green. A global that is never stored is
            // refused outright (see the census in runOnOperation), so 0 is not
            // a value any correct program can observe here.
            auto global =
                ec::GlobalOp::create(b, module.getLoc(), ("g_" + name).str(),
                                     mlir::Float64Type::get(context), mlir::Attribute{},
                                     /*extern_specifier=*/false, /*static_specifier=*/true,
                                     /*const_specifier=*/false);
            global->setAttr("ctnative.provenance", b.getStringAttr("global " + name.str()));
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

        lowering lower{solver, &getContext(), module};
        for (ctjs::FuncOp fn : accepted) {
            lower.names[fn.getSymName()] =
                fn.getSymName().starts_with("_script_$") ? "main" : cIdentifier(fn.getSymName());
        }

        // THE GLOBAL CENSUS, over the whole accepted set and BEFORE any
        // function is lowered.
        //
        // Two things depend on it. First, `main` prints the globals from this
        // set, and it used to be filled lazily as each global was first
        // touched - so a global written and read only inside a helper was
        // declared and never printed, because main is the importer's function
        // 0 and is lowered first. The differential then failed by naming the
        // missing line rather than the ordering, which is a bug report
        // pointing at the wrong file.
        //
        // Second, a global with no store anywhere in the unit is `undefined`,
        // and this tier carries undefined as NaN - exact for arithmetic and
        // comparison, NOT for printing, where the interpreter reports "not a
        // Number" and the binary would print `nan`. That difference is
        // observable, so it is refused rather than represented, which is the
        // rule this file is built on.
        // A FUNCTION'S OWN NAME IS A GLOBAL TOO, and it is not one of these.
        // `function f(){}` at the top level is a store of a closure into the
        // global "f", and every call is a load of it; both lower to nothing,
        // because the closed world turned the call into a direct one. Counting
        // them would declare `static double g_accumulate;` and print
        // `accumulate=0` beside the numbers - which is exactly what happened
        // the first time this census ran.
        const auto bindsAFunction = [](ctjs::StoreGlobalOp store) {
            return llvm::isa_and_nonnull<ctjs::CreateClosureOp>(store.getValue().getDefiningOp());
        };
        const auto callsOnly = [](ctjs::LoadGlobalOp load) {
            return !load.getResult().use_empty() &&
                   llvm::all_of(load.getResult().getUsers(), [](mlir::Operation * user) {
                       return llvm::isa<ctjs::CallDirectOp>(user);
                   });
        };
        llvm::StringSet<> storedGlobals;
        for (ctjs::FuncOp fn : accepted) {
            fn.getBody().walk([&](ctjs::StoreGlobalOp store) {
                if (!bindsAFunction(store)) { storedGlobals.insert(store.getName()); }
            });
        }
        llvm::SmallVector<llvm::StringRef> neverStored;
        for (ctjs::FuncOp fn : accepted) {
            fn.getBody().walk([&](mlir::Operation * o) {
                llvm::StringRef name;
                if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(o)) {
                    if (callsOnly(load)) { return; }
                    name = load.getName();
                }
                if (auto store = llvm::dyn_cast<ctjs::StoreGlobalOp>(o)) {
                    if (bindsAFunction(store)) { return; }
                    name = store.getName();
                }
                if (name.empty()) { return; }
                if (lower.globals.insert(name).second && !storedGlobals.contains(name)) {
                    neverStored.push_back(name);
                }
            });
        }
        if (!neverStored.empty()) {
            llvm::sort(neverStored);
            const auto entry = llvm::find_if(
                accepted, [](ctjs::FuncOp fn) { return fn.getSymName().starts_with("_script_$"); });
            if (entry != accepted.end()) {
                entry->getOperation()->setAttr(
                    "ctnative.not_native",
                    mlir::StringAttr::get(
                        &getContext(),
                        ("global `" + neverStored.front() +
                         "` is read but never stored, so it is undefined - which this tier "
                         "carries as NaN, and printing that as a number is not what the "
                         "interpreter answers")
                            .str()));
                accepted.erase(entry);
            }
        }

        for (ctjs::FuncOp fn : accepted) { lower.lower(fn); }
        if (!accepted.empty()) { lower.declareGlobals(); }
        lower.finish();
    }
};

} // namespace

} // namespace ctcompile::ctnative
