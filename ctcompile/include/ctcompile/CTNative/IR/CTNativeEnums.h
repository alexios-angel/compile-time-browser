//===- CTNativeEnums.h - ctnative enums -------------------------*- C++ -*-===//
//
// The generated NumKind, StrEncoding and EscapeReason enums and their
// stringification.
//
// Separate from CTNativeTypes.h for the reason CTJSEnums.h is separate from
// CTJSAttrs.h: the enums are the parameters of the parameterized types, so
// anything that includes the types needs these first.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/StringRef.h"

#include "ctcompile/CTNative/IR/CTNativeEnums.h.inc"
