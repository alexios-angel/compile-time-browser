#include "../lib/CTNative/Analysis/OwnedMethodTableSlots.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <cstdio>

namespace {
namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::OwnedMethodTableSlots;

// Two independently valid owners ensure a late budget cutoff has an earlier
// candidate to discard. The scalar payloads are intentional: this structural
// query supplies slot edges, while its consumers must prove method-table types.
constexpr const char * fixture = R"MLIR(
module {
  ctjs.func private @slots(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %key = ctjs.constant #ctjs.string<"item">
    %value = ctjs.constant #ctjs.number<4631107791820423168>
    %first = ctjs.create_object
    ctjs.set_property %first[%key], %value
    %read_first = ctjs.get_property %first[%key]
    %second = ctjs.create_object
    ctjs.set_property %second[%key], %value
    %read_second = ctjs.get_property %second[%key]
    ctjs.return %read_second
  }
}
)MLIR";

int failures = 0;
void check(bool value, const char * message) {
    if (value) { return; }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}
} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    auto module = mlir::parseSourceString<mlir::ModuleOp>(fixture, &context);
    check(static_cast<bool>(module), "two-owner slot fixture parses");
    if (!module) { return 1; }
    llvm::SmallVector<ctjs::SetPropertyOp> writes;
    llvm::SmallVector<ctjs::GetPropertyOp> reads;
    module->walk([&](ctjs::SetPropertyOp operation) { writes.push_back(operation); });
    module->walk([&](ctjs::GetPropertyOp operation) { reads.push_back(operation); });
    check(writes.size() == 2 && reads.size() == 2, "fixture contains both store/read pairs");
    if (writes.size() != 2 || reads.size() != 2) { return 1; }

    const auto empty = [&](const OwnedMethodTableSlots & query) {
        if (!query.slots().empty()) { return false; }
        for (ctjs::SetPropertyOp write : writes) {
            if (query.lookup(write)) { return false; }
        }
        for (ctjs::GetPropertyOp read : reads) {
            if (query.lookup(read)) { return false; }
        }
        return true;
    };
    const auto complete = [&](const OwnedMethodTableSlots & query) {
        if (query.exhausted() || query.slots().size() != 2) { return false; }
        for (unsigned index = 0; index != 2; ++index) {
            const auto * slot = query.lookup(writes[index]);
            if (!slot || slot != query.lookup(reads[index]) ||
                slot->initialization != writes[index]) {
                return false;
            }
        }
        return true;
    };
    OwnedMethodTableSlots normal(*module);
    check(complete(normal), "default budget proves both independent slot edges");
    OwnedMethodTableSlots zero(*module, 0);
    check(zero.exhausted() && empty(zero), "zero budget exposes no usable proof");

    // Discover the finite completion boundary instead of duplicating internal
    // accounting. Every earlier cutoff, including one after a successful first
    // slot, must discard the entire proof and both lookup indices.
    unsigned completionBudget = 0;
    for (unsigned budget = 1; budget <= 4096; ++budget) {
        OwnedMethodTableSlots query(*module, budget);
        if (query.exhausted()) {
            check(empty(query), "exhaustion never publishes a successful prefix");
            continue;
        }
        completionBudget = budget;
        check(complete(query), "first completed finite budget proves every slot");
        break;
    }
    check(completionBudget != 0, "small fixture completes within a finite work budget");
    if (completionBudget != 0) {
        OwnedMethodTableSlots exact(*module, completionBudget);
        check(complete(exact), "exact completion budget reproduces the complete proof");
        OwnedMethodTableSlots shortByOne(*module, completionBudget - 1);
        check(shortByOne.exhausted() && empty(shortByOne),
              "one step below completion withholds all slot edges");
    }
    // A completed query borrows one IR snapshot. Rebuild after semantic edits:
    // neither a previously successful edge nor a supplied marker can rescue an
    // owner whose initialization, read ordering or confinement has changed.
    // The independent second owner must remain available in each local refusal.
    const auto rejectsFirst = [&](const OwnedMethodTableSlots & query) {
        const auto * second = query.lookup(writes[1]);
        return !query.exhausted() && query.slots().size() == 1 && !query.lookup(writes[0]) &&
               !query.lookup(reads[0]) && second && second == query.lookup(reads[1]) &&
               second->initialization == writes[1];
    };
    auto firstOwner = writes[0].getObject().getDefiningOp();
    for (mlir::Operation * operation :
         {firstOwner, writes[0].getOperation(), reads[0].getOperation()}) {
        operation->setAttr("ctnative.owned_method_table_slot",
                           mlir::StringAttr::get(&context, "stale-success"));
        operation->setAttr("ctnative.method_table", mlir::StringAttr::get(&context, "forged"));
    }
    check(complete(OwnedMethodTableSlots(*module)), "markers do not change a valid slot census");

    mlir::OpBuilder builder(&context);
    builder.setInsertionPointAfter(reads[0]);
    auto * rewrite = builder.clone(*writes[0].getOperation());
    OwnedMethodTableSlots rewritten(*module);
    check(rejectsFirst(rewritten) && !rewritten.lookup(llvm::cast<ctjs::SetPropertyOp>(rewrite)),
          "a later rewrite revokes both stores and the original read despite stale markers");
    rewrite->erase();
    check(complete(OwnedMethodTableSlots(*module)), "removing the rewrite restores both slots");

    writes[0]->moveAfter(reads[0]);
    check(rejectsFirst(OwnedMethodTableSlots(*module)),
          "moving initialization after its read revokes the slot despite stale markers");
    writes[0]->moveBefore(reads[0]);
    check(complete(OwnedMethodTableSlots(*module)), "restoring dominance restores both slots");

    auto returned = llvm::cast<ctjs::ReturnOp>(writes[0]->getBlock()->getTerminator());
    const auto originalResult = returned.getValue();
    returned->setOperand(0, writes[0].getObject());
    check(rejectsFirst(OwnedMethodTableSlots(*module)),
          "a newly escaping owner revokes its slot despite stale markers");
    returned->setOperand(0, originalResult);
    check(complete(OwnedMethodTableSlots(*module)), "removing the escape restores both slots");

    if (failures == 0) { std::puts("owned method-table slot budget and live-query checks passed"); }
    return failures == 0 ? 0 : 1;
}
