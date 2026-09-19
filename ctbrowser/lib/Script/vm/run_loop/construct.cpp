#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ctbrowser::script {

void context::execute_construct(instruction in, call_frame * vm_frame,
                                const function_proto * vm_proto, std::size_t base) {
    const auto reg = [&](std::uint16_t r) -> value & { return registers_[base + r]; };
    do {
        {
            const value callee = reg(in.a);
            // A PROXY GOES THE LONG WAY ROUND. The inline path exists to avoid
            // a nested interpreter loop, and a construct trap needs one - so
            // this hands over to the general form rather than duplicating it.
            if (callee.is_kind(heap_kind::proxy)) {
                const std::size_t arg_base = base + in.a + 1;
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                reg(in.a) = construct(callee, args);
                break;
            }
            // A NATIVE GOES THE LONG WAY TOO, for the same reason as a proxy:
            // the inline path exists to avoid a nested interpreter loop, which
            // only a JavaScript body needs, and a second copy of the native
            // case is a second chance to disagree about `new Number(5)`.
            if (callee.is_kind(heap_kind::native)) {
                const std::size_t arg_base = base + in.a + 1;
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                reg(in.a) = construct(callee, args);
                break;
            }
            // The instance's prototype comes from the constructor's own
            // `prototype` property, which is what makes a method defined on the
            // class reachable from every instance.
            auto * instance = allocate<object_object>();
            if (callee.is_object()) {
                if (value * proto =
                        static_cast<object_object *>(callee.as_heap())->find("prototype")) {
                    instance->prototype = *proto;
                }
            } else if (callee.is_kind(heap_kind::function)) {
                instance->prototype = ensure_prototype(callee);
            }
            const value self = value::object(instance);
            // ROOTED FOR THE SAME REASON context::construct roots its own:
            // the instance is in a C++ local while field initialisers run
            // user JavaScript, and it stays in one until the frame that
            // carries it as a receiver is pushed. reg(in.a) still holds the
            // CALLEE at this point, so nothing else refers to it.
            const rooted keep_instance{*this, self};
            run_field_initialisers(callee, self);
            const std::size_t arg_base = base + in.a + 1;

            if (!callee.is_kind(heap_kind::function)) {
                // THE MESSAGE IS SHARED, so a compiled `new` on a
                // non-constructor cannot spell it differently. The origin
                // is the backwards scan, which only an interpreted frame
                // has an ip for.
                new_callee_type_error((*vm_proto),
                                      callee_origin((*vm_proto), vm_frame->ip - 1, in.a), callee);
                break;
            }
            auto * fnobj = static_cast<closure_object *>(callee.as_heap());
            const function_proto & target = *fnobj->proto;
            const std::size_t new_base = arg_base;
            const std::size_t needed = new_base + target.frame_size + 8u;
            if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
            for (std::size_t i = in.b; i < target.param_count; ++i) {
                registers_[new_base + i] = value::undefined();
            }
            if (frames_.size() > 512) {
                raise("call stack exhausted");
                break;
            }
            // `new` ASKS FOR A COMPILED BODY TOO, and passes `constructing`,
            // which is what makes a constructor returning a primitive
            // evaluate to its receiver (ct_aot_return_value).
            //
            // IT MUST HAND OVER new.target: the interpreted path below sets
            // fresh.new_target directly, but ct_aot_enter can only read it
            // from pending_new_target_.
            //
            // SET AND RESTORED rather than set and cleared: op::construct
            // never consumes the flag on its own path, and ct_aot_enter
            // clears it once the frame is pushed. Restoring keeps the
            // interpreted path's behaviour identical either way.
            const value saved_new_target = pending_new_target_;
            pending_new_target_ = callee;
            if (value produced = value::undefined();
                enter_compiled(*this, target, callee, registers_.data() + new_base, new_base, in.b,
                               self, /*constructing*/ true, produced)) {
                pending_new_target_ = saved_new_target;
                reg(in.a) = produced.is_object_like() ? produced : self;
                break;
            }
            pending_new_target_ = saved_new_target;
            call_frame fresh{&target, 0, new_base, in.a, in.b, fnobj, self, handlers_.size()};
            fresh.constructing = true;
            fresh.new_target = callee;
            frames_.push_back(fresh);
            break;
        }
    } while (0);
}

} // namespace ctbrowser::script
