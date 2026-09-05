#include "Heap.h"

#include <ctbrowser/script/vm.hpp>

#include <bit>
#include <cmath>
#include <compare>
#include <memory>

namespace ctcompile::ctnative::partial_eval {
namespace {
namespace runtime = ctbrowser::script;

// A primitive-only adapter. Strings have ordinary C++ ownership; they never
// enter a script heap. Graph references cannot cross this boundary, so the
// runtime helpers cannot invoke an object's conversion hook or host code.
struct primitiveInput {
    std::unique_ptr<runtime::string_object> string;
    std::optional<runtime::value> data;
    explicit primitiveInput(value input) {
        if (input.tag != value::kind::constant) { return; }
        if (auto n = llvm::dyn_cast<ctjs::NumberAttr>(input.constant)) {
            // CTJS numbers can carry any IEEE NaN payload. Purify before the
            // runtime's NaN-boxed representation, whose spare bits are tags.
            const double d = n.getDouble();
            data = runtime::value::number(std::isnan(d) ? runtime::canonical_nan() : d);
        } else if (auto b = llvm::dyn_cast<ctjs::BooleanAttr>(input.constant)) {
            data = runtime::value::boolean(b.getValue());
        } else if (llvm::isa<ctjs::NullAttr>(input.constant)) {
            data = runtime::value::null();
        } else if (llvm::isa<ctjs::UndefinedAttr>(input.constant)) {
            data = runtime::value::undefined();
        } else if (auto s = llvm::dyn_cast<ctjs::StringAttr>(input.constant)) {
            if (s.getValue().size() > 65536) { return; }
            string = std::make_unique<runtime::string_object>(s.getValue().str());
            data = runtime::value::object(string.get());
        } else if (auto n = llvm::dyn_cast<mlir::IntegerAttr>(input.constant)) {
            // Integer attributes are SCF control flags, not JavaScript integers.
            data = runtime::value::number(static_cast<double>(n.getInt()));
        }
    }
};

value exported(runtime::value result, mlir::MLIRContext * context) {
    if (result.is_number()) { return numeric(context, result.as_number()); }
    if (result.is_boolean()) { return boolean(context, result.as_boolean()); }
    if (result.is_null()) { return value::primitive(ctjs::NullAttr::get(context)); }
    if (result.is_undefined()) { return value::primitive(ctjs::UndefinedAttr::get(context)); }
    if (result.is_string()) {
        const auto & text = static_cast<runtime::string_object *>(result.as_heap())->text;
        if (text.size() > 65536) { return {}; }
        return value::primitive(ctjs::StringAttr::get(context, text));
    }
    return {}; // No runtime heap object can enter the residual graph.
}
} // namespace

value numeric(mlir::MLIRContext * context, double input) {
    return value::primitive(ctjs::NumberAttr::get(context, std::bit_cast<uint64_t>(input)));
}
value boolean(mlir::MLIRContext * context, bool input) {
    return value::primitive(ctjs::BooleanAttr::get(context, input));
}

std::optional<bool> truthy(value input) {
    if (input.tag == value::kind::reference) { return true; }
    primitiveInput imported(input);
    if (!imported.data) { return {}; }
    return runtime::context::truthy(*imported.data);
}

bool sameValue(value left, value right, bool mapKey) {
    if (left.tag == value::kind::reference || right.tag == value::kind::reference) {
        return left.tag == right.tag && left.node == right.node;
    }
    // Heap-key equality need not allocate a runtime string. It must remain
    // exact even when a key exceeds the primitive execution string budget.
    auto aString = llvm::dyn_cast_or_null<ctjs::StringAttr>(left.constant);
    auto bString = llvm::dyn_cast_or_null<ctjs::StringAttr>(right.constant);
    if (aString || bString) { return aString && bString && aString == bString; }
    primitiveInput a(left), b(right);
    if (!a.data || !b.data) { return false; }
    return mapKey ? a.data->same_value_zero(*b.data) : a.data->strict_equals(*b.data);
}

value binary(ctjs::BinaryKind kind, value left, value right, mlir::MLIRContext * context,
             bool staticConversions) {
    primitiveInput a(left), b(right);
    if (!a.data || !b.data) { return {}; }
    runtime::op opcode;
    switch (kind) {
    case ctjs::BinaryKind::Add:
        opcode = staticConversions ? runtime::op::add : runtime::op::add_generic;
        break;
    case ctjs::BinaryKind::Sub: opcode = runtime::op::sub; break;
    case ctjs::BinaryKind::Mul: opcode = runtime::op::mul; break;
    case ctjs::BinaryKind::Div: opcode = runtime::op::div; break;
    case ctjs::BinaryKind::Mod: opcode = runtime::op::mod; break;
    case ctjs::BinaryKind::Pow: opcode = runtime::op::pow; break;
    case ctjs::BinaryKind::Concat: opcode = runtime::op::concat; break;
    default: return {};
    }
    // This context has no program, builtins, host callbacks or graph objects.
    // It owns only helper-produced primitive strings until they are copied
    // back into MLIR attributes. No bytecode dispatcher is called.
    runtime::context engine;
    if (staticConversions && kind != ctjs::BinaryKind::Add) { return {}; }
    const auto result = staticConversions ? engine.binary_op_static(opcode, *a.data, *b.data)
                                          : engine.binary_op(opcode, *a.data, *b.data);
    return engine.failed() ? value{} : exported(result, context);
}

value compare(ctjs::CompareKind kind, value left, value right, mlir::MLIRContext * context) {
    const auto known = [](value input) {
        return input.tag == value::kind::constant || input.tag == value::kind::reference;
    };
    if (!known(left) || !known(right)) { return {}; }
    if (left.tag == value::kind::reference || right.tag == value::kind::reference) {
        if (kind == ctjs::CompareKind::StrictEq ||
            (kind == ctjs::CompareKind::Eq && left.tag == right.tag)) {
            return boolean(context, sameValue(left, right));
        }
        return {}; // An object-to-primitive comparison may execute user code.
    }
    primitiveInput a(left), b(right);
    if (!a.data || !b.data) { return {}; }
    if (kind == ctjs::CompareKind::StrictEq) {
        return boolean(context, a.data->strict_equals(*b.data));
    }
    runtime::context engine;
    if (kind == ctjs::CompareKind::Eq) {
        return boolean(context, engine.loose_equals(*a.data, *b.data));
    }
    const auto order = engine.compare_relational(*a.data, *b.data);
    switch (kind) {
    case ctjs::CompareKind::Lt: return boolean(context, std::is_lt(order));
    case ctjs::CompareKind::Le: return boolean(context, std::is_lteq(order));
    case ctjs::CompareKind::Gt: return boolean(context, std::is_gt(order));
    case ctjs::CompareKind::Ge: return boolean(context, std::is_gteq(order));
    default: return {};
    }
}

value unary(ctjs::UnaryKind kind, value input, mlir::MLIRContext * context) {
    if (kind == ctjs::UnaryKind::Not) {
        if (auto b = truthy(input)) { return boolean(context, !*b); }
        return {};
    }
    if (kind == ctjs::UnaryKind::Void) {
        return value::primitive(ctjs::UndefinedAttr::get(context));
    }
    primitiveInput imported(input);
    if (!imported.data) { return {}; }
    runtime::context engine;
    switch (kind) {
    case ctjs::UnaryKind::Plus: return numeric(context, engine.to_number_value(*imported.data));
    case ctjs::UnaryKind::Neg: return exported(engine.negate_value(*imported.data), context);
    case ctjs::UnaryKind::TypeOf:
        return value::primitive(
            ctjs::StringAttr::get(context, runtime::context::type_of(*imported.data)));
    default: return {};
    }
}

} // namespace ctcompile::ctnative::partial_eval
