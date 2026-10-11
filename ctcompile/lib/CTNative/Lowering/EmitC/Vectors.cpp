//===- Vectors.cpp - Emission of vectors of one proved element type ------===//
//
// Lowers arrays of Strings to `std::vector<ctnative::js_string>` and the
// runtime's built-in objects: a literal is a local vector its appends fill,
// `length` is vec_length, `a[i]` is vec_at, and push, join, sort and split
// are called with their JavaScript spelling,
// `ctnative::Array.prototype.push.call(a, x)` and
// `ctnative::String.prototype.split.call(s, sep)`.
// Everything decided here was decided by the proof and by admission; this
// file only spells it.
//
//===----------------------------------------------------------------------===//

#include "Vectors.h"

#include "mlir/Interfaces/FunctionInterfaces.h"

#include <array>
#include <limits>

namespace ctcompile::ctnative::lowering_detail {

// Array-method callback calls (ArrayCallbacks.cpp).
void planArrayCallback(lowering & lower, ctjs::CallOp call);
bool replaceArrayCallback(lowering & lower, mlir::Operation * o);
// Listeners handed to the host (HostCallbacks.cpp).
bool replaceHostCallback(lowering & lower, mlir::Operation * o);
// Promise operations (Promises.cpp).
bool replacePromise(lowering & lower, mlir::Operation * o);
/// The action for a read or call on a listener's Event, and its replacement
/// (HostCallbacks.cpp).
std::optional<std::string> eventAction(lowering & lower, mlir::Operation * o);
bool replaceEvent(lowering & lower, mlir::Operation * o, llvm::StringRef kind);
/// The action for an element member call or read, and its replacement
/// (Elements.cpp).
std::optional<std::string> elementAction(lowering & lower, mlir::Operation * o);
bool replaceElement(mlir::Operation * o, llvm::StringRef kind);

namespace {

/// The attribute holding an operation's planned lowering.
constexpr llvm::StringLiteral kStringArrayAction = "ctnative.string_array";
/// On a push of a tuple literal, the array's element type: the std::pair the
/// push builds. Recorded with the plan because the receiver is an emitc value
/// with no lattice type by the time the push is replaced.
constexpr llvm::StringLiteral kStringArrayRow = "ctnative.string_array_row";
constexpr llvm::StringLiteral kArrayHeader = "ctcompile/CTNative/Runtime/Array.hpp";

/// Adds `#include "Runtime/Array.hpp"` once, at the top of the module. The
/// runtime header and its defines are inserted in front of it later
/// (lowering::declareGlobals), and Array.hpp includes ctnative.hpp itself.
void includeArrayHeader(mlir::ModuleOp module) {
    for (auto include : module.getBody()->getOps<ec::IncludeOp>()) {
        if (include.getInclude() == kArrayHeader) { return; }
    }
    mlir::OpBuilder b(module.getContext());
    b.setInsertionPointToStart(module.getBody());
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr(kArrayHeader), mlir::UnitAttr{});
    // std::span lends an element array to a callee.
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr("span"), b.getUnitAttr());
}

} // namespace

llvm::DenseSet<mlir::Value> planStringArrays(lowering & lower, ctjs::FuncOp fn) {
    using VectorCall = TypeInference::VectorCall;
    llvm::DenseSet<mlir::Value> arrays;
    const auto consider = [&](mlir::Value v) {
        if (isElementArray(v, lower.typeOf(v), lower.pageViews)) { arrays.insert(v); }
    };
    for (mlir::Block & block : fn.getBody()) {
        for (mlir::BlockArgument argument : block.getArguments()) { consider(argument); }
    }
    fn.getBody().walk([&](mlir::Operation * op) {
        for (mlir::Value result : op->getResults()) { consider(result); }
        for (mlir::Region & region : op->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) { consider(argument); }
            }
        }
    });
    const auto mark = [&](mlir::Operation * op, llvm::StringRef action) {
        op->setAttr(kStringArrayAction, mlir::StringAttr::get(op->getContext(), action));
    };
    fn.getBody().walk([&](mlir::Operation * op) {
        if (isVectorCallPlumbing(op) ||
            (lower.pageViews && (isPageGlobal(op) || isPageMethodRead(op)))) {
            mark(op, "plumbing");
            return;
        }
        // An array or a rect is an object, so always truthy.
        if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(op);
            truthy && (arrays.contains(truthy.getValue()) ||
                       carrierOf(lower.typeOf(truthy.getValue())) == carrier::domRect)) {
            mark(op, "truthy");
            return;
        }
        if (lower.pageViews && TypeInference::isStyleCall(op) &&
            carrierOf(lower.typeOf(op->getResult(0))) == carrier::computedStyle) {
            mark(op, "computed_style");
            return;
        }
        if (lower.pageViews && TypeInference::isStylePropertyCall(op) &&
            carrierOf(lower.typeOf(llvm::cast<ctjs::CallOp>(op).getReceiver())) ==
                carrier::computedStyle) {
            mark(op, "property_value");
            return;
        }
        if (lower.pageViews && TypeInference::isRectCall(op) &&
            carrierOf(lower.typeOf(op->getResult(0))) == carrier::domRect) {
            mark(op, "bounding_rect");
            return;
        }
        if (lower.pageViews && TypeInference::isHostCallbackCall(op)) {
            mark(op, "host_callback");
            return;
        }
        if (lower.pageViews && TypeInference::isPromiseCall(op)) {
            mark(op, "promise");
            return;
        }
        if (auto action = lower.pageViews ? eventAction(lower, op) : std::nullopt) {
            mark(op, *action);
            return;
        }
        if (auto action = lower.pageViews ? elementAction(lower, op) : std::nullopt) {
            mark(op, *action);
            return;
        }
        // The null on the branch an element's getBoundingClientRect test never
        // takes (TypeInference::deadRectReceiver): any rect will do.
        if (lower.pageViews && TypeInference::deadRectReceiver(op) &&
            carrierOf(lower.typeOf(op->getResult(0))) == carrier::domRect) {
            mark(op, "dead_rect");
            return;
        }
        if (auto negated = llvm::dyn_cast<ctjs::UnaryOp>(op);
            negated && negated.getKind() == ctjs::UnaryKind::Not &&
            arrays.contains(negated.getOperand())) {
            mark(op, "falsy");
            return;
        }
        // The page reads of an ordinary script (TypeInference::domReadType).
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op); read && lower.pageViews) {
            const llvm::StringRef key = ctjs::constantKey(read.getKey());
            if (isPageGlobal(read.getObject().getDefiningOp())) {
                mark(op, key == "documentElement" ? "document_element" : "device_pixel_ratio");
                return;
            }
            // The member is recorded now: by replacement time the key is an
            // emitc constant.
            if (carrierOf(lower.typeOf(read.getObject())) == carrier::domRect) {
                mark(op, ("rect:" + key).str());
                return;
            }
            // The method exists on every element, so the test is true.
            if (TypeInference::isRectTest(op) &&
                carrierOf(lower.typeOf(read.getObject())) == carrier::domElement) {
                mark(op, "method_test");
                return;
            }
            if (carrierOf(lower.typeOf(read.getObject())) == carrier::domElement) {
                const bool client = key == "clientWidth" || key == "clientHeight";
                mark(op, key == "children" ? std::string{"children"}
                                           : ((client ? "client:" : "member:") + key).str());
                return;
            }
        }
        // A tuple literal and its appends lower to nothing until the push
        // that copies the tuple into its array builds the std::pair there.
        if (auto literal = llvm::dyn_cast<ctjs::CreateArrayOp>(op)) {
            if (TypeInference::isTupleLiteral(literal.getResult())) {
                mark(op, "tuple");
            } else if (arrays.contains(literal.getResult())) {
                mark(op, "create");
            }
            return;
        }
        if (auto append = llvm::dyn_cast<ctjs::AppendOp>(op)) {
            if (TypeInference::isTupleLiteral(append.getArray())) {
                mark(op, "tuple_part");
            } else if (arrays.contains(append.getArray())) {
                mark(op, "append");
            }
            return;
        }
        if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op)) {
            if (carrierOf(lower.typeOf(read.getObject())) == carrier::tuple) {
                // The position is decided now: by replacement time the key
                // is an emitc constant.
                mark(op,
                     TypeInference::constantIndex(read.getKey()) == 0 ? "position0" : "position1");
                return;
            }
            if (!arrays.contains(read.getObject())) { return; }
            mark(op, ctjs::constantKey(read.getKey()) == "length"                   ? "length"
                     : TypeInference::isBoundedIndexRead(op)                        ? "element"
                     : carrierOf(lower.typeOf(read.getObject())) == carrier::vector ? "at_number"
                                                                                    : "at");
            return;
        }
        auto call = llvm::dyn_cast<ctjs::CallOp>(op);
        if (!call) { return; }
        switch (TypeInference::vectorCallOf(call)) {
        case VectorCall::push:
            if (!arrays.contains(call.getReceiver())) { break; }
            mark(op, "push");
            if (TypeInference::isTupleLiteral(call.getArgs()[0])) {
                auto vector = llvm::dyn_cast_or_null<VecType>(lower.typeOf(call.getReceiver()));
                auto row =
                    vector ? llvm::dyn_cast<TupleType>(vector.getElementType()) : TupleType{};
                if (row) { op->setAttr(kStringArrayRow, mlir::TypeAttr::get(row)); }
            }
            break;
        case VectorCall::join:
            if (arrays.contains(call.getReceiver())) { mark(op, "join"); }
            break;
        case VectorCall::sort:
            if (arrays.contains(call.getReceiver())) { mark(op, "sort"); }
            break;
        case VectorCall::includes:
            if (arrays.contains(call.getReceiver())) { mark(op, "includes"); }
            break;
        case VectorCall::builtinStatic: {
            // The row rides on the mark, as `static <kinds> <callee>` with one
            // `s` (String), `n` (Number) or `e` (Element) per parameter: the
            // reads it was recognised by are plumbing, replaced first, and so
            // are String literal arguments.
            using Param = TypeInference::BuiltinStatic::Param;
            const TypeInference::BuiltinStatic & row = *TypeInference::builtinStaticOf(call);
            std::string action = "static ";
            for (unsigned i = 0; i < row.arity; ++i) {
                action += row.params[i] == Param::string    ? 's'
                          : row.params[i] == Param::number  ? 'n'
                          : row.params[i] == Param::element ? 'e'
                                                            : 'p';
            }
            mark(op, action + " " + row.callee.str());
            break;
        }
        case VectorCall::callback:
            if (arrays.contains(call.getReceiver())) {
                mark(op, "callback");
                planArrayCallback(lower, call);
            }
            break;
        case VectorCall::split: mark(op, "split"); break;
        case VectorCall::splitSpace: mark(op, "split_space"); break;
        case VectorCall::regexp:
        case VectorCall::none: break;
        }
    });
    return arrays;
}

void passElementArrays(lowering & lower, ctjs::FuncOp fn,
                       const llvm::DenseSet<mlir::Value> & arrays) {
    if (arrays.empty()) { return; }
    includeArrayHeader(fn->getParentOfType<mlir::ModuleOp>());
    const auto isSpan = [](mlir::Value v) {
        auto opaque = llvm::dyn_cast<ec::OpaqueType>(v.getType());
        return opaque && opaque.getValue().starts_with("std::span<");
    };
    // Everything is collected first: the rewrites below insert operations.
    llvm::SmallVector<ctjs::CallDirectOp> calls;
    llvm::SmallVector<ctjs::ReturnOp> returns;
    fn.getBody().walk([&](ctjs::CallDirectOp call) { calls.push_back(call); });
    fn.getBody().walk([&](ctjs::ReturnOp ret) { returns.push_back(ret); });
    // A call result moved into a local below is that local from then on: a
    // later call may lend it and a return may move it.
    llvm::DenseMap<mlir::Value, mlir::Value> owners;
    const auto owner = [&](mlir::Value v) {
        const auto found = owners.find(v);
        return found == owners.end() ? v : found->second;
    };
    const auto isArray = [&](mlir::Value v) {
        return arrays.contains(v) || llvm::is_contained(llvm::make_second_range(owners), v);
    };
    for (ctjs::CallDirectOp call : calls) {
        mlir::OpBuilder at(call);
        // Operands 0-2 are the implicit arguments; the rest are lent as spans.
        for (unsigned i = 3; i < call->getNumOperands(); ++i) {
            mlir::Value argument = call->getOperand(i);
            if (!isArray(argument) || isSpan(argument)) { continue; }
            // The lattice still knows the source value's type (a local's comes
            // from the call result it holds): only operations are replaced
            // after this, never values.
            mlir::Value source = argument;
            for (const auto & [result, local] : owners) {
                if (local == argument) { source = result; }
            }
            const std::string span = elementSpanSpelling(lower.typeOf(source));
            call->setOperand(i, callValue(at, call.getLoc(),
                                          ec::OpaqueType::get(fn.getContext(), span), span,
                                          lowering::cellPlace(at, call.getLoc(), argument)));
        }
        // A returned array lands in a local that owns it from here on.
        if (call->getNumResults() == 1 && arrays.contains(call->getResult(0)) &&
            !isSpan(call->getResult(0))) {
            mlir::Value result = call->getResult(0);
            at.setInsertionPointAfter(call);
            auto local =
                ec::VariableOp::create(at, call.getLoc(), ec::LValueType::get(result.getType()),
                                       ec::OpaqueAttr::get(fn.getContext(), ""));
            auto assign = ec::AssignOp::create(at, call.getLoc(), local, result);
            result.replaceAllUsesExcept(local, assign);
            owners[result] = local;
        }
    }
    for (ctjs::ReturnOp ret : returns) {
        mlir::Value value = owner(ret.getValue());
        if (!isArray(value) || isSpan(value)) { continue; }
        mlir::OpBuilder at(ret);
        mlir::Value place = lowering::cellPlace(at, ret.getLoc(), value);
        auto vector = llvm::cast<ec::LValueType>(place.getType()).getValueType();
        // The owner ends with the function, so its contents move out.
        ret->setOperand(0, callValue(at, ret.getLoc(), vector, "std::move", place));
    }
}

bool replaceStringArray(lowering & lower, mlir::Operation * o) {
    auto action = o->getAttrOfType<mlir::StringAttr>(kStringArrayAction);
    if (!action) { return false; }
    includeArrayHeader(lower.module);
    mlir::MLIRContext * context = o->getContext();
    mlir::OpBuilder b(o);
    const mlir::Location where = o->getLoc();
    const auto swap = [&](mlir::Value with) {
        o->getResult(0).replaceAllUsesWith(with);
        lowering::eraseIfUnused(o);
    };
    const auto place = [&](mlir::Value array) { return lowering::cellPlace(b, where, array); };
    const auto numeric = carrierType(context, carrier::number);
    const auto vectorValue = ec::OpaqueType::get(context, kStringArrayType);
    llvm::StringRef kind = action.getValue();
    if (kind == "plumbing") {
        // A method read or RegExp literal the call below it consumes: a dead
        // placeholder, as for an erased property-key constant.
        swap(lower.numberConstant(b, where, std::numeric_limits<double>::quiet_NaN()));
        return true;
    }
    if (kind == "create") {
        // The vector, by value, in this frame (or in the region that made
        // it, when nothing carries it out), filled by the appends below.
        auto literal = llvm::cast<ctjs::CreateArrayOp>(o);
        // An empty literal carried out of a branch takes its slot at the
        // function's entry (TypeInference::elementVectorAliases).
        mlir::OpBuilder slot = b;
        if (literal.getElements().empty()) {
            if (auto fn = o->getParentOfType<mlir::FunctionOpInterface>();
                fn && o->getBlock() != &fn.getFunctionBody().front()) {
                slot.setInsertionPointToStart(&fn.getFunctionBody().front());
            }
        }
        mlir::Value local = ec::VariableOp::create(slot, where, literal.getResult().getType(),
                                                   ec::OpaqueAttr::get(context, ""));
        for (mlir::Value element : literal.getElements()) { lower.push(b, where, local, element); }
        swap(local);
        return true;
    }
    if (kind == "append") {
        auto append = llvm::cast<ctjs::AppendOp>(o);
        lower.push(b, where, place(append.getArray()), append.getElement());
        lowering::eraseIfUnused(o);
        return true;
    }
    if (kind == "truthy" || kind == "falsy") {
        const mlir::Value constant =
            ec::ConstantOp::create(b, where, b.getI1Type(), b.getBoolAttr(kind == "truthy"));
        // `!a` is a Boolean value; truthiness is the bare i1.
        swap(isBooleanCarrier(o->getResult(0).getType())
                 ? lower.convertScalar(b, where, constant, o->getResult(0).getType())
                 : constant);
        return true;
    }
    if (kind == "method_test") {
        swap(lower.boolConstant(b, where, true));
        return true;
    }
    if (kind == "bounding_rect") {
        swap(callValue(b, where, o->getResult(0).getType(), "ctnative::getBoundingClientRect",
                       llvm::cast<ctjs::CallOp>(o).getReceiver()));
        return true;
    }
    if (kind == "computed_style") {
        swap(callValue(b, where, o->getResult(0).getType(), "ctnative::getComputedStyle",
                       llvm::cast<ctjs::CallOp>(o).getArgs()[0]));
        return true;
    }
    if (kind == "property_value") {
        auto call = llvm::cast<ctjs::CallOp>(o);
        swap(memberCall(b, where, carrierType(context, carrier::string), call.getReceiver(),
                        "getPropertyValue", call.getArgs()[0]));
        return true;
    }
    if (kind == "dead_rect") {
        swap(ec::ConstantOp::create(b, where, o->getResult(0).getType(),
                                    ec::OpaqueAttr::get(context, "ctnative::js_dom_rect{}")));
        return true;
    }
    if (llvm::StringRef field = kind; field.consume_front("rect:")) {
        swap(callValue(b, where, numeric, "ctnative::rect_" + field, o->getOperand(0)));
        return true;
    }
    if (kind == "document_element") {
        // Null only for a document with no root element, where the
        // interpreter throws a TypeError on the first member read.
        swap(callValue(b, where, ec::OpaqueType::get(context, kPageElementType),
                       "ctnative::require_document_element", mlir::ValueRange{}));
        return true;
    }
    if (kind == "device_pixel_ratio") {
        swap(ec::LiteralOp::create(b, where, numeric, "ctnative::window.devicePixelRatio"));
        return true;
    }
    if (llvm::StringRef member = kind; member.consume_front("member:")) {
        // tagName, id and className are the view's members of those names.
        swap(memberCall(b, where, carrierType(context, carrier::string), o->getOperand(0), member));
        return true;
    }
    if (llvm::StringRef size = kind; size.consume_front("client:")) {
        swap(callValue(b, where, numeric, "ctnative::" + size, o->getOperand(0)));
        return true;
    }
    if (kind == "children") {
        // A snapshot the frame owns, like a split result.
        mlir::Value local = ec::VariableOp::create(b, where, o->getResult(0).getType(),
                                                   ec::OpaqueAttr::get(context, ""));
        auto owner = llvm::cast<ec::LValueType>(o->getResult(0).getType()).getValueType();
        ec::AssignOp::create(b, where, local,
                             memberCall(b, where, owner, o->getOperand(0), "children"));
        swap(local);
        return true;
    }
    if (kind == "length") {
        swap(callValue(b, where, numeric, "ctnative::vec_length", place(o->getOperand(0))));
        return true;
    }
    if (kind == "tuple" || kind == "tuple_part") {
        // Built and erased by the push that copies the tuple out.
        return true;
    }
    if (kind == "position0" || kind == "position1") {
        // `t[0]` and `t[1]` on a std::pair.
        swap(callValue(b, where, o->getResult(0).getType(),
                       kind == "position0" ? "std::get<0>" : "std::get<1>", o->getOperand(0)));
        return true;
    }
    if (kind == "element") {
        // An index proved in bounds reads the element itself.
        swap(callValue(b, where, o->getResult(0).getType(), "ctnative::vec_element",
                       mlir::ValueRange{place(o->getOperand(0)), o->getOperand(1)}));
        return true;
    }
    if (kind == "at" || kind == "at_number") {
        const mlir::Type result = o->getResult(0).getType();
        const mlir::Value value = callValue(
            b, where,
            carrierType(context, kind == "at" ? carrier::nullableString : carrier::nullable),
            "ctnative::vec_at", mlir::ValueRange{place(o->getOperand(0)), o->getOperand(1)});
        swap(lower.convertScalar(b, where, value, result));
        return true;
    }
    if (kind == "callback") { return replaceArrayCallback(lower, o); }
    if (kind == "host_callback") { return replaceHostCallback(lower, o); }
    if (kind == "promise") { return replacePromise(lower, o); }
    if (kind.starts_with("event") || kind.starts_with("timer_") || kind.starts_with("host_")) {
        return replaceEvent(lower, o, kind);
    }
    if (kind.starts_with("element_")) { return replaceElement(o, kind); }
    auto call = llvm::cast<ctjs::CallOp>(o);
    if (kind == "push") {
        mlir::Value value = call.getArgs()[0];
        mlir::Operation * tuple = value.getDefiningOp();
        const bool row =
            tuple && tuple->hasAttr(kStringArrayAction) &&
            tuple->getAttrOfType<mlir::StringAttr>(kStringArrayAction).getValue() == "tuple";
        llvm::SmallVector<mlir::Operation *, 2> parts;
        if (row) {
            // Build the std::pair the array holds from the tuple's parts, each
            // widened to its position in the array's element type.
            auto recorded = o->getAttrOfType<mlir::TypeAttr>(kStringArrayRow);
            auto element = recorded ? llvm::dyn_cast<TupleType>(recorded.getValue()) : TupleType{};
            if (!element) {
                llvm::report_fatal_error("ctnative lowering: a push of a row onto an array whose "
                                         "planned element type is not a tuple - admission and "
                                         "the plan disagree");
            }
            llvm::SmallVector<mlir::Value, 2> positions;
            auto literal = llvm::cast<ctjs::CreateArrayOp>(tuple);
            llvm::append_range(positions, literal.getElements());
            for (mlir::Operation * user : literal.getResult().getUsers()) {
                if (llvm::isa<ctjs::AppendOp>(user)) { parts.push_back(user); }
            }
            llvm::sort(parts, [](mlir::Operation * a, mlir::Operation * b) {
                return a->isBeforeInBlock(b);
            });
            for (mlir::Operation * part : parts) {
                positions.push_back(llvm::cast<ctjs::AppendOp>(part).getElement());
            }
            for (std::size_t index = 0; index < positions.size(); ++index) {
                positions[index] = lower.convertScalar(
                    b, where, positions[index],
                    carrierType(context, carrierOf(element.getElements()[index])));
            }
            const std::string pair = elementSpelling(element);
            value = callValue(b, where, ec::OpaqueType::get(context, pair), pair, positions);
        }
        const std::array<mlir::Value, 2> operands{place(call.getReceiver()), value};
        if (call.getResult().use_empty()) {
            // No result, so no unused variable under -Werror (see lowering::push).
            callWithConstValueOperands(b, where, mlir::TypeRange{},
                                       b.getStringAttr("ctnative::Array.prototype.push.call"),
                                       operands);
            lowering::eraseIfUnused(o);
        } else {
            swap(callValue(b, where, numeric, "ctnative::Array.prototype.push.call", operands));
        }
        // The tuple literal and its appends are now unused.
        for (mlir::Operation * part : parts) { lowering::eraseIfUnused(part); }
        if (row) { lowering::eraseIfUnused(tuple); }
        return true;
    }
    if (kind == "join") {
        // join() with no argument joins with "," (ECMA-262 23.1.3.18 step 3).
        const mlir::Value separator =
            call.getArgs().empty() ? lower.stringConstant(b, where, ",") : call.getArgs()[0];
        swap(callValue(b, where, carrierType(context, carrier::string),
                       "ctnative::Array.prototype.join.call",
                       mlir::ValueRange{place(call.getReceiver()), separator}));
        return true;
    }
    if (kind.consume_front("static ")) {
        // `ctnative::Number.parseInt(s, radix)`: admission proved each
        // argument the kind its parameter asks; only a Number converts.
        const auto [kinds, callee] = kind.split(' ');
        llvm::SmallVector<mlir::Value, 2> operands;
        for (auto [index, argument] : llvm::enumerate(call.getArgs())) {
            mlir::Value operand =
                kinds[index] == 'n' ? lower.convertScalar(b, where, argument, numeric) : argument;
            if (kinds[index] == 'e' && opaqueSpelling(operand.getType()) == kNullableElementType) {
                operand = callValue(b, where, ec::OpaqueType::get(context, kPageElementType),
                                    "ctnative::present_element", operand);
            }
            operands.push_back(operand);
        }
        // A returned array (querySelectorAll's snapshot) lands in a local
        // that owns it, as split's pieces do.
        if (auto owner = llvm::dyn_cast<ec::LValueType>(o->getResult(0).getType())) {
            mlir::Value local =
                ec::VariableOp::create(b, where, owner, ec::OpaqueAttr::get(context, ""));
            ec::AssignOp::create(b, where, local,
                                 callValue(b, where, owner.getValueType(), callee, operands));
            swap(local);
            return true;
        }
        swap(callValue(b, where, o->getResult(0).getType(), callee, operands));
        return true;
    }
    if (kind == "includes") {
        swap(callValue(b, where, o->getResult(0).getType(),
                       "ctnative::Array.prototype.includes.call",
                       mlir::ValueRange{place(call.getReceiver()), call.getArgs()[0]}));
        return true;
    }
    if (kind == "sort") {
        // `a.sort()` sorts in place and answers `a`.
        const mlir::Value sorted = place(call.getReceiver());
        callWithConstValueOperands(b, where, mlir::TypeRange{},
                                   b.getStringAttr("ctnative::Array.prototype.sort.call"),
                                   mlir::ValueRange{sorted});
        swap(sorted);
        return true;
    }
    if (kind == "split" || kind == "split_space") {
        mlir::Value local = ec::VariableOp::create(b, where, call.getResult().getType(),
                                                   ec::OpaqueAttr::get(context, ""));
        // The recognised /\s+/ separator is the runtime's tag for it.
        const mlir::Value separator =
            kind == "split"
                ? mlir::Value(call.getArgs()[0])
                : mlir::Value(ec::LiteralOp::create(
                      b, where, ec::OpaqueType::get(context, "ctnative::whitespace_run_t"),
                      "ctnative::whitespace_run"));
        const mlir::Value pieces =
            callValue(b, where, vectorValue, "ctnative::String.prototype.split.call",
                      mlir::ValueRange{call.getReceiver(), separator});
        ec::AssignOp::create(b, where, local, pieces);
        swap(local);
        return true;
    }
    llvm::report_fatal_error(llvm::Twine("ctnative lowering: unknown string array action `") +
                             kind + "`");
}

} // namespace ctcompile::ctnative::lowering_detail
