//===- ArgumentTypeCopies.cpp - One copy of a function per argument type --===//
//
// A function called directly with values of different types, such as
// Bootstrap's toType called with a Number at one site and a String at
// another, has a parameter whose type is their union. A union of a String with
// another kind has only a String-union carrier, which tests its tag at every
// use, or none. When every call site passes a value of
// one exact type, the function is copied once per exact type instead: each
// call goes to the copy for its argument's type, so each copy's parameter has
// one type and the ordinary per-type helpers apply. The calls of the first
// type stay on the original. Each copy is recorded in ctnative.copies, so a
// count of source functions can be rebuilt after lowering.
//
// A call that passes a union itself is a real merge: no copy can give it one
// type, so its function is not copied.
//
// In each copy, and in the original, a test of such a parameter's type
// (`typeof p`, `p == null`) then has one answer, which replaces it, and the
// caller runs the precompute pass to keep only the branch the answer selects:
// admission reads every operation, even in a branch no value of the type
// reaches, such as the `${t}` arm of Bootstrap's `null == t ? `${t}` : ...`.
//
//===----------------------------------------------------------------------===//

#include "ClosureLifting/ClassInheritance.h"
#include "LoweringSupport.h"
#include "ctcompile/CTNative/IR/CTNativeAttrNames.h"
#include "ctcompile/CTNative/IR/CTNativeOps.h"

#include "llvm/ADT/MapVector.h"

#include <string>

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// Whether a value of `type` needs a String-union carrier, or has no carrier:
/// the parameter types a copy per argument type replaces.
bool needsUnion(mlir::Type type) {
    const carrier held = carrierOf(type);
    return held == carrier::none || held == carrier::numberString ||
           held == carrier::nullableNumberString || held == carrier::booleanString ||
           held == carrier::nullableBooleanString;
}

/// What `typeof` answers for every value of `type`, or empty when values of
/// it may answer differently.
llvm::StringRef typeOfAnswer(mlir::Type type) {
    if (llvm::isa_and_nonnull<NumType>(type)) { return "number"; }
    if (llvm::isa_and_nonnull<BoolType>(type)) { return "boolean"; }
    if (carrierOf(type) == carrier::string) { return "string"; }
    return {};
}

/// Whether every value of `type` is null or undefined (true) or none is
/// (false); nullopt when either may hold.
std::optional<bool> alwaysAbsent(mlir::Type type) {
    if (!typeOfAnswer(type).empty()) { return false; }
    auto optional = llvm::dyn_cast_or_null<OptType>(type);
    if (optional && llvm::isa<BottomType>(optional.getElementType())) { return true; }
    return std::nullopt;
}

/// Replaces, in `fn`, `typeof p`, and `p == null`, `p === null` and the same
/// with undefined either way round, by what each answers for every value of
/// `type`, where `p` is entry argument `index` or a narrowed name of it. None
/// of them can run code: `p` is a primitive, null or undefined.
void foldTypeTests(ctjs::FuncOp fn, unsigned index, mlir::Type type) {
    llvm::SmallVector<mlir::Value> names{fn.getBody().front().getArgument(index)};
    for (size_t i = 0; i < names.size(); ++i) {
        for (mlir::Operation * user : names[i].getUsers()) {
            if (auto refine = llvm::dyn_cast<RefineOp>(user)) {
                names.push_back(refine.getResult());
            }
        }
    }
    const llvm::StringRef name = typeOfAnswer(type);
    const std::optional<bool> absent = alwaysAbsent(type);
    mlir::MLIRContext * context = fn.getContext();
    const auto isAbsentConstant = [](mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && llvm::isa<ctjs::NullAttr, ctjs::UndefinedAttr>(constant.getValue());
    };
    llvm::SmallVector<std::pair<mlir::Operation *, mlir::Attribute>> answers;
    for (mlir::Value value : names) {
        for (mlir::Operation * user : value.getUsers()) {
            if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(user);
                unary && unary.getKind() == ctjs::UnaryKind::TypeOf && !name.empty()) {
                answers.push_back({user, ctjs::StringAttr::get(context, name)});
                continue;
            }
            auto compare = llvm::dyn_cast<ctjs::CompareOp>(user);
            if (!compare || !absent ||
                (compare.getKind() != ctjs::CompareKind::Eq &&
                 compare.getKind() != ctjs::CompareKind::StrictEq)) {
                continue;
            }
            const mlir::Value other =
                compare.getLhs() == value ? compare.getRhs() : compare.getLhs();
            // `p === null` has no one answer when p may be undefined instead.
            if (!isAbsentConstant(other) ||
                (*absent && compare.getKind() == ctjs::CompareKind::StrictEq)) {
                continue;
            }
            answers.push_back({user, ctjs::BooleanAttr::get(context, *absent)});
        }
    }
    for (auto & [op, answer] : answers) {
        mlir::OpBuilder at(op);
        auto constant =
            ctjs::ConstantOp::create(at, op->getLoc(), ctjs::ValueType::get(context), answer);
        op->getResult(0).replaceAllUsesWith(constant.getResult());
        op->erase();
    }
}

} // namespace

bool copyPerArgumentType(mlir::ModuleOp module,
                         llvm::function_ref<bool(ctjs::FuncOp)> keepsOriginal) {
    // Only a private function whose every use is a direct call of it: no
    // closure or other reference can pass it values the calls do not show.
    llvm::MapVector<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    module.walk([&](ctjs::CallDirectOp call) {
        if (ctjs::FuncOp target = call.getTarget()) {
            callers[target.getOperation()].push_back(call);
        }
    });
    // A closure the lift marked lifted is never called: its calls became the
    // direct calls above. Any other closure of a function can be called with
    // values no call shows.
    const auto lifted = [](ctjs::CreateClosureOp made) { return made->hasAttr(kNativeLifted); };
    llvm::DenseSet<unsigned> closed;
    module.walk([&](ctjs::CreateClosureOp made) {
        if (!lifted(made)) { closed.insert(made.getFunction()); }
    });
    llvm::SmallVector<ctjs::FuncOp> candidates;
    for (auto & [op, calls] : callers) {
        auto fn = llvm::cast<ctjs::FuncOp>(op);
        const std::optional<unsigned> index = ctjs::functionIndex(fn);
        if (calls.size() < 2 || !fn.isPrivate() || fn.getBody().empty() || !index ||
            closed.contains(*index) || keepsOriginal(fn)) {
            continue;
        }
        auto uses = mlir::SymbolTable::getSymbolUses(fn, module.getOperation());
        if (!uses || llvm::any_of(*uses, [](const mlir::SymbolTable::SymbolUse & use) {
                return !llvm::isa<ctjs::CallDirectOp>(use.getUser());
            })) {
            continue;
        }
        // A closure made inside would need a copy of its own.
        if (fn->walk([&](ctjs::CreateClosureOp made) {
                  return lifted(made) ? mlir::WalkResult::advance() : mlir::WalkResult::interrupt();
              }).wasInterrupted()) {
            continue;
        }
        candidates.push_back(fn);
    }
    if (candidates.empty()) { return false; }

    // The types every call site passes, from a solve of the module as it is.
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<TypeInference>(nullptr, nullptr);
    if (mlir::failed(solver.initializeAndRun(module))) { return false; }
    const auto typeOf = [&](mlir::Value value) -> mlir::Type {
        const TypeLattice * lattice = solver.lookupState<TypeLattice>(value);
        return lattice ? lattice->getValue().getType() : mlir::Type{};
    };

    unsigned nextIndex = 0;
    module.walk([&](ctjs::FuncOp fn) {
        if (const std::optional<unsigned> index = ctjs::functionIndex(fn)) {
            nextIndex = std::max(nextIndex, *index + 1);
        }
    });
    auto earlier = module->getAttrOfType<mlir::DictionaryAttr>(kNativeCopies);
    llvm::SmallVector<mlir::NamedAttribute> copies;
    if (earlier) { copies.append(earlier.begin(), earlier.end()); }
    bool changed = false;
    for (ctjs::FuncOp fn : candidates) {
        mlir::Block & entry = fn.getBody().front();
        // The parameters whose type is a union.
        llvm::SmallVector<unsigned> mixed;
        for (unsigned i = ctjs::implicit_arguments; i < entry.getNumArguments(); ++i) {
            if (!entry.getArgument(i).use_empty() && needsUnion(typeOf(entry.getArgument(i)))) {
                mixed.push_back(i);
            }
        }
        if (mixed.empty()) { continue; }
        // The calls grouped by the carrier of each of those arguments. A call
        // that passes a union, or a value with no type, is a real merge.
        // Each group also keeps, per such argument, the meet of the types its
        // calls pass: the type the parameter has in the group's function.
        struct group {
            std::string key;
            llvm::SmallVector<ctjs::CallDirectOp> calls;
            llvm::SmallVector<mlir::Type> types;
        };
        llvm::SmallVector<group> byType;
        bool merged = false;
        for (ctjs::CallDirectOp call : callers[fn.getOperation()]) {
            std::string key;
            llvm::SmallVector<mlir::Type> types;
            for (unsigned i : mixed) {
                const mlir::Type passed = typeOf(call->getOperand(i));
                merged |= !passed || needsUnion(passed);
                const carrier held = carrierOf(passed);
                key.push_back(static_cast<char>(held));
                if (held == carrier::instance) {
                    const llvm::StringRef name = instanceClassName(passed);
                    key += llvm::Twine(name.size()).str() + ":" + name.str();
                }
                types.push_back(passed);
            }
            auto row = llvm::find_if(byType, [&](const group & g) { return g.key == key; });
            if (row == byType.end()) {
                byType.push_back({key, {}, types});
                row = std::prev(byType.end());
            }
            row->calls.push_back(call);
            for (auto [kept, passed] : llvm::zip_equal(row->types, types)) {
                kept = meet(kept, passed);
            }
        }
        if (merged || byType.size() < 2) { continue; }
        const llvm::StringRef name = fn.getSymName();
        // A copy of a copy counts toward the function the first was copied from.
        mlir::StringAttr source = fn.getSymNameAttr();
        if (earlier) {
            if (auto from = earlier.getAs<mlir::StringAttr>(name)) { source = from; }
        }
        const auto foldAll = [&](ctjs::FuncOp target, const group & g) {
            for (auto [index, type] : llvm::zip_equal(mixed, g.types)) {
                foldTypeTests(target, index, type);
            }
        };
        for (const group & g : llvm::drop_begin(byType)) {
            auto copy = llvm::cast<ctjs::FuncOp>(fn->clone());
            copy.setSymName(
                (name.substr(0, name.rfind('$')) + "$" + llvm::Twine(nextIndex++)).str());
            module.getBody()->getOperations().insertAfter(fn->getIterator(), copy);
            for (ctjs::CallDirectOp call : g.calls) {
                call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(copy.getSymNameAttr()));
            }
            copies.emplace_back(copy.getSymNameAttr(), source);
            foldAll(copy, g);
        }
        // The original last: every copy is made from its unfolded body.
        foldAll(fn, byType.front());
        changed = true;
    }
    if (changed) {
        module->setAttr(kNativeCopies, mlir::DictionaryAttr::get(module.getContext(), copies));
    }
    return changed;
}

} // namespace ctcompile::ctnative::lowering_detail
