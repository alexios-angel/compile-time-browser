//===- Lowering.h - EmitC lowering internals --------------------*- C++ -*-===//
//
// Private to the ctjs -> EmitC lowering (--ctjs-lower-to-emitc). This backend
// emits C++ that calls the ctbrowser AOT runtime helpers (ct_aot_*), whose
// contracts ("ABI entries") are in ctbrowser/include/ctbrowser/aot/helpers/
// *.def. Every JavaScript value is a 64-bit NaN-boxed `uint64_t` that lives in
// the VM's register file, so this is the VM-backed backend, as opposed to the
// native (no-GC) backend in lib/CTNative.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/IR/CTJSOps.h"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/EmitC/IR/EmitC.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Transforms/RegionUtils.h"

#include <ctbrowser/aot/aot_entry.h>
#include <ctbrowser/script/bytecode.hpp>

#include "ctcompile/CTJS/Lowering/OpcodeMapping.hpp"

#include <bitset>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ctcompile::ctjs::emitc_detail {
namespace ec = mlir::emitc;
/// The frame block's element type, aligned for whatever the runtime constructs
/// in it rather than for a byte.
inline constexpr const char * kFrameStorageElement = "alignas(::std::max_align_t) unsigned char";

/// The per-function state of lowering one ctjs.func to one emitc.func.
struct compiled_entry {
    ec::FuncOp entry;
    /// The `ct_aot_frame *` that ct_aot_enter returned.
    mlir::Value frame;
    mlir::Value receiver;
    mlir::Value constructing;
    /// The entry's `uint64_t *out` result parameter.
    mlir::Value out;
    /// The emitted types of a JavaScript value and of a status.
    mlir::Type value;
    mlir::Type status;
    /// The shared failure epilogue. Built on first use, because a function
    /// whose helpers cannot fail should not carry an unreachable epilogue.
    mlir::Block * propagate = nullptr;

    /// The frame slot each JavaScript value is rooted in.
    ///
    /// The collector is precise and walks only the roots of
    /// context::each_root. A value held only in a C++ local of the emitted
    /// function is not one of them, so any helper marked is_safepoint (which
    /// includes every arithmetic helper) can free it while the generated code
    /// still holds its bits. Example: in `a+b+c`, keeping (a+b) in a local
    /// across the second ct_aot_binary_op is a use-after-free under GC stress.
    ///
    /// So every value this backend produces is stored ("parked") in a frame
    /// slot, which the collector does trace, as soon as it exists. One slot
    /// per produced value, never reused: keeping a dead value alive is a leak
    /// until the frame is left, and losing a live one is a use-after-free.
    llvm::DenseMap<mlir::Value, unsigned> slots;

    /// Where each call's argument window starts.
    ///
    /// ct_aot_call takes `const uint64_t *argv`, a contiguous run, and the
    /// arguments are individually rooted in slots of their own, which are not
    /// adjacent. So each call site gets a run reserved for it, written just
    /// before the call. In the frame rather than in a C++ array, for the reason
    /// everything else is: ct_aot_call is a safepoint that runs arbitrary user
    /// JavaScript before it reads the arguments.
    llvm::DenseMap<mlir::Operation *, unsigned> argument_windows;

    /// The key ct_aot_new_string memoises by, which is not the entry's `site`.
    ///
    /// The entry's site is the function_proto, and the interpreter keys the
    /// same cache by that proto with the string's constant-pool index as the
    /// slot. This backend numbers its slots in walk order, a different
    /// numbering, so sharing the key would let a compiled body read a slot the
    /// interpreter filled with a different literal and return the wrong
    /// string.
    ///
    /// So each compiled function memoises under an address of its own. The
    /// interpreter and the compiled body then allocate one object each for the
    /// same literal instead of sharing one, which is unobservable (string
    /// identity is invisible; strict equality compares text), and allocation
    /// stays bounded by one per site and slot, as ct_aot_new_string requires.
    mlir::Value memo_site;

    /// The entry's real `site`, distinct from memo_site above: the
    /// function_proto ct_aot_entry_fn was handed. ct_aot_construct uses it to
    /// name this function in the TypeError a `new` on a non-constructor
    /// throws.
    mlir::Value entry_site;

    /// Which memo slot each string constant uses. The slots must be unique
    /// within the function and stable across calls; one per ctjs.constant
    /// carrying a string is both. They are not frame slots: the cache is a
    /// map.
    llvm::DenseMap<mlir::Operation *, unsigned> memo_slots;

    /// The handler this block's fallible calls branch to, if it has one.
    ///
    /// Set from the source block's terminator before the block is converted:
    /// a ctjs.check names the pad and carries the register file as of the
    /// throw. Every status call in the block then tests CT_AOT_CAUGHT before
    /// it tests ok, and takes this edge instead of returning.
    ///
    /// The operands can be mapped this early because the importer splits
    /// after every instruction inside a protected region, so such a block
    /// holds one instruction and the snapshot the check carries is the block's
    /// own arguments. If that ever stops being true, this has to move.
    mlir::Block * caught_target = nullptr;
    llvm::SmallVector<mlir::Value> caught_operands{};
};

// Spelling helpers, defined and documented in Support.cpp.
ec::OpaqueType opaque(mlir::MLIRContext * context, llvm::StringRef spelling);
ec::PointerType pointer_to(mlir::MLIRContext * context, llvm::StringRef spelling);
std::string opcode_spelling(ctbrowser::script::op which);
std::string callee(llvm::StringRef helper);
mlir::Value literal(mlir::OpBuilder & build, mlir::Location where, mlir::Type type,
                    llvm::StringRef text);
std::pair<mlir::Value, mlir::Value> bytes_and_length(mlir::OpBuilder & build, mlir::Location where,
                                                     llvm::StringRef text);
mlir::Value intern(mlir::OpBuilder & build, mlir::Location where, llvm::StringRef name);
mlir::Value nonzero(mlir::OpBuilder & build, mlir::Location where, mlir::Value u32);
mlir::Value compare_zero(mlir::OpBuilder & build, mlir::Location where, ec::CmpPredicate predicate,
                         mlir::Value u32);
mlir::Value u32_literal(mlir::OpBuilder & build, mlir::Location where, std::uint64_t number);
mlir::Value opaque_call(mlir::OpBuilder & build, mlir::Location where, mlir::Type produces,
                        llvm::StringRef helper, mlir::ValueRange arguments);
mlir::Value status_literal(mlir::OpBuilder & build, mlir::Location where, mlir::Type status,
                           llvm::StringRef named);
mlir::Value status_is(mlir::OpBuilder & build, mlir::Location where, mlir::Type status,
                      mlir::Value answered, llvm::StringRef named);
std::optional<mlir::ValueRange> window_operands(mlir::Operation & op);
std::string c_identifier(llvm::StringRef symbol);
// Admission, defined and documented in Admission.cpp.
bool runtime_defines(llvm::StringRef helper);
void refuse(FuncOp function, llvm::StringRef because);
bool body_is_supported(FuncOp function, std::string & why);

/// The prelude's shims: each is a name box() calls and the body Module.cpp
/// emits for it, in one row so the two cannot drift apart. See lowering::run.
enum class shim : unsigned {
    box_bool,
    box_number
};
struct shim_row {
    llvm::StringLiteral name;
    llvm::StringLiteral body;
};
inline constexpr shim_row shim_rows[] = {
    {"ctc_box_bool", "inline uint64_t ctc_box_bool(bool b) { return "
                     "::ctbrowser::script::value::boolean(b).bits(); }"},
    {"ctc_box_number", "inline uint64_t ctc_box_number(double d) { return "
                       "::ctbrowser::script::value::number(d).bits(); }"},
};

/// The lowering of one module. The members are defined across the .cpp files
/// in this directory: run and lower in Module.cpp and Functions.cpp, the
/// status paths in Status.cpp, roots in Roots.cpp, constants in Constants.cpp,
/// and the per-operation conversions in Values.cpp, RuntimeOps.cpp and
/// Operations.cpp.
struct lowering {
    /// The shims some box() call used, so only those are emitted.
    std::bitset<std::size(shim_rows)> used_shims;
    void run(mlir::ModuleOp module, mlir::MLIRContext * context);
    bool lower(FuncOp function, mlir::OpBuilder & build, mlir::MLIRContext * context);
    static mlir::Type as_emitc(mlir::Type type, mlir::Type value);
    mlir::Block * failure_path(compiled_entry & scope, mlir::OpBuilder & build,
                               mlir::Location where);
    mlir::Block * caught_or_failure(compiled_entry & scope, mlir::OpBuilder & build,
                                    mlir::Location where);
    mlir::Value status_call(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                            const std::string & symbol, llvm::ArrayRef<mlir::Value> arguments,
                            mlir::Type produces, mlir::Value seed = nullptr);
    void status_call_void(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                          const std::string & symbol, llvm::ArrayRef<mlir::Value> arguments);
    bool convertValues(mlir::Operation & op, mlir::OpBuilder & build, mlir::IRMapping & mapping,
                       compiled_entry & scope);
    bool convertRuntime(mlir::Operation & op, mlir::OpBuilder & build, mlir::IRMapping & mapping,
                        compiled_entry & scope);
    void convert(mlir::Operation & op, mlir::OpBuilder & build, mlir::IRMapping & mapping,
                 compiled_entry & scope);
    void park(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
              mlir::Value rooted, unsigned slot);
    void park_if_tracked(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                         mlir::Value original, mlir::Value emitted);
    mlir::Value cell_of_upvalue(compiled_entry & scope, mlir::OpBuilder & build,
                                mlir::Location where, std::uint32_t index, bool read,
                                mlir::Value written);
    mlir::Value window_pointer(compiled_entry & scope, mlir::OpBuilder & build,
                               mlir::Location where, unsigned base);
    unsigned park_window(compiled_entry & scope, mlir::OpBuilder & build, mlir::Location where,
                         mlir::IRMapping & mapping, mlir::Operation & op);
    mlir::Value box(mlir::OpBuilder & build, mlir::Location where, mlir::Type value, shim which,
                    mlir::Value machine);
    mlir::Value undefined(mlir::OpBuilder & build, mlir::Location where, mlir::Type value);
    mlir::Value constant_value(mlir::OpBuilder & build, mlir::Location where, mlir::Type value,
                               ConstantOp constant);
};

} // namespace ctcompile::ctjs::emitc_detail
