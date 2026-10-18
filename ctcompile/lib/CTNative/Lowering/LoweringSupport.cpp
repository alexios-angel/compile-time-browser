//===- LoweringSupport.cpp - Carrier selection and lowering helpers -------===//
//
// Shared helpers for the native EmitC lowering: which C++ carrier holds a value
// of each ctnative type (carrierOf), the C++ spellings of Map, closure and
// method-table storage, source-site strings for provenance comments, and the
// declarative rewrite patterns applied before retyping.
//
//===----------------------------------------------------------------------===//

#include "LoweringSupport.h"
#include "Admission/Admission.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/Support/CppLiterals.hpp"
#include "ctcompile/Support/SourceSite.h"

namespace ctcompile::ctnative::lowering_detail {

namespace {

/// Rewrites `+x` to `x`. This is sound only once admission has proved `x` a
/// number, and the soundness lives there, not in the match: `+x` in general is
/// ToNumber, which can re-enter user code through valueOf and can produce a
/// different value ("1" becomes 1). applyDeclarativeRules() checks the
/// operand's NumType before seeding the driver. Admitted Boolean, nullable and
/// String operands keep their explicit conversions. The rewrite is
/// type-preserving (`!ctjs.value` for `!ctjs.value`), which is why it must run
/// before retype(). If it ever stops firing, the Plus arm of
/// lowering::replace() reports a fatal error naming it.
struct UnaryPlusIsIdentity : mlir::OpRewritePattern<ctjs::UnaryOp> {
    using OpRewritePattern::OpRewritePattern;

    mlir::LogicalResult matchAndRewrite(ctjs::UnaryOp o,
                                        mlir::PatternRewriter & rewriter) const override {
        if (o.getKind() != ctjs::UnaryKind::Plus) { return mlir::failure(); }
        rewriter.replaceOp(o, o.getOperand());
        return mlir::success();
    }
};

/// Whether a Variant's `alternatives` are exactly two, one satisfying each
/// predicate.
template <class First, class Second>
bool pairAlternatives(llvm::ArrayRef<mlir::Type> alternatives, First && first, Second && second) {
    return alternatives.size() == 2 && llvm::any_of(alternatives, first) &&
           llvm::any_of(alternatives, second);
}

template <class Kind> bool is(mlir::Type type) {
    return llvm::isa<Kind>(type);
}

/// Whether `level` holds for this Map or for any Map nested as its values.
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

/// Returns which C++ carrier holds a value of this ctnative type (see kCarriers
/// in LoweringSupport.h). `carrier::none` means the type is not representable
/// natively, and the value must be refused.
carrier carrierOf(mlir::Type type) {
    if (type == nullptr) { return carrier::none; }
    if (llvm::isa<BoolType>(type)) { return carrier::boolean; }
    if (llvm::isa<SymbolType>(type)) { return carrier::symbol; }
    if (llvm::isa<DOMElementType>(type)) { return carrier::domElement; }
    if (llvm::isa<DOMRectType>(type)) { return carrier::domRect; }
    if (llvm::isa<ComputedStyleType>(type)) { return carrier::computedStyle; }
    // A promise, or its resolve function, of a scalar value
    // (Runtime/Promise.hpp); any other value is refused.
    if (llvm::isa<PromiseType, PromiseResolveType>(type)) {
        const mlir::Type value = llvm::isa<PromiseType>(type)
                                     ? llvm::cast<PromiseType>(type).getElementType()
                                     : llvm::cast<PromiseResolveType>(type).getElementType();
        if (!isScalarPromiseValue(carrierOf(value))) { return carrier::none; }
        return llvm::isa<PromiseType>(type) ? carrier::promise : carrier::promiseResolve;
    }
    if (llvm::isa<EventType>(type)) { return carrier::domEvent; }
    if (llvm::isa<TimerType>(type)) { return carrier::timer; }
    if (llvm::isa<ClassTagType>(type)) { return carrier::classTag; }
    if (llvm::isa<JsonType>(type)) { return carrier::json; }
    if (llvm::isa<NumType>(type)) { return carrier::number; }
    if (llvm::isa<ClosureType>(type)) { return carrier::closure; }
    if (llvm::isa<ClosureSetType>(type)) { return carrier::closureSet; }
    if (llvm::isa<MethodTableType>(type)) { return carrier::methodTable; }
    if (llvm::isa<ObjectIdentityType>(type)) { return carrier::objectIdentity; }
    if (isObjectValueType(type)) { return carrier::objectValue; }
    // The table, or undefined before the run-once function stores it: one
    // pointer holds both.
    if (llvm::isa<StaticTableType>(type)) { return carrier::staticTable; }
    if (auto opt = llvm::dyn_cast<OptType>(type);
        opt && llvm::isa<StaticTableType>(opt.getElementType())) {
        return carrier::staticTable;
    }
    // An identity class's instance, or neither one: a null shared_ptr holds
    // both null and undefined, so admission refuses where they differ.
    if (llvm::isa<InstanceType>(type)) { return carrier::instance; }
    if (auto opt = llvm::dyn_cast<OptType>(type);
        opt && llvm::isa<InstanceType>(opt.getElementType())) {
        return carrier::instance;
    }
    if (auto string = llvm::dyn_cast<StrType>(type);
        string && string.getEncoding() == StrEncoding::UTF8) {
        return carrier::string;
    }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        const carrier inner = carrierOf(opt.getElementType());
        if (inner == carrier::booleanString) { return carrier::nullableBooleanString; }
        if (inner == carrier::numberString) { return carrier::nullableNumberString; }
        if (inner == carrier::string) { return carrier::nullableString; }
        if (inner == carrier::domElement) { return carrier::nullableElement; }
        // A timer's handle that may be null or undefined: the empty
        // ctnative::timer_id, which names no timer.
        if (inner == carrier::timer) { return carrier::timer; }
        // A possibly absent member of a closure set: std::optional of its
        // struct (EmitC/ClosureSets.cpp).
        if (inner == carrier::closureSet) { return carrier::closureSet; }
        if (llvm::isa<BottomType>(opt.getElementType()) || isScalarCarrier(inner)) {
            return carrier::nullable;
        }
    }
    // Number/Boolean unions use the same tags as an optional scalar. The
    // lattice keeps its exact alternatives: choosing this representation
    // neither adds nullability nor admits objects or unknown values. A
    // Boolean|String or Number|String value has its own owning String union.
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
    // A table holds one value carrier (Admission/Dictionaries.cpp says which),
    // or tables. A table that may be undefined is the same pointer, null.
    if (auto optional = llvm::dyn_cast<OptType>(type);
        optional && llvm::isa<DictType>(optional.getElementType())) {
        return carrierOf(optional.getElementType());
    }
    if (auto table = llvm::dyn_cast<DictType>(type)) {
        const carrier value = carrierOf(table.getElementType());
        return value == carrier::dictionary || !carrierInfo(value).spelling.empty()
                   ? carrier::dictionary
                   : carrier::none;
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
            (llvm::isa<MapType>(value) && carrierOf(value) == carrier::map) ||
            llvm::isa<InstanceType>(value);
        return supportedKey && ownedValue ? carrier::map : carrier::none;
    }
    // Generic mutable arrays keep their numeric-content proof. Arrays of
    // strings and of borrowed DOM elements have their own carriers and
    // admission rules.
    // A tuple has a carrier when it has two positions and each is a String, a
    // Number, a Number|String or an ordinary script's element (admission
    // refuses one under a host contract), which a std::pair of them can hold.
    if (auto tuple = llvm::dyn_cast<TupleType>(type)) {
        const auto position = [](mlir::Type part) {
            const carrier c = carrierOf(part);
            return c == carrier::string || c == carrier::number || c == carrier::numberString ||
                   c == carrier::domElement;
        };
        return tuple.getElements().size() == 2 && llvm::all_of(tuple.getElements(), position)
                   ? carrier::tuple
                   : carrier::none;
    }
    if (auto elements = llvm::dyn_cast<VecType>(type)) {
        auto element = elements.getElementType();
        if (carrierOf(element) == carrier::tuple) { return carrier::tupleVector; }
        if (llvm::isa<DOMElementType>(element)) { return carrier::domElementVector; }
        if (carrierOf(element) == carrier::string) { return carrier::stringVector; }
        if (auto opt = llvm::dyn_cast<OptType>(element)) { element = opt.getElementType(); }
        return llvm::isa<BottomType, NumType>(element) ? carrier::vector : carrier::none;
    }
    return carrier::none;
}

bool isElementArray(mlir::Value v, mlir::Type type, bool pageViews) {
    const carrier c = carrierOf(type);
    if (c == carrier::vector) {
        auto vector = llvm::dyn_cast<VecType>(type);
        return vector && llvm::isa<NumType>(vector.getElementType()) &&
               TypeInference::isNumberElementVector(v);
    }
    return (c == carrier::stringVector || c == carrier::tupleVector ||
            (pageViews && c == carrier::domElementVector)) &&
           !TypeInference::elementVectorAliases(v).empty();
}

std::string elementSpelling(mlir::Type type) {
    switch (carrierOf(type)) {
    case carrier::string: return kStringType.str();
    case carrier::number: return kNumberType.str();
    case carrier::numberString: return kNumberStringType.str();
    // An ordinary script's element is the runtime's view (Runtime/Browser.hpp).
    case carrier::domElement: return kPageElementType.str();
    case carrier::tuple: {
        auto tuple = llvm::cast<TupleType>(type);
        return "std::pair<" + elementSpelling(tuple.getElements()[0]) + ", " +
               elementSpelling(tuple.getElements()[1]) + ">";
    }
    default: return {};
    }
}

std::string elementArraySpelling(mlir::Type type) {
    auto vector = llvm::dyn_cast_or_null<VecType>(type);
    return vector ? "std::vector<" + elementSpelling(vector.getElementType()) + ">" : std::string{};
}

std::string elementSpanSpelling(mlir::Type type) {
    auto vector = llvm::dyn_cast_or_null<VecType>(type);
    return vector ? "std::span<const " + elementSpelling(vector.getElementType()) + ">"
                  : std::string{};
}

bool isPageGlobal(mlir::Operation * o) {
    auto load = llvm::dyn_cast_or_null<ctjs::LoadGlobalOp>(o);
    if (!load || load.getResult().use_empty()) { return false; }
    // setTimeout and setInterval only to call them with a callback closure
    // lifting specialised, and clearTimeout and clearInterval only to call
    // them (HostCallbacks.cpp): the calls lower to the ctnative functions of
    // the same names.
    if (load.getName() == "setTimeout" || load.getName() == "setInterval") {
        return llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
            return use.getOperandNumber() == 0 &&
                   TypeInference::isHostCallbackCall(use.getOwner()) &&
                   TypeInference::isTimerCall(use.getOwner());
        });
    }
    // Promise only as the callee and new.target of a `new Promise(f)` closure
    // lifting specialised, or to read `Promise.resolve` for a call
    // (Runtime/Promise.hpp).
    if (load.getName() == "Promise") {
        return TypeInference::isRealmGlobal(load, load.getName()) &&
               llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
                   mlir::Operation * user = use.getOwner();
                   if (llvm::isa<ctjs::ConstructOp>(user)) {
                       return use.getOperandNumber() < 2 && TypeInference::isPromiseCall(user);
                   }
                   if (llvm::isa<ctjs::CallOp>(user)) {
                       return use.getOperandNumber() == 1 && TypeInference::isPromiseCall(user);
                   }
                   return isPageMethodRead(user);
               });
    }
    if (load.getName() == "clearTimeout" || load.getName() == "clearInterval") {
        return llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
            return use.getOperandNumber() == 0 && TypeInference::isTimerClear(use.getOwner());
        });
    }
    // The realm's Event only as the constructor and new.target of a
    // `new Event`, which the lowering spells ctnative::Event or admission
    // refuses with its own reason (HostCallbacks.cpp).
    if (load.getName() == "Event") {
        return TypeInference::isRealmGlobal(load, load.getName()) &&
               llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
                   return use.getOperandNumber() <= 1 &&
                          llvm::isa<ctjs::ConstructOp>(use.getOwner());
               });
    }
    if (load.getName() == "getComputedStyle") {
        return TypeInference::isRealmGlobal(load, load.getName()) &&
               llvm::all_of(load.getResult().getUses(), [](mlir::OpOperand & use) {
                   return use.getOperandNumber() == 0 && TypeInference::isStyleCall(use.getOwner());
               });
    }
    const llvm::StringRef read = load.getName() == "document" ? "documentElement"
                                 : load.getName() == "window" ? "devicePixelRatio"
                                                              : "";
    if (read.empty() || !TypeInference::isRealmGlobal(load, load.getName())) { return false; }
    return llvm::all_of(load.getResult().getUses(), [&](mlir::OpOperand & use) {
        // The receiver of a listener registration (HostCallbacks.cpp), and
        // its method read.
        mlir::Operation * user = use.getOwner();
        if ((use.getOperandNumber() == 1 && TypeInference::isHostCallbackCall(user)) ||
            isPageMethodRead(user)) {
            return true;
        }
        auto get = llvm::dyn_cast<ctjs::GetPropertyOp>(user);
        return get && use.getOperandNumber() == 0 && ctjs::constantKey(get.getKey()) == read;
    });
}

bool isPageMethodRead(mlir::Operation * o) {
    auto read = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(o);
    if (!read || !read.getResult().hasOneUse()) { return false; }
    auto call = llvm::dyn_cast<ctjs::CallOp>(*read.getResult().getUsers().begin());
    return call &&
           (TypeInference::isRectCall(call) || TypeInference::isStylePropertyCall(call) ||
            TypeInference::isHostCallbackCall(call) || TypeInference::isPromiseCall(call) ||
            TypeInference::isEventCall(call) || TypeInference::isDispatchEventCall(call) ||
            !TypeInference::elementCallKey(call).empty()) &&
           ctjs::receiverMethod(call) == read;
}

bool isVectorCallPlumbing(mlir::Operation * o) {
    using VectorCall = TypeInference::VectorCall;
    if (TypeInference::isSpreadCopyPlumbing(o)) { return true; }
    const auto onlyUse = [](mlir::Value value) -> mlir::OpOperand * {
        return value.hasOneUse() ? &*value.getUses().begin() : nullptr;
    };
    // The reads a built-in call is recognised by: the namespace object, its
    // `prototype` and the method, each used only by those reads and as the
    // call's callee or receiver.
    const auto builtinRead = [](mlir::Operation * read) {
        const auto plumbing = [](mlir::OpOperand & use) {
            mlir::Operation * user = use.getOwner();
            if (use.getOperandNumber() <= 1 && TypeInference::builtinStaticOf(user)) {
                return true;
            }
            return use.getOperandNumber() == 0 && llvm::isa<ctjs::GetPropertyOp>(user) &&
                   isVectorCallPlumbing(user);
        };
        return !read->getResult(0).use_empty() &&
               llvm::all_of(read->getResult(0).getUses(), plumbing);
    };
    if (auto read = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(o)) {
        mlir::OpOperand * use = onlyUse(read.getResult());
        const VectorCall kind =
            use ? TypeInference::vectorCallOf(use->getOwner()) : VectorCall::none;
        return (use && use->getOperandNumber() == 0 && kind != VectorCall::none &&
                kind != VectorCall::regexp) ||
               builtinRead(o);
    }
    // The RegExp call is plumbing only as split(/\s+/)'s separator.
    const auto separatesWhitespace = [&](mlir::Operation * call) {
        if (TypeInference::vectorCallOf(call) != VectorCall::regexp) { return false; }
        mlir::OpOperand * use = onlyUse(call->getResult(0));
        return use && use->getOperandNumber() == 2 &&
               TypeInference::vectorCallOf(use->getOwner()) == VectorCall::splitSpace;
    };
    if (llvm::isa_and_nonnull<ctjs::CallOp>(o)) { return separatesWhitespace(o); }
    if (llvm::isa_and_nonnull<ctjs::LoadGlobalOp>(o) && builtinRead(o)) { return true; }
    if (auto load = llvm::dyn_cast_or_null<ctjs::LoadGlobalOp>(o)) {
        return load.getName() == "__ctbrowser_regexp" && !load.getResult().use_empty() &&
               llvm::all_of(load.getResult().getUses(), [&](mlir::OpOperand & use) {
                   return use.getOperandNumber() == 0 && separatesWhitespace(use.getOwner());
               });
    }
    return false;
}

bool isScalarCarrier(carrier value) {
    return value == carrier::number || value == carrier::boolean || value == carrier::nullable;
}

bool isScalarPromiseValue(carrier value) {
    return value == carrier::number || value == carrier::boolean || value == carrier::string ||
           value == carrier::nullable;
}

std::string promiseSpelling(mlir::Type type) {
    if (auto promise = llvm::dyn_cast_or_null<PromiseType>(type)) {
        return "ctnative::js_promise<" +
               carrierInfo(carrierOf(promise.getElementType())).spelling.str() + ">";
    }
    auto resolve = llvm::cast<PromiseResolveType>(type);
    return "ctnative::promise_resolve_function<" +
           carrierInfo(carrierOf(resolve.getElementType())).spelling.str() +
           ", ctnative::no_rejection>";
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

/// The one C++ type a dense array lowers to. It is spelled once because the
/// emitted declaration, the helper signatures and the lit tests must agree.
mlir::Type vectorCarrierType(mlir::MLIRContext * c, bool strings) {
    return ec::LValueType::get(ec::OpaqueType::get(c, strings ? kStringVectorType : kVectorType));
}

/// Returns the C++ storage type for a two-alternative Map key or value
/// (Boolean|Number or Boolean|String), a tagged struct of the runtime, or
/// empty if there is none. This covers Map storage only; call operands and
/// typed reads are admitted separately.
llvm::StringRef mixedMapSpelling(mlir::Type type) {
    auto variant = llvm::dyn_cast<VariantType>(type);
    if (!variant) { return {}; }
    const auto alternatives = variant.getAlternatives();
    if (pairAlternatives(alternatives, is<BoolType>, is<NumType>)) {
        return "ctnative::boolean_or_number";
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
    // A Number|object key is an object_value, as a Map value of that type is:
    // a key compares objects by identity and Numbers by SameValueZero.
    return variant &&
                   pairAlternatives(variant.getAlternatives(), is<NumType>, is<ObjectIdentityType>)
               ? llvm::StringRef{kObjectValueType}
               : llvm::StringRef{};
}

/// Returns the tagged Map storage type for an optional scalar, nullable String
/// or nullable Boolean|String, or empty if `type` is none of those. Map
/// storage is chosen independently of the scalar and signature carriers.
llvm::StringRef nullableMapSpelling(mlir::Type type) {
    auto optional = llvm::dyn_cast<OptType>(type);
    if (!optional) { return {}; }
    if (carrierOf(type) == carrier::nullable) { return kNullableType; }
    if (carrierOf(optional.getElementType()) == carrier::string) { return kNullableStringType; }
    if (mixedMapSpelling(optional.getElementType()) == kBooleanStringMapType) {
        return "ctnative::nullable_boolean_or_string";
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

std::string instanceClassSpelling(llvm::StringRef className) {
    // As nameFamilies names the class's struct.
    // ponytail: assumes no other shape family took the class's name first
    // (nameFamilies would append `_2`); a clash then fails the C++ build
    // rather than miscompiling. Look the family up by class if one occurs.
    return admission::isReservedInCpp(className) ? "js_" + className.str() : className.str();
}

llvm::StringRef instanceClassName(mlir::Type type) {
    if (auto optional = llvm::dyn_cast<OptType>(type)) { type = optional.getElementType(); }
    return llvm::cast<InstanceType>(type).getClassName();
}

mlir::Type instanceCarrierType(mlir::Type type) {
    return ec::OpaqueType::get(type.getContext(),
                               "std::shared_ptr<" + instanceClassSpelling(instanceClassName(type)) +
                                   ">");
}

std::string mapValueSpelling(mlir::Type type) {
    if (carrierOf(type) == carrier::instance) {
        return llvm::cast<ec::OpaqueType>(instanceCarrierType(type)).getValue().str();
    }
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

std::string dictionaryValueSpelling(DictType type) {
    const mlir::Type value = type.getElementType();
    return carrierOf(value) == carrier::dictionary ? dictionarySpelling(value)
                                                   : carrierInfo(carrierOf(value)).spelling.str();
}

std::string dictionarySpelling(mlir::Type type) {
    if (auto optional = llvm::dyn_cast<OptType>(type)) { type = optional.getElementType(); }
    return "std::shared_ptr<ctnative::dictionary<" +
           dictionaryValueSpelling(llvm::cast<DictType>(type)) + ">>";
}

mlir::Type valueCarrierType(mlir::MLIRContext * context, mlir::Type type) {
    if (auto map = llvm::dyn_cast_or_null<MapType>(type); map && carrierOf(type) == carrier::map) {
        return mapCarrierType(map);
    }
    return carrierOf(type) == carrier::dictionary
               ? mlir::Type(ec::OpaqueType::get(context, dictionarySpelling(type)))
               : carrierType(context, carrierOf(type));
}

bool isDictionaryCarrier(mlir::Type type) {
    auto opaque = llvm::dyn_cast<ec::OpaqueType>(type);
    return opaque && opaque.getValue().starts_with("std::shared_ptr<ctnative::dictionary<");
}

mlir::Type mapCarrierType(MapType type) {
    const auto key = mapKeySpelling(type.getKeyType());
    const auto value = type.getValueType();
    const std::string body =
        (llvm::isa<MapType, BoolType, StrType>(value) || isObjectValueType(value) ||
         carrierOf(value) == carrier::instance || !mixedMapSpelling(value).empty() ||
         !nullableMapSpelling(value).empty())
            ? ("ctnative::map_storage<" + key + ", " + mapValueSpelling(value) + ">").str()
        : llvm::isa<StrType>(type.getKeyType()) ? std::string{"ctnative::string_to_number_map"}
                                                : ("ctnative::number_map<" + key + ">").str();
    return ec::OpaqueType::get(type.getContext(), "std::shared_ptr<" + body + ">");
}

std::string classTagSpelling(ClassTagType type) {
    return type.getClassName().str() + "_class";
}

mlir::Type classTagCarrierType(ClassTagType type) {
    return ec::OpaqueType::get(type.getContext(), classTagSpelling(type));
}

mlir::Type closureCarrierType(ClosureType type) {
    return ec::OpaqueType::get(type.getContext(), "ctnative::" + environmentType(type.getTarget()));
}

mlir::Type methodTableCarrierType(MethodTableType type) {
    return ec::OpaqueType::get(
        type.getContext(), "std::shared_ptr<ctnative::" + methodTableClass(type.getSite()) + ">");
}

mlir::Type carrierType(mlir::MLIRContext * c, carrier which) {
    // The method-table carrier needs its proved schema
    // (methodTableCarrierType), and `none` has no C++ representation at all.
    // Either one reaching this point means admission let an unproved value
    // through, so crash with a clear message rather than invent a type.
    if (which == carrier::methodTable) {
        llvm::report_fatal_error("method table carrier needs its proved schema");
    }
    if (which == carrier::classTag) {
        llvm::report_fatal_error("class tag carrier needs its class (classTagCarrierType)");
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

/// Every value that names the same object as `v`, `v` included. A literal
/// that nothing is lifted onto is a group of one, so callers need no special
/// case.
llvm::SmallVector<mlir::Value, 2> aliasesOf(const receiverGroups * groups, mlir::Value v) {
    if (groups != nullptr) {
        const auto entry = groups->find(v);
        if (entry != groups->end()) { return entry->second; }
    }
    return {v};
}

/// Formats a source location as `file:line:column`. The emitter writes it in a
/// comment above every generated definition, so a C++ diagnostic on the
/// generated code can be traced back to the JavaScript source (see
/// ctcompile::sourceSite).
static std::string spelledSite(mlir::FileLineColLoc file) {
    return file ? file.getFilename().str() + ":" + std::to_string(file.getLine()) + ":" +
                      std::to_string(file.getColumn())
                : std::string{"<no source location>"};
}
std::string siteOf(mlir::Location loc) {
    return spelledSite(sourceSite(loc));
}
/// The source site of `fn`. The importer gives a ctjs.func no location of its
/// own, so the first located operation in its body stands in for it.
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

/// The declarative rewrite patterns, built once per lowering rather than per
/// function; FrozenRewritePatternSet is what both greedy entry points take.
mlir::FrozenRewritePatternSet declarativePatterns(mlir::MLIRContext * context) {
    mlir::RewritePatternSet patterns(context);
    patterns.add<UnaryPlusIsIdentity>(context);
    return patterns;
}

} // namespace ctcompile::ctnative::lowering_detail
