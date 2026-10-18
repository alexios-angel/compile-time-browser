//===- Admission/Operations.cpp - Per-operation native admission ----------===//
//
// admission::op decides, for each ctjs operation in a function, whether the
// native (C++) backend can lower it. It returns true to admit the operation
// or calls refuse() with a reason that is reported to the user. The larger
// arms (DOM operations, Map calls, closed objects, direct calls, returns) are
// separate members defined below.
//
//===----------------------------------------------------------------------===//

#include "../../Analysis/OwnedGlobalRoots.h"
#include "../../Analysis/OwnedMethodTableSlots.h"
#include "../ForOf.h"
#include "Admission.h"
#include "ctcompile/CTNative/Analysis/HostContract.h"
#include "ctcompile/CTNative/Analysis/StaticTables.h"
#include "ctcompile/CTNative/IR/CTNativeOps.h"

namespace ctcompile::ctnative::lowering_detail {

/// Admits an operation of an object used as a table (Dictionaries.cpp), or
/// nullopt when `o` is not one.
std::optional<bool> dictionaryOperation(admission & self, mlir::Operation * o);
/// Admits a call specialised for its Array-method callback (ArrayCallbacks.cpp).
bool arrayCallbackOperation(admission & self, ctjs::CallOp call);
/// Admits an addEventListener call specialised for its listener
/// (HostCallbacks.cpp).
bool hostCallbackOperation(admission & self, ctjs::CallOp call);
bool promiseOperation(admission & self, mlir::Operation * o);
/// Admits a use of a listener's Event (HostCallbacks.cpp); nullopt when `o`
/// uses none.
std::optional<bool> eventOperation(admission & self, mlir::Operation * o);
/// Admits an element member call or read (Elements.cpp); nullopt when `o`
/// is none.
std::optional<bool> elementOperation(admission & self, mlir::Operation * o);

namespace {

/// Admits an operation on an element array (isElementArray: Strings or
/// tuples), a tuple literal or a tuple read, a vector call on one, or split
/// on a String; nullopt when `o` is none of these.
std::optional<bool> stringArrayOperation(admission & self, mlir::Operation * o) {
    using namespace ctjs;
    using VectorCall = TypeInference::VectorCall;
    const auto isString = [&](mlir::Value v) {
        return carrierOf(self.typeOf(v)) == carrier::string;
    };
    const auto array = [&](mlir::Value v) {
        return isElementArray(v, self.typeOf(v), self.pageViews());
    };
    // Whether `value` may be stored into the element array `into`: a String
    // into an array of Strings, a tuple literal into an array of tuples.
    const auto fits = [&](mlir::Value into, mlir::Value value) {
        auto vector = llvm::dyn_cast_or_null<VecType>(self.typeOf(into));
        if (vector && llvm::isa<TupleType>(vector.getElementType())) {
            return TypeInference::isTupleLiteral(value) &&
                   carrierOf(self.typeOf(value)) == carrier::tuple;
        }
        // An element into an array of elements (`[i]`, the spread loop).
        if (vector && llvm::isa<DOMElementType>(vector.getElementType())) {
            return carrierOf(self.typeOf(value)) == carrier::domElement;
        }
        return isString(value);
    };
    if (isVectorCallPlumbing(o)) { return true; }
    // The Array and String methods below are lowered as the built-ins, which
    // the program must not be able to replace (ForOf.cpp).
    if (auto call = llvm::dyn_cast<CallOp>(o)) {
        const VectorCall kind = TypeInference::vectorCallOf(call);
        const auto * row =
            kind == VectorCall::builtinStatic ? TypeInference::builtinStaticOf(call) : nullptr;
        const bool builtin =
            (kind != VectorCall::none && kind != VectorCall::regexp &&
             kind != VectorCall::builtinStatic) ||
            (row && row->prototype && (row->global == "Array" || row->global == "String"));
        auto module = call->getParentOfType<mlir::ModuleOp>();
        auto why =
            module ? module->getAttrOfType<mlir::StringAttr>(kRealmBuiltinsUnproved) : nullptr;
        if (builtin && why) {
            return self.refuse(
                ("`" + ctjs::methodKey(call) +
                 "` needs the realm's own Array and String built-ins: " + why.getValue())
                    .str());
        }
    }
    if (auto literal = llvm::dyn_cast<CreateArrayOp>(o);
        literal && TypeInference::isTupleLiteral(literal.getResult())) {
        // An element in a row is the runtime's view, which only an ordinary
        // script's elements are.
        auto row = llvm::dyn_cast_or_null<TupleType>(self.typeOf(literal.getResult()));
        if (row && !self.pageViews() && llvm::any_of(row.getElements(), [](mlir::Type part) {
                return carrierOf(part) == carrier::domElement;
            })) {
            return self.refuse("a row holding a DOM element under a host contract");
        }
        return carrierOf(self.typeOf(literal.getResult())) == carrier::tuple ||
               self.refuse("a two-element row whose positions are " +
                           printed(self.typeOf(literal.getResult())) +
                           ", not Strings, Numbers, Number|String or elements");
    }
    if (auto append = llvm::dyn_cast<AppendOp>(o);
        append && TypeInference::isTupleLiteral(append.getArray())) {
        return true; // The tuple literal checked its positions.
    }
    if (auto read = llvm::dyn_cast<GetPropertyOp>(o);
        read && carrierOf(self.typeOf(read.getObject())) == carrier::tuple) {
        const int64_t position = TypeInference::constantIndex(read.getKey());
        return (position == 0 || position == 1) ||
               self.refuse("a two-element row read at a position that is not 0 or 1");
    }
    if (auto literal = llvm::dyn_cast<CreateArrayOp>(o)) {
        if (array(literal.getResult())) {
            for (mlir::Value element : literal.getElements()) {
                if (!fits(literal.getResult(), element)) {
                    return self.refuse("an element array given an element of type " +
                                       printed(self.typeOf(element)));
                }
            }
            return true;
        }
        if (TypeInference::isDenseVectorSite(literal.getResult())) { return std::nullopt; }
        // Only an array that holds a String is meant as an array of Strings,
        // and only then is the element-vector proof's reason the one to give;
        // an array of numbers keeps the dense proof's refusal.
        mlir::Type stored;
        for (mlir::Value value : TypeInference::elementVectorStores(literal.getResult())) {
            stored = meet(stored, self.typeOf(value));
        }
        const bool holdsStrings =
            llvm::any_of(TypeInference::elementVectorStores(literal.getResult()),
                         [&](mlir::Value value) { return isString(value); });
        if (!holdsStrings) { return std::nullopt; }
        std::string why;
        if (!TypeInference::elementVectorAliases(literal.getResult(), &why).empty()) {
            return self.refuse("an array whose elements are " + printed(stored) + ", not Strings");
        }
        return self.refuse("an array of Strings: " + why);
    }
    if (auto append = llvm::dyn_cast<AppendOp>(o); append && array(append.getArray())) {
        return fits(append.getArray(), append.getElement()) ||
               self.refuse("an array of Strings appended a " +
                           printed(self.typeOf(append.getElement())));
    }
    if (auto read = llvm::dyn_cast<GetPropertyOp>(o); read && array(read.getObject())) {
        const llvm::StringRef key = constantKey(read.getKey());
        if (key == "length") { return true; }
        if (key.empty() && carrierOf(self.typeOf(read.getObject())) == carrier::domElementVector &&
            !TypeInference::isBoundedIndexRead(read)) {
            return self.refuse("an element of an element's children read at an index not "
                               "proved in bounds");
        }
        if (key.empty()) { return self.numeric(read.getKey(), "array index"); }
        return self.refuse(
            ("an element array read through the named property `" + key + "`").str());
    }
    if (auto load = llvm::dyn_cast<LoadGlobalOp>(o);
        load && load.getName() == "__ctbrowser_regexp") {
        // Plumbing returned above; every other RegExp literal is refused here.
        return self.refuse("a regular expression literal - only `text.split(/\\s+/)` is native");
    }
    auto call = llvm::dyn_cast<CallOp>(o);
    if (!call) { return std::nullopt; }
    // A method of a vector of Strings or tuples whose uses the element-vector
    // proof refuses: give the proof's reason rather than "not native yet".
    if (const auto kind = TypeInference::vectorCallOf(call);
        (kind == VectorCall::push || kind == VectorCall::join || kind == VectorCall::sort) &&
        !array(call.getReceiver())) {
        const carrier receiver = carrierOf(self.typeOf(call.getReceiver()));
        std::string why;
        if ((receiver == carrier::stringVector || receiver == carrier::tupleVector) &&
            TypeInference::elementVectorAliases(call.getReceiver(), &why).empty()) {
            return self.refuse("an element array: " + why);
        }
    }
    switch (TypeInference::vectorCallOf(call)) {
    case VectorCall::push:
        if (!array(call.getReceiver())) { return std::nullopt; }
        return fits(call.getReceiver(), call.getArgs()[0]) ||
               self.refuse("an array of " + printed(self.typeOf(call.getReceiver())) +
                           " pushed a " + printed(self.typeOf(call.getArgs()[0])));
    case VectorCall::join:
        if (!array(call.getReceiver()) ||
            carrierOf(self.typeOf(call.getReceiver())) != carrier::stringVector) {
            return std::nullopt;
        }
        return call.getArgs().empty() || isString(call.getArgs()[0]) ||
               self.refuse("join's separator is " + printed(self.typeOf(call.getArgs()[0])) +
                           ", not a String");
    case VectorCall::sort:
        return array(call.getReceiver()) &&
                       carrierOf(self.typeOf(call.getReceiver())) == carrier::stringVector
                   ? std::optional<bool>{true}
                   : std::nullopt;
    case VectorCall::split:
    case VectorCall::splitSpace: {
        if (!isString(call.getReceiver())) { return std::nullopt; }
        if (TypeInference::vectorCallOf(call) == VectorCall::split &&
            !isString(call.getArgs()[0])) {
            return self.refuse("split's separator is " + printed(self.typeOf(call.getArgs()[0])) +
                               ", not a String");
        }
        // The pieces are an array of Strings only while every use of them is
        // one the element-vector proof accepts.
        std::string why;
        if (TypeInference::elementVectorAliases(call.getResult(), &why).empty()) {
            return self.refuse("the result of split is " + why);
        }
        return array(call.getResult()) ||
               self.refuse("the result of split is " + printed(self.typeOf(call.getResult())));
    }
    case VectorCall::builtinStatic: {
        const TypeInference::BuiltinStatic & row = *TypeInference::builtinStaticOf(call);
        const std::string name =
            row.member.empty()
                ? row.global.str()
                : (row.global + (row.prototype ? ".prototype." : ".") + row.member).str();
        using Param = TypeInference::BuiltinStatic::Param;
        for (auto [index, argument] : llvm::enumerate(call.getArgs())) {
            const Param kind = row.params[index];
            const carrier held = carrierOf(self.typeOf(argument));
            if (kind == Param::record) {
                // A table serializes its own keys itself (Runtime/Json.hpp);
                // its site's admission checked its values.
                if (auto table = llvm::dyn_cast_or_null<DictType>(self.typeOf(argument))) {
                    if (carrierOf(table.getElementType()) != carrier::dictionary) { continue; }
                    return self.refuse(name + " of a table of tables");
                }
                if (!admission::isClosedObject(argument)) {
                    return self.refuse(name +
                                       " of a value that is not a closed-shape object literal");
                }
                if (const std::string why = TypeInference::stringifiedRecordProblem(argument, call);
                    !why.empty()) {
                    return self.refuse(name + " of an object literal: " + why);
                }
                for (mlir::Operation * user : argument.getUsers()) {
                    auto write = llvm::dyn_cast<SetPropertyOp>(user);
                    const carrier c =
                        write ? carrierOf(self.typeOf(write.getValue())) : carrier::number;
                    if (c != carrier::number && c != carrier::string && c != carrier::boolean) {
                        return self.refuse(name + " of an object literal whose field `" +
                                           ctjs::constantKey(write.getKey()).str() + "` is " +
                                           printed(self.typeOf(write.getValue())) +
                                           ", not a Number, String or Boolean");
                    }
                }
                continue;
            }
            // An element that may be null is passed through present_element,
            // which throws the VM's TypeError for null as the call would. A
            // printable is a carrier Runtime/Console.hpp's console_detail::append
            // takes.
            const bool printable = held == carrier::string || held == carrier::number ||
                                   held == carrier::boolean || held == carrier::nullable ||
                                   held == carrier::nullableString;
            if (auto why = call->getAttrOfType<mlir::StringAttr>(kNativeStaticTableLoopRefusal)) {
                return self.refuse(why.getValue().str());
            }
            // A constant table only where its proof marked the call.
            const bool table =
                held == carrier::staticTable && call->hasAttr(kNativeStaticTableEnumeration);
            const bool fits = kind == Param::string   ? isString(argument)
                              : kind == Param::number ? held == carrier::number
                              : kind == Param::element
                                  ? held == carrier::domElement || held == carrier::nullableElement
                              : kind == Param::staticTable ? table
                                                           : printable;
            if (!fits) {
                return self.refuse(name + "'s argument " + std::to_string(index) + " is " +
                                   printed(self.typeOf(argument)) + ", not " +
                                   (kind == Param::string    ? "a String"
                                    : kind == Param::number  ? "a Number"
                                    : kind == Param::element ? "an Element"
                                    : kind == Param::staticTable
                                        ? "a constant table of Strings"
                                        : "a String, Number, Boolean, null "
                                          "or undefined"));
            }
        }
        return true;
    }
    case VectorCall::includes:
        if (!array(call.getReceiver()) ||
            carrierOf(self.typeOf(call.getReceiver())) != carrier::stringVector) {
            return std::nullopt;
        }
        return isString(call.getArgs()[0]) ||
               self.refuse("includes on an array of Strings is given " +
                           printed(self.typeOf(call.getArgs()[0])) + ", not a String");
    case VectorCall::callback: return arrayCallbackOperation(self, call);
    case VectorCall::regexp:
        // Plumbing returned above; every other RegExp literal is refused here.
        return self.refuse("a regular expression literal - only `text.split(/\\s+/)` is native");
    case VectorCall::none: return std::nullopt;
    }
    return std::nullopt;
}

/// Admits the page reads of an ordinary script (no host contract; see
/// TypeInference::domReadType); nullopt when `o` is not one.
std::optional<bool> pageReadOperation(admission & self, mlir::Operation * o) {
    using namespace ctjs;
    if (!self.pageViews()) { return std::nullopt; }
    if (isPageGlobal(o) || isPageMethodRead(o)) { return true; }
    if (TypeInference::isHostCallbackCall(o)) {
        return hostCallbackOperation(self, llvm::cast<CallOp>(o));
    }
    if (TypeInference::isPromiseCall(o)) { return promiseOperation(self, o); }
    if (auto admitted = eventOperation(self, o)) { return *admitted; }
    if (auto admitted = elementOperation(self, o)) { return *admitted; }
    const auto rect = [&](mlir::Value v) { return carrierOf(self.typeOf(v)) == carrier::domRect; };
    if (TypeInference::isRectCall(o)) {
        auto call = llvm::cast<CallOp>(o);
        if (carrierOf(self.typeOf(call.getReceiver())) != carrier::domElement) {
            return std::nullopt;
        }
        return rect(call.getResult()) || self.refuse("getBoundingClientRect lost its rect type");
    }
    if (TypeInference::isStyleCall(o)) {
        auto call = llvm::cast<CallOp>(o);
        if (carrierOf(self.typeOf(call.getArgs()[0])) != carrier::domElement) {
            return self.refuse("getComputedStyle of a value that is not a proved element");
        }
        return carrierOf(self.typeOf(call.getResult())) == carrier::computedStyle ||
               self.refuse("getComputedStyle lost its style type");
    }
    if (TypeInference::isStylePropertyCall(o) &&
        carrierOf(self.typeOf(llvm::cast<CallOp>(o).getReceiver())) == carrier::computedStyle) {
        auto call = llvm::cast<CallOp>(o);
        return (carrierOf(self.typeOf(call.getArgs()[0])) == carrier::string &&
                carrierOf(self.typeOf(call.getResult())) == carrier::string) ||
               self.refuse("getPropertyValue of a name that is not a String");
    }
    if (TypeInference::deadRectReceiver(o)) {
        return rect(o->getResult(0)) ||
               self.refuse("the null of a getBoundingClientRect test on a value that is not "
                           "a proved element");
    }
    auto read = llvm::dyn_cast<GetPropertyOp>(o);
    if (!read) { return std::nullopt; }
    const llvm::StringRef key = constantKey(read.getKey());
    const carrier result = carrierOf(self.typeOf(read.getResult()));
    if (carrierOf(self.typeOf(read.getObject())) == carrier::computedStyle) {
        return self.refuse(
            ("a computed style read through `" + key + "` - only getPropertyValue is native")
                .str());
    }
    if (rect(read.getObject())) {
        return result == carrier::number ||
               self.refuse(("a rect read through `" + key +
                            "` - only left, top, width and height are native")
                               .str());
    }
    if (isPageGlobal(read.getObject().getDefiningOp())) {
        // The native read throws where the interpreter would read a property
        // of a null root, so it needs that property's name.
        if (key == "documentElement" &&
            TypeInference::firstPropertyRead(read.getResult()).empty()) {
            return self.refuse("document.documentElement with no property read of it found - "
                               "a null root would not throw where the native read does");
        }
        return result == (key == "documentElement" ? carrier::domElement : carrier::number) ||
               self.refuse(("`" + key + "` lost its page type").str());
    }
    if (carrierOf(self.typeOf(read.getObject())) != carrier::domElement) { return std::nullopt; }
    if (key == "tagName" || key == "id" || key == "className") {
        return result == carrier::string ||
               self.refuse(("an element's `" + key + "` that is not a String").str());
    }
    if (key == "clientWidth" || key == "clientHeight") {
        return result == carrier::number ||
               self.refuse(("`" + key +
                            "` of an element other than document.documentElement - only the "
                            "root's client size is native")
                               .str());
    }
    if (key == "getBoundingClientRect") {
        return (TypeInference::isRectTest(read) && result == carrier::boolean) ||
               self.refuse("an element's getBoundingClientRect read as a value - only "
                           "testing it and calling it are native");
    }
    if (key == "children") {
        std::string why;
        if (TypeInference::elementVectorAliases(read.getResult(), &why).empty()) {
            return self.refuse("an element's children: " + why);
        }
        return isElementArray(read.getResult(), self.typeOf(read.getResult()), true) ||
               self.refuse("an element's children that are not a vector of elements");
    }
    return self.refuse(("an element read through `" + key +
                        "` - an ordinary script reads only tagName, id, className, children, "
                        "clientWidth, clientHeight and getBoundingClientRect")
                           .str());
}

} // namespace

bool admission::ownedTableField(ctjs::SetPropertyOp store) {
    const auto * slot = ownedTableSlots ? ownedTableSlots->lookup(store) : nullptr;
    auto table = llvm::dyn_cast_or_null<MethodTableType>(typeOf(store.getValue()));
    if (!slot || !table) {
        return refuse("owned method-table field needs one initialization on a confined local "
                      "object that dominates every read");
    }
    for (ctjs::GetPropertyOp read : slot->reads) {
        if (typeOf(read.getResult()) != table) {
            return refuse("owned method-table field has no single definite schema at every read");
        }
    }
    return true;
}

/// Admits a borrowed callback's closure or a call through a borrowed
/// parameter (BorrowedCallbacks.cpp).
bool borrowedCallbackOperation(admission & self, mlir::Operation * o);

namespace {

/// The refusal for a read of a run-once function's static variable whose type
/// has no carrier: the variable's JavaScript name and what its owner stores in
/// it, so the reason points at the binding rather than at this frame.
std::string staticReferenceRefusal(ctjs::CreateCellOp reference, llvm::StringRef type) {
    const auto name = reference->getAttrOfType<mlir::StringAttr>(kNativeStaticCell).getValue();
    std::string holds;
    reference->getParentOfType<mlir::ModuleOp>().walk([&](ctjs::CreateCellOp owner) {
        if (owner->hasAttr(kNativeStaticReference) ||
            owner->getAttrOfType<mlir::StringAttr>(kNativeStaticCell) !=
                reference->getAttr(kNativeStaticCell)) {
            return;
        }
        for (mlir::Operation * user : owner.getResult().getUsers()) {
            auto store = llvm::dyn_cast<ctjs::CellSetOp>(user);
            if (!store || store.getCell() != owner.getResult()) { continue; }
            mlir::Operation * made = store.getValue().getDefiningOp();
            const char * what =
                llvm::isa_and_nonnull<ctjs::CreateObjectOp>(made)    ? "an object literal"
                : llvm::isa_and_nonnull<ctjs::CreateArrayOp>(made)   ? "an array literal"
                : llvm::isa_and_nonnull<ctjs::CreateClosureOp>(made) ? "a function"
                                                                     : "a value";
            if (holds.find(what) == std::string::npos) {
                holds += holds.empty() ? what : std::string(" or ") + what;
            }
        }
    });
    return ("it reads `" + name.drop_front(llvm::StringRef("static_").size()) +
            "`, a static variable of a function that runs once, which holds " +
            (holds.empty() ? "nothing this frame can see" : holds) + " of type " + type +
            " - that type has no native carrier")
        .str();
}

} // namespace
/// Admits a String.prototype.replace call with a specialised callback and the
/// plumbing it consumes (StringCallbacks.cpp).
std::optional<bool> stringCallbackOperation(admission & self, mlir::Operation * o);

bool admission::op(mlir::Operation * o) {
    using namespace ctjs;
    if (auto admitted = domOperation(o)) { return *admitted; }
    // A folded for-of indexes its source as an array (ForOf.cpp).
    if (auto read = llvm::dyn_cast<GetPropertyOp>(o);
        read && o->hasAttr(kForOfArray) &&
        !isElementArray(read.getObject(), typeOf(read.getObject()), pageViews())) {
        return refuse("a for-of over " + printed(typeOf(read.getObject())) +
                      ", not an array this tier holds");
    }
    // A folded array destructuring reads its value as a tuple or an array.
    if (auto read = llvm::dyn_cast<GetPropertyOp>(o);
        read && o->hasAttr(kDestructureArray) &&
        carrierOf(typeOf(read.getObject())) != carrier::tuple &&
        !isElementArray(read.getObject(), typeOf(read.getObject()), pageViews())) {
        return refuse("array destructuring of " + printed(typeOf(read.getObject())) +
                      ", not a tuple or an array this tier holds");
    }
    if (auto why = o->getAttrOfType<mlir::StringAttr>(kIterationRefusal)) {
        return refuse(why.str());
    }
    if (auto admitted = staticTableOperation(o)) { return *admitted; }
    if (auto admitted = instanceOperation(o)) { return *admitted; }
    if (auto admitted = pageReadOperation(*this, o)) { return *admitted; }
    if (auto admitted = stringCallbackOperation(*this, o)) { return *admitted; }
    if (auto admitted = stringArrayOperation(*this, o)) { return *admitted; }
    if (auto admitted = dictionaryOperation(*this, o)) { return *admitted; }
    if (auto refined = llvm::dyn_cast<RefineOp>(o)) {
        // The same carrier, or the String or Number alternative of one that
        // holds several kinds (Runtime/Narrowing.hpp spells the conversion).
        const carrier from = carrierOf(typeOf(refined.getValue()));
        const carrier to = carrierOf(typeOf(refined.getResult()));
        const bool alternative =
            (to == carrier::string &&
             (from == carrier::nullableString || from == carrier::numberString ||
              from == carrier::nullableNumberString || from == carrier::booleanString)) ||
            (to == carrier::number &&
             (from == carrier::numberString || from == carrier::nullableNumberString));
        return (from == to && to != carrier::none) || alternative ||
               refuse(("a value narrowed by `" + refined.getKind() + "` from ").str() +
                      printed(typeOf(refined.getValue())));
    }
    if (ownedGlobals && ownedGlobals->lookup(o)) { return ownedGlobalOperation(o); }
    if (const auto * edge = ownedGlobals ? ownedGlobals->objectGlobal(o) : nullptr) {
        auto object = edge->object;
        if (!llvm::isa_and_nonnull<ObjectIdentityType>(typeOf(object.getResult()))) {
            return refuse("object key global lacks an independently proved identity carrier");
        }
        if (auto load = llvm::dyn_cast<LoadGlobalOp>(o)) {
            return llvm::isa_and_nonnull<ObjectIdentityType>(typeOf(load.getResult())) ||
                   refuse("object key global read lost its allocation's identity carrier");
        }
        return true; // The owner query also checked this exact initialization.
    }
    if (llvm::isa<CreateObjectOp>(o) && o->hasAttr(kNativeObjectIdentity)) { return true; }
    if (!methodTableName(o).empty()) {
        if (llvm::isa<CreateObjectOp>(o)) { return true; }
        mlir::Value object;
        if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) { object = get.getObject(); }
        if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
            object = set.getObject();
            if (!llvm::isa_and_nonnull<ClosureType>(typeOf(set.getValue()))) {
                return refuse("returned method table field has no proved callable");
            }
        }
        if (object) {
            auto table = llvm::dyn_cast_or_null<MethodTableType>(typeOf(object));
            return (table && table.getSite() == methodTableName(o)) ||
                   refuse("returned method table access has no single proved schema");
        }
    }
    if (o->hasAttr(kNativeBorrowedCallback) || o->hasAttr(kNativeBorrowedCall)) {
        return borrowedCallbackOperation(*this, o);
    }
    if (auto made = llvm::dyn_cast<CreateClosureOp>(o); made && !environmentTarget(o).empty()) {
        for (mlir::Value captured : made.getUpvalues()) {
            const auto c = carrierOf(typeOf(captured));
            if (!isScalarCarrier(c) && c != carrier::string && c != carrier::map &&
                !isObjectCarrier(c)) {
                return refuse("returned closure capture needs an owning scalar, Map or object "
                              "identity carrier; got " +
                              printed(typeOf(captured)));
            }
        }
        return true;
    }
    if (auto read = llvm::dyn_cast<LoadUpvalueOp>(o); read && o->hasAttr(kNativeEnvironmentRead)) {
        auto closure = llvm::dyn_cast_or_null<ClosureType>(typeOf(read.getClosure()));
        return (closure && closure.getTarget() == environmentTarget(o)) ||
               refuse("returned closure invocation has no single proved target");
    }
    if (auto reason = o->getAttrOfType<mlir::StringAttr>(kNativeMapReason)) {
        return refuse(reason.getValue().str());
    }
    if (isNativeMapBookkeeping(o)) { return true; }
    if (auto made = llvm::dyn_cast<ConstructOp>(o)) {
        if (o->hasAttr(kNativeMapSite)) {
            return nativeMapRecordPayload(made.getResult()) ||
                   carrierOf(typeOf(made.getResult())) == carrier::map ||
                   refuse("native Map needs supported keys and numeric, boolean, closed mixed, "
                          "owning-string, object-identity union or acyclic Map values; inferred " +
                          printed(typeOf(made.getResult())));
        }
    }
    if (o->hasAttr(kNativeMapSnapshotCopy)) {
        auto copy = llvm::cast<CallOp>(o);
        return (isVectorSite(copy.getArgs().front()) && isVectorSite(copy.getResult()) &&
                isVectorCarrier(carrierOf(typeOf(copy.getResult())))) ||
               refuse("native Array.from requires a confined numeric or string Map snapshot");
    }
    if (const llvm::StringRef action = nativeMapAction(o); !action.empty()) {
        return nativeMapCall(o, action);
    }
    if (llvm::isa<FrameEnterOp, FrameExitOp, RootOp>(o)) { return true; }
    // A method field's store lowers to nothing, with the closure in it.
    // Checked before the closed-shape arms below because the value it
    // stores is a closure, which has no carrier and would be refused as a
    // field the moment the literal is examined.
    if (o->hasAttr(kNativeMethod)) { return true; }
    if (auto object = llvm::dyn_cast<CreateObjectOp>(o)) { return closedObject(object); }
    if (auto array = llvm::dyn_cast<CreateArrayOp>(o)) {
        if (!isVectorSite(array.getResult())) { return refuse(whyNotDense(array.getResult())); }
        // An array literal must live in a single frame-scope slot. A literal
        // made inside an `if` or a loop body would declare its vector inside
        // that block and the storage would end at the closing brace, so only
        // literals in the function's entry block are accepted.
        if (!llvm::isa<ctjs::FuncOp>(o->getParentOp()) || !o->getBlock()->isEntryBlock()) {
            return refuse("an array literal created inside a branch or a loop - its storage "
                          "has to be one frame slot (obligation O-4)");
        }
        if (carrierOf(typeOf(array.getResult())) != carrier::vector) {
            auto elements = llvm::dyn_cast_or_null<VecType>(typeOf(array.getResult()));
            const mlir::Type element = elements ? elements.getElementType() : mlir::Type{};
            const auto optional = llvm::dyn_cast_or_null<OptType>(element);
            if (carrierOf(optional ? optional.getElementType() : element) == carrier::boolean) {
                return refuse("an array of booleans - `std::vector<bool>` is a bit-packed "
                              "specialisation whose elements are a proxy, not a `bool`");
            }
            return refuse("an array whose elements are " + printed(element) + ", not numbers");
        }
        for (mlir::Value element : array.getElements()) {
            if (carrierOf(typeOf(element)) != carrier::number) {
                return refuse("dense array storage requires definite numbers");
            }
        }
        return true;
    }
    if (auto push = llvm::dyn_cast<AppendOp>(o)) {
        if (!isVectorSite(push.getArray())) {
            return refuse("an append onto an array that is not a dense literal");
        }
        return carrierOf(typeOf(push.getElement())) == carrier::number ||
               refuse("dense array storage requires definite numbers");
    }
    if (auto get = llvm::dyn_cast<GetPropertyOp>(o)) {
        if (nativeObjectFieldGroup(o) >= 0) { return identityField(o); }
        if (carrierOf(typeOf(get.getObject())) == carrier::string &&
            ctjs::constantKey(get.getKey()) == "length") {
            return true;
        }
        // `text[i]`: ctnative::string_at takes a Number index.
        if (carrierOf(typeOf(get.getObject())) == carrier::string &&
            ctjs::constantKey(get.getKey()).empty()) {
            return carrierOf(typeOf(get.getKey())) == carrier::number ||
                   refuse("a String indexed by " + printed(typeOf(get.getKey())) +
                          ", not a Number");
        }
        if (isVectorSite(get.getObject())) {
            // `length` is exactly `size()`, because the site proof is what
            // rules out a hole; every other key is an index, and the index
            // has to be a number - `a[k]` with a string `k` reads a
            // property, and `a["push"]` is a function.
            if (ctjs::constantKey(get.getKey()) == "length") { return true; }
            return numeric(get.getKey(), "array index");
        }
        if (!isClosedObject(get.getObject())) {
            return refuse("a property read on an object that is not a closed-shape literal");
        }
        return true; // Its result's carrier is checked with every other value.
    }
    if (auto set = llvm::dyn_cast<SetPropertyOp>(o)) {
        if (nativeObjectFieldGroup(o) >= 0) { return identityField(o); }
        if (isVectorSite(set.getObject())) {
            return (ctjs::constantKey(set.getKey()) == "length" ||
                    carrierOf(typeOf(set.getKey())) == carrier::number ||
                    refuse("dense array writes require definite numeric indices")) &&
                   (carrierOf(typeOf(set.getValue())) == carrier::number ||
                    refuse("dense array storage requires definite numbers"));
        }
        // A write to an array literal not proved dense: report why it is not dense.
        if (set.getObject().getDefiningOp<CreateArrayOp>()) {
            return refuse(whyNotDense(set.getObject()));
        }
        if (!isClosedObject(set.getObject())) {
            return refuse("a property write on an object that is not a closed-shape literal");
        }
        return true; // The value's carrier was checked at the object.
    }
    if (isKeyOnlyString(o) || isVectorKeyString(o)) { return true; }
    if (auto load = llvm::dyn_cast<LoadGlobalOp>(o);
        load && feedsOnlyDirectCallees(load.getResult())) {
        return true;
    }
    if (auto call = llvm::dyn_cast<CallDirectOp>(o)) { return directCall(call); }
    if (isDeclarationClosure(o) || isDeclarationStore(o)) { return true; }
    // Closures that closure lifting turned into direct calls, and the constant
    // cells they captured, are erased before emission, so they need no check.
    // A closure that could not be lifted carries the lift's reason, which
    // becomes the refusal message.
    if (isLiftedClosure(o) || isUnboxedCell(o)) { return true; }
    if (llvm::isa<CreateClosureOp>(o)) { return refuse(closureRefusal(o)); }
    // A captured binding shared by reference becomes a local variable in this
    // frame, and lifted callees receive its address. Closure lifting runs
    // before type inference, so this is the first place the variable's C++
    // type can be checked; a cell whose type has no C++ representation is
    // refused here, so no pointer to it is ever taken. (The other conditions
    // for passing the address are listed beside `carriedCells` in
    // ClosureLifter.h.)
    if (auto cell = llvm::dyn_cast<CreateCellOp>(o); cell && isCarriedCell(o)) {
        // A binding a kept callback shares (rule 2b in ClosureLifter.h) may
        // hold only values that reference no closure, so no closure over the
        // cell can be stored in it and the cell is never in a cycle.
        if (o->hasAttr(kNativeSharedCell) &&
            !isPrimitiveCarrier(carrierOf(typeOf(cell.getResult())))) {
            return refuse("a binding a kept callback writes holds " +
                          printed(typeOf(cell.getResult())) +
                          " - its shared cell holds only a Number, a Boolean, a String or an "
                          "optional one; a function there could capture the cell holding it, "
                          "a reference cycle");
        }
        if (carrierOf(typeOf(cell.getResult())) == carrier::none) {
            if (o->hasAttr(kNativeStaticReference)) {
                return refuse(staticReferenceRefusal(cell, printed(typeOf(cell.getResult()))));
            }
            return refuse("a shared binding of type " + printed(typeOf(cell.getResult())) +
                          ", which has no native carrier - a variable this tier cannot "
                          "spell is not one it may point at");
        }
        if (carrierOf(typeOf(cell.getResult())) == carrier::string &&
            carrierOf(typeOf(cell.getInitial())) != carrier::string &&
            !cell->hasAttr(kAssignedBeforeRead)) {
            return refuse("a shared string binding whose non-string initial is observable");
        }
        return true;
    }
    // A read or a write of a shared binding, in this frame or through the
    // pointer a lifted call handed us. The read's result carrier is checked by
    // the per-result walk in function(); here the write must store the
    // carrier the variable holds (two carriers in one variable would be a
    // `double` assigned a `bool`), and the join that produced the
    // cell's type must select tagged storage for mixed scalar assignments.
    // A refusal here names the store rather than the box.
    if (auto get = llvm::dyn_cast<CellGetOp>(o); get && namesASharedCell(get.getCell())) {
        return true;
    }
    if (auto set = llvm::dyn_cast<CellSetOp>(o); set && namesASharedCell(set.getCell())) {
        const auto cellType = typeOf(set.getCell()), valueType = typeOf(set.getValue());
        const carrier held = carrierOf(cellType), stored = carrierOf(valueType);
        // Widen only when the selected storage already represents every
        // alternative. Sharing a nullable carrier does not make Number and
        // Boolean interchangeable in their separate String unions.
        const bool widens = isPrimitiveCarrier(held) && isPrimitiveCarrier(stored) &&
                            carrierOf(meet(cellType, valueType)) == held;
        if (held == carrier::none || (stored != held && !widens)) {
            return refuse("an assignment of " + printed(valueType) +
                          " to a shared binding of type " + printed(cellType));
        }
        return true;
    }
    if (llvm::isa<CreateCellOp>(o)) {
        auto why = o->getAttrOfType<mlir::StringAttr>(kNativeCellReason);
        return refuse(why ? ("a captured binding that stays a cell: " + why.getValue()).str()
                          : std::string{"a captured binding that stays a cell - Phase 59"});
    }
    // The lift's undefined value for a block argument no predecessor sets:
    // never read on any executed path. Lowering chooses NaN or an empty
    // string per destination slot, so poison itself needs no carrier.
    if (o->getName().getStringRef() == "ub.poison") { return true; }
    // The structuring pass's multiplexers: --ctjs-lift-to-scf encodes which
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
        if (carrierOf(typeOf(k.getResult())) == carrier::classTag) { return true; }
        if (llvm::isa<NumberAttr, BooleanAttr, UndefinedAttr, NullAttr>(k.getValue())) {
            return true;
        }
        if (llvm::isa<StringAttr>(k.getValue()) &&
            carrierOf(typeOf(k.getResult())) == carrier::string) {
            return true;
        }
        return refuse("a constant that is not a number, a boolean or undefined");
    }
    const auto numericOperand = [&](mlir::Value value, llvm::StringRef where) {
        const auto c = carrierOf(typeOf(value));
        return isStringCarrier(c) || isStringUnionCarrier(c) || numeric(value, where);
    };
    if (auto b = llvm::dyn_cast<BinaryOp>(o)) {
        switch (b.getKind()) {
        case BinaryKind::Add: {
            if (stringConcatenation(typeOf(b.getLhs()), typeOf(b.getRhs()))) { return true; }
            if (carrierOf(typeOf(b.getResult())) == carrier::numberString &&
                isPrimitiveCarrier(carrierOf(typeOf(b.getLhs()))) &&
                isPrimitiveCarrier(carrierOf(typeOf(b.getRhs())))) {
                return true;
            }
            if ((carrierOf(typeOf(b.getLhs())) == carrier::string &&
                 isPrimitiveCarrier(carrierOf(typeOf(b.getRhs())))) ||
                (carrierOf(typeOf(b.getRhs())) == carrier::string &&
                 isPrimitiveCarrier(carrierOf(typeOf(b.getLhs()))))) {
                return true;
            }
            return numeric(b.getLhs(), "binary") && numeric(b.getRhs(), "binary");
        }
        case BinaryKind::Concat: {
            // A template substitution is ToString of its value (ECMA-262
            // 13.2.8.6). For a Number, a Boolean, null or undefined that is the
            // text `+` gives it, and the lowering spells both alike.
            const carrier left = carrierOf(typeOf(b.getLhs()));
            const carrier right = carrierOf(typeOf(b.getRhs()));
            return stringConcatenation(typeOf(b.getLhs()), typeOf(b.getRhs())) ||
                   (left == carrier::string && isScalarCarrier(right)) ||
                   (right == carrier::string && isScalarCarrier(left)) ||
                   refuse("concatenation requires a proved owning UTF-8 string and a string, "
                          "number, boolean, null or undefined");
        }
        case BinaryKind::Sub:
        case BinaryKind::Mul:
        case BinaryKind::Div:
        case BinaryKind::Mod:
        case BinaryKind::Pow:
        case BinaryKind::BitAnd:
        case BinaryKind::BitOr:
        case BinaryKind::BitXor:
        case BinaryKind::Shl:
        case BinaryKind::Shr:
        case BinaryKind::UShr:
            return numericOperand(b.getLhs(), "binary") && numericOperand(b.getRhs(), "binary");
        // Any other kind is refused. (`**` is lowered by lowering::exponentiate in
        // EmitC/Expressions.cpp, not std::pow, because they differ for a base
        // of magnitude one with an infinite exponent.)
        default: return refuse("a bitwise or string operator is not native yet");
        }
    }
    if (auto b = llvm::dyn_cast<BinaryStaticOp>(o)) {
        if (b.getKind() != BinaryKind::Add) {
            // The verifier restricts the remaining static kinds to bitwise
            // operations. Primitive conversion cannot invoke object hooks.
            return numericOperand(b.getLhs(), "bitwise") && numericOperand(b.getRhs(), "bitwise");
        }
        return numeric(b.getLhs(), "++") && numeric(b.getRhs(), "++");
    }
    if (auto u = llvm::dyn_cast<UnaryOp>(o)) {
        switch (u.getKind()) {
        case UnaryKind::BitNot:
        case UnaryKind::Neg:
        case UnaryKind::Plus: return numericOperand(u.getOperand(), "unary");
        case UnaryKind::TypeOf:
            return carrierOf(typeOf(u.getOperand())) == carrier::symbol ||
                   isPrimitiveCarrier(carrierOf(typeOf(u.getOperand()))) ||
                   isObjectCarrier(carrierOf(typeOf(u.getOperand()))) ||
                   refuse("typeof requires a scalar, object identity or owning string carrier");
        case UnaryKind::Not:
            // `!x` applies the carrier's exact truthiness conversion, then
            // negates it. Tagged null/undefined and numeric NaN are falsy.
            if (carrierOf(typeOf(u.getOperand())) == carrier::none) {
                return refuse("! of " + printed(typeOf(u.getOperand())));
            }
            return true;
        default: return refuse("void is not native yet");
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
            if (carrierOf(typeOf(cmp.getLhs())) == carrier::symbol &&
                carrierOf(typeOf(cmp.getRhs())) == carrier::symbol) {
                return true;
            }
            if (stringEquality(typeOf(cmp.getLhs()), typeOf(cmp.getRhs()))) { return true; }
            if (isObjectCarrier(carrierOf(typeOf(cmp.getLhs()))) ||
                isObjectCarrier(carrierOf(typeOf(cmp.getRhs())))) {
                const auto left = typeOf(cmp.getLhs()), right = typeOf(cmp.getRhs());
                const auto supported = [](carrier c) {
                    return isScalarCarrier(c) || isObjectCarrier(c);
                };
                if (!supported(carrierOf(left)) || !supported(carrierOf(right))) {
                    return refuse("object equality requires identity or scalar alternatives");
                }
                if (cmp.getKind() == CompareKind::Eq && !onlyAbsent(left) && !onlyAbsent(right) &&
                    !(identityOrAbsent(left) && identityOrAbsent(right))) {
                    return refuse(
                        "loose object equality may invoke object-to-primitive conversion");
                }
                return true;
            }
            return numeric(cmp.getLhs(), "equality") && numeric(cmp.getRhs(), "equality");
        }
        return refuse("an unknown comparison");
    }
    if (auto t = llvm::dyn_cast<TruthyOp>(o)) {
        const carrier c = carrierOf(typeOf(t.getValue()));
        if (c == carrier::none) { return refuse("truthiness of " + printed(typeOf(t.getValue()))); }
        return true;
    }
    if (auto load = llvm::dyn_cast<LoadGlobalOp>(o)) {
        if (!isPrimitiveCarrier(carrierOf(typeOf(load.getResult())))) {
            return refuse(("global `" + load.getName() + "` is " +
                           printed(typeOf(load.getResult())) + ", not a number")
                              .str());
        }
        return true;
    }
    if (auto store = llvm::dyn_cast<StoreGlobalOp>(o)) {
        // One carrier must preserve every source store, including writes in
        // callees and on other paths. A narrowed read cannot select storage.
        const std::string where = ("store to global `" + store.getName() + "`").str();
        const auto storeType = [&](StoreGlobalOp write) -> mlir::Type {
            if (ownedGlobals) {
                if (auto type = scalarObservationType(
                        o->getContext(), ownedGlobals->returnedScalar(write.getValue()))) {
                    return type;
                }
            }
            return typeOf(write.getValue());
        };
        mlir::Type joined = storeType(store);
        if (joined == typeOf(store.getValue()) && !printable(store.getValue(), where)) {
            return false;
        }
        bool consistent = true;
        store->getParentOfType<mlir::ModuleOp>().walk([&](StoreGlobalOp other) {
            if (other.getName() != store.getName()) { return; }
            const auto type = storeType(other);
            if (!type) {
                consistent = false;
            } else {
                joined = meet(joined, type);
            }
        });
        consistent &= isPrimitiveCarrier(carrierOf(joined));
        return consistent || refuse(where + " has inconsistent global observation types");
    }
    if (auto attempt = llvm::dyn_cast<TryOp>(o)) { return exceptionRegion(attempt); }
    if (llvm::isa<TryExitOp, TryYieldOp>(o)) { return true; }
    if (auto ret = llvm::dyn_cast<ReturnOp>(o)) { return returned(ret); }
    if (llvm::isa<mlir::scf::IfOp, mlir::scf::WhileOp, mlir::scf::ForOp, mlir::scf::ConditionOp,
                  mlir::scf::YieldOp>(o)) {
        return true;
    }
    if (llvm::isa<mlir::cf::BranchOp, mlir::cf::CondBranchOp, mlir::cf::SwitchOp>(o)) {
        return refuse("unstructured control flow - run --ctjs-lift-to-scf first");
    }
    return refuse(("`" + o->getName().getStringRef() + "` is not native yet").str());
}

/// Admits the DOM entry's own operations; nullopt when `o` is not one of them.
std::optional<bool> admission::domOperation(mlir::Operation * o) {
    using namespace ctjs;
    if (!domEntry) { return std::nullopt; }
    if (auto thrown = llvm::dyn_cast<ThrowOp>(o);
        thrown && llvm::is_contained(domEntry->savedThrows(), thrown)) {
        const auto type = typeOf(thrown.getValue());
        const auto optional = llvm::dyn_cast_or_null<OptType>(type);
        auto literal = thrown.getValue().getDefiningOp<ConstantOp>();
        return llvm::isa_and_nonnull<NumType, BoolType, StrType>(type) ||
               (optional && llvm::isa<StrType>(optional.getElementType())) ||
               (literal && llvm::isa<NullAttr>(literal.getValue())) ||
               refuse("saved DOM throw lost its owning primitive carrier");
    }
    if (llvm::isa<mlir::scf::ExecuteRegionOp>(o) &&
        llvm::any_of(domEntry->savedThrows(),
                     [&](ThrowOp thrown) { return thrown->getParentOp() == o; })) {
        return true;
    }
    if (auto read = llvm::dyn_cast<GetPropertyOp>(o);
        read && (!domEntry->wellKnownSymbol(read).empty() || domEntry->isSymbolDescription(read) ||
                 domEntry->isStringVectorLength(read) || domEntry->isStringVectorIndex(read) ||
                 domEntry->isElementVectorLength(read) || domEntry->isElementVectorIndex(read) ||
                 domEntry->isDocumentElement(read) || domEntry->datasetValueElement(read))) {
        return true;
    }
    if (domBookkeeping(*domEntry, o)) { return true; }
    if (auto object = llvm::dyn_cast<CreateObjectOp>(o); object && domEntry->jsonObject(object)) {
        return true;
    }
    if (auto copy = llvm::dyn_cast<CopyPropsOp>(o); copy && domEntry->jsonCopy(copy)) {
        return true;
    }
    if (auto write = llvm::dyn_cast<SetPropertyOp>(o); write && domEntry->jsonAssignment(write)) {
        return true;
    }
    if (auto unary = llvm::dyn_cast<UnaryOp>(o);
        unary && unary.getKind() == UnaryKind::TypeOf &&
        carrierOf(typeOf(unary.getOperand())) == carrier::json) {
        return true;
    }
    if (auto call = llvm::dyn_cast<CallOp>(o); call && domEntry->call(call)) { return true; }
    if (auto invoke = llvm::dyn_cast<InvokeOp>(o); invoke && domEntry->invocation(invoke)) {
        return true;
    }
    if (llvm::isa<InvokeExitOp, InvokeYieldOp>(o)) {
        auto invoke = llvm::dyn_cast<InvokeOp>(o->getParentOp());
        if (invoke && domEntry->invocation(invoke)) { return true; }
    }
    if (auto compare = llvm::dyn_cast<CompareOp>(o);
        compare && compare.getKind() == CompareKind::StrictEq &&
        domEntry->isElementIdentity(compare.getLhs()) &&
        domEntry->isElementIdentity(compare.getRhs())) {
        return true;
    }
    return std::nullopt;
}

/// Admits a native Map method call, named by its `action`.
bool admission::nativeMapCall(mlir::Operation * o, llvm::StringRef action) {
    using namespace ctjs;
    if (action == "size") { return true; }
    auto call = llvm::cast<CallOp>(o);
    if (nativeMapRecordPayload(call.getReceiver())) {
        // Record schema and exact read identity live in the same side
        // proof as ordinary closed objects, never in a boxed carrier.
        return ((action != "set" || isClosedObject(call.getArgs()[1])) &&
                (action != "get" || isClosedObject(call.getResult()))) ||
               refuse("native record Map requires closed record payloads and aliases");
    }
    if (carrierOf(typeOf(call.getReceiver())) != carrier::map) {
        return refuse("native Map receiver has no supported key/value carrier");
    }
    const auto map = llvm::cast<MapType>(typeOf(call.getReceiver()));
    const bool scalarKey =
        llvm::isa<OptType>(map.getKeyType()) && carrierOf(map.getKeyType()) == carrier::nullable;
    if (scalarKey && !call.getArgs().empty() &&
        !isScalarCarrier(carrierOf(typeOf(call.getArgs()[0])))) {
        return refuse("nullable scalar Map key requires Number, Boolean, Null or Undefined");
    }
    // Numeric vectors erase absent tags; a key snapshot must preserve them.
    if (scalarKey && action == "keys") {
        return refuse("nullable scalar Map key snapshot needs a tagged element carrier");
    }
    const auto typedAlternative = [&](mlir::Type schema, mlir::Value operand) {
        const auto value = typeOf(operand);
        auto alternatives = llvm::cast<VariantType>(schema).getAlternatives();
        return llvm::any_of(alternatives, [&](mlir::Type alternative) {
            return value == alternative ||
                   (llvm::isa_and_nonnull<NumType>(value) && llvm::isa<NumType>(alternative));
        });
    };
    const auto nullableAlternative = [&](mlir::Type schema, mlir::Value operand,
                                         llvm::StringRef proofName) {
        const auto value = typeOf(operand);
        const auto c = carrierOf(value);
        if (nullableMapSpelling(schema) == kNullableType) { return isScalarCarrier(c); }
        const auto absent = llvm::dyn_cast_or_null<OptType>(value);
        const auto element = llvm::cast<OptType>(schema).getElementType();
        const bool allowsBoolean = !mixedMapSpelling(element).empty();
        bool represented = c == carrier::string || c == carrier::nullableString ||
                           (absent && llvm::isa<BottomType>(absent.getElementType())) ||
                           (allowsBoolean && c == carrier::boolean);
        // As with nonnullable mixed storage, a wider temporary may supply
        // an exact scalar only through the rederived per-operation fact.
        if (auto proof = o->getAttrOfType<mlir::StringAttr>(proofName)) {
            represented |= (proof.getValue() == "string" && c == carrier::booleanString) ||
                           (allowsBoolean && proof.getValue() == "bool" &&
                            (c == carrier::booleanString || c == carrier::nullable));
        }
        return represented;
    };
    if (!nullableMapSpelling(map.getKeyType()).empty() && !call.getArgs().empty() &&
        !nullableAlternative(map.getKeyType(), call.getArgs()[0], kNativeMapKeyType)) {
        return refuse("nullable native Map key needs a proved scalar, String or absent "
                      "alternative with an owning carrier");
    }
    // Whether the mixed variant `schema` has the alternative a proof tagged.
    const auto tagInSchema = [](mlir::Type schema, llvm::StringRef tag) {
        return llvm::any_of(llvm::cast<VariantType>(schema).getAlternatives(),
                            [&](mlir::Type type) {
                                return (tag == "bool" && llvm::isa<BoolType>(type)) ||
                                       (tag == "number" && llvm::isa<NumType>(type)) ||
                                       (tag == "string" && carrierOf(type) == carrier::string);
                            });
    };
    if (!mixedMapKeySpelling(map.getKeyType()).empty() && !call.getArgs().empty() &&
        !typedAlternative(map.getKeyType(), call.getArgs()[0])) {
        const auto proof = o->getAttrOfType<mlir::StringAttr>(kNativeMapKeyType);
        const auto tag = proof ? proof.getValue() : llvm::StringRef{};
        const auto c = carrierOf(typeOf(call.getArgs()[0]));
        const bool represented =
            (tag == "string" && (c == carrier::nullableString || c == carrier::booleanString)) ||
            (tag == "bool" && (c == carrier::nullable || c == carrier::booleanString)) ||
            (tag == "number" && c == carrier::nullable);
        if (!represented || !tagInSchema(map.getKeyType(), tag)) {
            return refuse("mixed native Map key needs one proved scalar alternative");
        }
    }
    if (!mixedMapSpelling(map.getValueType()).empty()) {
        if (action == "set" && !typedAlternative(map.getValueType(), call.getArgs()[1])) {
            // A path-sensitive proof can establish one actual tag even
            // when upstream SCF inference retains a wider scalar union.
            const auto proof = o->getAttrOfType<mlir::StringAttr>(kNativeMapWriteType);
            const auto tag = proof ? proof.getValue() : llvm::StringRef{};
            const auto c = carrierOf(typeOf(call.getArgs()[1]));
            const bool represented = c == carrier::booleanString || c == carrier::nullable;
            if (!represented || !tagInSchema(map.getValueType(), tag)) {
                return refuse("mixed native Map write needs one proved scalar alternative");
            }
        }
        if (action == "get" && (!o->hasAttr(kNativeMapReadType) ||
                                !typedAlternative(map.getValueType(), call.getResult()))) {
            return refuse("mixed native Map read needs independent present payload type evidence");
        }
    }
    if (!nullableMapSpelling(map.getValueType()).empty()) {
        if (action == "set" &&
            !nullableAlternative(map.getValueType(), call.getArgs()[1], kNativeMapWriteType)) {
            return refuse("nullable native Map write needs a proved scalar, String or "
                          "absent alternative with an owning carrier");
        }
        const auto element = llvm::cast<OptType>(map.getValueType()).getElementType();
        const auto proof = o->getAttrOfType<mlir::StringAttr>(kNativeMapReadType);
        const bool nullableRead = proof && proof.getValue() == "nullable_string" &&
                                  carrierOf(typeOf(call.getResult())) == carrier::nullableString;
        if (action == "get" && nullableMapSpelling(map.getValueType()) != kNullableType &&
            !mixedMapSpelling(element).empty() &&
            (!proof || !o->hasAttr(kNativeMapPresent) ||
             (!nullableRead && !typedAlternative(element, call.getResult())))) {
            return refuse("mixed native Map read needs independent present payload type evidence");
        }
    }
    if (action == "keys" || action == "values") {
        if (action == "values" && nullableMapSpelling(map.getValueType()) == kNullableType) {
            return refuse("nullable scalar Map value snapshot needs a tagged element carrier");
        }
        return (isVectorSite(call.getResult()) &&
                isVectorCarrier(carrierOf(typeOf(call.getResult())))) ||
               refuse("native Map snapshot requires confined numeric or string elements");
    }
    return true;
}

bool admission::closedObject(ctjs::CreateObjectOp object) {
    using namespace ctjs;
    if (!isClosedObject(object.getResult())) { return refuse(whyOpen(object.getResult())); }
    // Check every access in the receiver group, not only the literal's own. A
    // lifted method reaches these fields through its `%arg0`, in a different
    // function, and those reads and writes belong to this shape too, so
    // `this.class = 1` has to be refused here, at the one place that checks
    // a key, or it reaches the emitter as `self->class`.
    llvm::SmallVector<mlir::Operation *> accesses;
    for (mlir::Value alias : aliasesOf(groups, object.getResult())) {
        for (mlir::OpOperand & use : alias.getUses()) {
            if (use.getOperandNumber() == 0 &&
                llvm::isa<GetPropertyOp, SetPropertyOp>(use.getOwner())) {
                accesses.push_back(use.getOwner());
            }
        }
    }
    // The keys this literal is ever written with. A read of one of
    // them is an own property; a read of anything else falls through
    // to the prototype, which is what makes an inherited name wrong.
    llvm::StringSet<> written;
    llvm::StringSet<> stringFields;
    for (mlir::Operation * user : accesses) {
        if (auto set = llvm::dyn_cast<SetPropertyOp>(user)) {
            // A method field is not a data field. Its store lowers to
            // nothing and it takes no space in the class, so it is not
            // a key that shadows an inherited name either.
            if (!user->hasAttr(kNativeMethod)) {
                written.insert(ctjs::constantKey(set.getKey()));
                if (carrierOf(typeOf(set.getValue())) == carrier::string) {
                    stringFields.insert(ctjs::constantKey(set.getKey()));
                }
            }
        }
    }
    for (mlir::Operation * user : accesses) {
        if (user->hasAttr(kNativeMethod)) { continue; }
        const llvm::StringRef key =
            llvm::isa<GetPropertyOp>(user)
                ? ctjs::constantKey(llvm::cast<GetPropertyOp>(user).getKey())
                : ctjs::constantKey(llvm::cast<SetPropertyOp>(user).getKey());
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
        // A field joining String with absence, Boolean or Number is a union
        // member (Admission/VariantFields.cpp).
        if (auto admitted = variantField(key, user, accesses)) {
            if (!*admitted) { return false; }
            continue;
        }
        // The existing shape census already spells one definite string as
        // std::string. Mixed or possibly absent reads need a different join;
        // do not route them through the numeric nullable carrier.
        if (stringFields.contains(key)) {
            auto set = llvm::dyn_cast<SetPropertyOp>(user);
            auto value = set ? set.getValue() : llvm::cast<GetPropertyOp>(user).getResult();
            if (carrierOf(typeOf(value)) != carrier::string) {
                return refuse("string field requires definite string reads and writes");
            }
        }
        if (auto set = llvm::dyn_cast<SetPropertyOp>(user)) {
            const carrier c = carrierOf(typeOf(set.getValue()));
            if (c == carrier::methodTable) {
                if (!ownedTableField(set)) { return false; }
                continue;
            }
            // A field one of a proved set of closures (ClosureSets.cpp) is
            // stored as that set's std::variant, whether the stored value is
            // the set or one member's closure.
            if ((c == carrier::closureSet && isProvedClosureSet(set, typeOf(set.getValue()))) ||
                (c == carrier::closure && set.getValue().getDefiningOp() &&
                 set.getValue().getDefiningOp()->hasAttr(kNativeClosureSet))) {
                continue;
            }
            // A class held as a field: every read of it was resolved to the
            // class at compile time, so the field is only its empty tag.
            if (c == carrier::classTag) {
                const bool read = llvm::any_of(accesses, [&](mlir::Operation * other) {
                    auto get = llvm::dyn_cast<GetPropertyOp>(other);
                    return get && ctjs::constantKey(get.getKey()) == key;
                });
                if (read) {
                    return refuse(("field `" + key + "` holds a class and is still read").str());
                }
                continue;
            }
            if (!isScalarCarrier(c) && c != carrier::string) {
                return refuse(("field `" + key + "` is stored a " +
                               printed(typeOf(set.getValue())) + ", not a number or a boolean")
                                  .str());
            }
            // The shape census joins all scalar writes before choosing
            // storage, including a field nobody reads. Mixed boolean
            // and number stores therefore retain their runtime tags.
        }
    }
    return true;
}

bool admission::directCall(ctjs::CallDirectOp call) {
    using namespace ctjs;
    mlir::Operation * o = call;
    if (o->hasAttr(kNativeStoredCall)) {
        auto callable = llvm::dyn_cast_or_null<ClosureType>(typeOf(call.getCalleeValue()));
        if (!callable || callable.getTarget() != call.getCallee()) {
            return refuse("stored callable invocation has no single proved target");
        }
    }
    // new.target and the callee value are dropped; each remaining
    // argument needs a proved carrier. Whether the callee is native is decided
    // by the fixpoint in runOnOperation, not by one function.
    //
    // The receiver is not dropped when the lift marked this call. It is
    // operand 0, it becomes the callee's first C++ parameter, and its carrier
    // is the generated class, so it must really be a closed-shape object and
    // not some other value the lift never looked at.
    const auto operands = call.getArgOperands();
    // An instance a registry Map owns is passed as its owner or borrowed
    // (EmitC/Instances.cpp).
    if (o->hasAttr(kNativeReceiver) && !isClosedObject(call.getReceiver()) &&
        carrierOf(typeOf(call.getReceiver())) != carrier::instance) {
        return refuse("a method call whose receiver is not a closed-shape object");
    }
    for (unsigned i = 3; i < operands.size(); ++i) {
        // An object argument is a pointer, not a number. The lift proved the
        // literal closed before it wrote the index; this asks the same
        // question of the IR that came out, as the receiver check above
        // does, because a refusal since then can have opened it.
        if (isObjectArg(o, i)) {
            if (!isClosedObject(operands[i])) {
                return refuse("it passes an object whose shape is no longer closed to a "
                              "parameter this tier gave a pointer");
            }
            continue;
        }
        // A shared-binding argument is the address of a variable in this
        // frame. Check that the operand is still such a variable and that
        // its type has a C++ representation; a mismatch means closure
        // lifting and admission disagree.
        if (isCellArg(o, i)) {
            if (!namesASharedCell(operands[i])) {
                return refuse("it passes something that is not a shared binding of this "
                              "frame to a parameter this tier gave a pointer");
            }
            if (carrierOf(typeOf(operands[i])) == carrier::none) {
                return refuse("it passes a shared binding of type " + printed(typeOf(operands[i])) +
                              ", which has no native carrier");
            }
            continue;
        }
        if (carrierOf(typeOf(operands[i])) != carrier::methodTable &&
            !(carrierOf(typeOf(operands[i])) == carrier::domElement && ownedGlobals &&
              ownedGlobals->proved() && !ownedGlobals->domInputs().empty()) &&
            // An ordinary script's element is the runtime's view, passed by value.
            !(carrierOf(typeOf(operands[i])) == carrier::domElement && pageViews()) &&
            !isObjectCarrier(carrierOf(typeOf(operands[i]))) &&
            carrierOf(typeOf(operands[i])) != carrier::closure &&
            carrierOf(typeOf(operands[i])) != carrier::boolean &&
            carrierOf(typeOf(operands[i])) != carrier::instance &&
            !isStringCarrier(carrierOf(typeOf(operands[i]))) &&
            !isStringUnionCarrier(carrierOf(typeOf(operands[i]))) &&
            !(carrierOf(typeOf(operands[i])) == carrier::map && nativeMapGroup(operands[i]) >= 0) &&
            // A table is shared with the callee (Admission/Dictionaries.cpp).
            carrierOf(typeOf(operands[i])) != carrier::dictionary &&
            // An element array is lent to the callee as a std::span.
            !isElementArray(operands[i], typeOf(operands[i]), pageViews()) &&
            // An ordinary script's element (or element or null) is a copied view.
            !(pageViews() && (carrierOf(typeOf(operands[i])) == carrier::domElement ||
                              carrierOf(typeOf(operands[i])) == carrier::nullableElement)) &&
            !numeric(operands[i], "argument")) {
            return false;
        }
    }
    return true;
}

bool admission::returned(ctjs::ReturnOp ret) {
    // A data record is returned by value as its class; closedObject checks it.
    if (TypeInference::returnsRecord(ret)) { return true; }
    const carrier c = carrierOf(typeOf(ret.getValue()));
    if (c == carrier::none) { return refuse("returns " + printed(typeOf(ret.getValue()))); }
    returnedType =
        returnedType ? meet(returnedType, typeOf(ret.getValue())) : typeOf(ret.getValue());
    if (returns == carrier::none) { returns = c; }
    if (returns != c) {
        if (isStringUnionCarrier(carrierOf(returnedType))) {
            returns = carrierOf(returnedType);
        } else if (isScalarCarrier(returns) && isScalarCarrier(c)) {
            returns = carrier::nullable;
        } else if ((isObjectCarrier(returns) || isScalarCarrier(returns)) &&
                   (isObjectCarrier(c) || isScalarCarrier(c))) {
            returns = carrier::objectValue;
        } else {
            return refuse("returns different native carriers on different paths");
        }
    }
    return true;
}

} // namespace ctcompile::ctnative::lowering_detail
