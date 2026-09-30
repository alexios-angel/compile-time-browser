#include "LoweringSupport.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/Support/CppLiterals.hpp"
#include "ctcompile/Support/SourceSite.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {

// `+x` IS `x` ONCE ADMISSION HAS PROVED x A NUMBER, and that is where the
// soundness lives, not in the match: `+x` in general is ToNumber, which can
// re-enter user code through valueOf and can produce a different value ("1"
// becomes 1). It is the identity only on a value already proved numeric, which
// applyDeclarativeRules() checks the operand's NumType before seeding the
// driver. Admitted Boolean, nullable and String operands retain their explicit
// conversions. The rewrite is type-preserving (`!ctjs.value` for `!ctjs.value`),
// which is why it must run before retype(). If it ever stops firing,
// replace()'s Plus arm reports a fatal error naming it.
struct UnaryPlusIsIdentity : mlir::OpRewritePattern<ctjs::UnaryOp> {
    using OpRewritePattern::OpRewritePattern;

    mlir::LogicalResult matchAndRewrite(ctjs::UnaryOp o,
                                        mlir::PatternRewriter & rewriter) const override {
        if (o.getKind() != ctjs::UnaryKind::Plus) { return mlir::failure(); }
        rewriter.replaceOp(o, o.getOperand());
        return mlir::success();
    }
};

// Whether a Variant's `alternatives` are exactly two, one satisfying each predicate.
template <class First, class Second>
bool pairAlternatives(llvm::ArrayRef<mlir::Type> alternatives, First && first, Second && second) {
    return alternatives.size() == 2 && llvm::any_of(alternatives, first) &&
           llvm::any_of(alternatives, second);
}

template <class Kind> bool is(mlir::Type type) {
    return llvm::isa<Kind>(type);
}

// Whether `level` holds for this Map or for any Map nested as its values.
template <class Level> bool anyMapLevel(MapType type, Level && level) {
    for (; type; type = llvm::dyn_cast<MapType>(type.getValueType())) {
        if (level(type)) { return true; }
    }
    return false;
}

} // namespace

mlir::Type scalarObservationType(mlir::MLIRContext * context, PrimitiveAlternatives alternatives) {
    using Primitive = PrimitiveAlternatives;
    const unsigned mask = alternatives.truthy | alternatives.falsy;
    constexpr unsigned supported =
        Primitive::Boolean | Primitive::Number | Primitive::Null | Primitive::Undefined;
    if (!alternatives.known || !mask || (mask & ~supported)) { return {}; }
    mlir::Type type = BottomType::get(context);
    if (mask & Primitive::Boolean) { type = meet(type, BoolType::get(context)); }
    if (mask & Primitive::Number) { type = meet(type, NumType::get(context, NumKind::F64)); }
    if (mask & (Primitive::Null | Primitive::Undefined)) { type = OptType::get(context, type); }
    return type;
}

// What C++ type carries a value of this ctnative type, per the table above.
// `none` is "not representable here", and is the reason for a refusal.
carrier carrierOf(mlir::Type type) {
    if (type == nullptr) { return carrier::none; }
    if (llvm::isa<BoolType>(type)) { return carrier::boolean; }
    if (llvm::isa<SymbolType>(type)) { return carrier::symbol; }
    if (llvm::isa<DOMElementType>(type)) { return carrier::domElement; }
    if (llvm::isa<JsonType>(type)) { return carrier::json; }
    if (llvm::isa<NumType>(type)) { return carrier::number; }
    if (llvm::isa<ClosureType>(type)) { return carrier::closure; }
    if (llvm::isa<MethodTableType>(type)) { return carrier::methodTable; }
    if (llvm::isa<ObjectIdentityType>(type)) { return carrier::objectIdentity; }
    if (isObjectValueType(type)) { return carrier::objectValue; }
    if (auto string = llvm::dyn_cast<StrType>(type);
        string && string.getEncoding() == StrEncoding::UTF8) {
        return carrier::string;
    }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        const carrier inner = carrierOf(opt.getElementType());
        if (inner == carrier::booleanString) { return carrier::nullableBooleanString; }
        if (inner == carrier::numberString) { return carrier::nullableNumberString; }
        if (inner == carrier::string) { return carrier::nullableString; }
        if (llvm::isa<BottomType>(opt.getElementType()) || isScalarCarrier(inner)) {
            return carrier::nullable;
        }
    }
    // Numeric/Boolean unions use the same tags as an optional scalar. The
    // lattice retains its exact alternatives: selecting this representation
    // neither adds nullability nor admits objects or unknown values. A closed
    // Boolean/String temporary has its own finite, owning variant carrier.
    if (auto variant = llvm::dyn_cast<VariantType>(type)) {
        if (pairAlternatives(variant.getAlternatives(), is<BoolType>, [](mlir::Type alternative) {
                auto string = llvm::dyn_cast<StrType>(alternative);
                return string && string.getEncoding() == StrEncoding::UTF8;
            })) {
            return carrier::booleanString;
        }
        if (pairAlternatives(variant.getAlternatives(), is<NumType>, [](mlir::Type alternative) {
                return carrierOf(alternative) == carrier::string;
            })) {
            return carrier::numberString;
        }
        if (llvm::all_of(variant.getAlternatives(), [](mlir::Type alternative) {
                return llvm::isa<NumType, BoolType>(alternative);
            })) {
            return carrier::nullable;
        }
    }
    if (auto map = llvm::dyn_cast<MapType>(type)) {
        const auto key = map.getKeyType();
        const auto string = llvm::dyn_cast<StrType>(key);
        const bool supportedKey =
            llvm::isa<BottomType, NumType, BoolType, ObjectIdentityType, DOMElementType>(key) ||
            (string && string.getEncoding() == StrEncoding::UTF8) ||
            (llvm::isa<OptType>(key) && carrierOf(key) == carrier::nullable) ||
            !mixedMapKeySpelling(key).empty() || !nullableMapSpelling(key).empty();
        const auto value = map.getValueType();
        const bool ownedValue =
            llvm::isa<BottomType, NumType, BoolType>(value) ||
            (llvm::isa<StrType>(value) && carrierOf(value) == carrier::string) ||
            isObjectValueType(value) || !mixedMapSpelling(value).empty() ||
            !nullableMapSpelling(value).empty() ||
            (llvm::isa<MapType>(value) && carrierOf(value) == carrier::map);
        return supportedKey && ownedValue ? carrier::map : carrier::none;
    }
    // Generic mutable arrays retain their numeric content proof. Proved
    // String and borrowed Element snapshots have
    // distinct carriers and their own access/ownership admission.
    if (auto elements = llvm::dyn_cast<VecType>(type)) {
        auto element = elements.getElementType();
        if (llvm::isa<DOMElementType>(element)) { return carrier::domElementVector; }
        if (carrierOf(element) == carrier::string) { return carrier::stringVector; }
        if (auto opt = llvm::dyn_cast<OptType>(element)) { element = opt.getElementType(); }
        return llvm::isa<BottomType, NumType>(element) ? carrier::vector : carrier::none;
    }
    return carrier::none;
}

bool isScalarCarrier(carrier value) {
    return value == carrier::number || value == carrier::boolean || value == carrier::nullable;
}

bool isStringUnionCarrier(carrier value) {
    return carrierInfo(value).stringUnion;
}

bool isPrimitiveCarrier(carrier value) {
    return isScalarCarrier(value) || isStringCarrier(value) || isStringUnionCarrier(value);
}

bool isCallableCarrier(carrier value) {
    return isScalarCarrier(value) || isStringCarrier(value) || isObjectCarrier(value) ||
           value == carrier::domElement || value == carrier::map;
}

llvm::StringRef opaqueSpelling(mlir::Type type) {
    if (auto value = llvm::dyn_cast_if_present<ec::LValueType>(type)) {
        type = value.getValueType();
    }
    auto opaque = llvm::dyn_cast_if_present<ec::OpaqueType>(type);
    return opaque ? opaque.getValue() : llvm::StringRef{};
}

// The one C++ type a dense array lowers to. Spelled once: the emitted
// declaration, the helper signatures and the lit test all have to agree, and
// three copies of a string is how they stop agreeing.
mlir::Type vectorCarrierType(mlir::MLIRContext * c, bool strings) {
    return ec::LValueType::get(ec::OpaqueType::get(c, strings ? kStringVectorType : kVectorType));
}

// These are exact closed storage alternatives, not a general JS value.
// Scalar call operands and independently typed reads remain separate gates.
llvm::StringRef mixedMapSpelling(mlir::Type type) {
    auto variant = llvm::dyn_cast<VariantType>(type);
    if (!variant) { return {}; }
    const auto alternatives = variant.getAlternatives();
    if (pairAlternatives(alternatives, is<BoolType>, is<NumType>)) {
        return "std::variant<ctnative::js_boolean_t, double>";
    }
    if (pairAlternatives(alternatives, is<BoolType>, [](mlir::Type alternative) {
            return carrierOf(alternative) == carrier::string;
        })) {
        return kBooleanStringMapType;
    }
    return {};
}

llvm::StringRef mixedMapKeySpelling(mlir::Type type) {
    if (auto scalar = mixedMapSpelling(type); !scalar.empty()) { return scalar; }
    auto variant = llvm::dyn_cast<VariantType>(type);
    // Keys own exact identities; object/scalar payloads retain object_value storage.
    return variant &&
                   pairAlternatives(variant.getAlternatives(), is<NumType>, is<ObjectIdentityType>)
               ? "std::variant<double, std::shared_ptr<ctnative::identity_object>>"
               : llvm::StringRef{};
}

// Closed tagged storage for optional scalars and owning nullable Strings.
// Map storage remains independent of scalar/signature and snapshot carriers.
llvm::StringRef nullableMapSpelling(mlir::Type type) {
    auto optional = llvm::dyn_cast<OptType>(type);
    if (!optional) { return {}; }
    if (carrierOf(type) == carrier::nullable) { return kNullableType; }
    if (carrierOf(optional.getElementType()) == carrier::string) { return kNullableStringType; }
    if (mixedMapSpelling(optional.getElementType()) == kBooleanStringMapType) {
        return "std::variant<ctnative::js_boolean_t, ctnative::nullable_string>";
    }
    return {};
}

llvm::StringRef mapKeySpelling(mlir::Type type) {
    if (llvm::isa<OptType>(type) && carrierOf(type) == carrier::nullable) { return kNullableType; }
    if (auto nullable = nullableMapSpelling(type); !nullable.empty()) { return nullable; }
    if (auto mixed = mixedMapKeySpelling(type); !mixed.empty()) { return mixed; }
    if (llvm::isa<BottomType, NumType>(type)) { return "double"; }
    if (llvm::isa<BoolType>(type)) { return kBooleanType; }
    if (llvm::isa<StrType>(type)) { return "std::string"; }
    if (llvm::isa<ObjectIdentityType>(type)) { return kObjectIdentityType; }
    if (llvm::isa<DOMElementType>(type)) { return kDOMElementType; }
    llvm::report_fatal_error("native Map key has no carrier; admission should refuse it");
}

std::string mapValueSpelling(mlir::Type type) {
    if (auto nullable = nullableMapSpelling(type); !nullable.empty()) { return nullable.str(); }
    if (auto mixed = mixedMapSpelling(type); !mixed.empty()) { return mixed.str(); }
    if (isObjectValueType(type)) { return kObjectValueType.str(); }
    if (llvm::isa<BottomType, NumType>(type)) { return "double"; }
    if (llvm::isa<BoolType>(type)) { return kBooleanType.str(); }
    if (carrierOf(type) == carrier::string) { return "std::string"; }
    if (auto map = llvm::dyn_cast<MapType>(type)) {
        return llvm::cast<ec::OpaqueType>(mapCarrierType(map)).getValue().str();
    }
    llvm::report_fatal_error("native Map value has no carrier; admission should refuse it");
}

bool mapNeedsString(MapType type) {
    return anyMapLevel(type, [](MapType level) {
        return llvm::isa<StrType>(level.getKeyType()) ||
               !nullableMapSpelling(level.getKeyType()).empty() ||
               llvm::isa<StrType>(level.getValueType()) ||
               !nullableMapSpelling(level.getValueType()).empty();
    });
}

bool mapNeedsNullableString(MapType type) {
    return anyMapLevel(type, [](MapType level) {
        return !nullableMapSpelling(level.getKeyType()).empty() ||
               !nullableMapSpelling(level.getValueType()).empty();
    });
}

bool mapNeedsNullableStringKey(MapType type) {
    return anyMapLevel(
        type, [](MapType level) { return !nullableMapSpelling(level.getKeyType()).empty(); });
}

mlir::Type mapCarrierType(MapType type) {
    const auto key = mapKeySpelling(type.getKeyType());
    const auto value = type.getValueType();
    const std::string body =
        (llvm::isa<MapType, BoolType, StrType>(value) || isObjectValueType(value) ||
         !mixedMapSpelling(value).empty() || !nullableMapSpelling(value).empty())
            ? ("ctnative::map_storage<" + key + ", " + mapValueSpelling(value) + ">").str()
        : llvm::isa<StrType>(type.getKeyType()) ? std::string{"ctnative::string_to_number_map"}
                                                : ("ctnative::number_map<" + key + ">").str();
    return ec::OpaqueType::get(type.getContext(), "std::shared_ptr<" + body + ">");
}

mlir::Type closureCarrierType(ClosureType type) {
    return ec::OpaqueType::get(type.getContext(), "ctnative::" + environmentType(type.getTarget()));
}

mlir::Type methodTableCarrierType(MethodTableType type) {
    return ec::OpaqueType::get(
        type.getContext(), "std::shared_ptr<ctnative::" + methodTableClass(type.getSite()) + ">");
}

mlir::Type carrierType(mlir::MLIRContext * c, carrier which) {
    // `none` HAS NO REPRESENTATION, and returning f64 for it was a silent
    // guess at the one thing this tier exists not to guess at. A value with no
    // proved carrier must be refused by admission long before it gets here;
    // reaching this point means a rule let one through, and a crash naming
    // that is worth far more than a double that happens to verify.
    if (which == carrier::methodTable) {
        llvm::report_fatal_error("method table carrier needs its proved schema");
    }
    if (const auto spelling = carrierInfo(which).spelling; !spelling.empty()) {
        return ec::OpaqueType::get(c, spelling);
    }
    llvm::report_fatal_error("ctnative lowering: asked for the C++ carrier of a value that has "
                             "none - admission should have refused it");
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

bool isScriptEntry(ctjs::FuncOp fn) {
    return fn.getSymName() == "_script_$0";
}

bool isDeclarationClosure(mlir::Operation * o) {
    auto closure = llvm::dyn_cast_or_null<ctjs::CreateClosureOp>(o);
    if (!closure || !closure.getResult().hasOneUse()) { return false; }
    return llvm::isa<ctjs::StoreGlobalOp>(*closure.getResult().getUsers().begin());
}

bool domBookkeeping(const DOMEntryAnalysis & entry, mlir::Operation * op) {
    if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
        return static_cast<bool>(entry.callback(closure));
    }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
        return entry.isInitialIntrinsic(load) || entry.isCurrentDocument(load);
    }
    if (auto call = llvm::dyn_cast<ctjs::CallOp>(op)) { return entry.isStringPrefixRegExp(call); }
    auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
    return read && (entry.method(read) || entry.isElementPrototype(read) ||
                    entry.isTokenList(read.getResult()) || entry.isDataset(read.getResult()));
}

// Every value that names the same object as `v`, `v` included. A literal
// nothing is lifted onto is a group of one, so callers need no special case.
llvm::SmallVector<mlir::Value, 2> aliasesOf(const receiverGroups * groups, mlir::Value v) {
    if (groups != nullptr) {
        const auto entry = groups->find(v);
        if (entry != groups->end()) { return entry->second; }
    }
    return {v};
}

// PART 24 PHASE 63 STEP 7: a provenance comment above every generated
// definition, so a C++ diagnostic on generated code maps back to the
// JavaScript site (ctcompile::sourceSite).
static std::string spelledSite(mlir::FileLineColLoc file) {
    return file ? file.getFilename().str() + ":" + std::to_string(file.getLine()) + ":" +
                      std::to_string(file.getColumn())
                : std::string{"<no source location>"};
}
std::string siteOf(mlir::Location loc) {
    return spelledSite(sourceSite(loc));
}
// The importer gives a ctjs.func no location of its own; its first located
// operation is the function's site.
std::string siteOfFunction(ctjs::FuncOp fn) {
    auto site = sourceSite(fn.getLoc());
    if (!site) {
        fn.getBody().walk([&](mlir::Operation * o) {
            site = sourceSite(o->getLoc());
            return site ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
        });
    }
    return spelledSite(site);
}

std::string cIdentifier(llvm::StringRef symbol) {
    return cpp::identifier(symbol);
}

// BUILT ONCE PER LOWERING, not per function: FrozenRewritePatternSet is what
// both greedy entry points take.
mlir::FrozenRewritePatternSet declarativePatterns(mlir::MLIRContext * context) {
    mlir::RewritePatternSet patterns(context);
    patterns.add<UnaryPlusIsIdentity>(context);
    return patterns;
}

} // namespace ctcompile::ctnative::lowering_detail
