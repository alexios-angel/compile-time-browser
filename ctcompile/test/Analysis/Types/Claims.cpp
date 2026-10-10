//===- ctcompile/test/Analysis/Types/Claims.cpp - Static type claims ------===//
//
// The static half of the type oracle: what the compiler's type inference claims
// each register will hold, written in the format tools/check/type-oracle.py
// reads.
//
// Oracle.cpp runs a corpus through the interpreter and writes what every
// register actually held. This tool runs the same corpus through the compiler
// and writes what the inference says every register will hold. type-oracle.py
// compares the two and reports a soundness number.
//
// Both sides must describe the same program, and nothing here enforces that
// beyond both compiling the same file with the same compiler. The program key
// covers it: `program_source_hash` is FNV-1a over the source text, so a claim
// written against different bytes joins to no observation and shows up as
// unverifiable rather than as a false pass.
//
// This is a separate executable from Oracle.cpp on purpose: the recording is
// the interpreter's opinion and must not ask the compiler anything, so the
// recording and the claim cannot share a bug.
//
// A register's claim is the join over every value that ever occupied it. That
// is the only sound choice, because the recording is the set of everything the
// register held across the whole run. The importer's register map makes the
// join possible, and it records at write sites rather than at block
// terminators: a register written twice in one block leaves only its second
// value at the terminator, but the interpreter saw both.
//
//===----------------------------------------------------------------------===//

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
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "options.hpp"

namespace {

using ctcompile::ctnative::TypeLattice;

/// Returns the comma-separated atom names tools/check/type-oracle.py knows for
/// `type`, or "boxed" when nothing is proved.
///
/// The atom names are spelled here rather than shared, for the reason that file
/// gives about its own duplication: a checker that reads its definitions out of
/// the thing it checks cannot fail for the one reason that matters. If these
/// drift, the checker rejects the atom by name.
std::string atomsOf(mlir::Type type) {
    using namespace ctcompile::ctnative;

    // Nothing proved is `boxed`, not an empty claim. An empty claim is the
    // bottom of the lattice and asserts the register is never reached, which is
    // a violation the moment it is. This is the fallback, so it must be the
    // top.
    if (type == nullptr) { return "boxed"; }

    if (llvm::isa<BoolType>(type)) { return "bool"; }
    if (llvm::isa<StrType, StrViewType>(type)) { return "str"; }
    if (auto num = llvm::dyn_cast<NumType>(type)) {
        // i64 is not an atom the checker knows, and it is not an i32 either;
        // `f64` is the only sound name for it. The checker's `covers` admits
        // an observed i32 under a claimed f64 and never the reverse.
        return num.getKind() == NumKind::I32 ? "i32" : "f64";
    }
    // An empty optional stands for both `null` and `undefined`, because the type
    // inference represents both as an empty OptType. Claiming both keeps this
    // sound; CTNative_OptType's description records the precision this costs.
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
    // json, boxed, and every container. The inference does not produce container
    // types yet, so they fall back to `boxed` rather than to an untested rule.
    return "boxed";
}

} // namespace

int main(int argc, char ** argv) {
    namespace po = boost::program_options;
    std::string script;
    std::string out;
    std::uint32_t dump = ~0U; // No function has this index; without --dump nothing prints.
    po::options_description options{
        "usage: ctcompile-test-type-claims --script BUNDLE.js --out CLAIMS [--dump N]"};
    auto add = options.add_options();
    add("script", po::value(&script)->required(),
        "the corpus to import and run the inference over");
    add("out", po::value(&out)->required(), "where to write one claim per register");
    add("dump", po::value(&dump), "print function N's slot map to stderr");
    if (!ctbrowser_cli::parse_options(argc, argv, options)) { return 2; }

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

    // DeadCodeAnalysis is not optional; see TypeInference.h.
    mlir::DataFlowSolver solver;
    solver.load<mlir::dataflow::DeadCodeAnalysis>();
    // SparseConstantPropagation is not optional either. DeadCodeAnalysis decides
    // which successor of a branch is live by asking for every branch operand's
    // ConstantValue lattice; if nothing provides one, those lattices stay
    // uninitialized, it bails out, and no successor is ever marked live. The
    // sparse analysis then skips every op in every non-entry block. Without
    // this line the fixture had 109 unvisited values and zero registers beating
    // `boxed`, while the single-block unit tests all passed.
    solver.load<mlir::dataflow::SparseConstantPropagation>();
    solver.load<ctcompile::ctnative::TypeInference>();
    if (failed(solver.initializeAndRun(imported.module->getOperation()))) {
        std::fprintf(stderr, "type-claims: the solver did not converge\n");
        return 1;
    }

    // Count live blocks. The sparse analysis skips every operation in a block
    // DeadCodeAnalysis has not marked live, so a function whose entry block is
    // dead claims `boxed` for every register: sound, useless, and
    // indistinguishable from an inference with nothing to say. The lit tests in
    // claims/*.test require a non-zero `beat-boxed` count, which is what makes
    // a liveness regression fail.
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
    // Functions the importer refused get no claim, and the checker counts their
    // observed registers as "unclaimed" rather than sound. Report the refusals
    // grouped by reason: the p5.js corpus alone has about seventy generator
    // functions, and one line each would bury the summary.
    {
        std::map<std::string, std::size_t> reasons;
        for (const ctcompile::js::unsupported_opcode & skip : imported.skipped) {
            ++reasons[skip.reason];
        }
        for (const auto & [reason, count] : reasons) {
            std::fprintf(stderr, "type-claims: %zu function(s) not imported: %.80s\n", count,
                         reason.c_str());
        }
    }

    std::FILE * file = std::fopen(out.c_str(), "w");
    if (file == nullptr) {
        std::fprintf(stderr, "type-claims: cannot write %s\n", out.c_str());
        return 1;
    }
    std::fprintf(file, "# ctcompile Phase 54A, over %s\n", script.c_str());

    std::size_t claims = 0;
    std::size_t beatBoxed = 0;
    // Two kinds of value the solver never visited, handled differently. Treating
    // them as the join's identity would make registers claim only their entry
    // seed; boxing them all would destroy precision, because the importer
    // records arguments of the leader block it creates after every `ret`, a
    // block with no predecessors whose values never occupy a register at run
    // time.
    //
    //   * A value in a block DeadCodeAnalysis proved unreachable contributes
    //     nothing: it never executes, so the interpreter never observed it and
    //     dropping it is exact, not optimistic.
    //   * A value in a live block with no lattice is a gap in the analysis: it
    //     is boxed so the claim stays sound, and counted; the claims/*.test lit
    //     tests require that count to be zero.
    std::size_t deadValues = 0;
    std::size_t unvisited = 0;
    const auto blockIsLive = [&](mlir::Block * block) {
        if (block == nullptr) { return true; }
        const auto * executable =
            solver.lookupState<mlir::dataflow::Executable>(solver.getProgramPointBefore(block));
        return executable != nullptr && executable->isLive();
    };
    for (const ctcompile::js::register_map & map : imported.register_maps) {
        // Slot -> the join of every value that ever occupied it.
        std::map<std::uint16_t, mlir::Type> perSlot;
        for (const auto & entry : map.slots) {
            const TypeLattice * lattice = solver.lookupState<TypeLattice>(entry.first);
            mlir::Type inferred = lattice == nullptr ? mlir::Type{} : lattice->getValue().getType();
            mlir::Value occupant = entry.first; // getParentBlock is not const.
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
                    // Join the claims of every value that occupied this slot
                    // (ctnative::meet is this lattice's join; see
                    // TypeInference.h). This is why a claim is per register
                    // while the analysis is per value.
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
