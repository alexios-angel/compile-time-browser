// PHASE 54A'S HALF OF THE ORACLE: the static claim, written in the format the
// checker reads.
//
// TypeOracle.cpp runs a corpus through the INTERPRETER and writes what every
// register actually held. This runs the same corpus through the COMPILER and
// writes what the inference says every register will hold. tools/check/
// type-oracle.py compares the two and the answer is a soundness number.
//
// THE TWO PROGRAMS MUST BE THE SAME PROGRAM, and nothing here enforces that
// beyond both sides compiling the same file with the same compiler. That is
// what the program key is for: `program_source_hash` is FNV-1a over the source
// text, so a claim written against different bytes simply fails to join to any
// observation and shows up as unverifiable rather than as a false pass.
//
// WHY A SEPARATE EXECUTABLE FROM TypeOracle.cpp. That file says it "needs no
// MLIR: a recording is the INTERPRETER's opinion, and asking the compiler
// anything here would beg the question." Keeping the two apart is what stops
// the recording and the claim sharing a bug.
//
// A REGISTER'S CLAIM IS THE JOIN OVER EVERY VALUE THAT EVER OCCUPIED IT, which
// is the only thing that can be sound: the recording is a SET of everything the
// register held across the whole run, so the claim has to admit all of it. The
// importer's register map is what makes that join possible - and it records at
// the write sites rather than at block terminators, because a register written
// twice in one block leaves only its second value at the terminator and the
// interpreter saw both.
#include "ctcompile/CTJS/IR/CTJSDialect.h"
#include "ctcompile/CTJS/Import/BytecodeImport.hpp"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "ctbrowser/script/compile.hpp"
#include "ctbrowser/script/type_record.hpp"

#include "mlir/Analysis/DataFlow/ConstantPropagationAnalysis.h"
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlowFramework.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Interfaces/FunctionInterfaces.h"

#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using ctcompile::ctnative::TypeLattice;

// The atom names tools/check/type-oracle.py knows. Spelled here rather than
// shared, for the reason that file gives about its own duplication: a checker
// that reads its definitions out of the thing it checks cannot fail for the one
// reason that matters. If these drift, the checker rejects the atom by name.
std::string atomsOf(mlir::Type type) {
    using namespace ctcompile::ctnative;

    // NOTHING PROVED IS `boxed`, NOT AN EMPTY CLAIM. An empty claim is the
    // bottom of the lattice and asserts the register is never reached, which is
    // a violation the moment it is. This is the fall-back, so it must be the
    // top.
    if (type == nullptr) { return "boxed"; }

    if (llvm::isa<BoolType>(type)) { return "bool"; }
    if (llvm::isa<StrType, StrViewType>(type)) { return "str"; }
    if (auto num = llvm::dyn_cast<NumType>(type)) {
        // i64 IS NOT AN ATOM the checker knows, and it is not an i32 either -
        // `f64` is the only sound name for it. The checker's `covers` admits
        // an observed i32 under a claimed f64 and never the reverse.
        return num.getKind() == NumKind::I32 ? "i32" : "f64";
    }
    // AN EMPTY OPTIONAL CARRIES BOTH `null` AND `undefined`, because part 24
    // collapses the two into it. Claiming both is the over-approximation that
    // keeps this sound; CTNative_OptType's description records the narrowing
    // this costs.
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        const std::string inner = atomsOf(opt.getElementType());
        if (inner == "boxed") { return "boxed"; }
        std::string out = "undefined,null";
        if (!inner.empty()) { out += "," + inner; }
        return out;
    }
    if (llvm::isa<BottomType>(type)) { return ""; }
    if (auto variant = llvm::dyn_cast<VariantType>(type)) {
        std::string out;
        for (mlir::Type alternative : variant.getAlternatives()) {
            const std::string part = atomsOf(alternative);
            if (part == "boxed") { return "boxed"; }
            if (part.empty()) { continue; }
            if (!out.empty()) { out += ","; }
            out += part;
        }
        return out.empty() ? "boxed" : out;
    }
    // json, boxed, and every container. Nothing in Phase 54A produces a
    // container, and a rule for a case with no producer is a rule nobody has
    // tested.
    return "boxed";
}

} // namespace

int main(int argc, char ** argv) {
    std::string script;
    std::string out;
    std::uint32_t dump = ~0U; // --dump N: print function N's slot map to stderr
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto next = [&]() -> std::string { return ++i < argc ? argv[i] : std::string{}; };
        if (flag == "--script") {
            script = next();
        } else if (flag == "--out") {
            out = next();
        } else if (flag == "--dump") {
            dump = static_cast<std::uint32_t>(std::atoi(next().c_str()));
        } else {
            std::fprintf(stderr, "usage: type-claims --script BUNDLE.js --out CLAIMS\n");
            return 2;
        }
    }
    if (script.empty() || out.empty()) {
        std::fprintf(stderr, "type-claims: --script and --out are both required\n");
        return 2;
    }

    std::ifstream in{script, std::ios::binary};
    if (!in) {
        std::fprintf(stderr, "type-claims: cannot read %s\n", script.c_str());
        return 1;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();

    const ctbrowser::script::program prog = ctbrowser::script::compiler::compile(source);
    if (!prog.ok) {
        std::fprintf(stderr, "type-claims: %s does not compile: %s\n", script.c_str(),
                     prog.error.c_str());
        return 1;
    }
    const std::uint64_t hash = ctbrowser::script::program_source_hash(prog);

    mlir::MLIRContext context;
    context.getOrLoadDialect<ctcompile::ctjs::CTJSDialect>();
    context.getOrLoadDialect<ctcompile::ctnative::CTNativeDialect>();

    ctcompile::js::import_result imported = ctcompile::js::import_program(prog, "corpus", &context);
    if (!imported.module) {
        std::fprintf(stderr, "type-claims: %s did not import\n", script.c_str());
        return 1;
    }

    // DeadCodeAnalysis is not optional - see TypeInference.h.
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    // AND SparseConstantPropagation, WHICH IS NOT OPTIONAL EITHER, and the
    // reason is a trap worth naming. DeadCodeAnalysis decides which successor
    // of a branch is live by asking for every branch operand's ConstantValue
    // lattice; if nothing provides one, those lattices stay uninitialized, it
    // bails out, and NO successor is ever marked live. The sparse analysis
    // then skips every op in every non-entry block - measured: 109 unvisited
    // values and zero registers beating `boxed` on the fixture, while the
    // single-block unit rows all passed.
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<ctcompile::ctnative::TypeInference>();
    if (failed(solver.initializeAndRun(imported.module->getOperation()))) {
        std::fprintf(stderr, "type-claims: the solver did not converge\n");
        return 1;
    }

    // LIVENESS, COUNTED. The sparse analysis skips every operation in a block
    // DeadCodeAnalysis has not marked live, and a function whose entry block
    // is dead produces a claim of `boxed` for every register - which is sound
    // and worthless and looks, from the outside, exactly like an inference
    // that has nothing to say. So the ratio is printed, and the fixture's
    // cmake gate on "beat boxed" is what turns a liveness regression red.
    std::size_t blocks = 0;
    std::size_t liveBlocks = 0;
    imported.module->walk([&](mlir::FunctionOpInterface fn) {
        for (mlir::Block & block : fn.getFunctionBody()) {
            ++blocks;
            const auto * executable = solver.lookupState<mlir::dataflow::Executable>(
                solver.getProgramPointBefore(&block));
            if (executable != nullptr && executable->isLive()) { ++liveBlocks; }
        }
    });
    for (const ctcompile::js::unsupported_opcode & skip : imported.skipped) {
        std::fprintf(stderr, "type-claims: function %u not imported: %s\n", skip.function_index,
                     skip.reason.c_str());
    }

    std::FILE * file = std::fopen(out.c_str(), "w");
    if (file == nullptr) {
        std::fprintf(stderr, "type-claims: cannot write %s\n", out.c_str());
        return 1;
    }
    std::fprintf(file, "# ctcompile Phase 54A, over %s\n", script.c_str());

    std::size_t claims = 0;
    std::size_t beatBoxed = 0;
    // TWO KINDS OF "THE SOLVER NEVER VISITED THIS", AND THEY MUST NOT BE
    // CONFLATED. The first version of this loop treated an unvisited value as
    // the identity of the join, and thirty bootstrap registers claimed only
    // their entry seed. The second boxed every unvisited value, and precision
    // went to zero - because the importer records the arguments of the leader
    // block it makes after every `ret`, a block with no predecessors, whose
    // values never occupy a register at run time.
    //
    //   * a value in a block DeadCodeAnalysis proved UNREACHABLE contributes
    //     nothing: it is never executed, so the interpreter never observed it
    //     and dropping it is exact, not optimistic;
    //   * a value in a LIVE block with no lattice is a gap in the analysis, is
    //     boxed so the claim stays sound, and is counted - check-type-claims
    //     .cmake fails the fixture if that count is anything but zero.
    std::size_t deadValues = 0;
    std::size_t unvisited = 0;
    const auto blockIsLive = [&](mlir::Block * block) {
        if (block == nullptr) { return true; }
        const auto * executable =
            solver.lookupState<mlir::dataflow::Executable>(solver.getProgramPointBefore(block));
        return executable != nullptr && executable->isLive();
    };
    for (const ctcompile::js::register_map & map : imported.register_maps) {
        // slot -> the join of every value that ever occupied it.
        std::map<std::uint16_t, mlir::Type> perSlot;
        for (const auto & entry : map.slots) {
            const TypeLattice * lattice = solver.lookupState<TypeLattice>(entry.first);
            mlir::Type inferred = lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
            mlir::Value occupant = entry.first; // getParentBlock is not const
            const bool dead = !blockIsLive(occupant.getParentBlock());
            const bool wasUnvisited = !dead && inferred == nullptr;
            if (dead) {
                ++deadValues;
            } else if (wasUnvisited) {
                ++unvisited;
                inferred = ctcompile::ctnative::BoxedType::get(&context);
            }
            if (map.function_index == dump) {
                std::string type;
                llvm::raw_string_ostream os{type};
                if (dead) {
                    os << "<dead block>";
                } else if (wasUnvisited) {
                    os << "<UNVISITED IN A LIVE BLOCK>";
                } else {
                    os << inferred;
                }
                mlir::Operation * definer = entry.first.getDefiningOp();
                std::fprintf(stderr, "  fn %u slots [", map.function_index);
                for (const std::uint16_t slot : entry.second) { std::fprintf(stderr, " %u", slot); }
                std::fprintf(stderr, " ] <- %s : %s\n",
                             definer != nullptr ? definer->getName().getStringRef().str().c_str()
                                                : "block-arg",
                             type.c_str());
            }
            if (dead) { continue; }
            for (const std::uint16_t slot : entry.second) {
                auto found = perSlot.find(slot);
                if (found == perSlot.end()) {
                    perSlot.emplace(slot, inferred);
                } else {
                    // THE JOIN, and the reason a claim is per REGISTER while
                    // the analysis is per VALUE.
                    found->second = ctcompile::ctnative::meet(found->second, inferred);
                }
            }
        }
        for (const auto & [slot, type] : perSlot) {
            const std::string atoms = atomsOf(type);
            std::fprintf(file, "claim %016llx %u %u %s\n", static_cast<unsigned long long>(hash),
                         map.function_index, static_cast<unsigned>(slot),
                         atoms.empty() ? "" : atoms.c_str());
            ++claims;
            if (atoms != "boxed") { ++beatBoxed; }
        }
    }
    std::fclose(file);

    std::fprintf(stderr,
                 "type-claims: %zu functions, %zu register claims, %zu beat boxed, "
                 "%zu values in dead blocks dropped, %zu unvisited live values boxed, "
                 "%zu of %zu blocks live, program %016llx -> %s\n",
                 imported.register_maps.size(), claims, beatBoxed, deadValues, unvisited,
                 liveBlocks, blocks, static_cast<unsigned long long>(hash), out.c_str());
    return 0;
}
