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

// THE TWO CHAINS, CHECKED AT COMPILE TIME.
//
// CTNativeEnums.td says the enumerator values ARE the lattice rank, which is
// what lets `widerOf` be a `max` instead of a table. That claim is only true so
// long as nobody reorders the cases, and a reorder produces no diagnostic at
// all - just a lattice that quietly widens the wrong way. The plan is explicit
// about preferring a build error to a test, so this is a build error.
static_assert(static_cast<std::uint32_t>(NumKind::I32) < static_cast<std::uint32_t>(NumKind::I64),
              "the numeric chain is i32 -> i64 -> f64; CTNativeEnums.td was reordered");
static_assert(static_cast<std::uint32_t>(NumKind::I64) < static_cast<std::uint32_t>(NumKind::F64),
              "the numeric chain is i32 -> i64 -> f64; CTNativeEnums.td was reordered");
static_assert(static_cast<std::uint32_t>(StrEncoding::UTF8) <
                  static_cast<std::uint32_t>(StrEncoding::UTF16),
              "the encoding chain is utf8 -> utf16; CTNativeEnums.td was reordered");

template <typename Enum> Enum widerOf(Enum a, Enum b) {
    using Underlying = std::underlying_type_t<Enum>;
    return static_cast<Enum>(std::max(static_cast<Underlying>(a), static_cast<Underlying>(b)));
}

// THE ORDER OF A VARIANT'S ALTERNATIVES IS ITS PRINTED ORDER, and it has to be
// SOMETHING deterministic rather than nothing.
//
// MLIR uniques a parameterized type on its parameters, so `variant<bool, str>`
// and `variant<str, bool>` are two DIFFERENT types that print differently and
// compare unequal. Without a total order on alternatives, `meet(a, b)` and
// `meet(b, a)` would be equivalent-but-unequal, `==` would stop meaning
// equality, and the fixpoint check in any dataflow analysis over this lattice
// would never converge. Sorting by the printed form is not elegant; it is total
// and stable across runs, which are the only two properties that matter.
std::string printedForm(Type type) {
    std::string buffer;
    llvm::raw_string_ostream stream(buffer);
    stream << type;
    return buffer;
}

// The parts of a union, gathered before anything is decided about them.
struct Alternatives {
    llvm::SmallVector<Type, 8> members;
    bool nullable = false;
    bool sawBoxed = false;
    bool sawJson = false;
};

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
    // NULLABILITY IS HOISTED, NOT CARRIED. `opt` inside a union becomes a flag
    // and comes back outside it, which is what makes `opt` a LIFT rather than
    // another rung of the ladder.
    //
    // MEASURED, BECAUSE THE OBVIOUS CLAIM ABOUT IT IS WRONG. Deleting this
    // branch - so that `opt<T>` is just another opaque alternative - leaves the
    // meet PERFECTLY ASSOCIATIVE: 9261 triples, 0 disagreements. An opaque
    // element trivially satisfies the laws. What it breaks is the CANONICAL
    // FORM and the precision that comes with it: `meet(opt<i32>, f64)` answers
    // `variant<num<f64>, opt<num<i32>>>` instead of `opt<num<f64>>`, which is
    // still a sound answer and a much worse one, and which the variant verifier
    // would reject if anything tried to print it.
    //
    // So the guard on this is the HAND-WRITTEN MEET TABLE, not the property
    // sweep - six of its rows go red - and that is the argument for having both
    // in ctcompile/test/CTNativeLattice.cpp rather than either alone.
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

void sortAndUnique(llvm::SmallVectorImpl<Type> & members) {
    llvm::sort(members, [](Type a, Type b) { return printedForm(a) < printedForm(b); });
    members.erase(std::unique(members.begin(), members.end()), members.end());
}

} // namespace

// Declared here rather than in the anonymous block above because `meet` and
// this are mutually recursive: a container's element types are themselves met.
// The recursion is on type DEPTH, which is finite, so it terminates.
static Type mergeSameFamily(Type a, Type b);

static Type finishUnion(MLIRContext * context, Alternatives & parts) {
    // `boxed` absorbs everything, and `json` absorbs everything but `boxed`.
    // Both already carry the absent case, so `nullable` says nothing further.
    if (parts.sawBoxed) { return BoxedType::get(context); }
    if (parts.sawJson) { return JsonType::get(context); }

    sortAndUnique(parts.members);

    // PAIRWISE MERGE TO A FIXPOINT. Two numbers in a union are not a union;
    // they are the wider number. Doing this before the cap is checked is what
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
    // AND SORTED AGAIN, WHICH IS NOT REDUNDANT AND WAS MEASURED. The merge loop
    // REPLACES members - `num<i32>` and `num<f64>` become `num<f64>` in the
    // first slot - so a list sorted before it is not sorted after it, and the
    // surviving order then depends on which pair merged first, which depends on
    // the order the two operands arrived in. Delete this line and
    // `meet(set<bool>, meet(owned<bool>, shared<bool>))` answers
    // `variant<set, shared>` while the other bracketing answers
    // `variant<shared, set>`: two MLIR types for one lattice element.
    //
    // THE HAND-WRITTEN TABLE DOES NOT CATCH IT - all thirty rows still pass -
    // and the associativity sweep reports exactly four disagreements out of
    // 9261. That is the argument for the sweep existing.
    sortAndUnique(parts.members);

    Type result;
    if (parts.members.empty()) {
        result = BottomType::get(context);
    } else if (parts.members.size() == 1) {
        result = parts.members.front();
    } else if (parts.members.size() > kMaxVariantAlternatives) {
        // THE CAP, AND IT IS THE LATTICE'S HEIGHT BOUND. See
        // kMaxVariantAlternatives in CTNativeLattice.h for why an uncapped
        // union would leave Phase 54's analysis with no termination argument.
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

// Do these two types belong to one family with a widening between them? A null
// answer means they do not, and the caller makes a union instead.
//
// EVERY ARM HERE IS A CLAIM THAT ONE REPRESENTATION SUBSUMES ANOTHER, and each
// one is written down in CTNativeTypes.td beside the type it is about.
static Type mergeSameFamily(Type a, Type b) {
    if (a == b) { return a; }
    MLIRContext * context = a.getContext();

    if (auto numberA = llvm::dyn_cast<NumType>(a)) {
        auto numberB = llvm::dyn_cast<NumType>(b);
        if (!numberB) { return Type(); }
        return NumType::get(context, widerOf(numberA.getKind(), numberB.getKind()));
    }

    // `str` AND `strview` ARE ONE FAMILY AND THE OWNING ONE WINS. A view can
    // always be widened into an owning string; narrowing the other way needs a
    // lifetime proof, and this phase has no analysis to supply one.
    const auto encodingOf = [](Type type) {
        return llvm::isa<StrType>(type) ? llvm::cast<StrType>(type).getEncoding()
                                        : llvm::cast<StrViewType>(type).getEncoding();
    };
    const bool aIsString = llvm::isa<StrType, StrViewType>(a);
    const bool bIsString = llvm::isa<StrType, StrViewType>(b);
    if (aIsString || bIsString) {
        if (!aIsString || !bIsString) { return Type(); }
        const StrEncoding wider = widerOf(encodingOf(a), encodingOf(b));
        if (llvm::isa<StrType>(a) || llvm::isa<StrType>(b)) {
            return StrType::get(context, wider);
        }
        return StrViewType::get(context, wider);
    }

    if (auto vectorA = llvm::dyn_cast<VecType>(a)) {
        auto vectorB = llvm::dyn_cast<VecType>(b);
        if (!vectorB) { return Type(); }
        return VecType::get(context, meet(vectorA.getElementType(), vectorB.getElementType()));
    }
    if (auto setA = llvm::dyn_cast<SetType>(a)) {
        auto setB = llvm::dyn_cast<SetType>(b);
        if (!setB) { return Type(); }
        return SetType::get(context, meet(setA.getElementType(), setB.getElementType()));
    }
    if (auto mapA = llvm::dyn_cast<MapType>(a)) {
        auto mapB = llvm::dyn_cast<MapType>(b);
        if (!mapB) { return Type(); }
        return MapType::get(context, meet(mapA.getKeyType(), mapB.getKeyType()),
                            meet(mapA.getValueType(), mapB.getValueType()));
    }

    // THE ONLY ORDERING AMONG THE THREE POINTERS IS `owned` INTO `shared`.
    // A unique owner can always be made shared; a shared one cannot be made
    // unique without proving no other reference exists, which is exactly what
    // the analysis failed to prove if it produced `shared`. `weak` is
    // deliberately unordered against both - see CTNative_WeakType, and Stage
    // 55C, where silently unifying a strong and a weak reference is the
    // miscompile the whole rule exists to prevent.
    if (auto ownedA = llvm::dyn_cast<OwnedType>(a)) {
        if (auto ownedB = llvm::dyn_cast<OwnedType>(b)) {
            return OwnedType::get(context,
                                  meet(ownedA.getPointeeType(), ownedB.getPointeeType()));
        }
        if (auto sharedB = llvm::dyn_cast<SharedType>(b)) {
            return SharedType::get(context,
                                   meet(ownedA.getPointeeType(), sharedB.getPointeeType()));
        }
        return Type();
    }
    if (auto sharedA = llvm::dyn_cast<SharedType>(a)) {
        if (auto ownedB = llvm::dyn_cast<OwnedType>(b)) {
            return SharedType::get(context,
                                   meet(sharedA.getPointeeType(), ownedB.getPointeeType()));
        }
        if (auto sharedB = llvm::dyn_cast<SharedType>(b)) {
            return SharedType::get(context,
                                   meet(sharedA.getPointeeType(), sharedB.getPointeeType()));
        }
        return Type();
    }
    if (auto weakA = llvm::dyn_cast<WeakType>(a)) {
        auto weakB = llvm::dyn_cast<WeakType>(b);
        if (!weakB) { return Type(); }
        return WeakType::get(context, meet(weakA.getPointeeType(), weakB.getPointeeType()));
    }

    return Type();
}

Type meet(Type a, Type b) {
    // A null Type is ABSENT, not `bottom`. It is what a caller folding over an
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

bool isNativeType(Type type) {
    return type && llvm::isa<CTNativeDialect>(&type.getDialect());
}

} // namespace ctcompile::ctnative
