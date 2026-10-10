//===- ctbrowser/lib/Script/vm/run_loop/construct.cpp - The new opcode ----===//
//
// The interpreter's `new` handler, context::execute_construct, called from
// run_loop.cpp's VM_CASE(construct). Proxies and natives go through
// context::construct; an interpreted constructor gets a fresh instance and a
// new frame pushed inline.
//
//===----------------------------------------------------------------------===//

#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/vm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ctbrowser::script {

/// Executes `new`: register a holds the constructor and receives the result;
/// the b arguments follow it. For an interpreted constructor this only pushes
/// the frame; the run loop then continues in it.
void context::execute_construct(instruction in, call_frame * vm_frame,
                                const function_proto * vm_proto, std::size_t base) {
    const auto reg = [&](std::uint16_t r) -> value & { return registers_[base + r]; };
    do {
        {
            const value callee = reg(in.a);
            // A proxy goes through context::construct. The inline path exists to
            // avoid a nested interpreter loop, and a construct trap needs one,
            // so this hands over to the general form rather than duplicating it.
            if (callee.is_kind(heap_kind::proxy)) {
                const std::size_t arg_base = base + in.a + 1;
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                reg(in.a) = construct(callee, args);
                break;
            }
            // A native goes through context::construct too: only a JavaScript
            // body benefits from the inline path, and a second copy of the
            // native case could disagree about `new Number(5)`.
            if (callee.is_kind(heap_kind::native)) {
                const std::size_t arg_base = base + in.a + 1;
                std::vector<value> args{registers_.begin() + static_cast<std::ptrdiff_t>(arg_base),
                                        registers_.begin() +
                                            static_cast<std::ptrdiff_t>(arg_base + in.b)};
                reg(in.a) = construct(callee, args);
                break;
            }
            // An arrow, generator or async function has no [[Construct]] (ECMA-262
            // 10.2.3, 15.5.4): `new` on one throws TypeError before any instance is
            // created (13.3.5.1.1 step 7), as construct_new and context::construct do.
            if (callee.is_kind(heap_kind::function) && !is_constructor(callee)) {
                const function_proto * p =
                    static_cast<const closure_object *>(callee.as_heap())->proto;
                const std::string name = p != nullptr ? p->display_name() : std::string{};
                throw_error("TypeError", (name.empty() ? std::string{"the value"} : name) +
                                             " is not a constructor");
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
            // Rooted, as in context::construct: the instance lives only in a C++
            // local while field initialisers run user JavaScript, until the
            // frame that carries it as a receiver is pushed. reg(in.a) still
            // holds the callee at this point, so nothing else refers to it.
            const rooted keep_instance{*this, self};
            run_field_initialisers(callee, self);
            const std::size_t arg_base = base + in.a + 1;

            if (!callee.is_kind(heap_kind::function)) {
                // The message comes from new_callee_type_error, shared with
                // AOT-compiled `new`, so both spell it the same. The origin
                // comes from callee_origin's backwards scan, which needs an
                // interpreted frame's ip.
                new_callee_type_error((*vm_proto),
                                      callee_origin((*vm_proto), vm_frame->ip - 1, in.a), callee);
                break;
            }
            auto * fnobj = static_cast<closure_object *>(callee.as_heap());
            const function_proto & target = *fnobj->proto;
            const std::size_t new_base = arg_base;
            // Grow the register file to hold the callee's frame plus 8 spare
            // slots, the same headroom every call path reserves.
            const std::size_t needed = new_base + target.frame_size + 8u;
            if (registers_.size() < needed) { registers_.resize(needed, value::undefined()); }
            for (std::size_t i = in.b; i < target.param_count; ++i) {
                registers_[new_base + i] = value::undefined();
            }
            // Interpreter frame-depth limit, shared with op::call and
            // context::invoke; past it, throw the usual stack-overflow RangeError.
            if (frames_.size() > 512) {
                throw_error("RangeError", "Maximum call stack size exceeded");
                break;
            }
            // `new` runs an AOT-compiled body too, passing `constructing`, which
            // makes a constructor returning a primitive evaluate to its
            // receiver (ct_aot_return_value).
            //
            // It must hand over new.target: the interpreted path below sets
            // fresh.new_target directly, but ct_aot_enter can only read it from
            // pending_new_target_.
            //
            // The flag is saved and restored rather than cleared: op::construct
            // never consumes it on its own path, and ct_aot_enter clears it once
            // the frame is pushed, so restoring keeps the interpreted path's
            // behaviour identical either way.
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
            fresh.realm = realm_for(fnobj);
            fresh.constructing = true;
            fresh.new_target = callee;
            frames_.push_back(fresh);
            break;
        }
    } while (0);
}

} // namespace ctbrowser::script
