#pragma once

#include "HostContractFixtures.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"

namespace ctcompile::test::host_contract {

inline void checkDOMIteration(mlir::MLIRContext & context) {
    using namespace ctcompile::ctnative;
    context.getOrLoadDialect<mlir::arith::ArithDialect>();
    context.getOrLoadDialect<mlir::scf::SCFDialect>();
    context.getOrLoadDialect<mlir::ub::UBDialect>();
    const std::string prefix = R"MLIR(
module {
  ctjs.func @iterate$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %Object = ctjs.load_global "Object"
    %keysName = ctjs.constant #ctjs.string<"keys">
    %keysMethod = ctjs.get_property %Object[%keysName]
    %datasetName = ctjs.constant #ctjs.string<"dataset">
    %dataset = ctjs.get_property %element[%datasetName]
    %keys = ctjs.call %keysMethod(%Object, %dataset)
    %lengthName = ctjs.constant #ctjs.string<"length">
    %length = ctjs.get_property %keys[%lengthName]
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
)MLIR";
    const std::string suffix = R"MLIR(
    ctjs.return %loop#2
  }
}
)MLIR";
    const std::string source = prefix + R"MLIR(
    %loop:3 = scf.while (%count = %zero, %index = %zero) : (!ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value, !ctjs.value) {
      %less = ctjs.compare lt %index, %length
      %test = ctjs.truthy %less
      %selected:4 = scf.if %test -> (i1, !ctjs.value, !ctjs.value, !ctjs.value) {
        %key = ctjs.get_property %keys[%index]
        %nextCount = ctjs.binary add %count, %one
        %nextIndex = ctjs.binary_static add %index, %one
        %yes = arith.constant true
        scf.yield %yes, %nextCount, %nextIndex, %count : i1, !ctjs.value, !ctjs.value, !ctjs.value
      } else {
        %no = arith.constant false
        scf.yield %no, %count, %index, %count : i1, !ctjs.value, !ctjs.value, !ctjs.value
      }
      scf.condition(%selected#0) %selected#1, %selected#2, %selected#3 : !ctjs.value, !ctjs.value, !ctjs.value
    } do {
    ^bb0(%count: !ctjs.value, %index: !ctjs.value, %answer: !ctjs.value):
      scf.yield %count, %index : !ctjs.value, !ctjs.value
    }
)MLIR" + suffix;
    auto module = mlir::parseSourceString<mlir::ModuleOp>(source, &context);
    check(static_cast<bool>(module), "independent prepared dataset loop fixture parses");
    if (!module) { return; }
    HostContract contract;
    contract.entry = "iterate$0";
    contract.elementParameters = {0};
    contract.datasetParameters = {0};
    contract.initialIntrinsics = {"Object", "Array"};
    contract.moduleSha256 = hostContractFingerprint(*module);
    const auto noEvidence = [](mlir::ModuleOp input, const DOMEntryAnalysis & proof) {
        bool empty = !proof.proved() && !proof.entry() && !proof.wrapper() &&
                     proof.parameters().empty() && proof.callbacks().empty() &&
                     proof.stringResults().empty() && proof.optionalStringJoins().empty() &&
                     proof.stringRefinements().empty();
        input.walk([&](ctjs::CallOp call) { empty &= !proof.call(call); });
        input.walk([&](ctjs::LoadGlobalOp load) { empty &= !proof.isInitialIntrinsic(load); });
        input.walk([&](ctjs::SetPropertyOp write) {
            empty &= !proof.jsonAssignment(write) && !proof.jsonSnapshotAssignment(write);
        });
        input.walk([&](ctjs::GetPropertyOp read) {
            empty &= !proof.method(read) && !proof.isDataset(read.getResult()) &&
                     !proof.isStringVectorLength(read) && !proof.isStringVectorIndex(read) &&
                     !proof.isElementVectorLength(read) && !proof.isElementVectorIndex(read) &&
                     !proof.datasetValueElement(read);
        });
        return empty;
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        contract.provider = provider;
        DOMEntryAnalysis proof(*module, contract);
        check(proof.proved(), "scalar loop proves exact guarded snapshot extraction");
        if (!proof.proved()) {
            std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
            continue;
        }
        unsigned indices = 0;
        module->walk([&](ctjs::GetPropertyOp read) { indices += proof.isStringVectorIndex(read); });
        check(indices == 1, "only the integral guarded vector read receives index evidence");
        check(DOMEntryAnalysis(*module, contract, proof.steps()).proved(),
              "loop proof reproduces its exact completion budget");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*module, contract, budget);
            check(limited.exhausted() && noEvidence(*module, limited),
                  "every incomplete loop proof withholds all index and host evidence");
        }
    }
    for (const auto & invalid :
         {replaced(source, "%index = %zero", "%index = %one"),
          replaced(source, "ctjs.binary_static add %index, %one",
                   "ctjs.binary_static add %index, %zero"),
          replaced(source, "ctjs.compare lt %index, %length", "ctjs.compare le %index, %length"),
          replaced(source, "%key = ctjs.get_property %keys[%index]",
                   "%key = ctjs.get_property %keys[%count]"),
          replaced(source, "ctjs.compare lt %index, %length", "ctjs.compare lt %index, %one"),
          replaced(replaced(source, "        %key = ctjs.get_property %keys[%index]\n", ""),
                   "      %less =", "      %key = ctjs.get_property %keys[%index]\n      %less ="),
          replaced(
              replaced(source, "        %key = ctjs.get_property %keys[%index]\n", ""),
              "        %no =", "        %key = ctjs.get_property %keys[%index]\n        %no ="),
          replaced(source, "%length = ctjs.get_property %keys[%lengthName]",
                   "%other = ctjs.call %keysMethod(%Object, %dataset)\n"
                   "    %length = ctjs.get_property %other[%lengthName]"),
          replaced(source, "scf.yield %count, %index : !ctjs.value, !ctjs.value",
                   "scf.yield %index, %count : !ctjs.value, !ctjs.value")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsupported induction witness parses");
        if (!input) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis refused(*input, request);
        check(noEvidence(*input, refused),
              "nonzero starts, wrong latches, wrong indices and inexact bounds withhold evidence");
    }

    const std::string queryPrefix = R"MLIR(
module {
  ctjs.func @iterate$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %element: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %queryName = ctjs.constant #ctjs.string<"querySelectorAll">
    %query = ctjs.get_property %element[%queryName]
    %selector = ctjs.constant #ctjs.string<".selected">
    %keys = ctjs.call %query(%element, %selector)
    %lengthName = ctjs.constant #ctjs.string<"length">
    %length = ctjs.get_property %keys[%lengthName]
    %zero = ctjs.constant #ctjs.number<0>
    %one = ctjs.constant #ctjs.number<4607182418800017408>
)MLIR";
    const auto queried = replaced(source, prefix, queryPrefix);
    const auto ordinary = queryPrefix + R"MLIR(
    %loop:2 = scf.while (%count = %zero, %index = %zero) : (!ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value) {
      %less = ctjs.compare lt %index, %length
      %test = ctjs.truthy %less
      scf.condition(%test) %index, %count : !ctjs.value, !ctjs.value
    } do {
    ^bb0(%index: !ctjs.value, %count: !ctjs.value):
        %key = ctjs.get_property %keys[%index]
        %nextCount = ctjs.binary add %count, %one
        %nextIndex = ctjs.binary_static add %index, %one
        scf.yield %nextCount, %nextIndex : !ctjs.value, !ctjs.value
    }
    ctjs.return %loop#1
  }
}
)MLIR";
    const auto withMutation = [](const std::string & input) {
        return replaced(input, "        %nextCount =", R"MLIR(
        %setterName = ctjs.constant #ctjs.string<"setAttribute">
        %setter = ctjs.get_property %key[%setterName]
        %attribute = ctjs.constant #ctjs.string<"class">
        %text = ctjs.constant #ctjs.string<"visited">
        %written = ctjs.call %setter(%key, %attribute, %text)
        %nextCount =)MLIR");
    };
    const auto mutated = withMutation(queried);
    const auto ordinaryMutated = withMutation(ordinary);
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        auto request = contract;
        request.provider = provider;
        request.initialIntrinsics.clear();
        request.datasetParameters.clear();
        for (const auto & candidate : {queried, mutated, ordinary, ordinaryMutated}) {
            auto input = mlir::parseSourceString<mlir::ModuleOp>(candidate, &context);
            check(static_cast<bool>(input), "independent element snapshot loop parses");
            if (!input) { continue; }
            request.moduleSha256 = hostContractFingerprint(*input);
            DOMEntryAnalysis proof(*input, request);
            check(proof.proved(),
                  "guarded element snapshots support read and attribute-write loops");
            if (!proof.proved()) {
                std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
                continue;
            }
            unsigned lengths = 0, indices = 0, queries = 0;
            input->walk([&](ctjs::GetPropertyOp read) {
                lengths += proof.isElementVectorLength(read);
                indices += proof.isElementVectorIndex(read);
                if (proof.isElementVectorIndex(read)) {
                    check(proof.isElement(read.getResult()) &&
                              proof.isElementIdentity(read.getResult()),
                          "guarded snapshot member is a nonnull borrowed element");
                }
            });
            input->walk([&](ctjs::CallOp call) {
                const auto * edge = proof.call(call);
                if (edge && edge->kind == HostDOMMethod::querySelectorAll) {
                    ++queries;
                    check(edge->returnsElementVector() && !edge->returnsBoolean() &&
                              edge->usesStyle(),
                          "query collection has a distinct typed result and Style requirement");
                }
            });
            check(lengths == 1 && indices == 1 && queries == 1,
                  "snapshot evidence names only the proved length, index and selector call");
            check(DOMEntryAnalysis(*input, request, proof.steps()).proved(),
                  "element snapshot proof reproduces its exact work budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*input, request, budget);
                check(limited.exhausted() && noEvidence(*input, limited),
                      "incomplete element snapshots publish no borrowed handle evidence");
            }
            if (candidate == mutated || candidate == ordinaryMutated) {
                auto datasetRequest = request;
                datasetRequest.datasetParameters = {0};
                DOMEntryAnalysis refused(*input, datasetRequest);
                check(noEvidence(*input, refused) &&
                          refused.reason().contains("backedge dataset-alias proof"),
                      "dataset-enabled loops still require a backedge alias proof");
            }
        }
        for (const auto & invalid :
             {replaced(queried, "ctjs.compare lt %index, %length", "ctjs.compare lt %index, %one"),
              replaced(queried, "%key = ctjs.get_property %keys[%index]",
                       "%key = ctjs.get_property %keys[%one]"),
              replaced(queried, "ctjs.return %loop#2", "ctjs.return %keys"),
              replaced(queried, "ctjs.return %loop#2",
                       "ctjs.set_property %keys[%lengthName], %zero\n    ctjs.return %loop#2"),
              replaced(ordinary, "%index = %zero", "%index = %one"),
              replaced(ordinary, "ctjs.binary_static add %index, %one",
                       "ctjs.binary_static add %index, %zero"),
              replaced(ordinary, "scf.condition(%test) %index, %count",
                       "scf.condition(%test) %count, %index"),
              replaced(ordinary, "ctjs.compare lt %index, %length",
                       "ctjs.compare le %index, %length"),
              replaced(replaced(ordinary, "        %key = ctjs.get_property %keys[%index]\n", ""),
                       "      %less =",
                       "      %key = ctjs.get_property %keys[%index]\n      %less =")}) {
            auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
            check(static_cast<bool>(input), "invalid element snapshot witness parses");
            if (!input) { continue; }
            request.moduleSha256 = hostContractFingerprint(*input);
            DOMEntryAnalysis refused(*input, request);
            check(noEvidence(*input, refused),
                  "unsafe indices, snapshot escape and writes remain refused");
        }
    }

    // Independent importer-shaped element iteration: the proxy's eager copy
    // is bounded, while a separate observation of the NodeList stays whole.
    const auto iterated = replaced(replaced(replaced(queried, "ctjs.compare lt %index, %length",
                                                     "ctjs.compare lt %index, %materialized#1"),
                                            "%key = ctjs.get_property %keys[%index]",
                                            "%key = ctjs.get_property %materialized#0[%index]"),
                                   "    %length = ctjs.get_property %keys[%lengthName]", R"MLIR(
    %aliasLength = ctjs.get_property %keys[%lengthName]
    %open = ctjs.load_global "__ctbrowser_for_of_open"
    %undefined = ctjs.constant #ctjs.undefined
    %record = ctjs.call %open(%undefined, %keys)
    %fast = ctjs.compare strict_eq %record, %undefined
    %testFast = ctjs.truthy %fast
    %materialized:2 = scf.if %testFast -> (!ctjs.value, !ctjs.value) {
      %iterable = ctjs.iterable of %keys
      %length = ctjs.get_property %iterable[%lengthName]
      scf.yield %iterable, %length : !ctjs.value, !ctjs.value
    } else {
      scf.yield %keys, %aliasLength : !ctjs.value, !ctjs.value
    })MLIR");
    const auto iterationRequest = [&](mlir::ModuleOp input) {
        auto request = contract;
        request.datasetParameters.clear();
        request.initialIntrinsics = {"Array", "Element", "__ctbrowser_for_of_open",
                                     "__ctbrowser_iter_next", "__ctbrowser_iter_close"};
        request.moduleSha256 = hostContractFingerprint(input);
        return request;
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(iterated, &context);
        check(static_cast<bool>(input), "element for-of normalization fixture parses");
        if (!input) { continue; }
        auto request = iterationRequest(*input);
        request.provider = provider;
        if (auto failure = normalizeDOMIteration(*input, request, 100000)) {
            check(false, "element for-of normalizes through the complete snapshot proof");
            std::fprintf(stderr, "%s\n", llvm::toString(std::move(failure)).c_str());
            continue;
        }
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, request);
        check(proof.proved(), "normalized element iteration reproves without iterator objects");
        unsigned lengths = 0, indices = 0, caps = 0, iterables = 0;
        input->walk([&](ctjs::GetPropertyOp read) {
            lengths += proof.isElementVectorLength(read);
            indices += proof.isElementVectorIndex(read);
        });
        input->walk([&](ctjs::ConstantOp constant) {
            auto number = llvm::dyn_cast<ctjs::NumberAttr>(constant.getValue());
            caps += number && number.getDouble() == double(1U << 24);
        });
        input->walk([&](ctjs::IterableOp) { ++iterables; });
        check(lengths == 2 && indices == 1 && caps == 1 && iterables == 0,
              "only iteration is capped; the original snapshot alias remains whole");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*input, request, budget);
            check(limited.exhausted() && noEvidence(*input, limited),
                  "incomplete normalized iteration exposes no element or index evidence");
        }
    }
    const auto bodyBegin = iterated.find("    %queryName =");
    const auto bodyEnd = iterated.find("    ctjs.return %loop#2");
    const auto discardedInput =
        replaced(iterated, "    %queryName =",
                 "    %discard = arith.constant false\n    scf.if %discard {\n" +
                     iterated.substr(bodyBegin, bodyEnd - bodyBegin) +
                     "      scf.yield\n    } else {\n      scf.yield\n    }\n    %queryName =");
    for (const auto & [invalid, diagnostic] : {
             std::pair{replaced(iterated, "ctjs.compare lt %index, %materialized#1",
                                "ctjs.compare lt %index, %aliasLength"),
                       "own length guard"},
             std::pair{replaced(iterated, "ctjs.get_property %materialized#0[%index]",
                                "ctjs.get_property %materialized#0[%one]"),
                       "own length guard"},
             std::pair{replaced(iterated, "ctjs.return %loop#2", "ctjs.return %materialized#0"),
                       "escapes its observations"},
             std::pair{replaced(iterated, "ctjs.compare strict_eq %record, %undefined",
                                "ctjs.compare strict_eq %undefined, %undefined"),
                       "exact source materialization arm"},
             std::pair{discardedInput, "would discard a recorded input"},
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsafe element materialization fixture parses");
        if (!input) { continue; }
        auto request = iterationRequest(*input);
        const auto reason = llvm::toString(normalizeDOMIteration(*input, request, 100000));
        check(reason.find(diagnostic) != std::string::npos,
              "unsafe materialization bounds, uses and discarded identities refuse");
        request.moduleSha256 = hostContractFingerprint(*input);
        check(noEvidence(*input, DOMEntryAnalysis(*input, request)),
              "refused private materialization publishes no borrowed evidence");
    }
    auto staleIteration = mlir::parseSourceString<mlir::ModuleOp>(iterated, &context);
    check(static_cast<bool>(staleIteration), "materialization fingerprint fixture parses");
    if (staleIteration) {
        auto request = iterationRequest(*staleIteration);
        mlir::Value aliasLength;
        staleIteration->walk([&](ctjs::GetPropertyOp read) {
            if (ctjs::constantKey(read.getKey()) == "length" &&
                read.getObject().getDefiningOp<ctjs::CallOp>()) {
                aliasLength = read.getResult();
            }
        });
        staleIteration->walk([&](ctjs::CompareOp compare) {
            if (compare.getKind() == ctjs::CompareKind::Lt) {
                compare.getRhsMutable().assign(aliasLength);
                compare->setAttr("ctnative.host_element_vector_index",
                                 mlir::BoolAttr::get(&context, true));
            }
        });
        const auto changed = hostContractFingerprint(*staleIteration);
        for (bool fresh : {false, true}) {
            if (fresh) { request.moduleSha256 = changed; }
            const auto reason =
                llvm::toString(normalizeDOMIteration(*staleIteration, request, 100000));
            check(reason.find(fresh ? "own length" : "fingerprint") != std::string::npos,
                  "stale fingerprints and forged reports cannot authorize uncapped copy bounds");
            auto reproof = request;
            reproof.moduleSha256 = hostContractFingerprint(*staleIteration);
            check(noEvidence(*staleIteration, DOMEntryAnalysis(*staleIteration, reproof)),
                  "failed private materialization reproof withholds all evidence");
        }
    }

    const std::string memberRead = "%value = ctjs.get_property %dataset[%key]";
    const std::string valueSource = replaced(
        source, "        %nextCount =", "        " + memberRead + "\n        %nextCount =");
    const std::string assignment = "ctjs.set_property %target[%key], %value";
    const std::string assigned = replaced(
        replaced(replaced(valueSource,
                          "    %loop:3 =", "    %target = ctjs.create_object\n    %loop:3 ="),
                 memberRead, memberRead + "\n        " + assignment),
        "ctjs.return %loop#2", "ctjs.return %target");
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(assigned, &context);
        check(static_cast<bool>(input), "single snapshot assignment fixture parses");
        if (!input) { continue; }
        auto request = contract;
        request.provider = provider;
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, request);
        check(proof.proved(), "one direct snapshot traversal proves each result key at most once");
        if (!proof.proved()) {
            std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
            continue;
        }
        unsigned writes = 0;
        input->walk([&](ctjs::SetPropertyOp write) {
            ++writes;
            check(proof.jsonAssignment(write) && proof.jsonSnapshotAssignment(write),
                  "only the sole direct-key write receives snapshot assignment evidence");
        });
        check(writes == 1 && DOMEntryAnalysis(*input, request, proof.steps()).proved(),
              "snapshot assignment reproduces its complete charged proof");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*input, request, budget);
            check(limited.exhausted() && noEvidence(*input, limited),
                  "incomplete snapshot assignment publishes no partial writer evidence");
        }
        check(hostContractFingerprint(*input) == request.moduleSha256,
              "snapshot proof and budget cutoffs retain the original source");
    }
    for (const auto & invalid : {
             replaced(assigned, assignment, assignment + "\n        " + assignment),
             replaced(assigned, "    %loop:3 =",
                      "    ctjs.set_property %target[%keysName], %keysName\n    %loop:3 ="),
             replaced(assigned, "ctjs.return %target",
                      "ctjs.set_property %target[%keysName], %keysName\n    ctjs.return %target"),
             replaced(assigned, "    %loop:3 =",
                      "    %seed = ctjs.create_object\n"
                      "    ctjs.copy_props %seed into %target\n    %loop:3 ="),
             replaced(assigned, assignment,
                      "%changed = ctjs.binary add %key, %keysName\n"
                      "        ctjs.set_property %target[%changed], %value"),
             replaced(assigned, assignment,
                      "%proto = ctjs.constant #ctjs.string<\"__proto__\">\n"
                      "        ctjs.set_property %target[%proto], %value"),
             replaced(assigned, assignment,
                      assignment + "\n        %saved = ctjs.create_object\n"
                                   "        ctjs.copy_props %target into %saved"),
             replaced(replaced(replaced(assigned, "    %target = ctjs.create_object\n", ""),
                               assignment, "%target = ctjs.create_object\n        " + assignment),
                      "ctjs.return %target", "ctjs.return %loop#2"),
             replaced(assigned, assignment, R"MLIR(
        scf.while : () -> () {
          ctjs.set_property %target[%key], %value
          %stop = arith.constant false
          scf.condition(%stop)
        } do {
          scf.yield
        })MLIR"),
             replaced(
                 replaced(replaced(assigned,
                                   "    %keys = ctjs.call %keysMethod(%Object, %dataset)\n", ""),
                          "    %length = ctjs.get_property %keys[%lengthName]\n", ""),
                 "      %less =",
                 "      %keys = ctjs.call %keysMethod(%Object, %dataset)\n"
                 "      %length = ctjs.get_property %keys[%lengthName]\n"
                 "      %less ="),
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unproved snapshot assignment fixture parses");
        if (!input) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        DOMEntryAnalysis proof(*input, request);
        check(noEvidence(*input, proof),
              "repeated, transformed, seeded or intermediately observed writes refuse");
        check(hostContractFingerprint(*input) == request.moduleSha256,
              "refused snapshot assignment keeps the full source");
    }
    const std::string filteredKeys = R"MLIR(
    %u = ctjs.constant #ctjs.undefined
    %rawKeys = ctjs.call %keysMethod(%Object, %dataset)
    %filterName = ctjs.constant #ctjs.string<"filter">
    %filter = ctjs.get_property %rawKeys[%filterName]
    %callback = ctjs.create_closure %callee[1] this %u
    %keys = ctjs.call %filter(%rawKeys, %callback)
)MLIR";
    const std::string strippedKey = R"MLIR(
        %factory = ctjs.load_global "__ctbrowser_regexp"
        %pattern = ctjs.constant #ctjs.string<"^bs">
        %empty = ctjs.constant #ctjs.string<"">
        %regexp = ctjs.call %factory(%u, %pattern, %empty)
        %replaceName = ctjs.constant #ctjs.string<"replace">
        %replace = ctjs.get_property %key[%replaceName]
        %stripped = ctjs.call %replace(%key, %regexp, %empty)
        ctjs.set_property %target[%stripped], %value
)MLIR";
    const std::string predicate = R"MLIR(
  ctjs.func private @predicate$1(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value, %key: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %startsName = ctjs.constant #ctjs.string<"startsWith">
    %starts = ctjs.get_property %key[%startsName]
    %prefix = ctjs.constant #ctjs.string<"bs">
    %first = ctjs.call %starts(%key, %prefix)
    %condition = ctjs.truthy %first
    %selected = scf.if %condition -> (!ctjs.value) {
      %again = ctjs.get_property %key[%startsName]
      %excluded = ctjs.constant #ctjs.string<"bsConfig">
      %second = ctjs.call %again(%key, %excluded)
      %not = ctjs.unary not %second
      scf.yield %not : !ctjs.value
    } else {
      scf.yield %first : !ctjs.value
    }
    ctjs.return %selected
  }
)MLIR";
    const std::string stripped =
        replaced(replaced(replaced(assigned, "%keys = ctjs.call %keysMethod(%Object, %dataset)",
                                   filteredKeys),
                          assignment, strippedKey),
                 "\n}\n", predicate + "\n}\n");
    const auto prefixRequest = [&](mlir::ModuleOp input) {
        auto request = contract;
        request.initialIntrinsics = {"Object", "Array", "String", "RegExp", "__ctbrowser_regexp"};
        request.moduleSha256 = hostContractFingerprint(input);
        return request;
    };
    for (auto provider :
         {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(stripped, &context);
        check(static_cast<bool>(input), "filtered prefix assignment fixture parses");
        if (!input) { continue; }
        auto request = prefixRequest(*input);
        request.provider = provider;
        DOMEntryAnalysis proof(*input, request);
        check(proof.proved(), "original filter makes anchored prefix removal injective");
        if (!proof.proved()) {
            std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
            continue;
        }
        input->walk([&](ctjs::SetPropertyOp write) {
            check(proof.jsonSnapshotAssignment(write),
                  "prefix-stripped assignment retains the single prototype-write proof");
        });
        check(DOMEntryAnalysis(*input, request, proof.steps()).proved(),
              "prefix assignment reproduces its complete charged proof");
        for (unsigned budget = 0; budget < proof.steps(); ++budget) {
            DOMEntryAnalysis limited(*input, request, budget);
            check(limited.exhausted() && noEvidence(*input, limited),
                  "partial filter implications grant no snapshot assignment evidence");
        }
        check(hostContractFingerprint(*input) == request.moduleSha256,
              "prefix assignment proof preserves the original source");
    }
    for (const auto & invalid : {
             replaced(stripped, "#ctjs.string<\"bs\">", "#ctjs.string<\"b\">"),
             replaced(stripped, "scf.yield %first : !ctjs.value",
                      "%yes = ctjs.constant #ctjs.boolean<true>\n"
                      "      scf.yield %yes : !ctjs.value"),
             replaced(stripped, "ctjs.return %selected",
                      "%opposite = ctjs.unary not %first\n    ctjs.return %opposite"),
             replaced(stripped, "ctjs.set_property %target[%stripped], %value",
                      "%wrong = ctjs.get_property %dataset[%stripped]\n"
                      "        ctjs.set_property %target[%stripped], %wrong"),
             replaced(stripped, "ctjs.set_property %target[%stripped], %value",
                      "ctjs.set_property %target[%stripped], %value\n"
                      "        ctjs.set_property %target[%key], %value"),
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unproved prefix assignment fixture parses");
        if (!input) { continue; }
        auto request = prefixRequest(*input);
        DOMEntryAnalysis proof(*input, request);
        check(noEvidence(*input, proof),
              "weak filters, transformed dataset reads and multiple writers refuse");
        check(hostContractFingerprint(*input) == request.moduleSha256,
              "refused prefix assignment retains the original source");
    }
    const std::string mutation = R"MLIR(
    %removeName = ctjs.constant #ctjs.string<"removeAttribute">
    %remove = ctjs.get_property %element[%removeName]
    %attribute = ctjs.constant #ctjs.string<"data-bs-z">
    %removed = ctjs.call %remove(%element, %attribute)
)MLIR";
    for (const auto & admitted :
         {valueSource,
          replaced(valueSource, memberRead,
                   "%fresh = ctjs.get_property %element[%datasetName]\n        "
                   "%value = ctjs.get_property %fresh[%key]"),
          replaced(valueSource, "    %dataset =", mutation + "    %dataset =")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(admitted, &context);
        check(static_cast<bool>(input), "independent present dataset member fixture parses");
        if (!input) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            request.provider = provider;
            DOMEntryAnalysis proof(*input, request);
            check(proof.proved(), "same-element snapshot members prove in their enumeration epoch");
            if (!proof.proved()) {
                std::fprintf(stderr, "%s\n", proof.reason().str().c_str());
                continue;
            }
            auto entry = input->lookupSymbol<ctjs::FuncOp>(request.entry);
            const auto element = entry.getBody().front().getArgument(ctjs::implicit_arguments);
            unsigned members = 0;
            input->walk([&](ctjs::GetPropertyOp read) {
                const auto owner = proof.datasetValueElement(read);
                const bool member = proof.isDataset(read.getObject());
                check(static_cast<bool>(owner) == member && (!owner || owner == element),
                      "only the present member read receives its exact element capability");
                members += static_cast<bool>(owner);
            });
            check(members == 1 && hostContractFingerprint(*input) == request.moduleSha256,
                  "member proof preserves every source operation and publishes one capability");
            check(DOMEntryAnalysis(*input, request, proof.steps()).proved(),
                  "dataset member proof reproduces its exact charged budget");
            for (unsigned budget = 0; budget < proof.steps(); ++budget) {
                DOMEntryAnalysis limited(*input, request, budget);
                check(limited.exhausted() && noEvidence(*input, limited),
                      "incomplete member proof withholds all membership and host evidence");
            }
        }
    }
    const std::string freshRead = "%fresh = ctjs.get_property %element[%datasetName]\n        "
                                  "%value = ctjs.get_property %fresh[%key]";
    const std::string otherElement =
        replaced(replaced(valueSource, "%element: !ctjs.value)",
                          "%element: !ctjs.value, %other: !ctjs.value)"),
                 memberRead,
                 "%otherData = ctjs.get_property %other[%datasetName]\n        "
                 "%value = ctjs.get_property %otherData[%key]");
    for (const auto & invalid : {
             replaced(valueSource, memberRead,
                      "%literal = ctjs.constant #ctjs.string<\"bsZ\">\n        "
                      "%value = ctjs.get_property %dataset[%literal]"),
             replaced(valueSource, memberRead,
                      "%empty = ctjs.constant #ctjs.string<\"\">\n        "
                      "%changed = ctjs.binary add %key, %empty\n        "
                      "%value = ctjs.get_property %dataset[%changed]"),
             replaced(valueSource, memberRead,
                      "%joined = scf.if %test -> (!ctjs.value) {\n"
                      "          scf.yield %key : !ctjs.value\n"
                      "        } else {\n"
                      "          scf.yield %key : !ctjs.value\n"
                      "        }\n        %value = ctjs.get_property %dataset[%joined]"),
             replaced(valueSource, "    %loop:3 =", mutation + "    %loop:3 ="),
             replaced(replaced(valueSource, memberRead, freshRead),
                      "    %loop:3 =", mutation + "    %loop:3 ="),
             replaced(valueSource, "    %keys = ctjs.call %keysMethod(%Object, %dataset)",
                      mutation + "    %fresh = ctjs.get_property %element[%datasetName]\n"
                                 "    %keys = ctjs.call %keysMethod(%Object, %fresh)"),
             replaced(valueSource, memberRead,
                      "ctjs.set_property %keys[%index], %key\n        " + memberRead),
             replaced(valueSource, memberRead, mutation + "        " + memberRead),
             otherElement,
         }) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "unsupported dataset membership fixture parses");
        if (!input) { continue; }
        auto request = contract;
        if (invalid == otherElement) {
            request.elementParameters = {0, 1};
            request.datasetParameters = {0, 1};
        }
        request.moduleSha256 = hostContractFingerprint(*input);
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            request.provider = provider;
            DOMEntryAnalysis proof(*input, request);
            check(noEvidence(*input, proof),
                  "stale, changed, joined and cross-element keys cannot prove a present value");
            check(hostContractFingerprint(*input) == request.moduleSha256,
                  "refused membership proof preserves the complete source");
        }
    }
    auto staleMember = mlir::parseSourceString<mlir::ModuleOp>(valueSource, &context);
    check(static_cast<bool>(staleMember), "dataset membership fingerprint fixture parses");
    if (staleMember) {
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*staleMember);
        mlir::Builder builder(&context);
        staleMember->walk([&](ctjs::GetPropertyOp read) {
            read->setAttr("ctnative.host_dataset_value", builder.getBoolAttr(true));
        });
        check(DOMEntryAnalysis(*staleMember, request).proved(),
              "printed member claims do not replace original provenance discovery");
        staleMember->walk([&](ctjs::GetPropertyOp read) {
            if (ctjs::constantKey(read.getKey()).empty() &&
                !llvm::isa<mlir::BlockArgument>(read.getKey())) {
                read->setOperand(1, read.getObject());
            }
        });
        DOMEntryAnalysis stale(*staleMember, request);
        check(noEvidence(*staleMember, stale) && stale.reason().contains("fingerprint"),
              "changed membership operands invalidate the original fingerprint");
        request.moduleSha256 = hostContractFingerprint(*staleMember);
        check(noEvidence(*staleMember, DOMEntryAnalysis(*staleMember, request)),
              "a fresh fingerprint and forged member reports cannot supply presence");
    }

    const std::string guarded =
        replaced(replaced(valueSource, "    %Object =", R"MLIR(
    %absent = ctjs.unary not %element
    %testElement = ctjs.truthy %absent
    %guarded = scf.if %testElement -> (!ctjs.value) {
      %empty = ctjs.create_object
      ctjs.store_global "unreachable", %empty
      scf.yield %empty : !ctjs.value
    } else {
    %Object =)MLIR"),
                 "    ctjs.return %loop#2",
                 "      scf.yield %loop#2 : !ctjs.value\n    }\n    ctjs.return %guarded");
    const std::string undefinedGuard =
        replaced(guarded, "    %absent = ctjs.unary not %element", R"MLIR(
    %undefined = ctjs.constant #ctjs.undefined
    %absent = ctjs.compare strict_eq %element, %undefined)MLIR");
    const std::string truthyGuard = replaced(replaced(valueSource, "    %Object =", R"MLIR(
    %testElement = ctjs.truthy %element
    %guarded = scf.if %testElement -> (!ctjs.value) {
    %Object =)MLIR"),
                                             "    ctjs.return %loop#2", R"MLIR(
      scf.yield %loop#2 : !ctjs.value
    } else {
      %empty = ctjs.create_object
      ctjs.store_global "unreachable", %empty
      scf.yield %empty : !ctjs.value
    }
    ctjs.return %guarded)MLIR");
    const std::string nestedDiscarded =
        replaced(guarded, "      ctjs.store_global \"unreachable\", %empty", R"MLIR(
      %innerAbsent = ctjs.unary not %element
      %innerTest = ctjs.truthy %innerAbsent
      scf.if %innerTest {
        ctjs.store_global "unreachableThen", %empty
      } else {
        ctjs.store_global "unreachableElse", %empty
      })MLIR");
    const std::string nestedSelected = replaced(replaced(guarded, "    %Object =", R"MLIR(
    %innerTest = ctjs.truthy %element
    %inner = scf.if %innerTest -> (!ctjs.value) {
    %Object =)MLIR"),
                                                "      scf.yield %loop#2 : !ctjs.value", R"MLIR(
      scf.yield %loop#2 : !ctjs.value
    } else {
      %innerEmpty = ctjs.create_object
      ctjs.store_global "unreachableInner", %innerEmpty
      scf.yield %innerEmpty : !ctjs.value
    }
      scf.yield %inner : !ctjs.value)MLIR");
    for (const auto & admitted : {guarded, undefinedGuard,
                                  replaced(undefinedGuard, "strict_eq %element, %undefined",
                                           "strict_eq %undefined, %element"),
                                  truthyGuard, nestedDiscarded, nestedSelected,
                                  replaced(truthyGuard, "    %testElement = ctjs.truthy %element",
                                           "    %once = ctjs.unary not %element\n"
                                           "    %twice = ctjs.unary not %once\n"
                                           "    %testElement = ctjs.truthy %twice")}) {
        for (auto provider :
             {HostContract::Provider::ctbrowserDOM, HostContract::Provider::ctbrowserDOMSession}) {
            auto input = mlir::parseSourceString<mlir::ModuleOp>(admitted, &context);
            check(static_cast<bool>(input), "guarded dataset value loop fixture parses");
            if (!input) { continue; }
            auto request = contract;
            request.provider = provider;
            request.moduleSha256 = hostContractFingerprint(*input);
            auto error = normalizeDOMElementGuards(*input, request, 100000);
            check(!error, "exact nonnullable inputs select either guard polarity");
            if (error) {
                std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
                continue;
            }
            request.moduleSha256 = hostContractFingerprint(*input);
            DOMEntryAnalysis proof(*input, request);
            check(proof.proved(), "selected guard arm preserves the complete value-loop proof");
            unsigned members = 0, loops = 0, writes = 0, negations = 0, branches = 0;
            input->walk([&](ctjs::GetPropertyOp read) {
                members += static_cast<bool>(proof.datasetValueElement(read));
            });
            input->walk([&](mlir::scf::WhileOp) { ++loops; });
            input->walk([&](ctjs::StoreGlobalOp) { ++writes; });
            input->walk([&](ctjs::UnaryOp) { ++negations; });
            input->walk([&](mlir::scf::IfOp) { ++branches; });
            check(members == 1 && loops == 1 && writes == 0 && negations == 0 && branches == 1,
                  "only the impossible entry arm disappears; the guarded member loop remains");
        }
    }
    for (const auto & budgetSource : {guarded, undefinedGuard}) {
        bool completedGuardBudget = false;
        for (unsigned budget = 0; budget < 4096; ++budget) {
            auto input = mlir::parseSourceString<mlir::ModuleOp>(budgetSource, &context);
            check(static_cast<bool>(input), "guard budget fixture parses");
            if (!input) { break; }
            auto request = contract;
            request.moduleSha256 = hostContractFingerprint(*input);
            auto error = normalizeDOMElementGuards(*input, request, budget);
            if (!error) {
                completedGuardBudget = true;
                request.moduleSha256 = hostContractFingerprint(*input);
                check(DOMEntryAnalysis(*input, request).proved(),
                      "the first complete guard budget preserves the live loop proof");
                break;
            }
            const auto reason = llvm::toString(std::move(error));
            check(reason.find("budget") != std::string::npos &&
                      request.moduleSha256 == hostContractFingerprint(*input),
                  "every incomplete guard budget preserves all original source operations");
        }
        check(completedGuardBudget, "the guarded loop completes within the test work limit");
    }
    for (const auto & unproved :
         {replaced(undefinedGuard, "strict_eq %element, %undefined", "strict_eq %this, %undefined"),
          replaced(undefinedGuard, "    %absent = ctjs.compare strict_eq %element, %undefined",
                   R"MLIR(
    %cell = ctjs.create_cell %element
    %saved = ctjs.cell_get %cell
    %absent = ctjs.compare strict_eq %saved, %undefined)MLIR"),
          replaced(undefinedGuard, "    %absent = ctjs.compare strict_eq %element, %undefined",
                   R"MLIR(
    %closestName = ctjs.constant #ctjs.string<"closest">
    %closest = ctjs.get_property %element[%closestName]
    %selector = ctjs.constant #ctjs.string<".missing">
    %nullable = ctjs.call %closest(%element, %selector)
    %absent = ctjs.compare strict_eq %nullable, %undefined)MLIR"),
          replaced(guarded, "ctjs.unary not %element", "ctjs.unary not %this"),
          replaced(guarded, "    %absent = ctjs.unary not %element", R"MLIR(
    %cell = ctjs.create_cell %element
    %saved = ctjs.cell_get %cell
    %absent = ctjs.unary not %saved)MLIR"),
          replaced(guarded, "    %absent = ctjs.unary not %element", R"MLIR(
    %choose = ctjs.truthy %this
    %joined = scf.if %choose -> (!ctjs.value) {
      scf.yield %element : !ctjs.value
    } else {
      scf.yield %element : !ctjs.value
    }
    %absent = ctjs.unary not %joined)MLIR"),
          replaced(guarded, "    %absent = ctjs.unary not %element", R"MLIR(
    %closestName = ctjs.constant #ctjs.string<"closest">
    %closest = ctjs.get_property %element[%closestName]
    %selector = ctjs.constant #ctjs.string<".missing">
    %nullable = ctjs.call %closest(%element, %selector)
    %absent = ctjs.unary not %nullable)MLIR"),
          replaced(guarded, "    %absent = ctjs.unary not %element", R"MLIR(
    %attributeName = ctjs.constant #ctjs.string<"getAttribute">
    %attribute = ctjs.get_property %element[%attributeName]
    %name = ctjs.constant #ctjs.string<"data-any">
    %optional = ctjs.call %attribute(%element, %name)
    %absent = ctjs.unary not %optional)MLIR"),
          replaced(guarded, "    %absent = ctjs.unary not %element", R"MLIR(
    %ordinary = ctjs.create_object
    %absent = ctjs.unary not %ordinary)MLIR")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(unproved, &context);
        check(static_cast<bool>(input), "unproved element guard fixture parses");
        if (!input) { continue; }
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*input);
        mlir::Builder builder(&context);
        input->walk([&](ctjs::UnaryOp unary) {
            unary->setAttr("ctnative.host_element", builder.getBoolAttr(true));
        });
        auto error = normalizeDOMElementGuards(*input, request, 100000);
        check(!error, "unproved truthiness remains for complete entry analysis");
        if (error) { llvm::consumeError(std::move(error)); }
        check(request.moduleSha256 == hostContractFingerprint(*input),
              "implicit, cell, joined, nullable and ordinary values receive no guard report fact");
        check(noEvidence(*input, DOMEntryAnalysis(*input, request)),
              "unproved guards publish no entry, host or dataset membership evidence");
    }
    for (const auto & parameters :
         {std::vector<unsigned>{}, std::vector<unsigned>{1}, std::vector<unsigned>{0, 0}}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(guarded, &context);
        check(static_cast<bool>(input), "invalid guard declaration fixture parses");
        if (!input) { continue; }
        auto request = contract;
        request.elementParameters = parameters;
        request.moduleSha256 = hostContractFingerprint(*input);
        auto error = normalizeDOMElementGuards(*input, request, 100000);
        check(static_cast<bool>(error), "incomplete and invalid element declarations refuse");
        if (error) { llvm::consumeError(std::move(error)); }
        check(request.moduleSha256 == hostContractFingerprint(*input),
              "invalid element declarations preserve the complete guarded source");
    }
    for (const auto & staleSource : {guarded, undefinedGuard}) {
        auto staleGuard = mlir::parseSourceString<mlir::ModuleOp>(staleSource, &context);
        check(static_cast<bool>(staleGuard), "guard fingerprint fixture parses");
        if (staleGuard) {
            auto request = contract;
            request.moduleSha256 = hostContractFingerprint(*staleGuard);
            auto entry = staleGuard->lookupSymbol<ctjs::FuncOp>(request.entry);
            entry.walk([&](ctjs::UnaryOp unary) {
                unary->setOperand(0, entry.getBody().front().getArgument(0));
            });
            entry.walk([&](ctjs::CompareOp compare) {
                if (compare.getKind() == ctjs::CompareKind::StrictEq) {
                    compare->setOperand(0, entry.getBody().front().getArgument(0));
                }
            });
            const auto fingerprint = hostContractFingerprint(*staleGuard);
            auto error = normalizeDOMElementGuards(*staleGuard, request, 100000);
            const bool refused = static_cast<bool>(error);
            const auto reason = refused ? llvm::toString(std::move(error)) : std::string{};
            check(refused && reason.find("fingerprint") != std::string::npos &&
                      fingerprint == hostContractFingerprint(*staleGuard),
                  "changed guard operands refuse without changing the source");
        }
    }
    auto liveGuard = mlir::parseSourceString<mlir::ModuleOp>(
        replaced(guarded,
                 "    %Object =", "    ctjs.store_global \"live\", %element\n    %Object ="),
        &context);
    check(static_cast<bool>(liveGuard), "live guard effect fixture parses");
    if (liveGuard) {
        auto request = contract;
        request.moduleSha256 = hostContractFingerprint(*liveGuard);
        auto error = normalizeDOMElementGuards(*liveGuard, request, 100000);
        check(!error, "an exact guard can normalize before live effects are checked");
        if (error) { llvm::consumeError(std::move(error)); }
        request.moduleSha256 = hostContractFingerprint(*liveGuard);
        check(noEvidence(*liveGuard, DOMEntryAnalysis(*liveGuard, request)),
              "complete entry proof still rejects writes in the selected guard arm");
    }

    const std::string completion = prefix + R"MLIR(
    %poison = ub.poison : !ctjs.value
    %i0 = arith.constant 0 : i32
    %i1 = arith.constant 1 : i32
    %loop:3 = scf.while (%count = %zero, %index = %zero, %unused = %poison) : (!ctjs.value, !ctjs.value, !ctjs.value) -> (!ctjs.value, !ctjs.value, !ctjs.value) {
      %less = ctjs.compare lt %index, %length
      %test = ctjs.truthy %less
      %selected:3 = scf.if %test -> (!ctjs.value, !ctjs.value, i32) {
        %key = ctjs.get_property %keys[%index]
        %nextCount = ctjs.binary add %count, %one
        %nextIndex = ctjs.binary_static add %index, %one
        scf.yield %nextCount, %nextIndex, %i1 : !ctjs.value, !ctjs.value, i32
      } else {
        scf.yield %poison, %poison, %i0 : !ctjs.value, !ctjs.value, i32
      }
      %selector = arith.index_castui %selected#2 : i32 to index
      %dispatched:3 = scf.index_switch %selector -> i32, !ctjs.value, !ctjs.value
      case 0 {
        scf.yield %i0, %selected#0, %selected#1 : i32, !ctjs.value, !ctjs.value
      }
      default {
        scf.yield %i1, %selected#0, %selected#1 : i32, !ctjs.value, !ctjs.value
      }
      %continued = arith.trunci %dispatched#0 : i32 to i1
      scf.condition(%continued) %dispatched#1, %dispatched#2, %count : !ctjs.value, !ctjs.value, !ctjs.value
    } do {
    ^bb0(%count: !ctjs.value, %index: !ctjs.value, %answer: !ctjs.value):
      scf.yield %count, %index, %answer : !ctjs.value, !ctjs.value, !ctjs.value
    }
)MLIR" + suffix;
    auto normalized = mlir::parseSourceString<mlir::ModuleOp>(completion, &context);
    check(static_cast<bool>(normalized), "source completion tuple fixture parses");
    if (!normalized) { return; }
    auto error = expandDOMHelpers(*normalized, contract.entry, 100000);
    check(!error, "completion normalizer preserves the loop condition and terminal tuple");
    if (error) {
        std::fprintf(stderr, "%s\n", llvm::toString(std::move(error)).c_str());
        return;
    }
    unsigned loops = 0, reads = 0, inactive = 0, dispatches = 0;
    normalized->walk([&](mlir::scf::WhileOp loop) {
        ++loops;
        check(loop.getInits().size() == 2 && loop.getNumResults() == 3,
              "unused entry slot is removed without losing the final count");
    });
    normalized->walk(
        [&](ctjs::GetPropertyOp read) { reads += llvm::isa<mlir::BlockArgument>(read.getKey()); });
    normalized->walk([&](mlir::ub::PoisonOp) { ++inactive; });
    normalized->walk([&](mlir::scf::IndexSwitchOp) { ++dispatches; });
    check(loops == 1 && reads == 1 && inactive == 0 && dispatches == 0,
          "normalization preserves source reads and removes only inactive completion machinery");
    contract.moduleSha256 = hostContractFingerprint(*normalized);
    check(DOMEntryAnalysis(*normalized, contract).proved(),
          "normalized original completion loop receives the same complete entry proof");
    for (unsigned budget : {0U, 64U}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(completion, &context);
        const auto fingerprint = hostContractFingerprint(*input);
        auto limited = expandDOMHelpers(*input, contract.entry, budget);
        const bool refused = static_cast<bool>(limited);
        const auto reason = refused ? llvm::toString(std::move(limited)) : std::string{};
        check(refused && reason.find("budget") != std::string::npos &&
                  fingerprint == hostContractFingerprint(*input),
              "completion budget cutoffs preserve the original source body");
    }
    for (const auto & invalid :
         {replaced(completion, "%count = %zero", "%count = %poison"),
          replaced(completion, "ctjs.return %loop#2", "ctjs.return %loop#0"),
          replaced(completion, "scf.yield %nextCount, %nextIndex, %i1",
                   "scf.yield %poison, %nextIndex, %i1"),
          replaced(completion, "scf.condition(%continued)", "scf.condition(%test)")}) {
        auto input = mlir::parseSourceString<mlir::ModuleOp>(invalid, &context);
        check(static_cast<bool>(input), "observable inactive-slot witness parses");
        if (!input) { continue; }
        const auto fingerprint = hostContractFingerprint(*input);
        auto rejected = expandDOMHelpers(*input, contract.entry, 100000);
        const bool refused = static_cast<bool>(rejected);
        const auto reason = refused ? llvm::toString(std::move(rejected)) : std::string{};
        check(refused && reason.find("inactive value") != std::string::npos &&
                  fingerprint == hostContractFingerprint(*input),
              "live or uncorrelated poison refuses without replacing the source body");
    }
}

} // namespace ctcompile::test::host_contract
