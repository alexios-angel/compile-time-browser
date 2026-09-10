#include "Tests.h"

namespace ctcompile::test::host_contract_seeded_maps {

void checkNullablePayloadResults(mlir::MLIRContext & context, const std::string & nullable,
                                 bool prepared) {
    using Alternatives = ctcompile::ctnative::PrimitiveAlternatives;
    constexpr unsigned nullableMask = Alternatives::String | Alternatives::Null;
    constexpr llvm::StringLiteral write =
        "    %written = ctjs.call %setter(%state, %entryKey, %entryKey)";
    constexpr llvm::StringLiteral read = "    %loaded = ctjs.call %reader(%state, %entryKey)";
    constexpr llvm::StringLiteral returned = "    ctjs.return %loaded";
    auto source =
        replaced(nullable, "    %written = ctjs.call %setter(%state, %entryKey, %value)", write);
    source = replaced(source,
                      "    %u = ctjs.constant #ctjs.undefined\n"
                      "    ctjs.return %u\n  }\n}\n",
                      "    %getKey = ctjs.constant #ctjs.string<\"get\">\n"
                      "    %reader = ctjs.get_property %state[%getKey]\n" +
                          read.str() + "\n" + returned.str() + "\n  }\n}\n");
    source = replaced(source, "    ctjs.return %table",
                      "    %sizer = ctjs.create_closure %callee[4] this %u captures %" +
                          std::string(prepared ? "state" : "cell") +
                          "\n    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                          "    ctjs.set_property %table[%sizeKey], %sizer\n"
                          "    ctjs.return %table");
    const std::string environment = prepared ? ", %state: !ctjs.value" : "";
    source = replaced(source, "\n}\n",
                      "\n  ctjs.func private @size$4(%this: !ctjs.value, %new: !ctjs.value, "
                      "%callee: !ctjs.value" +
                          environment +
                          ", %entryKey: !ctjs.value) -> !ctjs.value attributes {upvalue_count = " +
                          std::string(prepared ? "0" : "1") + " : i32} {\n" +
                          (prepared ? "" : "    %state = ctjs.load_upvalue %callee[0]\n") + R"MLIR(
    %setKey = ctjs.constant #ctjs.string<"set">
    %setter = ctjs.get_property %state[%setKey]
    %written = ctjs.call %setter(%state, %entryKey, %entryKey)
    %sizeKey = ctjs.constant #ctjs.string<"size">
    %size = ctjs.get_property %state[%sizeKey]
    ctjs.return %size
  }
}
)MLIR");
    std::string consumer = "    %sizeKey = ctjs.constant #ctjs.string<\"size\">\n"
                           "    %sizer = ctjs.get_property %owned[%sizeKey]\n";
    if (prepared) {
        consumer += "    %sizeEnvironment = ctjs.load_upvalue %sizer[0]\n"
                    "    %answer = ctjs.call_direct @size$4(%owned, %u, %sizer, "
                    "%sizeEnvironment, %putResult)";
        source = replaced(source,
                          "    %getEnvironment = ctjs.load_upvalue %getter[0]\n"
                          "    %answer = ctjs.call_direct @get$2(%owned, %u, %getter, "
                          "%getEnvironment, %falseFlag)",
                          consumer);
    } else {
        consumer += "    %answer = ctjs.call %sizer(%owned, %putResult)";
        source = replaced(source, "    %answer = ctjs.call %getter(%owned, %falseFlag)", consumer);
    }
    source = replaced(source, "    %getter = ctjs.get_property %owned[%key]\n", "");
    const auto requested = [](mlir::ModuleOp module) {
        auto contract = contractFor(module);
        contract.initialIntrinsics = {"Map"};
        return contract;
    };
    const auto withheld = [](mlir::ModuleOp module, const HostContractAnalysis & query) {
        bool empty = query.callables().empty();
        module.walk([&](mlir::Operation * operation) {
            empty &= !query.callable(operation);
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
                empty &= !query.property(read);
            }
        });
        return empty;
    };
    const auto edges = [&](mlir::ModuleOp module, const HostContractAnalysis & query,
                           unsigned resultMask) {
        const auto calls = query.callables();
        bool complete = calls.size() == 3;
        for (unsigned index = 0; index < calls.size(); ++index) {
            const auto & call = calls[index];
            const char * name = index == 0 ? "get$2" : index == 1 ? "put$3" : "size$4";
            auto function = module.lookupSymbol<ctjs::FuncOp>(name);
            const unsigned mask = index == 0   ? Alternatives::Boolean
                                  : index == 1 ? nullableMask
                                               : resultMask;
            const Alternatives expected{
                mask & (Alternatives::Boolean | Alternatives::Number | Alternatives::String), mask,
                true};
            complete &= call.function == function && call.capturedMap && call.arguments.size() == 1;
            if (!function || !call.capturedMap || call.arguments.size() != 1) { continue; }
            const auto & argument = call.arguments.front();
            complete &=
                argument.parameter == function.getBody().front().getArgument(prepared ? 4 : 3) &&
                argument.actual == call.call->getOperand(prepared ? 4 : 2) &&
                argument.alternatives == expected;
            if (index) {
                complete &= argument.actual == calls[index - 1].call->getResult(0) &&
                            calls[index - 1].call->isBeforeInBlock(call.call);
            }
            unsigned families = 0;
            for (const auto & parameters : call.capturedMap->parameters) {
                if (parameters.function != function) { continue; }
                ++families;
                complete &= parameters.alternatives == std::vector{expected};
            }
            complete &= families == 1;
        }
        check(complete,
              "get, nullable payload readback and size retain independent complete SSA families");
    };
    unsigned rows = 0;
    const auto variant = [&](const std::string & program, bool expected, const char * message,
                             unsigned resultMask = Alternatives::String | Alternatives::Null) {
        ++rows;
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "source/prepared nullable payload fixture parses");
        if (!module) { return; }
        const auto contract = requested(*module);
        HostContractAnalysis query(*module, contract);
        check(query.proved() == expected && !query.exhausted() &&
                  (expected || withheld(*module, query)),
              message);
        if (query.proved() != expected || query.exhausted()) {
            std::fprintf(stderr, "nullable payload host %s case %u: %s\n",
                         prepared ? "prepared" : "source", rows, query.reason().str().c_str());
        }
        if (expected && query.proved()) { edges(*module, query, resultMask); }
        check(hostContractFingerprint(*module) == contract.moduleSha256,
              "nullable payload proof preserves every source operation and operand");
    };
    variant(source, true,
            "a definitely present nullable Map read feeds a distinct published method");
    const auto conditional = replaced(source, write, R"MLIR(
    %present = ctjs.truthy %entryKey
    scf.if %present {
      %left = ctjs.call %setter(%state, %entryKey, %entryKey)
      scf.yield
    } else {
      %null = ctjs.constant #ctjs.null
      %right = ctjs.call %setter(%state, %entryKey, %null)
      scf.yield
    }
)MLIR");
    variant(conditional, true,
            "both reaching writes independently retain String and Null payloads");
    variant(replaced(source, read, R"MLIR(
    %present = ctjs.truthy %entryKey
    scf.if %present {
      %null = ctjs.constant #ctjs.null
      %right = ctjs.call %setter(%state, %entryKey, %null)
      scf.yield
    }
)MLIR" + read.str()),
            true, "a no-else nullable overwrite joins with the incoming payload");
    const auto undefined = replaced(source, write, R"MLIR(
    %present = ctjs.truthy %entryKey
    %payload = scf.if %present -> (!ctjs.value) {
      scf.yield %entryKey : !ctjs.value
    } else {
      %missing = ctjs.constant #ctjs.undefined
      scf.yield %missing : !ctjs.value
    }
    %written = ctjs.call %setter(%state, %entryKey, %payload)
)MLIR");
    variant(undefined, true, "a conditional payload read preserves String and Undefined",
            Alternatives::String | Alternatives::Undefined);
    constexpr llvm::StringLiteral alias = R"MLIR(
    %aliasKey = ctjs.constant #ctjs.string<"possible">
    %aliasValue = ctjs.constant #ctjs.null
    %aliased = ctjs.call %setter(%state, %aliasKey, %aliasValue)
)MLIR";
    const auto joined = replaced(source, read, alias.str() + read.str());
    variant(joined, true, "a possibly aliasing Null write retains the finite nullable payload set");
    variant(replaced(joined, "%aliasValue = ctjs.constant #ctjs.null",
                     "%aliasValue = ctjs.constant #ctjs.boolean<true>"),
            false, "a possible truthy Boolean alias cannot borrow the nullable consumer signature");
    variant(replaced(joined, "%aliasValue = ctjs.constant #ctjs.null",
                     "%aliasValue = ctjs.load_global \"unknown\""),
            false, "a possible unknown write cannot be repaired by prior finite payload evidence");
    constexpr llvm::StringLiteral erase = R"MLIR(
    %deleteKey = ctjs.constant #ctjs.string<"delete">
    %deleter = ctjs.get_property %state[%deleteKey]
    %erased = ctjs.call %deleter(%state, %entryKey)
)MLIR";
    const auto saved = replaced(source, returned, R"MLIR(
    %changed = ctjs.constant #ctjs.boolean<true>
    %overwrite = ctjs.call %setter(%state, %entryKey, %changed)
)MLIR" + erase.str() + returned.str());
    variant(saved, true, "saved nullable SSA read evidence survives exact overwrite and deletion");
    variant(
        replaced(source, read, erase.str() + read.str()), true,
        "exact deletion replaces the nullable payload result with independently proved Undefined",
        Alternatives::Undefined);
    variant(replaced(source, write, ""), false,
            "a captured Map starts with unknown contents at each invocation");
    variant(replaced(source, read,
                     "    %missingKey = ctjs.constant #ctjs.string<\"missing\">\n"
                     "    %loaded = ctjs.call %reader(%state, %missingKey)"),
            false, "a different key cannot inherit the nullable entry's definite presence");
    variant(replaced(source, write, "    %written = ctjs.call %setter(%state, %entryKey, %this)"),
            false, "an unproved payload cannot inherit the nullable input's alternatives");
    variant(replaced(conditional, "      %right = ctjs.call %setter(%state, %entryKey, %null)", ""),
            false, "one reaching write cannot prove membership on both arms");
    variant(replaced(conditional, "%null = ctjs.constant #ctjs.null",
                     "%null = ctjs.constant #ctjs.boolean<true>"),
            false, "a truthy mixed branch remains outside the nullable consumer parameter family");
    variant(replaced(saved, returned,
                     "    %fresh = ctjs.call %reader(%state, %entryKey)\n"
                     "    ctjs.return %fresh"),
            true, "a fresh deleted read proves Undefined independently of the saved nullable value",
            Alternatives::Undefined);
    variant(replaced(source, read, "    %loaded = ctjs.call %reader(%this, %entryKey)"), false,
            "a different receiver cannot inherit captured Map payload evidence");
    check(rows == 16, "all nullable payload propagation and refusal rows ran");
    for (const auto & [program, label] :
         {std::pair{source, "nullable payload"}, std::pair{conditional, "nullable payload join"}}) {
        auto module = mlir::parseSourceString<mlir::ModuleOp>(program, &context);
        check(static_cast<bool>(module), "nullable payload budget fixture parses");
        if (!module) { continue; }
        auto contract = requested(*module);
        HostContractAnalysis complete(*module, contract);
        const unsigned completion = complete.steps();
        check(complete.proved() && completion < 20000,
              "the nullable payload dependency proof stays within its fixture work limit");
        if (!complete.proved() || completion >= 20000) { continue; }
        for (unsigned budget = 0; budget < completion; ++budget) {
            HostContractAnalysis limited(*module, contract, budget);
            check(
                !limited.proved() && limited.exhausted() && limited.steps() <= budget &&
                    withheld(*module, limited),
                "every incomplete payload budget withholds the whole callable and property proof");
        }
        HostContractAnalysis exact(*module, contract, completion);
        check(exact.proved() && exact.steps() == completion,
              "the exact payload budget reproduces all dependency edges");
        if (exact.proved()) { edges(*module, exact, nullableMask); }
        auto setter = module->lookupSymbol<ctjs::FuncOp>("put$3");
        auto result = llvm::cast<ctjs::ReturnOp>(setter.getBody().front().getTerminator());
        auto lookup = result.getValue().getDefiningOp<ctjs::CallOp>();
        check(static_cast<bool>(lookup), "the nullable payload producer retains its source read");
        if (!lookup) { continue; }
        mlir::Builder builder(&context);
        module->walk([&](mlir::Operation * operation) {
            if (!llvm::isa<ctjs::CallOp, ctjs::CallDirectOp>(operation)) { return; }
            operation->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
            operation->setAttr("ctnative.map_present", builder.getBoolAttr(true));
            operation->setAttr("ctnative.map_read_type", builder.getStringAttr("Opt<Str>"));
        });
        contract = requested(*module);
        check(HostContractAnalysis(*module, contract).proved(),
              "forged nullable reports leave the live proof independently reproducible");
        const auto mutation = [&](mlir::Operation * operation, unsigned operand,
                                  mlir::Value replacement) {
            const auto original = operation->getOperand(operand);
            operation->setOperand(operand, replacement);
            HostContractAnalysis stale(*module, contract);
            check(!stale.proved() && stale.reason().contains("fingerprint") &&
                      withheld(*module, stale),
                  "a live payload mutation invalidates its earlier host fingerprint");
            HostContractAnalysis fresh(*module, requested(*module));
            check(!fresh.proved() && !fresh.exhausted() && withheld(*module, fresh),
                  "fresh forged reports cannot recover unknown nullable payload evidence");
            operation->setOperand(operand, original);
            check(HostContractAnalysis(*module, contract).proved(),
                  "restoring the nullable payload edge restores the complete proof");
        };
        const auto unknown = setter.getBody().front().getArgument(0);
        mutation(lookup, 2, unknown);
        mutation(result, 0, lookup.getReceiver());
        setter.walk([&](ctjs::CallOp call) {
            if (call.getArgs().size() == 2) { mutation(call, 3, unknown); }
        });
        std::printf("%s host %s: %u rows and all %u incomplete budgets checked\n", label,
                    prepared ? "prepared" : "source", rows, completion);
    }
}

} // namespace ctcompile::test::host_contract_seeded_maps
