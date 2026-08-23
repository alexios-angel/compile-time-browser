//===- CTJSEnums.td - the kinds that fold variants into one operation -----===//
//
// "Folding six comparison operations into one op carrying a CompareKind is
// preferred over six separate operations whenever the operands, results,
// effects, and lowering are identical. It collapses six ODS records, six
// lowering patterns, and six test files into one of each. Split into distinct
// operations only when they differ in traits, effects, arity, or verification."
//
// EVERY ONE OF THESE IS A DISTINCTION THE RUNTIME ALREADY MAKES. The binary
// kinds are the fourteen `op::` enumerators context::binary_op and
// binary_op_static dispatch on; the compare kinds are what the four relational
// opcodes and the two equality families reduce to. Inventing a different
// vocabulary here would mean a translation step that can be wrong.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_ENUMS
#define CTCOMPILE_CTJS_ENUMS

include "mlir/IR/EnumAttr.td"

// THE TWO FAMILIES ARE ONE ENUM AND TWO OPERATIONS, not one operation with a
// flag: `add` and `add_generic` differ in whether they can run user code, which
// is a difference in TRAITS, and the policy says to split on exactly that.
def CTJS_BinaryKind : I32EnumAttr<"BinaryKind", "JavaScript binary operator", [
    I32EnumAttrCase<"Add",    0,  "add">,
    I32EnumAttrCase<"Sub",    1,  "sub">,
    I32EnumAttrCase<"Mul",    2,  "mul">,
    I32EnumAttrCase<"Div",    3,  "div">,
    I32EnumAttrCase<"Mod",    4,  "mod">,
    I32EnumAttrCase<"Pow",    5,  "pow">,
    I32EnumAttrCase<"Shl",    6,  "shl">,
    I32EnumAttrCase<"Shr",    7,  "shr">,
    I32EnumAttrCase<"UShr",   8,  "ushr">,
    I32EnumAttrCase<"BitAnd", 9,  "bitand">,
    I32EnumAttrCase<"BitOr",  10, "bitor">,
    I32EnumAttrCase<"BitXor", 11, "bitxor">,
    // CONCAT IS NOT `add`, and the runtime is emphatic about it: it is emitted
    // only by template literals, ToStrings both sides unconditionally, and must
    // never reach the BigInt arm - `${1n}` would throw "BigInts have no
    // unsigned right shift" from a switch with no case for it.
    I32EnumAttrCase<"Concat", 12, "concat">,
  ]> {
  let cppNamespace = "::ctcompile::ctjs";
  let genSpecializedAttr = 1;
}

def CTJS_UnaryKind : I32EnumAttr<"UnaryKind", "JavaScript unary operator", [
    I32EnumAttrCase<"Neg",    0, "neg">,
    I32EnumAttrCase<"Plus",   1, "plus">,
    I32EnumAttrCase<"Not",    2, "not">,
    I32EnumAttrCase<"BitNot", 3, "bitnot">,
    I32EnumAttrCase<"TypeOf", 4, "typeof">,
    I32EnumAttrCase<"Void",   5, "void">,
  ]> {
  let cppNamespace = "::ctcompile::ctjs";
  let genSpecializedAttr = 1;
}

def CTJS_CompareKind : I32EnumAttr<"CompareKind", "JavaScript comparison kind", [
    I32EnumAttrCase<"Eq",       0, "eq">,
    I32EnumAttrCase<"StrictEq", 1, "strict_eq">,
    I32EnumAttrCase<"Lt",       2, "lt">,
    I32EnumAttrCase<"Le",       3, "le">,
    I32EnumAttrCase<"Gt",       4, "gt">,
    I32EnumAttrCase<"Ge",       5, "ge">,
  ]> {
  let cppNamespace = "::ctcompile::ctjs";
  let genSpecializedAttr = 1;
}

def CTJS_ConvertKind : I32EnumAttr<"ConvertKind", "ECMAScript abstract conversion", [
    I32EnumAttrCase<"ToBoolean",     0, "to_boolean">,
    I32EnumAttrCase<"ToNumber",      1, "to_number">,
    I32EnumAttrCase<"ToString",      2, "to_string">,
    I32EnumAttrCase<"ToObject",      3, "to_object">,
    I32EnumAttrCase<"ToPrimitive",   4, "to_primitive">,
    I32EnumAttrCase<"ToPropertyKey", 5, "to_property_key">,
  ]> {
  let cppNamespace = "::ctcompile::ctjs";
  let genSpecializedAttr = 1;
}

def CTJS_SuspendKind : I32EnumAttr<"SuspendKind", "How a resumable function suspends", [
    I32EnumAttrCase<"Await",     0, "await">,
    I32EnumAttrCase<"Yield",     1, "yield">,
    I32EnumAttrCase<"YieldStar", 2, "yield_star">,
  ]> {
  let cppNamespace = "::ctcompile::ctjs";
  let genSpecializedAttr = 1;
}

#endif // CTCOMPILE_CTJS_ENUMS
