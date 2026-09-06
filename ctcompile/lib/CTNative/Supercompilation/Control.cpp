#include "Driver.h"

#include "ctcompile/CTNative/Analysis/ClosedCallable.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/STLExtras.h"
#include <vector>

namespace ctcompile::ctnative::supercompilation {
namespace {
struct Shape {
    unsigned label;
    std::vector<Shape> children;
};

Shape shape(llvm::ArrayRef<mlir::Attribute> bindings) {
    Shape result{0, {}};
    for (auto literal : bindings) {
        unsigned label = 1; // Dynamic argument, independent of its SSA name.
        if (llvm::isa_and_nonnull<ctjs::NumberAttr>(literal)) { label = 2; }
        if (llvm::isa_and_nonnull<ctjs::StringAttr>(literal)) { label = 3; }
        if (llvm::isa_and_nonnull<ctjs::NullAttr>(literal)) { label = 4; }
        if (llvm::isa_and_nonnull<ctjs::UndefinedAttr>(literal)) { label = 5; }
        if (auto boolean = llvm::dyn_cast_or_null<ctjs::BooleanAttr>(literal)) {
            label = boolean.getValue() ? 6 : 7;
        }
        result.children.push_back({label, {}});
    }
    return result;
}

bool embedding(const Shape & left, const Shape & right) {
    // Coupling and diving, the usual homeomorphic embedding rules. This first
    // slice has only tuple/leaf shapes; later structured configurations must
    // preserve a finite label alphabet and bounded arity too.
    if (left.label == right.label && left.children.size() == right.children.size() &&
        llvm::all_of(llvm::zip(left.children, right.children),
                     [](auto pair) { return embedding(std::get<0>(pair), std::get<1>(pair)); })) {
        return true;
    }
    return llvm::any_of(right.children,
                        [&](const Shape & child) { return embedding(left, child); });
}
} // namespace

bool embeds(llvm::ArrayRef<mlir::Attribute> ancestor, llvm::ArrayRef<mlir::Attribute> next) {
    return embedding(shape(ancestor), shape(next));
}

Bindings commonBindings(llvm::ArrayRef<mlir::Attribute> ancestor,
                        llvm::ArrayRef<mlir::Attribute> next) {
    Bindings result(next.size());
    if (ancestor.size() != next.size()) { return result; }
    for (auto [i, value] : llvm::enumerate(next)) {
        if (ancestor[i] == value) { result[i] = value; }
    }
    return result;
}

std::string refusal(ctjs::FuncOp function, mlir::ModuleOp module) {
    if (!functionIndex(function) || function->getParentOp() != module ||
        mlir::SymbolTable::getSymbolVisibility(function) !=
            mlir::SymbolTable::Visibility::Private ||
        !llvm::hasSingleElement(function.getBody()) || function.getUpvalueCount() != 0) {
        return "requires a private indexed capture-free kernel with one entry block";
    }
    auto & entry = function.getBody().front();
    if (entry.getNumArguments() < 3 || !entry.hasNoPredecessors()) {
        return "kernel has unsupported entry arguments";
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (llvm::any_of(entry.getArgument(i).getUsers(),
                         [](mlir::Operation * use) { return !llvm::isa<ctjs::RootOp>(use); })) {
            return "kernel observes implicit call state";
        }
    }
    std::string problem = closedCallableProblem(function, module);
    bool recursive = false;
    function.getBody().walk([&](mlir::Operation * op) {
        if (!problem.empty()) { return; }
        if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op)) {
            auto target = call.getNewTarget().getDefiningOp<ctjs::ConstantOp>();
            if (call.getCallee() != function.getSymName() || !target ||
                !llvm::isa<ctjs::UndefinedAttr>(target.getValue())) {
                problem = "only ordinary self calls belong to this kernel slice";
            } else {
                recursive = true;
            }
            return;
        }
        if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(op)) {
            if (llvm::any_of(load.getResult().getUses(), [&](mlir::OpOperand & use) {
                    auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                    return !llvm::isa<ctjs::RootOp>(use.getOwner()) &&
                           !(call && use.getOperandNumber() == 2 &&
                             call.getCallee() == function.getSymName());
                })) {
                problem = "kernel reads runtime global state";
            }
            return;
        }
        if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
            for (auto & region : branch->getRegions()) {
                if (!region.empty() &&
                    (!llvm::hasSingleElement(region) || region.front().getNumArguments())) {
                    problem = "kernel has unsupported structured control";
                }
            }
            return;
        }
        if (!llvm::isa<ctjs::ConstantOp, ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                       ctjs::CompareOp, ctjs::TruthyOp, ctjs::ReturnOp, ctjs::FrameEnterOp,
                       ctjs::FrameExitOp, ctjs::RootOp, mlir::arith::ConstantOp,
                       mlir::arith::TruncIOp, mlir::scf::YieldOp>(op)) {
            problem =
                ("kernel retains unsupported operation `" + op->getName().getStringRef() + "`")
                    .str();
        }
    });
    if (problem.empty() && !recursive) { return "kernel has no self recursion"; }
    return problem;
}

} // namespace ctcompile::ctnative::supercompilation
