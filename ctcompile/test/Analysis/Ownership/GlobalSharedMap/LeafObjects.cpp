//===- LeafObjects.cpp - Leaf object owners ---------------------*- C++ -*-===//
//
// Tests that a captured Map's owner also owns the plain objects ("leaf
// objects") its methods allocate and store, with an empty, Number or String
// field.
//
//===----------------------------------------------------------------------===//

#include "LeafObjects/Helpers.hpp"

namespace ctcompile::test::owned_global_shared_map {

void checkLeafOwner(mlir::MLIRContext & context, const std::string & source, bool lifted) {
    checkCallerPayloadOwner(context, source, lifted);
    checkChildMapOwner(context, source, lifted);
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    const std::string write = "    %written = ctjs.call %setter(%state, %entryKey, %value)";
    // Give the stored leaf a `value` field of 1.0 (4607182418800017408 is
    // 0x3FF0000000000000, the IEEE-754 bits of 1.0), or of a String.
    const auto numeric =
        replaced(source, write,
                 "    %fieldKey = ctjs.constant #ctjs.string<\"value\">\n"
                 "    %fieldValue = ctjs.constant #ctjs.number<4607182418800017408>\n"
                 "    ctjs.set_property %value[%fieldKey], %fieldValue\n" +
                     write);
    const auto string =
        replaced(numeric, "#ctjs.number<4607182418800017408>", "#ctjs.string<\"owned\">");
    for (const auto & [program, hasField] :
         {std::pair{source, false}, {numeric, true}, {string, true}}) {
        auto module = parsed(context, program, "source/prepared leaf owner fixture parses");
        if (!module) { continue; }
        auto contract = mapContractFor(*module);
        OwnedGlobalRoots query(*module, contract);
        check(query.proved() && query.roots().size() == 1,
              "a wrapper publishes a complete owner for repeated method-local leaf allocations");
        if (!query.proved() || query.roots().empty()) {
            std::fprintf(stderr, "leaf owner %s %s: %s\n", lifted ? "prepared" : "source",
                         hasField ? "field" : "empty", query.reason().str().c_str());
            continue;
        }
        const auto & table = *query.roots().front().methodTable;
        const auto & capture = *table.capturedMap;
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$4");
        const auto expected = Alternatives::forTag(mlir::TypeID::get<ctjs::StringAttr>());
        bool complete =
            table.methods.size() == 2 && table.calls.size() == 4 && capture.closures.size() == 2 &&
            capture.parameters.size() == 2 && capture.calls.size() == 1 &&
            capture.reads.size() == 3 && capture.upvalues.size() == (lifted ? 0u : 2u) &&
            capture.childScalarContents == Alternatives{} && capture.leafObjects.size() == 1 &&
            capture.leafWrites.size() == (hasField ? 1u : 0u) && capture.leafReads.empty();
        unsigned setters = 0;
        mlir::Operation * previous = nullptr;
        for (const auto & edge : table.calls) {
            complete &= edge.capturedMap && (!previous || previous->isBeforeInBlock(edge.call));
            previous = edge.call;
            if (!edge.capturedMap) { continue; }
            complete &= edge.capturedMap->allocation == capture.allocation &&
                        edge.capturedMap->calls == capture.calls &&
                        edge.capturedMap->reads == capture.reads &&
                        edge.capturedMap->parameters == capture.parameters &&
                        edge.capturedMap->leafObjects == capture.leafObjects &&
                        edge.capturedMap->leafWrites == capture.leafWrites &&
                        edge.capturedMap->leafReads == capture.leafReads;
            if (edge.function != setter) {
                complete &= edge.arguments.empty();
                continue;
            }
            ++setters;
            complete &= edge.arguments.size() == 1;
            if (edge.arguments.size() != 1) { continue; }
            const auto & argument = edge.arguments.front();
            // The prepared put$4 takes (this, new.target, callee, environment, key),
            // so the key is parameter 4 and its call_direct passes it as operand 4.
            // The source form has parameter 3, and its ctjs.call passes it as
            // operand 2, after the callee and the receiver.
            complete &=
                argument.alternatives == expected &&
                argument.parameter == setter.getBody().front().getArgument(lifted ? 4 : 3) &&
                argument.actual == edge.call->getOperand(lifted ? 4 : 2);
        }
        check(complete && setters == 3,
              "one owner retains all sibling effects and repeated/future String setter actuals");
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "leaf ownership does not move allocations into the entry or rewrite source calls");
        const unsigned completion = query.steps();
        check(completion < 15000, "leaf owner proof stays within the fixture work bound");
        if (completion >= 15000) { continue; }
        checkBudgetsBelow(
            *module, contract, completion,
            "every incomplete leaf owner budget withholds the whole published family");
        OwnedGlobalRoots exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact leaf owner budget reproduces the entire publication chain");
        ctjs::CreateObjectOp leaf;
        ctjs::SetPropertyOp field;
        setter.walk([&](ctjs::CreateObjectOp operation) { leaf = operation; });
        setter.walk([&](ctjs::SetPropertyOp operation) { field = operation; });
        check(leaf && static_cast<bool>(field) == hasField,
              "the owner retains the exact local allocation and its actual own-field write");
        if (!leaf) { continue; }
        check(
            capture.leafObjects == std::vector{leaf} &&
                capture.leafWrites ==
                    (hasField ? std::vector{field} : std::vector<ctjs::SetPropertyOp>{}),
            "source leaf handles occur once without entry allocations or scratch-body duplicates");
        auto store = capture.calls.front();
        check(store.getArgs().back() == leaf.getResult(),
              "the stored leaf is the current method allocation, never a provider token");
        mlir::Builder attributes(&context);
        module->walk([&](mlir::Operation * operation) {
            operation->setAttr("ctnative.host_owner_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.host_proved", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_present", attributes.getBoolAttr(true));
            operation->setAttr("ctnative.map_read_type", attributes.getStringAttr("number"));
            operation->setAttr("ctnative.map_write_type", attributes.getStringAttr("number"));
            operation->setAttr("ctnative.object_schema", attributes.getStringAttr("leaf"));
        });
        contract = mapContractFor(*module);
        check(OwnedGlobalRoots(*module, contract).proved(),
              "previous object and owner reports never replace the independent source query");
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value value) {
            const auto saved = operation->getOperand(operand);
            operation->setOperand(operand, value);
            checkStaleAndRefused(
                *module, contract, mapContractFor(*module),
                "a changed leaf source invalidates the original owner fingerprint",
                "forged object reports cannot restore ownership of an unsafe fresh source");
            operation->setOperand(operand, saved);
            check(OwnedGlobalRoots(*module, contract).proved(),
                  "restoring the source restores its independently checked leaf owner");
        };
        mutation(store, 2, leaf.getResult());
        mutation(store, 3, store.getReceiver());
        auto returned = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
        mutation(returned, 0, leaf.getResult());
        auto sibling = module->lookupSymbol<ctjs::FuncOp>("get$3");
        auto siblingReturn = llvm::cast<ctjs::ReturnOp>(sibling.getBody().front().getTerminator());
        mutation(siblingReturn, 0, sibling.getBody().front().getArgument(0));
        if (field) {
            mutation(field, 0, setter.getBody().front().getArgument(0));
            mutation(field, 1, setter.getBody().front().getArgument(lifted ? 4 : 3));
            mutation(field, 2, leaf.getResult());
        }
        std::printf("leaf owner %s %s: all %u incomplete budgets checked\n",
                    lifted ? "prepared" : "source", hasField ? "field" : "empty", completion);
    }
    const auto refuse = [&](const std::string & program, const char * message) {
        auto module = parsed(context, program, "source/prepared leaf owner refusal parses");
        if (!module) { return; }
        OwnedGlobalRoots query(*module, mapContractFor(*module));
        check(!query.proved() && !query.exhausted() && empty(*module, query), message);
    };
    refuse(replaced(numeric, "ctjs.set_property %value[%fieldKey], %fieldValue",
                    "ctjs.set_property %value[%fieldKey], %state"),
           "a leaf field retaining its owning Map is not an acyclic owner");
    refuse(replaced(numeric, "#ctjs.string<\"value\">", "#ctjs.string<\"__proto__\">"),
           "a prototype field cannot be published as an ordinary leaf owner");
    refuse(
        replaced(source, "%value = ctjs.create_object", "%value = ctjs.load_global \"external\""),
        "an external object cannot borrow a local leaf allocation identity");
    refuse(replaced(source,
                    "    %key = ctjs.constant #ctjs.string<\"size\">\n"
                    "    %size = ctjs.get_property %state[%key]",
                    "    %key = ctjs.constant #ctjs.string<\"get\">\n"
                    "    %reader = ctjs.get_property %state[%key]\n"
                    "    %entryKey = ctjs.constant #ctjs.string<\"x\">\n"
                    "    %size = ctjs.call %reader(%state, %entryKey)"),
           "a separate sibling cannot publish an unproved object-bearing Map result");
}

} // namespace ctcompile::test::owned_global_shared_map
