//===- constructors.hpp - Constructor tests ---------------------*- C++ -*-===//
//
// Declares the VM tests for calling and constructing functions: `this` binding
// through call/apply/bind, derived-class constructors, and subclasses of
// built-ins. Defined in constructors.cpp.
//
//===----------------------------------------------------------------------===//

#pragma once

/// `this` binding through call/apply/bind, and construct through bound functions.
void test_call_bind_this();
/// `this` and `super()` in a derived class's constructor.
void test_derived_constructors();
/// Classes that extend Array, typed arrays, Object and Function.
void test_builtin_subclasses();
