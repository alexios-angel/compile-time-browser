#pragma once
// The StaticTyped operation interface.
//
// IT NEEDS THE DIALECT CLASS, not just the op machinery: the shared declaration
// `hasProvedNativeType()` asks whether the proved type belongs to this dialect,
// which is an `isa<CTNativeDialect>` and therefore a complete type.
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/OpDefinition.h"

#include "ctcompile/CTNative/IR/CTNativeOpInterfaces.h.inc"
