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
#include "ctcompile/Support/CppLiterals.hpp"

#include <array>
#include <limits>

namespace ctcompile::ctnative::lowering_detail {

// Array-method callback calls (ArrayCallbacks.cpp).
void planArrayCallback(lowering & lower, ctjs::CallOp call);
bool replaceArrayCallback(lowering & lower, mlir::Operation * o);
// Listeners handed to the host (HostCallbacks.cpp).
bool replaceHostCallback(lowering & lower, mlir::Operation * o);

namespace {

/// The attribute holding an operation's planned lowering.
constexpr llvm::StringLiteral kStringArrayAction = "ctnative.string_array";
/// On a push of a tuple literal, the array's element type: the std::pair the
/// push builds. Recorded with the plan because the receiver is an emitc value
/// with no lattice type by the time the push is replaced.
constexpr llvm::StringLiteral kStringArrayRow = "ctnative.string_array_row";
constexpr llvm::StringLiteral kArrayHeader = "ctcompile/CTNative/Runtime/Array.hpp";
/// markWrittenArrays' list (isWrittenArray).
constexpr llvm::StringLiteral kWrittenArrays = "ctnative.written_arrays";
/// On the document.documentElement read, the property the program reads of it
/// first (TypeInference::firstPropertyRead).
constexpr llvm::StringLiteral kFirstRead = "ctnative.first_read";
/// On a row read (TypeInference::isRowRead), the position its first use reads.
constexpr llvm::StringLiteral kRowReading = "ctnative.row_reading";
/// On a built-in call taking a record, the record's keys in definition order.
constexpr llvm::StringLiteral kRecordKeys = "ctnative.record_keys";
constexpr llvm::StringLiteral kJsonHeader = "ctcompile/CTNative/Runtime/Json.hpp";

/// Adds `#include "<header>"` once, at the top of the module.
void includeHeader(mlir::ModuleOp module, llvm::StringRef header) {
    for (auto include : module.getBody()->getOps<ec::IncludeOp>()) {
        if (include.getInclude() == header) { return; }
    }
    mlir::OpBuilder b(module.getContext());
    b.setInsertionPointToStart(module.getBody());
    ec::IncludeOp::create(b, module.getLoc(), b.getStringAttr(header), mlir::UnitAttr{});
}

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
                if (key == "documentElement") {
                    op->setAttr(kFirstRead, mlir::StringAttr::get(op->getContext(),
                                                                  TypeInference::firstPropertyRead(
                                                                      read.getResult())));
                }
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
            // A row read records the position its first use reads, while the
            // keys are still constants.
            if (TypeInference::isRowRead(op)) {
                mlir::Operation * first = *read.getResult().getUsers().begin();
                for (mlir::Operation * user : read.getResult().getUsers()) {
                    if (user->getBlock() == first->getBlock() && user->isBeforeInBlock(first)) {
                        first = user;
                    }
                }
                op->setAttr(kRowReading, mlir::IntegerAttr::get(
                                             mlir::IntegerType::get(op->getContext(), 32),
                                             TypeInference::constantIndex(first->getOperand(1))));
            }
            mark(op, ctjs::constantKey(read.getKey()) == "length"                   ? "length"
                     : TypeInference::isBoundedIndexRead(op)                        ? "element"
                     : carrierOf(lower.typeOf(read.getResult())) == carrier::tuple  ? "row"
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
            // `s` (String) or `n` (Number) per parameter: the namespace object
            // and method read it was recognised by are plumbing, replaced
            // first, and so are String literal arguments.
            const TypeInference::BuiltinStatic & row = *TypeInference::builtinStaticOf(call);
            using Param = TypeInference::BuiltinStatic::Param;
            std::string action = "static ";
            for (unsigned i = 0; i < row.arity; ++i) {
                // A table (a dictionary) is passed as it is: `d`.
                const bool table =
                    row.params[i] == Param::record && i < call.getArgs().size() &&
                    carrierOf(lower.typeOf(call.getArgs()[i])) == carrier::dictionary;
                action += row.params[i] == Param::string   ? 's'
                          : table                          ? 'd'
                          : row.params[i] == Param::record ? 'r'
                                                           : 'n';
                // A record's keys, in definition order, read now while its
                // writes are still ctjs.
                if (row.params[i] == Param::record && !table && i < call.getArgs().size()) {
                    llvm::SmallVector<mlir::Attribute> keys;
                    for (llvm::StringRef key : TypeInference::recordKeys(call.getArgs()[i])) {
                        keys.push_back(mlir::StringAttr::get(op->getContext(), key));
                    }
                    op->setAttr(kRecordKeys, mlir::ArrayAttr::get(op->getContext(), keys));
                }
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

void markWrittenArrays(llvm::ArrayRef<ctjs::FuncOp> accepted) {
    for (ctjs::FuncOp fn : accepted) {
        if (fn.getBody().empty()) { continue; }
        llvm::SmallVector<int32_t> written;
        for (mlir::BlockArgument argument : fn.getBody().front().getArguments().drop_front(3)) {
            if (!TypeInference::elementVectorAliases(argument).empty() &&
                TypeInference::elementParameterWritten(argument)) {
                written.push_back(static_cast<int32_t>(argument.getArgNumber()));
            }
        }
        if (written.empty()) { continue; }
        const auto list = mlir::Builder(fn.getContext()).getDenseI32ArrayAttr(written);
        fn->setAttr(kWrittenArrays, list);
        fn->getParentOfType<mlir::ModuleOp>().walk([&](ctjs::CallDirectOp call) {
            if (call.getCallee() == fn.getSymName()) { call->setAttr(kWrittenArrays, list); }
        });
    }
}

bool isWrittenArray(mlir::Operation * op, unsigned index) {
    auto list = op->getAttrOfType<mlir::DenseI32ArrayAttr>(kWrittenArrays);
    return list && llvm::is_contained(list.asArrayRef(), static_cast<int32_t>(index));
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
        // Operands 0-2 are the implicit arguments; the rest are lent as spans,
        // or by address to a callee that writes them.
        for (unsigned i = 3; i < call->getNumOperands(); ++i) {
            mlir::Value argument = call->getOperand(i);
            if (!isArray(argument) || isSpan(argument)) { continue; }
            if (isWrittenArray(call, i)) {
                if (auto local = llvm::dyn_cast<ec::LValueType>(argument.getType())) {
                    call->setOperand(i, ec::AddressOfOp::create(
                                            at, call.getLoc(),
                                            ec::PointerType::get(local.getValueType()), argument));
                }
                continue;
            }
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
        mlir::Value local = ec::VariableOp::create(b, where, literal.getResult().getType(),
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
        // interpreter throws a TypeError on the first member read, naming it.
        const llvm::StringRef reading = o->getAttrOfType<mlir::StringAttr>(kFirstRead).getValue();
        const mlir::Value name = ec::LiteralOp::create(
            b, where, ec::PointerType::get(ec::OpaqueType::get(context, "const char")),
            cpp::c_string_literal(std::string_view(reading.data(), reading.size())));
        swap(callValue(b, where, ec::OpaqueType::get(context, kPageElementType),
                       "ctnative::require_document_element", mlir::ValueRange{name}));
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
    if (kind == "row") {
        // A row every use of which reads a position (TypeInference::isRowRead);
        // the position read first names the error past the end.
        const auto reading = o->getAttrOfType<mlir::IntegerAttr>(kRowReading).getInt();
        const mlir::Value position = ec::LiteralOp::create(
            b, where, ec::OpaqueType::get(context, "int"), std::to_string(reading));
        swap(callValue(b, where, o->getResult(0).getType(), "ctnative::vec_row",
                       mlir::ValueRange{place(o->getOperand(0)),
                                        lower.convertScalar(b, where, o->getOperand(1), numeric),
                                        position}));
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
                // An element position holds the runtime's view, as the
                // element itself is typed (retype); the rest widen to their
                // carrier.
                const mlir::Type part = element.getElements()[index];
                const mlir::Type target =
                    carrierOf(part) == carrier::domElement
                        ? mlir::Type(ec::OpaqueType::get(context, kPageElementType))
                        : carrierType(context, carrierOf(part));
                positions[index] = lower.convertScalar(b, where, positions[index], target);
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
        // argument a String or a Number, as its parameter asks. A record is
        // `ctnative::json_record("key", value, ...)` of its struct's fields.
        const auto [kinds, callee] = kind.split(' ');
        llvm::SmallVector<mlir::Value, 2> operands;
        for (auto [index, argument] : llvm::enumerate(call.getArgs())) {
            if (kinds[index] == 'd') {
                includeHeader(o->getParentOfType<mlir::ModuleOp>(), kJsonHeader);
                operands.push_back(argument);
                continue;
            }
            if (kinds[index] == 'r') {
                includeHeader(o->getParentOfType<mlir::ModuleOp>(), kJsonHeader);
                const auto text = ec::PointerType::get(ec::OpaqueType::get(context, "const char"));
                const auto any = ec::OpaqueType::get(context, "auto");
                llvm::SmallVector<mlir::Value> fields;
                for (mlir::Attribute key : o->getAttrOfType<mlir::ArrayAttr>(kRecordKeys)) {
                    const llvm::StringRef name = llvm::cast<mlir::StringAttr>(key).getValue();
                    fields.push_back(
                        ec::LiteralOp::create(b, where, text, ("\"" + name + "\"").str()));
                    fields.push_back(ec::LoadOp::create(
                        b, where, any, lower.memberAccess(b, where, argument, name, any)));
                }
                operands.push_back(callValue(b, where,
                                             ec::OpaqueType::get(context, "ctbrowser::json_value"),
                                             "ctnative::json_record", fields));
                continue;
            }
            operands.push_back(
                kinds[index] == 's' ? argument : lower.convertScalar(b, where, argument, numeric));
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
