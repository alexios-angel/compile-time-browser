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

#include <string>
#include <string_view>

namespace ctcompile::ctjs::emitc_detail {
namespace ec = mlir::emitc;
// THE IMPLICIT ARGUMENTS THE IMPORTER PREPENDS, named rather than counted.
// BytecodeImport.cpp declares the same three; they are repeated here because
// this file has to know which of them the entry ABI can actually supply.
inline constexpr unsigned arg_receiver = 0;
inline constexpr unsigned arg_new_target = 1;
inline constexpr unsigned arg_callee = 2;
inline constexpr unsigned implicit_arguments = 3;

// The frame block's element type, aligned for whatever the runtime constructs
// in it rather than for a byte.
inline constexpr const char * kFrameStorageElement = "alignas(::std::max_align_t) unsigned char";

struct compiled_entry {
    ec::FuncOp entry;
    mlir::Value frame;
    mlir::Value receiver;
    mlir::Value constructing;
    mlir::Value out;
    mlir::Type value;
    mlir::Type status;
    // Built on first use, because a function whose helpers cannot fail should
    // not carry an unreachable epilogue.
    mlir::Block * propagate = nullptr;

    // WHERE EACH JAVASCRIPT VALUE IS ROOTED.
    //
    // THE COLLECTOR IS PRECISE and walks exactly the roots in GCRoots.def. A
    // value living only in a C++ local of the emitted function is reachable
    // from NONE of them, so a helper that collects can free it while the
    // generated code still holds its bits - and 33 of the 69 ABI rows are
    // is_safepoint, including every arithmetic one.
    //
    // This was a real defect, not a hypothetical: `function f(a,b,c){return
    // a+b+c;}` compiled to code that kept (a+b) in a plain uint64_t across the
    // second ct_aot_binary_op, and under set_gc_stress it returned "qqqqqZ"
    // where the interpreter returned the correct 65-character string. ASan
    // called it a heap-use-after-free, freed and read inside the same call.
    //
    // The runtime's own reference body says what to do instead - "parked in a
    // slot, which is the whole discipline this phase exists to make possible" -
    // so every value this backend produces goes into a frame slot as soon as it
    // exists. The mapping is one slot per produced value, never reused: keeping
    // a dead value alive is a leak until the frame is left, and losing a live
    // one is a use-after-free.
    llvm::DenseMap<mlir::Value, unsigned> slots;

    // AND WHERE EACH CALL'S ARGUMENT WINDOW STARTS.
    //
    // ct_aot_call takes `const uint64_t *argv` - a CONTIGUOUS run - and the
    // arguments are individually rooted in slots of their own, which are not
    // adjacent. So each call site gets a run reserved for it, written just
    // before the call. In the frame rather than in a C++ array, for the reason
    // everything else is: ct_aot_call is a safepoint that runs arbitrary user
    // JavaScript before it reads the arguments.
    llvm::DenseMap<mlir::Operation *, unsigned> argument_windows;

    // THE KEY ct_aot_new_string MEMOISES BY, AND IT IS NOT THE ENTRY'S `site`.
    //
    // The entry's site IS the function_proto, and the interpreter keys the same
    // cache by that proto with the string's CONSTANT-POOL index as the slot.
    // This backend numbers its slots in walk order, which is a different
    // numbering - so sharing the key means a compiled body can read a slot the
    // interpreter filled with a DIFFERENT literal and return the wrong string.
    //
    // It coincided on the first fixture and hid a mutation that halved every
    // length: the interpreted baseline ran first, filled the cache, and the
    // compiled body never allocated at all.
    //
    // So each compiled function memoises under an address of its OWN. The two
    // tiers then allocate one object each for the same literal instead of
    // sharing one, which is invisible - string identity is unobservable, strict
    // equality compares text - and the ceiling is still bounded by one
    // allocation per site and slot, which is what the row requires.
    mlir::Value memo_site;

    // AND THE ENTRY'S REAL `site`, a different thing from memo_site above,
    // which is why they are two fields. This one IS the function_proto
    // ct_aot_entry_fn was handed; ct_aot_construct uses it to name THIS
    // function in the TypeError a `new` on a non-constructor throws.
    mlir::Value entry_site;

    // WHICH MEMO SLOT EACH STRING CONSTANT USES. The slots must be unique
    // WITHIN the function and stable across calls; one per ctjs.constant
    // carrying a string is both. They are not frame slots - the cache is a map.
    llvm::DenseMap<mlir::Operation *, unsigned> memo_slots;

    // THE HANDLER THIS BLOCK'S FALLIBLE CALLS BRANCH TO, if it has one.
    //
    // Set from the SOURCE block's terminator before the block is converted: a
    // ctjs.check names the pad and carries the register file as of the throw.
    // Every status call in the block then tests CT_AOT_CAUGHT before it tests
    // ok, and takes this edge instead of returning.
    //
    // THE OPERANDS CAN BE MAPPED THIS EARLY because the importer splits after
    // EVERY instruction inside a protected region - so such a block holds one
    // instruction, and the snapshot the check carries is the block's own
    // arguments. If that ever stops being true this has to move.
    mlir::Block * caught_target = nullptr;
    llvm::SmallVector<mlir::Value> caught_operands{};
};

ec::OpaqueType opaque(mlir::MLIRContext * context, llvm::StringRef spelling);
ec::PointerType pointer_to(mlir::MLIRContext * context, llvm::StringRef spelling);
std::string opcode_spelling(ctbrowser::script::op which);
std::string callee(llvm::StringRef helper);
mlir::Value literal(mlir::OpBuilder & build, mlir::Location where, mlir::Type type,
                    llvm::StringRef text);
std::string c_identifier(llvm::StringRef symbol);
bool runtime_defines(llvm::StringRef helper);
void refuse(FuncOp function, llvm::StringRef because);
bool body_is_supported(FuncOp function, std::string & why);

struct lowering {
    bool boxes_bool = false;
    bool boxes_number = false;
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
    mlir::Value box(mlir::OpBuilder & build, mlir::Location where, mlir::Type value,
                    llvm::StringRef shim, mlir::Value machine);
    mlir::Value undefined(mlir::OpBuilder & build, mlir::Location where, mlir::Type value);
    mlir::Value constant_value(mlir::OpBuilder & build, mlir::Location where, mlir::Type value,
                               ConstantOp constant);
};

} // namespace ctcompile::ctjs::emitc_detail
