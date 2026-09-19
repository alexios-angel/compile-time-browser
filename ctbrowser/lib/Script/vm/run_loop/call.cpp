#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ctbrowser::script {

void context::execute_call(instruction in, call_frame * vm_frame, const function_proto * vm_proto,
                           std::size_t base) {
    const auto reg = [&](std::uint16_t r) -> value & { return registers_[base + r]; };
    do {
        {
            value callee = reg(in.a);
            value receiver = value::undefined();
            // THE LOOKUP CAN THROW - a getter, a proxy trap, or a nullish
            // receiver - and a throw has already unwound to its handler by
            // the time it returns. Calling `undefined` after that would
            // throw a SECOND TypeError from the landing site.
            const std::size_t unwound = unwinds_;
            if (in.code == op::call_receiver) {
                // The callee was resolved elsewhere (up the prototype chain, for
                // `super`) and the receiver is passed explicitly.
                receiver = reg(in.c);
            } else if (in.code == op::call_method) {
                receiver = reg(in.a);
                // Through the SAME lookup as get_prop, so `s.split(...)` and
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
                // A HEAP PAST ITS THRESHOLD COLLECTS HERE TOO. A loop whose
                // only calls are natives - `nodeList[j]` through a native
                // proxy trap, 250 million times - never reaches invoke's
                // safepoint and grew to the 4 GB cap (std::bad_alloc,
                // dom/nodes/NodeList-static-length-getter-tampered-*).
                // Not a stress point: the ABI's stress pins count
                // collections at invoke and the tick only.
                if (!gc_stress_ && live_objects_ >= collect_threshold_) [[unlikely]] {
                    // The callee may exist only here (a trap made it) and the
                    // receiver is a C++ local until the call.
                    const rooted keep_callee{*this, callee};
                    const rooted keep_receiver{*this, receiver};
                    (void)collect_if_due();
                }
                // COPIED, not spanned into the register stack. A native may call
                // back into script - an event listener dispatching another
                // event - and that grows registers_, which would leave a span
                // into it dangling. One small vector per native call is the
                // price of natives being allowed to re-enter the VM at all.
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
                // A throw the native's `call` parked is thrown HERE, at
                // its call site - see context::call.
                if (rethrow_pending()) { break; }
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
                    // WHAT IT WAS CALLED ON. "`replace` is undefined" reads the
                    // same whether the method is missing from a real object or
                    // the object itself is undefined, and those are different
                    // bugs in different places.
                    if (in.code == op::call_method || in.code == op::call_computed) {
                        what += ", on " + std::string{type_of(receiver)};
                        if (receiver.is_nullish()) {
                            what += " (" + to_string(receiver) + ")";
                            // WHICH undefined. "`get` is undefined, on
                            // undefined" names the method and says nothing
                            // about the object, and the object is the bug -
                            // `get` is fine, whatever should have had it is
                            // missing. A method call keeps its receiver in the
                            // callee's own register, so the walk that names a
                            // plain call's callee names the receiver too.
                            const std::string from =
                                callee_origin((*vm_proto), vm_frame->ip - 1, in.a);
                            if (!from.empty()) { what += " from `" + from + "`"; }
                        }
                    }
                    throw_error("TypeError", std::move(what));
                }
                break;
            }
            auto * fnobj = static_cast<closure_object *>(callee.as_heap());
            const function_proto & target = *fnobj->proto;
            // CALLING A GENERATOR RUNS NOTHING. It hands back an object over a
            // (*vm_frame) that has not started; the first instruction runs on the
            // first `.next()`.
            if (target.is_generator) {
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                reg(in.a) = make_generator(fnobj, receiver, args);
                break;
            }
            // The callee's (*vm_frame) starts where its arguments already are, so no
            // copying is needed to pass them.
            const std::size_t new_base = arg_base;
            const std::size_t needed = new_base + target.frame_size + 8u;
            if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
            for (std::size_t i = in.b; i < target.param_count; ++i) {
                registers_[new_base + i] = value::undefined(); // missing args
            }
            // A COMPILED BODY, IF THIS FUNCTION HAS ONE - asked in the one
            // place that asks, so that every other entry into a function
            // gets the same answer. See script/dispatch.hpp.
            //
            // AFTER the argument fill, so `argv` is what the callee's row
            // promises: the window with its missing parameters already
            // undefined. BEFORE the depth guard, because ct_aot_enter owns
            // that guard for a compiled frame.
            if (value produced = value::undefined();
                enter_compiled(*this, target, callee, registers_.data() + new_base, new_base, in.b,
                               receiver, /*constructing*/ false, produced)) {
                reg(in.a) = produced;
                break;
            }
            if (frames_.size() > 512) {
                raise("call stack exhausted");
                break;
            }
            call_frame entered{&target, 0, new_base, in.a, in.b, fnobj, receiver, handlers_.size()};
            entered.new_target = pending_new_target_;
            pending_new_target_ = value::undefined();
            frames_.push_back(entered);
            break;
        }
    } while (0);
}

} // namespace ctbrowser::script
