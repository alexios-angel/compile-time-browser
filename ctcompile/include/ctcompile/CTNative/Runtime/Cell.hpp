//===- Cell.hpp - Bindings shared with kept callbacks -----------*- C++ -*-===//
//
// A JavaScript binding that a callback the runtime keeps can write: a timer
// handler, an event listener or a promise handler that runs after the frame
// declaring the binding has returned. A frame-local variable reached by
// pointer would dangle then, so the frame and every closure over the
// binding share one cell through a std::shared_ptr, and the last of them to
// go frees it.
//
// A cell never holds a function, so it cannot hold a closure that captures
// it: the compiler admits only Numbers, Booleans, Strings, their optional
// forms and promises of them, which reference no closure over the cell. A
// shared cell is therefore never part of a reference cycle.
//
// Only programs with such a binding include this header.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

namespace ctnative {

/// The storage of one shared binding. Generated code makes it with
/// `std::make_shared<ctnative::cell<T>>(initial)` and reads and writes it
/// through cell_place.
template <class T> struct cell {
    T value;
};

/// The binding a handle names, as a pointer generated code dereferences to
/// read or assign it. The handle keeps the cell alive for as long as the
/// caller holds it.
template <class T> T * cell_place(const std::shared_ptr<cell<T>> & binding) {
    return &binding->value;
}

} // namespace ctnative
