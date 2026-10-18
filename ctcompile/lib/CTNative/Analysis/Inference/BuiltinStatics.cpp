//===- BuiltinStatics.cpp - Calls of built-in static methods --------------===//
//
// The table of the realm's built-ins native code calls through the runtime's
// built-in objects: statics of namespace objects (`Number.parseInt`,
// `Number.parseFloat`, `Math.min`, `JSON.stringify`, `console.log`), globals
// called themselves (`Number(s)`) and prototype methods called through `call`
// (`String.prototype.toLowerCase.call(s)`), and the recognition of a call of
// one. Each lowers to its runtime spelling, `ctnative::Number.parseInt(s, r)`.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Lowering/NativeRealm.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "mlir/IR/BuiltinOps.h"

namespace ctcompile::ctnative {

namespace {

using Param = TypeInference::BuiltinStatic::Param;
using Result = TypeInference::BuiltinStatic::Result;

/// The one place built-ins are recognised. Each row is a method whose runtime member
/// (Runtime/Number.hpp, Runtime/Math.hpp, Runtime/String.hpp) implements the
/// spec for the parameter types listed; a call with fewer than `required`
/// arguments or more than `arity` (the receiver counted, for a prototype row)
/// is not recognised.
constexpr TypeInference::BuiltinStatic builtinStatics[] = {
    // Number.parseInt (ECMA-262 21.1.2.13); a missing radix is 0.
    {"Number", "parseInt", "ctnative::Number.parseInt", {Param::string, Param::number}, 2, 1},
    // Number.parseFloat (21.1.2.12) of a String.
    {"Number", "parseFloat", "ctnative::Number.parseFloat", {Param::string}, 1, 1},
    // Math.min (21.3.2.25) of exactly two Numbers.
    {"Math", "min", "ctnative::Math.min", {Param::number, Param::number}, 2, 2},
    // console.log (WHATWG Console 1.1.6) of up to three printable values,
    // one line in the VM's format (Runtime/Console.hpp).
    {"console",
     "log",
     "ctnative::console.log",
     {Param::printable, Param::printable, Param::printable},
     3,
     1,
     Result::undefined},
    // String.prototype.toLowerCase (22.1.3.28) of a String receiver.
    {"String",
     "toLowerCase",
     "ctnative::String.prototype.toLowerCase.call",
     {Param::string},
     1,
     1,
     Result::string,
     true},
    // Object.prototype.toString (20.1.3.6) of a primitive: Bootstrap's toType
    // reads the type name out of "[object Number]". A toType called with
    // several kinds is copied per kind (Lowering/ArgumentTypeCopies.cpp).
    {"Object",
     "toString",
     "ctnative::Object.prototype.toString.call",
     {Param::printable},
     1,
     1,
     Result::string,
     true},
    // Element.prototype.querySelectorAll (DOM 4.2.6) and querySelector:
    // Bootstrap's SelectorEngine calls them through the prototype on its root
    // element, which may be the document element read as a default.
    {"Element",
     "querySelectorAll",
     "ctnative::Element.prototype.querySelectorAll.call",
     {Param::element, Param::string},
     2,
     2,
     Result::elementVector,
     true},
    {"Element",
     "querySelector",
     "ctnative::Element.prototype.querySelector.call",
     {Param::element, Param::string},
     2,
     2,
     Result::nullableElement,
     true},
    // Number(value) (21.1.1.1) of a String: ToNumber. An empty member is a
    // call of the global itself.
    {"Number", "", "ctnative::Number", {Param::string}, 1, 1},
    // JSON.stringify (25.5.2.1) of an object literal or a dictionary, with no
    // replacer and no space: ctnative::JSON.stringify of the literal's
    // ctnative::json_record, or of the table itself.
    {"JSON", "stringify", "ctnative::JSON.stringify", {Param::record}, 1, 1, Result::string},
    // Object.entries, Object.keys and Object.values (ECMA-262 20.1.2.5, .18,
    // .22) of a constant table (Runtime/Object.hpp's object_table).
    {"Object",
     "entries",
     "ctnative::Object.entries",
     {Param::staticTable},
     1,
     1,
     Result::tableEntries},
    {"Object", "keys", "ctnative::Object.keys", {Param::staticTable}, 1, 1, Result::tableKeys},
    {"Object",
     "values",
     "ctnative::Object.values",
     {Param::staticTable},
     1,
     1,
     Result::tableValues},
};

/// Whether --ctjs-resolve-globals found that this program never binds
/// `name`, so a read of it is the realm's own value: its `ctjs.globals` row
/// carries kGlobalsHostBinding, which it sets only when no store, no write
/// through the global object and no dynamic global write can reach the name
/// (ResolveGlobals.cpp, bound_closure and clause 5).
bool realmBinding(mlir::Operation * op, llvm::StringRef name) {
    auto module = op->getParentOfType<mlir::ModuleOp>();
    auto rows = module ? module->getAttrOfType<mlir::ArrayAttr>("ctjs.globals") : nullptr;
    if (!rows) { return false; }
    for (auto row : rows.getAsRange<mlir::DictionaryAttr>()) {
        auto bound = row.getAs<mlir::StringAttr>("name");
        if (bound && bound.getValue() == name) { return row.contains(ctjs::kGlobalsHostBinding); }
    }
    return false;
}

} // namespace

const TypeInference::BuiltinStatic * TypeInference::builtinStaticOf(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    if (!call) { return nullptr; }
    const std::size_t count = call.getArgs().size();
    // A call of the global itself, `Number(s)`.
    if (auto callee = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>()) {
        for (const BuiltinStatic & row : builtinStatics) {
            if (row.member.empty() && callee.getName() == row.global && count >= row.required &&
                count <= row.arity) {
                return realmBinding(call, row.global) ? &row : nullptr;
            }
        }
        return nullptr;
    }
    auto read = ctjs::receiverMethod(call);
    if (!read || read.getObject() != call.getReceiver() || !read.getResult().hasOneUse()) {
        return nullptr;
    }
    // `G.member(args)` reads the method off the namespace object, its
    // receiver; `G.prototype.member.call(receiver, args)` reads `call` off the
    // method, which is the receiver of that call.
    mlir::Value object = read.getObject();
    llvm::StringRef member = ctjs::methodKey(call);
    const bool prototype = member == "call";
    if (prototype) {
        auto method = object.getDefiningOp<ctjs::GetPropertyOp>();
        auto shared = method ? method.getObject().getDefiningOp<ctjs::GetPropertyOp>() : nullptr;
        if (!shared || ctjs::constantKey(shared.getKey()) != "prototype") { return nullptr; }
        member = ctjs::constantKey(method.getKey());
        object = shared.getObject();
    }
    auto global = object.getDefiningOp<ctjs::LoadGlobalOp>();
    if (!global) { return nullptr; }
    for (const BuiltinStatic & row : builtinStatics) {
        if (row.prototype == prototype && global.getName() == row.global && member == row.member &&
            count >= row.required && count <= row.arity) {
            return realmBinding(call, row.global) ? &row : nullptr;
        }
    }
    return nullptr;
}

} // namespace ctcompile::ctnative
