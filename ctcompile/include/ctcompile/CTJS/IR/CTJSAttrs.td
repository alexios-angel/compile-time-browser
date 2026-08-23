//===- CTJSAttrs.td - JavaScript constants as attributes ------------------===//
//
// NOT BUILTIN ATTRIBUTES, and the policy gives the reason in one sentence:
// "-0.0 and NaN are the reason for APFloat and for a dedicated attribute. A
// builtin FloatAttr comparison that treats -0.0 as 0.0 would produce a silent,
// hard-to-find miscompile."
//
// This project has already paid that bill once at the other end of the
// pipeline: the program-image round trip keeps -0 and NaN explicitly, and a
// differential over 10,976 operand pairs distinguishes the two zeroes by their
// sign bit because `to_string` prints both as "0".
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTJS_ATTRS
#define CTCOMPILE_CTJS_ATTRS

include "ctcompile/CTJS/IR/CTJSBase.td"

def CTJS_UndefinedAttr : CTJS_Attr<"Undefined", "undefined"> {
  let summary = "The JavaScript value `undefined`";
}

def CTJS_NullAttr : CTJS_Attr<"Null", "null"> {
  let summary = "The JavaScript value `null`";
}

def CTJS_BooleanAttr : CTJS_Attr<"Boolean", "boolean"> {
  let summary = "`true` or `false`";
  let parameters = (ins "bool":$value);
  let assemblyFormat = "`<` $value `>`";
}

def CTJS_NumberAttr : CTJS_Attr<"Number", "number"> {
  let summary = "An IEEE-754 double, carried as its bit pattern";
  let description = [{
    THE BITS, NOT A FLOAT, and this is a deviation from the policy's snippet
    with the reason the policy asks for.

    Its objection to a builtin FloatAttr is exactly right: "-0.0 and NaN are
    the reason for APFloat and for a dedicated attribute. A builtin FloatAttr
    comparison that treats -0.0 as 0.0 would produce a silent, hard-to-find
    miscompile." `Object.is(-0, 0)` is false and `1 / -0` is -Infinity.

    But MLIR 22 has NO FieldParser for `::llvm::APFloat` - there is one for
    every integral type and for APInt, and none for a float - so an APFloat
    parameter generates a parser that does not compile. The alternatives were
    a hand-written parser, which is on the policy's "never hand-written" list,
    or a printer that goes through a double and cannot round-trip a NaN
    payload.

    A 64-bit pattern is exact for every double there is, including both zeroes
    and every NaN, needs no parser of its own, and compares bitwise because
    integers do. It reads worse than `#ctjs.number<1.5>` and it is the only
    one of the three that is both declarative and correct.
  }];
  let parameters = (ins "uint64_t":$bits);
  let assemblyFormat = "`<` $bits `>`";
  let extraClassDeclaration = [{
    /// The value these bits denote. A trivial accessor, which is all
    /// extraClassDeclaration is for.
    double getDouble() const {
      return ::llvm::bit_cast<double>(getBits());
    }
  }];
}

def CTJS_StringAttr : CTJS_Attr<"String", "string"> {
  let summary = "A JavaScript string literal";
  let parameters = (ins StringRefParameter<"">:$value);
  let assemblyFormat = "`<` $value `>`";
}

def CTJS_BigIntAttr : CTJS_Attr<"BigInt", "bigint"> {
  let summary = "A BigInt literal, carried as its source text";
  let description = [{
    The TEXT rather than a parsed integer, which is what the runtime does:
    ct_aot_new_bigint_literal takes the literal's characters and interns the
    result, and a literal the lexer accepted but that is not an integer -
    `1.5n` - substitutes 0n rather than throwing. Parsing here would be a
    second implementation of that rule.
  }];
  let parameters = (ins StringRefParameter<"">:$text);
  let assemblyFormat = "`<` $text `>`";
}

// NO KIND ATTRIBUTES HERE, and the absence is the finding.
//
// An I32EnumAttr with genSpecializedAttr - the default, and what the policy's
// snippet sets explicitly - makes -gen-enum-decls generate the `<Name>Attr`
// class ITSELF. Adding EnumAttr records for the same five was a redefinition of
// classes that already existed, and the "no type named BinaryKind" error that
// prompted it was only CTJSOps.h failing to include the generated enum header.
//
// The policy was right and the fix was one #include.

#endif // CTCOMPILE_CTJS_ATTRS
