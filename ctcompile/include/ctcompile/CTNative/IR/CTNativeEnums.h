#pragma once
// The generated NumKind and StrEncoding, and their stringification.
//
// Separate from CTNativeTypes.h for the reason CTJSEnums.h is separate from
// CTJSAttrs.h: the enums are what the parameterized types are ABOUT, so
// anything including the types needs these first.
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/StringRef.h"

#include "ctcompile/CTNative/IR/CTNativeEnums.h.inc"
