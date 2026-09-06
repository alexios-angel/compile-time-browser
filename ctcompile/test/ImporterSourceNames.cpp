#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTJS/Import/BytecodeImport.hpp"

#include <ctbrowser/script/compile.hpp>

#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <string>

namespace {
using namespace ctbrowser::script;
namespace ctjs = ctcompile::ctjs;

int failures = 0;

void check(bool holds, const char * message) {
    if (holds) { return; }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

// Read only the dedicated metadata. The instruction's NameLoc also contains
// strings, but none of them is a JavaScript binding name.
llvm::StringRef source_name(mlir::Value value) {
    if (!value) { return {}; }
    auto fused = mlir::dyn_cast<mlir::FusedLoc>(value.getLoc());
    if (!fused) { return {}; }
    auto metadata = mlir::dyn_cast_if_present<mlir::DictionaryAttr>(fused.getMetadata());
    if (!metadata) { return {}; }
    if (auto result = mlir::dyn_cast<mlir::OpResult>(value);
        result && result.getOwner()->getNumResults() > 1) {
        auto names = metadata.getAs<mlir::ArrayAttr>("ctnative.source_names");
        if (!names || result.getResultNumber() >= names.size()) { return {}; }
        auto name = mlir::dyn_cast<mlir::StringAttr>(names[result.getResultNumber()]);
        return name ? name.getValue() : llvm::StringRef{};
    }
    auto name = metadata.getAs<mlir::StringAttr>("ctnative.source_name");
    return name ? name.getValue() : llvm::StringRef{};
}

bool has_pc(mlir::Location location, unsigned pc) {
    auto fused = mlir::dyn_cast<mlir::FusedLoc>(location);
    if (!fused) { return false; }
    for (mlir::Location child : fused.getLocations()) {
        auto name = mlir::dyn_cast<mlir::NameLoc>(child);
        if (!name) { continue; }
        llvm::StringRef identity = name.getName().getValue();
        if (!identity.consume_front("program:names.js:")) { continue; }
        unsigned actual = 0;
        if (!identity.rsplit(':').second.getAsInteger(10, actual) && actual == pc) { return true; }
    }
    return false;
}

bool has_source_position(mlir::Location location, unsigned line) {
    auto fused = mlir::dyn_cast<mlir::FusedLoc>(location);
    if (!fused) { return false; }
    for (mlir::Location child : fused.getLocations()) {
        if (auto source = mlir::dyn_cast<mlir::FileLineColLoc>(child)) {
            return source.getFilename().getValue() == "names.js" && source.getLine() == line &&
                   source.getColumn() == 1;
        }
    }
    return false;
}

ctjs::FuncOp function_named(mlir::ModuleOp module, llvm::StringRef name) {
    for (auto function : module.getOps<ctjs::FuncOp>()) {
        if (function.getSymName() == name) { return function; }
    }
    return {};
}

mlir::Operation * instruction_at(ctjs::FuncOp function, unsigned pc, llvm::StringRef kind) {
    mlir::Operation * found = nullptr;
    function.walk([&](mlir::Operation * operation) {
        if (operation->getName().getStringRef() == kind && has_pc(operation->getLoc(), pc)) {
            found = operation;
        }
    });
    return found;
}

mlir::Value result_at(ctjs::FuncOp function, unsigned pc, llvm::StringRef kind) {
    auto * operation = instruction_at(function, pc, kind);
    check(operation != nullptr && operation->getNumResults() == 1, "expected instruction result");
    return operation && operation->getNumResults() == 1 ? operation->getResult(0) : mlir::Value{};
}

mlir::Block * block_at(ctjs::FuncOp function, unsigned pc) {
    for (mlir::Block & block : llvm::drop_begin(function.getBody())) {
        if (block.getNumArguments() > 0 && has_pc(block.getArgument(0).getLoc(), pc)) {
            return &block;
        }
    }
    return nullptr;
}

std::string semantic_ir(mlir::ModuleOp module) {
    std::string text;
    llvm::raw_string_ostream stream(text);
    module.print(stream, mlir::OpPrintingFlags().enableDebugInfo(false));
    return text;
}

program fixture() {
    program result;
    for (unsigned line = 0; line < 10; ++line) { result.source += "instruction\n"; }

    function_proto scopes;
    scopes.name = "scopes";
    scopes.param_count = 1;
    scopes.frame_size = 6;
    scopes.constants = {value::number(10), value::number(20), value::number(30), value::number(40)};
    scopes.code = {
        {op::move, 1, 0},                             // 0: parameter alias
        instruction::with_bx(op::load_const, 3, 0),   // 1: unnamed temporary
        {op::move, 2, 3},                             // 2: first binding is total
        {op::move, 4, 2},                             // 3: another alias keeps total
        instruction::with_bx(op::jump_if_true, 0, 2), // 4: branch to 7
        instruction::with_bx(op::load_const, 5, 1),   // 5: before
        instruction::with_bx(op::jump, 0, 1),         // 6: branch to 8
        instruction::with_bx(op::load_const, 5, 2),   // 7: no binding at last_pc
        instruction::with_bx(op::load_const, 5, 3),   // 8: reused slot, after
        {op::ret, 5},                                 // 9
    };
    // Deliberately unsorted: optional debug tables need not retain declaration
    // order. Register 5 has a gap between two scopes.
    scopes.locals = {{"after", 5, 8, 10}, {"price", 0, 0, 10}, {"alias", 1, 0, 10},
                     {"total", 2, 0, 10}, {"other", 4, 3, 10}, {"before", 5, 5, 7}};
    for (unsigned pc = 0; pc < scopes.code.size(); ++pc) { scopes.code_offsets.push_back(pc * 12); }
    result.functions.push_back(std::move(scopes));

    function_proto caught;
    caught.name = "caught";
    caught.param_count = 1;
    caught.frame_size = 3;
    caught.code = {instruction::with_bx(op::push_handler, 1, 3), // 0: pad at 4
                   {op::load_true, 2},                           // 1: check continues at PC2
                   {op::pop_handler},                            // 2
                   {op::ret, 0},                                 // 3: no fallthrough to pad
                   {op::move, 2, 1},                             // 4: thrown-value alias
                   {op::ret, 2}};                                // 5
    caught.locals = {{"input", 0, 0, 6},
                     {"during", 2, 1, 2},
                     {"after_check", 2, 2, 4},
                     {"error", 1, 4, 6},
                     {"error_alias", 2, 4, 6}};
    result.functions.push_back(std::move(caught));

    function_proto boxed;
    boxed.name = "boxed";
    boxed.param_count = 1;
    boxed.frame_size = 3;
    boxed.code = {{op::new_cell, 0},
                  {op::load_true, 2},
                  {op::new_cell, 1},
                  {op::cell_set, 1, 2},
                  {op::ret, 0}};
    boxed.locals = {{"captured_parameter", 0, 0, 5, true}, {"counter", 1, 0, 5, true}};
    result.functions.push_back(std::move(boxed));
    return result;
}

void check_scopes(ctcompile::js::import_result & imported) {
    auto function = function_named(*imported.module, "scopes$0");
    check(static_cast<bool>(function), "scope fixture imported");
    if (!function) { return; }
    mlir::Block & entry = function.getBody().front();
    for (unsigned arg = 0; arg < 3; ++arg) {
        check(source_name(entry.getArgument(arg)).empty(), "implicit ABI arguments stay unnamed");
    }
    check(source_name(entry.getArgument(3)) == "price", "move alias preserves parameter name");
    const auto total = result_at(function, 1, "ctjs.constant");
    check(source_name(total) == "total", "move names a temporary, then retains its first binding");
    check(total && has_pc(total.getLoc(), 1) && has_source_position(total.getLoc(), 2),
          "name metadata preserves direct instruction identity and original source position");
    check(source_name(result_at(function, 5, "ctjs.constant")) == "before", "first_pc is included");
    check(source_name(result_at(function, 7, "ctjs.constant")).empty(), "last_pc is excluded");
    check(source_name(result_at(function, 8, "ctjs.constant")) == "after",
          "a reused register gets its later source binding");

    const unsigned pcs[] = {5, 7, 8};
    const char * names[] = {"before", "", "after"};
    for (unsigned i = 0; i < 3; ++i) {
        mlir::Block * block = block_at(function, pcs[i]);
        check(block != nullptr, "expected CFG block");
        if (block == nullptr) { continue; }
        check(source_name(block->getArgument(5)) == names[i], "block argument uses its block PC");
        check(source_name(block->getArgument(0)) == "price", "parameter register survives CFG");
    }
    const auto & occupancy = imported.register_maps.front().slots;
    const auto argument_slots = occupancy.find(entry.getArgument(3));
    check(argument_slots != occupancy.end() && llvm::is_contained(argument_slots->second, 0) &&
              llvm::is_contained(argument_slots->second, 1),
          "parameter move aliases still occupy both registers");
    const auto total_slots = occupancy.find(total);
    check(total_slots != occupancy.end() && llvm::is_contained(total_slots->second, 2) &&
              llvm::is_contained(total_slots->second, 3) &&
              llvm::is_contained(total_slots->second, 4),
          "naming preserves all temporary and local occupancy");
}

void check_caught_and_boxed(mlir::ModuleOp module) {
    auto caught = function_named(module, "caught$1");
    check(static_cast<bool>(caught), "catch fixture imported");
    if (caught) {
        auto * land = instruction_at(caught, 4, "ctjs.catch_land");
        check(land != nullptr && land->getNumResults() == 2, "catch landing has both results");
        if (land != nullptr && land->getNumResults() == 2) {
            check(source_name(land->getResult(0)).empty(),
                  "catch status is not named as the error");
            check(source_name(land->getResult(1)) == "error", "catch value retains its own name");
        }
        mlir::Block * next = block_at(caught, 2);
        check(next != nullptr && source_name(next->getArgument(2)) == "after_check",
              "exception check continuation uses the next instruction's scope");
    }
    auto boxed = function_named(module, "boxed$2");
    check(static_cast<bool>(boxed), "boxed fixture imported");
    if (boxed) {
        check(source_name(boxed.getBody().front().getArgument(3)) == "captured_parameter",
              "capturing a parameter keeps its original argument name");
        check(source_name(result_at(boxed, 0, "ctjs.create_cell")) == "captured_parameter",
              "captured parameter cell has the parameter's name");
        check(source_name(result_at(boxed, 1, "ctjs.constant")) == "counter",
              "a boxed local store names its incoming temporary");
    }
}

void check_missing_and_ambiguous(mlir::MLIRContext & context, const program & original,
                                 mlir::ModuleOp named) {
    program stripped = original;
    for (auto & function : stripped.functions) { function.locals.clear(); }
    auto imported = ctcompile::js::import_program(stripped, "names.js", &context);
    check(imported.skipped.empty(), "missing optional local tables do not refuse functions");
    check(semantic_ir(named) == semantic_ir(*imported.module),
          "source names do not change any location-free IR");
    imported.module->walk([&](mlir::Operation * operation) {
        for (mlir::Value result : operation->getResults()) {
            check(source_name(result).empty(),
                  "absent debug tables leave operation results unnamed");
        }
        for (mlir::Region & region : operation->getRegions()) {
            for (mlir::Block & block : region) {
                for (mlir::BlockArgument argument : block.getArguments()) {
                    check(source_name(argument).empty(),
                          "absent debug tables leave arguments unnamed");
                }
            }
        }
    });

    program ambiguous = original;
    ambiguous.functions[0].locals.push_back({"overlap", 5, 6, 10});
    ambiguous.functions[0].locals.push_back({"bad_slot", 600, 0, 10});
    ambiguous.functions[0].locals.push_back({"empty_scope", 3, 1, 1});
    auto uncertain = ctcompile::js::import_program(ambiguous, "names.js", &context);
    check(uncertain.skipped.empty(), "ambiguous debug data does not affect import admission");
    auto function = function_named(*uncertain.module, "scopes$0");
    if (function) {
        check(source_name(result_at(function, 5, "ctjs.constant")).empty() &&
                  source_name(result_at(function, 8, "ctjs.constant")).empty(),
              "overlapping debug scopes do not invent a source name");
    }
}

void check_compiler_tables(mlir::MLIRContext & context) {
    const program compiled = compiler::compile(
        "function cost(price, tax) { var alias = price; var total = alias / 100; "
        "while (tax > 0) { total = total + tax; tax = tax - 1; } return total; }");
    check(compiled.ok, "JavaScript naming fixture compiles");
    auto imported = ctcompile::js::import_program(compiled, "names.js", &context);
    check(imported.skipped.empty(), "JavaScript naming fixture imports");
    auto function = function_named(*imported.module, "cost$1");
    check(static_cast<bool>(function), "compiled function is present");
    if (!function || !debug_names_enabled()) { return; }
    check(source_name(function.getBody().front().getArgument(3)) == "price",
          "real compiler parameter name survives its alias");
    check(source_name(function.getBody().front().getArgument(4)) == "tax",
          "real compiler records all ordinary parameters");
    bool named_total = false;
    bool named_loop_argument = false;
    function.walk([&](mlir::Operation * operation) {
        for (mlir::Value result : operation->getResults()) {
            named_total |= source_name(result) == "total";
        }
    });
    for (mlir::Block & block : llvm::drop_begin(function.getBody())) {
        for (mlir::BlockArgument argument : block.getArguments()) {
            named_loop_argument |= source_name(argument) == "total";
        }
    }
    check(named_total, "real compiler local assignment supplies a result name");
    check(named_loop_argument, "real compiler loop scopes supply block argument names");
}

} // namespace

int main() {
    mlir::MLIRContext context;
    const program source = fixture();
    auto imported = ctcompile::js::import_program(source, "names.js", &context);
    check(imported.skipped.empty(), "all source-name fixtures import");
    check_scopes(imported);
    check_caught_and_boxed(*imported.module);
    check_missing_and_ambiguous(context, source, *imported.module);
    check_compiler_tables(context);
    if (failures == 0) { std::puts("importer source-name provenance passed"); }
    return failures == 0 ? 0 : 1;
}
