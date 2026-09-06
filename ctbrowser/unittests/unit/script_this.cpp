// Script entry receives a realm value; it is not a bare function call and it
// does not look up the mutable globalThis binding. Keep the program alive for
// every closure it creates, including arrows called during a later turn.
#include <ctbrowser/aot/aot.hpp>
#include <ctbrowser/script/script.hpp>

#include "check.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>

using ctbrowser::script::compiler;
using ctbrowser::script::context;
using ctbrowser::script::program;
using ctbrowser::script::script_kind;
using ctbrowser::script::value;

namespace {

void check(bool ok, std::string_view what) {
    if (!ok) {
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
        ++ctbrowser_test_failures;
    }
}

void expect_script(std::string_view source, std::string_view expected,
                   script_kind kind = script_kind::classic) {
    const program prog = compiler::compile(source, kind);
    context cx;
    ctbrowser::script::install_builtins(cx);
    const auto result = cx.run(prog);
    const std::string actual = result.ok ? cx.to_string(cx.global("result")) : result.error;
    if (actual != expected) {
        std::printf("FAIL script this: %s (want %.*s)\n", actual.c_str(),
                    static_cast<int>(expected.size()), expected.data());
        ++ctbrowser_test_failures;
    }
}

void test_scope_and_bindings() {
    expect_script(R"(
        var result = (this === globalThis) + ',' + typeof this + ',' +
            (this.Array === Array);
    )",
                  "true,object,true");
    expect_script(R"(
        'use strict';
        function strictReceiver() { 'use strict'; return this; }
        var lexical = () => this;
        var object = {method: lexical};
        var result = (this === globalThis) + ',' + (strictReceiver() === undefined) + ',' +
            (object.method() === this) + ',' + (lexical.call({}) === this);
    )",
                  "true,true,true,true");
    expect_script(R"(
        var visible = 4;
        this.visible = 9;
        this.added = {value: 13};
        var result = this.visible + ',' + visible + ',' + added.value + ',' +
            ('visible' in this) + ',' + ('absent' in this);
    )",
                  "9,9,13,true,false");
    expect_script(R"(
        var original = this;
        var globalThis = undefined;
        var self = {};
        this.published = 7;
        var result = (this === original) + ',' + (this !== globalThis) + ',' +
            (this !== self) + ',' + published + ',' + (self.published === undefined);
    )",
                  "true,true,true,7,true");
    expect_script(R"(
        var original = this;
        globalThis = {replacement: true};
        var result = (this === original) + ',' + (this !== globalThis) + ',' +
            (this.globalThis === globalThis);
    )",
                  "true,true,true");
    expect_script(R"(
        var moduleLocal = 7;
        var lexical = () => this;
        globalThis.result = (this === undefined) + ',' + (lexical() === undefined) + ',' +
            (globalThis.moduleLocal === undefined);
    )",
                  "true,true,true", script_kind::module_);
}

void test_later_turn_and_collection() {
    const program first = compiler::compile(R"(
        var lexical = () => this;
        this.persisted = {value: 17};
        globalThis = undefined;
    )");
    // Clear the first script's register window and receiver before collecting.
    const program clear = compiler::compile("0;", script_kind::module_);
    const program later = compiler::compile(R"(
        var result = (lexical() === this) + ',' + this.persisted.value + ',' +
            (globalThis === undefined);
    )");
    const program remove_arrow = compiler::compile("lexical = undefined;", script_kind::module_);
    const program last = compiler::compile("var result = this.persisted.value;");
    context cx;
    ctbrowser::script::install_builtins(cx);
    const std::uint64_t identity = cx.global_this().bits();
    check(cx.run(first).ok && cx.run(clear).ok, "classic and module turns run");
    (void)cx.collect();
    check(cx.collections() != 0, "the retained arrow test really collects");
    check(cx.run(later).ok && cx.to_string(cx.global("result")) == "true,17,true",
          "the arrow keeps script this across turns and an overwritten globalThis");
    check(cx.run(remove_arrow).ok, "remove the arrow and clear its last register window");
    // Now no JavaScript alias or frame retains the realm object. Only the
    // context's independent root can preserve it for the next classic script.
    const std::size_t before = cx.collections();
    (void)cx.collect();
    for (int i = 0; i < 400; ++i) { (void)cx.make_object(); }
    (void)cx.collect();
    check(cx.collections() > before, "the realm-only root test really collects");
    check(cx.global_this().bits() == identity, "the realm preserves its receiver identity");
    check(cx.run(last).ok && cx.to_string(cx.global("result")) == "17",
          "the realm receiver and its forwarding view survive without visible aliases");

    context other;
    check(other.global_this().bits() != identity, "separate contexts have separate realm objects");
}

std::size_t compiled_entries = 0;

// The same entry ABI a generated script uses. Observe both the explicit
// receiver argument and load_this's helper, so testing only one cannot mask a
// disagreement between dispatcher and frame setup.
extern "C" std::int32_t sample_script_receiver(ctbrowser::aot::ct_aot_ctx * ctx,
                                               const ctbrowser::aot::ct_aot_site * site,
                                               const std::uint64_t *, std::uint32_t,
                                               std::uint64_t receiver, std::uint32_t,
                                               std::uint64_t * out) {
    ++compiled_entries;
    alignas(std::max_align_t) unsigned char storage[CT_AOT_FRAME_BYTES];
    auto * frame = ctbrowser::aot::ct_aot_enter(ctx, site, 1u, receiver, storage);
    if (frame == nullptr) {
        return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::failed);
    }
    ctbrowser::aot::ct_aot_global_set(frame, "observedReceiver", 16u,
                                      ctbrowser::aot::ct_aot_this(frame));
    ctbrowser::aot::ct_aot_global_set(frame, "observedArgument", 16u, receiver);
    *out = value::undefined().bits();
    ctbrowser::aot::ct_aot_leave(frame);
    return static_cast<std::int32_t>(ctbrowser::aot::ct_aot_status::ok);
}

void test_entry_paths(bool compiled) {
    program classic = compiler::compile("var observedReceiver = this;");
    program module = compiler::compile("globalThis.observedReceiver = this;", script_kind::module_);
    const program importing = compiler::compile(R"(
        var before = this;
        evaluateModule();
        var result = (before === this) + ',' + (observedReceiver === undefined);
    )");
    ctbrowser::script::module_record record;
    if (compiled) {
        classic.functions[0].aot_entry = &sample_script_receiver;
        module.functions[0].aot_entry = &sample_script_receiver;
    }
    context cx;
    ctbrowser::script::install_builtins(cx);
    compiled_entries = 0;
    check(cx.run(classic).ok, "a classic entry runs");
    check(cx.global("observedReceiver").strict_equals(cx.global_this()),
          "classic entry load_this receives the realm object");
    if (compiled) {
        check(cx.global("observedArgument").strict_equals(cx.global_this()),
              "the compiled classic entry's explicit receiver is the realm object");
    }
    check(cx.run_module(module, record).ok, "a module entry runs");
    check(cx.global("observedReceiver").is_undefined(), "module entry load_this is undefined");
    if (compiled) {
        check(cx.global("observedArgument").is_undefined(),
              "the compiled module entry's explicit receiver is undefined");
    }
    cx.define_native("evaluateModule", [&module, &record](context & inner, std::span<value>) {
        (void)inner.run_module(module, record);
        return value::undefined();
    });
    check(cx.run(importing).ok && cx.to_string(cx.global("result")) == "true,true",
          "a module entered reentrantly gets undefined and preserves its caller's script this");
    check(compiled_entries == (compiled ? 3u : 0u), "every intended compiled entry was exercised");
}

} // namespace

int main() {
    test_scope_and_bindings();
    test_later_turn_and_collection();
    test_entry_paths(false);
    test_entry_paths(true);
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
