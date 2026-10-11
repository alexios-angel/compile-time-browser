//===- CTNativeLattice.cpp - The ctnative type lattice ----------*- C++ -*-===//
//
// Implements `meet`, the least upper bound of two ctnative types: numbers and
// strings widen within their family, containers meet elementwise, and anything
// else becomes a canonical, capped union. CTNativeLattice.h documents the
// lattice itself.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTNative/IR/CTNativeLattice.h"

#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

using mlir::MLIRContext;
using mlir::Type;

namespace ctcompile::ctnative {
namespace {

// CTNativeEnums.td defines enumerator values as lattice rank, which lets
// `widerOf` be a plain `max` instead of a table. Reordering the enum cases
// would silently make the lattice widen the wrong way, so the order of both
// chains is checked at compile time.
static_assert(static_cast<std::uint32_t>(NumKind::I32) < static_cast<std::uint32_t>(NumKind::I64),
              "the numeric chain is i32 -> i64 -> f64; CTNativeEnums.td was reordered");
static_assert(static_cast<std::uint32_t>(NumKind::I64) < static_cast<std::uint32_t>(NumKind::F64),
              "the numeric chain is i32 -> i64 -> f64; CTNativeEnums.td was reordered");
static_assert(static_cast<std::uint32_t>(StrEncoding::UTF8) <
                  static_cast<std::uint32_t>(StrEncoding::UTF16),
              "the encoding chain is utf8 -> utf16; CTNativeEnums.td was reordered");

/// The wider of two ranked enumerators (see the static_asserts above).
template <typename Enum> Enum widerOf(Enum a, Enum b) {
    using Underlying = std::underlying_type_t<Enum>;
    return static_cast<Enum>(std::max(static_cast<Underlying>(a), static_cast<Underlying>(b)));
}

/// `type` as MLIR prints it, the sort key for a variant's alternatives.
///
/// MLIR uniques a parameterized type on its parameters, so `variant<bool, str>`
/// and `variant<str, bool>` are two different types that compare unequal.
/// Without a total order on alternatives, `meet(a, b)` and `meet(b, a)` would
/// be equivalent but unequal, and the fixpoint check of any dataflow analysis
/// over this lattice would never converge. The printed form is total and
/// stable across runs, which is all the order needs.
std::string printedForm(Type type) {
    std::string buffer;
    llvm::raw_string_ostream stream(buffer);
    stream << type;
    return buffer;
}

/// The parts of a union, gathered before anything is decided about them.
struct Alternatives {
    llvm::SmallVector<Type, 8> members;
    bool nullable = false;
    bool sawBoxed = false;
    bool sawJson = false;
};

/// Flattens `type` into `into`: variants are expanded, `opt` sets the nullable
/// flag, `bottom` is dropped, and `boxed`/`json` are recorded as flags.
void collect(Type type, Alternatives & into) {
    if (!type) { return; }
    // `bottom` contributes no values, so it drops out of a union entirely.
    if (llvm::isa<BottomType>(type)) { return; }
    if (llvm::isa<BoxedType>(type)) {
        into.sawBoxed = true;
        return;
    }
    if (llvm::isa<JsonType>(type)) {
        into.sawJson = true;
        return;
    }
    // Nullability is hoisted: an `opt` inside a union becomes the `nullable`
    // flag and is re-applied around the result, so `meet(opt<i32>, f64)` is
    // `opt<num<f64>>` rather than `variant<num<f64>, opt<num<i32>>>`. Removing
    // this keeps the meet associative but loses the canonical form (which
    // the variant verifier requires); the hand-written meet table in
    // ctcompile/test/Analysis/Types/Lattice.cpp catches that, the
    // associativity sweep does not.
    if (auto optional = llvm::dyn_cast<OptType>(type)) {
        into.nullable = true;
        collect(optional.getElementType(), into);
        return;
    }
    if (auto variant = llvm::dyn_cast<VariantType>(type)) {
        for (Type alternative : variant.getAlternatives()) { collect(alternative, into); }
        return;
    }
    into.members.push_back(type);
}

/// Sorts `members` by printed form and drops duplicates.
void sortAndUnique(llvm::SmallVectorImpl<Type> & members) {
    llvm::sort(members, [](Type a, Type b) { return printedForm(a) < printedForm(b); });
    members.erase(std::unique(members.begin(), members.end()), members.end());
}

} // namespace

// Declared here rather than in the anonymous block above because `meet` and
// this are mutually recursive: a container's element types are themselves met.
// The recursion is on type depth, which is finite, so it terminates.
static Type mergeSameFamily(Type a, Type b);

/// Builds the canonical union of `parts`: `boxed` or `json` if either was
/// seen, otherwise the merged, sorted members as `bottom`, one type, a
/// variant, or `json` past the cap, wrapped in `opt` when nullable.
static Type finishUnion(MLIRContext * context, Alternatives & parts) {
    // `boxed` absorbs everything, and `json` absorbs everything but `boxed`.
    // Both already carry the absent case, so `nullable` says nothing further.
    if (parts.sawBoxed) { return BoxedType::get(context); }
    if (parts.sawJson) { return JsonType::get(context); }

    sortAndUnique(parts.members);

    // Merge pairs until nothing changes. Two numbers in a union are not a
    // union; they are the wider number. Doing this before the cap is checked
    // stops `meet` falling off the top of the lattice for a value that is
    // merely an `i32` in four places.
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t i = 0; i < parts.members.size() && !changed; ++i) {
            for (std::size_t j = i + 1; j < parts.members.size(); ++j) {
                Type merged = mergeSameFamily(parts.members[i], parts.members[j]);
                if (!merged) { continue; }
                parts.members[i] = merged;
                parts.members.erase(parts.members.begin() + static_cast<std::ptrdiff_t>(j));
                changed = true;
                break;
            }
        }
    }
    // Sort again: the merge loop replaces members in place (`num<i32>` and
    // `num<f64>` become `num<f64>` in the first slot), so the list is no
    // longer sorted, and without this the result's order would depend on
    // operand order, giving two MLIR types for one lattice element. The
    // associativity sweep in ctcompile/test/Analysis/Types/Lattice.cpp catches
    // its removal; the hand-written meet table does not.
    sortAndUnique(parts.members);
    // A merge can answer `boxed` (two callable sets past their cap), which
    // absorbs the union like a `boxed` operand would have.
    if (llvm::any_of(parts.members, [](Type t) { return llvm::isa<BoxedType>(t); })) {
        return BoxedType::get(context);
    }

    Type result;
    if (parts.members.empty()) {
        result = BottomType::get(context);
    } else if (parts.members.size() == 1) {
        result = parts.members.front();
    } else if (parts.members.size() > kMaxVariantAlternatives) {
        // Too many alternatives widen to `json`. This cap bounds the lattice's
        // height, which is what guarantees the type-inference dataflow
        // analysis (CTNative/Analysis/TypeInference) reaches a fixpoint; see
        // kMaxVariantAlternatives in CTNativeLattice.h.
        result = JsonType::get(context);
    } else {
        result = VariantType::get(context, parts.members);
    }

    if (!parts.nullable) { return result; }
    // `opt<boxed>`, `opt<json>`: both elements already hold the absent case, so
    // wrapping would be a second spelling of a type that exists. The variant
    // verifier rejects the same duplication from the other direction.
    if (llvm::isa<BoxedType>(result) || llvm::isa<JsonType>(result)) { return result; }
    return OptType::get(context, result);
}

/// Two single-element containers of kind T merge elementwise; anything else
/// does not merge.
template <class T> static Type mergeElements(Type a, Type b) {
    auto other = llvm::dyn_cast<T>(b);
    if (!other) { return Type(); }
    return T::get(a.getContext(), meet(llvm::cast<T>(a).getElementType(), other.getElementType()));
}

/// The wider of two types in one family (numbers, strings, containers,
/// pointers), or null when they are in different families and the caller must
/// make a union instead. Every arm claims that one representation subsumes
/// another; each claim is documented in CTNativeTypes.td beside its type.
static Type mergeSameFamily(Type a, Type b) {
    if (a == b) { return a; }
    MLIRContext * context = a.getContext();

    if (auto numberA = llvm::dyn_cast<NumType>(a)) {
        auto numberB = llvm::dyn_cast<NumType>(b);
        if (!numberB) { return Type(); }
        return NumType::get(context, widerOf(numberA.getKind(), numberB.getKind()));
    }

    // `str` and `strview` are one family, and the owning `str` wins. A view
    // can always be copied into an owning string; going the other way would
    // need a lifetime proof, which the lattice has no information to make.
    const auto encodingOf = [](Type type) {
        return llvm::isa<StrType>(type) ? llvm::cast<StrType>(type).getEncoding()
                                        : llvm::cast<StrViewType>(type).getEncoding();
    };
    const bool aIsString = llvm::isa<StrType, StrViewType>(a);
    const bool bIsString = llvm::isa<StrType, StrViewType>(b);
    if (aIsString || bIsString) {
        if (!aIsString || !bIsString) { return Type(); }
        const StrEncoding wider = widerOf(encodingOf(a), encodingOf(b));
        if (llvm::isa<StrType>(a) || llvm::isa<StrType>(b)) { return StrType::get(context, wider); }
        return StrViewType::get(context, wider);
    }

    // Two callables are one of their targets: the sorted, distinct union of
    // both target lists, and `boxed` once it is larger than the cap. The
    // size of a union does not depend on the order it was formed in, so the
    // cap cannot break associativity.
    const auto targetsOf = [](Type type, llvm::SmallVectorImpl<mlir::StringAttr> & into) {
        if (auto closure = llvm::dyn_cast<ClosureType>(type)) {
            into.push_back(mlir::StringAttr::get(type.getContext(), closure.getTarget()));
        } else {
            llvm::append_range(into, llvm::cast<ClosureSetType>(type).getTargets());
        }
    };
    if (llvm::isa<ClosureType, ClosureSetType>(a)) {
        if (!llvm::isa<ClosureType, ClosureSetType>(b)) { return Type(); }
        llvm::SmallVector<mlir::StringAttr, 8> targets;
        targetsOf(a, targets);
        targetsOf(b, targets);
        llvm::sort(targets, [](mlir::StringAttr x, mlir::StringAttr y) {
            return x.getValue() < y.getValue();
        });
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
        if (targets.size() > kMaxClosureSetTargets) { return BoxedType::get(context); }
        return ClosureSetType::get(context, targets);
    }

    if (llvm::isa<VecType>(a)) { return mergeElements<VecType>(a, b); }
    // Tuples of one arity meet position by position; another arity is a
    // different record, so the two become alternatives of a union.
    if (auto tupleA = llvm::dyn_cast<TupleType>(a)) {
        auto tupleB = llvm::dyn_cast<TupleType>(b);
        if (!tupleB || tupleA.getElements().size() != tupleB.getElements().size()) {
            return Type();
        }
        llvm::SmallVector<Type> elements;
        for (auto [left, right] : llvm::zip_equal(tupleA.getElements(), tupleB.getElements())) {
            elements.push_back(meet(left, right));
        }
        return TupleType::get(context, elements);
    }
    if (llvm::isa<SetType>(a)) { return mergeElements<SetType>(a, b); }
    // Promises and their resolve functions meet by value type
    // (CTNativeTypes.td).
    if (llvm::isa<PromiseType>(a)) { return mergeElements<PromiseType>(a, b); }
    if (llvm::isa<PromiseResolveType>(a)) { return mergeElements<PromiseResolveType>(a, b); }
    if (auto mapA = llvm::dyn_cast<MapType>(a)) {
        auto mapB = llvm::dyn_cast<MapType>(b);
        if (!mapB) { return Type(); }
        return MapType::get(context, meet(mapA.getKeyType(), mapB.getKeyType()),
                            meet(mapA.getValueType(), mapB.getValueType()));
    }

    // Among the pointer types, only `owned` widens to `shared`. A unique owner
    // can always become shared; a shared one cannot become unique without
    // proving no other reference exists, which is exactly what the analysis
    // failed to prove if it produced `shared`. `weak` is unordered against
    // both, because merging a strong and a weak reference would change object
    // lifetimes; see the CTNative_WeakType description in CTNativeTypes.td.
    const auto pointeeOf = [](Type type) {
        return llvm::isa<OwnedType>(type) ? llvm::cast<OwnedType>(type).getPointeeType()
                                          : llvm::cast<SharedType>(type).getPointeeType();
    };
    if (llvm::isa<OwnedType, SharedType>(a)) {
        if (!llvm::isa<OwnedType, SharedType>(b)) { return Type(); }
        const Type pointee = meet(pointeeOf(a), pointeeOf(b));
        if (llvm::isa<OwnedType>(a) && llvm::isa<OwnedType>(b)) {
            return OwnedType::get(context, pointee);
        }
        return SharedType::get(context, pointee);
    }
    if (auto weakA = llvm::dyn_cast<WeakType>(a)) {
        auto weakB = llvm::dyn_cast<WeakType>(b);
        if (!weakB) { return Type(); }
        return WeakType::get(context, meet(weakA.getPointeeType(), weakB.getPointeeType()));
    }

    return Type();
}

Type meet(Type a, Type b) {
    // A null Type is absent, not `bottom`. It is what a caller folding over an
    // empty range has before the first element, and returning the other operand
    // is what lets that caller not carry a seed.
    if (!a) { return b; }
    if (!b) { return a; }
    if (a == b) { return a; }

    MLIRContext * context = a.getContext();
    if (llvm::isa<BottomType>(a)) { return b; }
    if (llvm::isa<BottomType>(b)) { return a; }
    if (llvm::isa<BoxedType>(a) || llvm::isa<BoxedType>(b)) { return BoxedType::get(context); }
    if (llvm::isa<JsonType>(a) || llvm::isa<JsonType>(b)) { return JsonType::get(context); }

    if (Type merged = mergeSameFamily(a, b)) { return merged; }

    Alternatives parts;
    collect(a, parts);
    collect(b, parts);
    return finishUnion(context, parts);
}

Type meet(MLIRContext * context, llvm::ArrayRef<Type> types) {
    Type accumulated = BottomType::get(context);
    for (Type type : types) { accumulated = meet(accumulated, type); }
    return accumulated;
}

StrType defaultStringType(MLIRContext * context) {
    return StrType::get(context, kDefaultStringEncoding);
}

} // namespace ctcompile::ctnative
