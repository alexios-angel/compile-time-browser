//===- ctbrowser/lib/Script/vm/run_loop/call.cpp - The call opcode --------===//
//
// The interpreter's call handler, context::execute_call, shared by the four
// call opcodes (call, call_method, call_computed, call_receiver) in
// run_loop.cpp. It calls natives directly, routes proxies through invoke, and
// pushes a new frame for an interpreted closure.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ctbrowser::script {

/// Executes a call instruction: register a holds the callee (or, for
/// call_method and call_computed, the receiver) and receives the result; the
/// b arguments follow it. For an interpreted closure this only pushes the
/// callee's frame; the run loop then continues in it.
void context::execute_call(instruction in, call_frame * vm_frame, const function_proto * vm_proto,
                           std::size_t base) {
    const auto reg = [&](std::uint16_t r) -> value & { return registers_[base + r]; };
    do {
        {
            value callee = reg(in.a);
            value receiver = value::undefined();
            // Taken whatever the callee turns out to be, so a super() into a
            // native parent leaves nothing behind for the next call.
            const bool super_call = std::exchange(super_call_, false);
            // The lookup can throw (a getter, a proxy trap, or a nullish
            // receiver), and a throw has already unwound to its handler by
            // the time it returns. Calling `undefined` after that would throw
            // a second TypeError from the landing site.
            const std::size_t unwound = unwinds_;
            if (in.code == op::call_receiver) {
                // The callee was resolved elsewhere (up the prototype chain, for
                // `super`) and the receiver is passed explicitly.
                receiver = reg(in.c);
            } else if (in.code == op::call_method) {
                receiver = reg(in.a);
                // Through the same lookup as get_prop, so `s.split(...)` and
                // `var f = s.split; f(...)` find the same function.
                callee = lookup_property(receiver, vm_proto->names[in.c]);
            } else if (in.code == op::call_computed) {
                receiver = reg(in.a);
                callee = lookup_index(receiver, reg(in.c));
            }
            if (unwinds_ != unwound) { break; }
            const std::size_t arg_base = base + in.a + 1;
            if (callee.is_kind(heap_kind::native)) {
                auto * nat = static_cast<native_object *>(callee.as_heap());
                if (nat->find(CTBROWSER_PRIVATE_KEY("BoundTargetFunction")) == nullptr) {
                    pending_new_target_ = value::undefined();
                }
                // A heap past its threshold collects here too: a loop whose
                // only calls are natives (`nodeList[j]` through a native proxy
                // trap, millions of times) never reaches invoke's safepoint
                // and would grow without bound. This is not a GC-stress point;
                // stress mode collects only at invoke and at the tick.
                if (!gc_stress_ && live_objects_ >= collect_threshold_) [[unlikely]] {
                    // The callee may exist only here (a trap made it) and the
                    // receiver is a C++ local until the call.
                    const rooted keep_callee{*this, callee};
                    const rooted keep_receiver{*this, receiver};
                    (void)collect_if_due();
                }
                // Copied, not a span into the register file: a native may call
                // back into script (an event listener dispatching another
                // event), which can grow registers_ and leave a span dangling.
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                const value saved_this = current_this_;
                current_this_ = receiver;
                const value produced = [&] {
                    const native_scope pinned{*this};
                    return nat->fn(*this, args);
                }();
                current_this_ = saved_this;
                // A throw the native's `call` parked is thrown here, at its
                // call site (see context::call).
                if (rethrow_pending()) { break; }
                reg(in.a) = produced;
                break;
            }
            // A proxy over a callable is called through its [[Call]] (the
            // `apply` trap or its target), which invoke owns. A throw out of
            // either has landed on a handler by the time it returns
            // (unwinds_), and then the result register must not be written.
            if (callee.is_kind(heap_kind::proxy) && callee.is_callable()) {
                const rooted keep_callee{*this, callee};
                const rooted keep_receiver{*this, receiver};
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                const rooted_values keep_args{*this, args};
                const value produced = invoke(callee, args, receiver, /*constructing*/ false);
                if (unwinds_ != unwound || throw_pending()) { break; }
                reg(in.a) = produced;
                break;
            }
            if (!callee.is_kind(heap_kind::function)) {
                {
                    std::string what =
                        describe_callee((*vm_proto),
                                        in.code == op::call_method ? vm_proto->names[in.c]
                                        : in.code == op::call_computed
                                            ? to_string(reg(in.c))
                                            : callee_origin((*vm_proto), vm_frame->ip - 1, in.a),
                                        callee);
                    // Name what it was called on: "`replace` is undefined"
                    // reads the same whether the method is missing from a real
                    // object or the object itself is undefined, and those are
                    // different bugs.
                    if (in.code == op::call_method || in.code == op::call_computed) {
                        what += ", on " + std::string{type_of(receiver)};
                        if (receiver.is_nullish()) {
                            what += " (" + to_string(receiver) + ")";
                            // Say which expression was undefined, since the
                            // missing object is the bug, not the method. A
                            // method call keeps its receiver in the callee's
                            // own register, so the walk that names a plain
                            // call's callee names the receiver too.
                            const std::string from =
                                callee_origin((*vm_proto), vm_frame->ip - 1, in.a);
                            if (!from.empty()) { what += " from `" + from + "`"; }
                        }
                    }
                    throw_error("TypeError", std::move(what));
                }
                break;
            }
            if (refuse_class_call(callee, super_call)) { break; }
            auto * fnobj = static_cast<closure_object *>(callee.as_heap());
            const function_proto & target = *fnobj->proto;
            // Calling a generator runs nothing. It returns an object over a frame
            // that has not started; the first instruction runs on the first
            // `.next()`.
            if (target.is_generator) {
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                reg(in.a) = make_generator(fnobj, receiver, args);
                break;
            }
            // The callee's frame starts where its arguments already are, so
            // passing them copies nothing. The register file is grown to the
            // callee's frame_size plus 8 spare slots, the headroom every frame
            // entry reserves (see also call/run.cpp and call/coroutines.cpp).
            const std::size_t new_base = arg_base;
            const std::size_t needed = new_base + target.frame_size + 8u;
            if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
            for (std::size_t i = in.b; i < target.param_count; ++i) {
                registers_[new_base + i] = value::undefined(); // Missing arguments.
            }
            // Run the AOT-compiled body if this function has one; enter_compiled
            // is the single place that decides (see script/dispatch.hpp).
            //
            // This comes after the argument fill, so `argv` is the window with
            // missing parameters already undefined, and before the depth
            // guard, because ct_aot_enter owns that guard for a compiled frame.
            if (value produced = value::undefined();
                enter_compiled(*this, target, callee, registers_.data() + new_base, new_base, in.b,
                               receiver, /*constructing*/ false, produced)) {
                reg(in.a) = produced;
                break;
            }
            // Interpreter frame-depth limit, shared with op::construct and
            // context::invoke; past it, throw the usual stack-overflow RangeError.
            if (frames_.size() > 512) {
                throw_error("RangeError", "Maximum call stack size exceeded");
                break;
            }
            call_frame entered{&target, 0, new_base, in.a, in.b, fnobj, receiver, handlers_.size()};
            entered.realm = realm_for(fnobj);
            entered.new_target = pending_new_target_;
            pending_new_target_ = value::undefined();
            frames_.push_back(entered);
            frames_.back().receiver = bind_this(target, *entered.realm, receiver);
            break;
        }
    } while (0);
}

} // namespace ctbrowser::script
