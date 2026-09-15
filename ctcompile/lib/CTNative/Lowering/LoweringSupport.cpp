#include "LoweringSupport.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {

// `+x` IS `x` ONCE ADMISSION HAS PROVED x A NUMBER, and that is where the
// soundness lives, not in the match: `+x` in general is ToNumber, which can
// re-enter user code through valueOf and can produce a different value ("1"
// becomes 1). It is the identity only on a value already proved numeric, which
// admission::op()'s Plus arm establishes for every function lowering::lower()
// is called on - applyDeclarativeRules() seeds the driver with exactly those
// unaries. The rewrite is type-preserving (`!ctjs.value` for `!ctjs.value`),
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
    if (llvm::isa<DOMElementType>(type)) { return carrier::domElement; }
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
        if (carrierOf(opt.getElementType()) == carrier::string) { return carrier::nullableString; }
        if (llvm::isa<BottomType>(opt.getElementType()) ||
            isScalarCarrier(carrierOf(opt.getElementType()))) {
            return carrier::nullable;
        }
    }
    // Numeric/Boolean unions use the same tags as an optional scalar. The
    // lattice retains its exact alternatives: selecting this representation
    // neither adds nullability nor admits objects or unknown values. A closed
    // Boolean/String temporary has its own finite, owning variant carrier.
    if (auto variant = llvm::dyn_cast<VariantType>(type)) {
        if (variant.getAlternatives().size() == 2 &&
            llvm::any_of(variant.getAlternatives(),
                         [](mlir::Type alternative) { return llvm::isa<BoolType>(alternative); }) &&
            llvm::any_of(variant.getAlternatives(), [](mlir::Type alternative) {
                auto string = llvm::dyn_cast<StrType>(alternative);
                return string && string.getEncoding() == StrEncoding::UTF8;
            })) {
            return carrier::booleanString;
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
    // PHASE 57A: A DENSE ARRAY IS A `std::vector<double>` AND NOTHING ELSE
    // YET. The element carrier decides: `vector<bool>` is a bit-packed
    // specialisation whose `operator[]` returns a proxy that aliases the
    // container and converts differently from `bool` (part 24 Stage 57A says
    // so by name), and a vector of anything with no carrier has none either.
    // So only a numeric element has a representation here, and the refusal
    // for the rest is named at the literal.
    if (auto elements = llvm::dyn_cast<VecType>(type)) {
        auto element = elements.getElementType();
        if (carrierOf(element) == carrier::string) { return carrier::stringVector; }
        if (auto opt = llvm::dyn_cast<OptType>(element)) { element = opt.getElementType(); }
        return llvm::isa<BottomType, NumType>(element) ? carrier::vector : carrier::none;
    }
    return carrier::none;
}

bool isScalarCarrier(carrier value) {
    return value == carrier::number || value == carrier::boolean || value == carrier::nullable;
}

bool isNullableCarrier(mlir::Type type) {
    if (!type) { return false; }
    if (auto value = llvm::dyn_cast<ec::LValueType>(type)) { type = value.getValueType(); }
    auto opaque = llvm::dyn_cast<ec::OpaqueType>(type);
    return opaque && opaque.getValue() == kNullableType;
}

bool isBooleanStringCarrier(mlir::Type type) {
    if (auto value = llvm::dyn_cast_if_present<ec::LValueType>(type)) {
        type = value.getValueType();
    }
    auto opaque = llvm::dyn_cast_if_present<ec::OpaqueType>(type);
    return opaque && opaque.getValue() == kBooleanStringType;
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
    if (!variant || variant.getAlternatives().size() != 2) { return {}; }
    bool boolean = false, number = false, string = false;
    for (mlir::Type alternative : variant.getAlternatives()) {
        boolean |= llvm::isa<BoolType>(alternative);
        number |= llvm::isa<NumType>(alternative);
        string |= carrierOf(alternative) == carrier::string;
    }
    if (boolean && number) { return "std::variant<bool, double>"; }
    if (boolean && string) { return "std::variant<bool, std::string>"; }
    return {};
}

llvm::StringRef mixedMapKeySpelling(mlir::Type type) {
    if (auto scalar = mixedMapSpelling(type); !scalar.empty()) { return scalar; }
    auto variant = llvm::dyn_cast<VariantType>(type);
    if (!variant || variant.getAlternatives().size() != 2) { return {}; }
    bool number = false, object = false;
    for (mlir::Type alternative : variant.getAlternatives()) {
        number |= llvm::isa<NumType>(alternative);
        object |= llvm::isa<ObjectIdentityType>(alternative);
    }
    // Keys own exact identities; object/scalar payloads retain object_value storage.
    return number && object ? "std::variant<double, std::shared_ptr<ctnative::identity_object>>"
                            : llvm::StringRef{};
}

// Closed owning storage for nullable String, optionally composed with Bool.
// This does not add a general optional-union scalar/signature or snapshot.
llvm::StringRef nullableMapSpelling(mlir::Type type) {
    auto optional = llvm::dyn_cast<OptType>(type);
    if (!optional) { return {}; }
    if (carrierOf(optional.getElementType()) == carrier::string) { return kNullableStringType; }
    if (mixedMapSpelling(optional.getElementType()) == kBooleanStringType) {
        return "std::variant<bool, ctnative::nullable_string>";
    }
    return {};
}

llvm::StringRef mapKeySpelling(mlir::Type type) {
    if (auto nullable = nullableMapSpelling(type); !nullable.empty()) { return nullable; }
    if (auto mixed = mixedMapKeySpelling(type); !mixed.empty()) { return mixed; }
    if (llvm::isa<BottomType, NumType>(type)) { return "double"; }
    if (llvm::isa<BoolType>(type)) { return "bool"; }
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
    if (llvm::isa<BoolType>(type)) { return "bool"; }
    if (carrierOf(type) == carrier::string) { return "std::string"; }
    if (auto map = llvm::dyn_cast<MapType>(type)) {
        return llvm::cast<ec::OpaqueType>(mapCarrierType(map)).getValue().str();
    }
    llvm::report_fatal_error("native Map value has no carrier; admission should refuse it");
}

bool mapNeedsString(MapType type) {
    return llvm::isa<StrType>(type.getKeyType()) ||
           !nullableMapSpelling(type.getKeyType()).empty() ||
           llvm::isa<StrType>(type.getValueType()) ||
           !nullableMapSpelling(type.getValueType()).empty() ||
           (llvm::isa<MapType>(type.getValueType()) &&
            mapNeedsString(llvm::cast<MapType>(type.getValueType())));
}

bool mapNeedsNullableString(MapType type) {
    return !nullableMapSpelling(type.getKeyType()).empty() ||
           !nullableMapSpelling(type.getValueType()).empty() ||
           (llvm::isa<MapType>(type.getValueType()) &&
            mapNeedsNullableString(llvm::cast<MapType>(type.getValueType())));
}

bool mapNeedsNullableStringKey(MapType type) {
    return !nullableMapSpelling(type.getKeyType()).empty() ||
           (llvm::isa<MapType>(type.getValueType()) &&
            mapNeedsNullableStringKey(llvm::cast<MapType>(type.getValueType())));
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
    return ec::OpaqueType::get(type.getContext(),
                               "ctnative::ctn_env_" + cIdentifier(type.getTarget()));
}

mlir::Type methodTableCarrierType(MethodTableType type) {
    return ec::OpaqueType::get(type.getContext(), "std::shared_ptr<ctnative::method_" +
                                                      cIdentifier(type.getSite()) + ">");
}

mlir::Type carrierType(mlir::MLIRContext * c, carrier which) {
    // `none` HAS NO REPRESENTATION, and returning f64 for it was a silent
    // guess at the one thing this tier exists not to guess at. A value with no
    // proved carrier must be refused by admission long before it gets here;
    // reaching this point means a rule let one through, and a crash naming
    // that is worth far more than a double that happens to verify.
    switch (which) {
    case carrier::domElement: return ec::OpaqueType::get(c, kDOMElementType);
    case carrier::nullable: return ec::OpaqueType::get(c, kNullableType);
    case carrier::booleanString: return ec::OpaqueType::get(c, kBooleanStringType);
    case carrier::nullableString: return ec::OpaqueType::get(c, kNullableStringType);
    case carrier::objectValue: return ec::OpaqueType::get(c, kObjectValueType);
    case carrier::objectIdentity: return ec::OpaqueType::get(c, kObjectIdentityType);
    case carrier::methodTable:
        llvm::report_fatal_error("method table carrier needs its proved schema");
    case carrier::boolean: return mlir::IntegerType::get(c, 1);
    case carrier::number: return mlir::Float64Type::get(c);
    case carrier::string:
        return ec::OpaqueType::get(c, StrType::get(c, StrEncoding::UTF8).cppCarrier());
    case carrier::structure:
    case carrier::closure:
    case carrier::map:
    case carrier::vector:
    case carrier::stringVector:
    case carrier::none: break;
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

// The function index the importer put after the last `$` of the symbol. The
// same reading ResolveGlobals does, and the only link there is between a
// `ctjs.create_closure`'s `$function` attribute and the `ctjs.func` it names.
std::optional<unsigned> functionIndexOf(ctjs::FuncOp fn) {
    const llvm::StringRef name = fn.getSymName();
    const std::size_t dollar = name.rfind('$');
    if (dollar == llvm::StringRef::npos) { return std::nullopt; }
    unsigned index = 0;
    if (name.substr(dollar + 1).getAsInteger(10, index)) { return std::nullopt; }
    return index;
}

bool isScriptEntry(ctjs::FuncOp fn) {
    return fn.getSymName() == "_script_$0";
}

bool isUndefinedConstant(mlir::Value v) {
    auto k = v.getDefiningOp<ctjs::ConstantOp>();
    return k && llvm::isa<ctjs::UndefinedAttr>(k.getValue());
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
// JavaScript site. The importer fuses a NameLoc with the FileLineColLoc of
// the site; the first file location found, at any depth, is the site.
std::string siteOf(mlir::Location loc) {
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
std::string siteOfFunction(ctjs::FuncOp fn) {
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

// BUILT ONCE PER LOWERING, not per function: FrozenRewritePatternSet is what
// both greedy entry points take.
mlir::FrozenRewritePatternSet declarativePatterns(mlir::MLIRContext * context) {
    mlir::RewritePatternSet patterns(context);
    patterns.add<UnaryPlusIsIdentity>(context);
    return patterns;
}

} // namespace ctcompile::ctnative::lowering_detail
