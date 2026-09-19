#include "Body.hpp"

namespace ctcompile::ctnative::dom_entry_detail {

std::optional<bool> Body::browserOperation(mlir::Operation & operation) {
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
        // The DOM provider fixes this initial binding. The complete
        // source census admits no replacement or script reentry.
        if (load.getName() == "undefined") {
            values[load.getResult()] = Kind::undefined;
            return true;
        }
        if (suppliedString && suppliedRegExp && load.getName() == "__ctbrowser_regexp") {
            values[load.getResult()] = Kind::regexpFactory;
            provedRegExpIntrinsics.push_back(load);
            return true;
        }
        if (suppliedURI && load.getName() == "decodeURIComponent") {
            values[load.getResult()] = Kind::uriIntrinsic;
            provedURIIntrinsics.push_back(load);
            return true;
        }
        if (suppliedNumber && load.getName() == "Number") {
            values[load.getResult()] = Kind::numberIntrinsic;
            provedNumberIntrinsics.push_back(load);
            return true;
        }
        if (suppliedObject && load.getName() == "Object") {
            values[load.getResult()] = Kind::objectIntrinsic;
            provedObjectIntrinsics.push_back(load);
            return true;
        }
        if (suppliedJSON && load.getName() == "JSON") {
            values[load.getResult()] = Kind::jsonIntrinsic;
            provedJSONIntrinsics.push_back(load);
            return true;
        }
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
        const auto key = ctjs::constantKey(read.getKey());
        if (hasKind(read.getObject(), Kind::stringVector) && key == "length") {
            values[read.getResult()] = Kind::number;
            provedStringVectorLengths.push_back(read);
            return true;
        }
        if (hasKind(read.getObject(), Kind::stringVector) &&
            increasingIndices.contains(read.getKey())) {
            bool guarded = false;
            for (mlir::Operation * parent = read->getParentOp(); parent != function;
                 parent = parent->getParentOp()) {
                if (!spend()) { return false; }
                auto branch = llvm::dyn_cast<mlir::scf::IfOp>(parent);
                if (!branch || !branch.getThenRegion().isAncestor(read->getParentRegion())) {
                    continue;
                }
                auto truth = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
                auto compare =
                    truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>() : ctjs::CompareOp{};
                auto length = compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>()
                                      : ctjs::GetPropertyOp{};
                guarded |= compare && compare.getKind() == ctjs::CompareKind::Lt &&
                           compare.getLhs() == read.getKey() && length &&
                           length.getObject() == read.getObject() &&
                           llvm::is_contained(provedStringVectorLengths, length);
            }
            if (guarded) {
                if (!spend()) { return false; }
                if (auto origin = snapshotOrigins.find(read.getObject());
                    origin != snapshotOrigins.end()) {
                    if (!spend()) { return false; }
                    keyOrigins.try_emplace(read.getResult(), origin->second);
                }
                values[read.getResult()] = Kind::string;
                provedStringVectorIndices.push_back(read);
                provedStrings.push_back(read.getResult());
                return true;
            }
        }
        if (suppliedArray && hasKind(read.getObject(), Kind::stringVector) && key == "filter") {
            values[read.getResult()] = Kind::filterStrings;
            provedMethods.emplace_back(read, HostDOMMethod::filterStrings);
            return true;
        }
        if (suppliedString && hasKind(read.getObject(), Kind::string) && key == "startsWith") {
            values[read.getResult()] = Kind::startsWith;
            provedMethods.emplace_back(read, HostDOMMethod::startsWith);
            return true;
        }
        if (suppliedString && suppliedRegExp && hasKind(read.getObject(), Kind::string) &&
            key == "replace") {
            values[read.getResult()] = Kind::replacePrefix;
            provedMethods.emplace_back(read, HostDOMMethod::removeStringPrefix);
            return true;
        }
        if (suppliedString && hasKind(read.getObject(), Kind::string) &&
            (key == "charAt" || key == "slice")) {
            values[read.getResult()] = key == "charAt" ? Kind::charAt : Kind::slice;
            provedMethods.emplace_back(read, key == "charAt" ? HostDOMMethod::stringCharAt
                                                             : HostDOMMethod::stringSlice);
            return true;
        }
        if (suppliedString && hasKind(read.getObject(), Kind::string) && key == "toLowerCase" &&
            firstUnits.contains(read.getObject())) {
            values[read.getResult()] = Kind::lowercaseUnit;
            provedMethods.emplace_back(read, HostDOMMethod::stringLowercaseUnit);
            return true;
        }
        if (hasKind(read.getObject(), Kind::element) && key == "dataset" &&
            llvm::is_contained(provedDatasetElements, read.getObject())) {
            values[read.getResult()] = Kind::dataset;
            datasetEpochs[read.getResult()] = mutationEpoch;
            provedDatasets.push_back(read);
            return true;
        }
        if (hasKind(read.getObject(), Kind::dataset)) {
            if (!spend() || !spend() || !spend()) { return false; }
            const auto origin = keyOrigins.find(read.getKey());
            auto dataset = read.getObject().getDefiningOp<ctjs::GetPropertyOp>();
            // ponytail: only direct snapshot members carry presence.
            // Joined or transformed keys need a separate provenance proof.
            if (!dataset || origin == keyOrigins.end() ||
                origin->second.element != dataset.getObject() ||
                origin->second.epoch != mutationEpoch ||
                datasetEpochs.lookup(read.getObject()) != mutationEpoch) {
                refusal = "DOM dataset value requires a live member key from the same "
                          "element";
                return false;
            }
            provedDatasetValues.emplace_back(read, dataset.getObject());
            values[read.getResult()] = Kind::string;
            provedStrings.push_back(read.getResult());
            return true;
        }
        if (hasKind(read.getObject(), Kind::objectIntrinsic) && key == "keys") {
            values[read.getResult()] = Kind::objectKeys;
            provedMethods.emplace_back(read, HostDOMMethod::datasetKeys);
            return true;
        }
        if (hasKind(read.getObject(), Kind::jsonIntrinsic) && key == "parse") {
            values[read.getResult()] = Kind::jsonParse;
            provedMethods.emplace_back(read, HostDOMMethod::jsonParse);
            return true;
        }
        if (suppliedNumber && hasKind(read.getObject(), Kind::number) && key == "toString") {
            values[read.getResult()] = Kind::numberToString;
            provedMethods.emplace_back(read, HostDOMMethod::numberToString);
            return true;
        }
        if (hasKind(read.getObject(), Kind::element) && key == "classList") {
            values[read.getResult()] = Kind::tokenList;
            provedTokens.push_back(read);
            return true;
        }
        if (hasKind(read.getObject(), Kind::element) && key == "getAttribute") {
            values[read.getResult()] = Kind::getAttribute;
            provedMethods.emplace_back(read, HostDOMMethod::getAttribute);
            return true;
        }
        if (hasKind(read.getObject(), Kind::element) && key == "setAttribute") {
            values[read.getResult()] = Kind::attribute;
            provedMethods.emplace_back(read, HostDOMMethod::setAttribute);
            return true;
        }
        if (hasKind(read.getObject(), Kind::element) &&
            (key == "contains" || key == "matches" || key == "closest")) {
            values[read.getResult()] = key == "contains"  ? Kind::contains
                                       : key == "matches" ? Kind::matches
                                                          : Kind::closest;
            provedMethods.emplace_back(read, key == "contains"  ? HostDOMMethod::contains
                                             : key == "matches" ? HostDOMMethod::matches
                                                                : HostDOMMethod::closest);
            return true;
        }
        if (hasKind(read.getObject(), Kind::element) &&
            (key == "toggleAttribute" || key == "hasAttribute" || key == "removeAttribute")) {
            values[read.getResult()] = key == "toggleAttribute" ? Kind::toggleAttribute
                                       : key == "hasAttribute"  ? Kind::hasAttribute
                                                                : Kind::removeAttribute;
            provedMethods.emplace_back(read,
                                       key == "toggleAttribute" ? HostDOMMethod::toggleAttribute
                                       : key == "hasAttribute"  ? HostDOMMethod::hasAttribute
                                                                : HostDOMMethod::removeAttribute);
            return true;
        }
        if (hasKind(read.getObject(), Kind::tokenList) && key == "toggle") {
            values[read.getResult()] = Kind::toggle;
            provedMethods.emplace_back(read, HostDOMMethod::toggleClass);
            return true;
        }
        refusal = "DOM property read lacks a proved receiver and supported member";
        return false;
    }
    if (auto invoke = llvm::dyn_cast<ctjs::CallOp>(operation)) {
        auto arguments = invoke.getArgs();
        if (hasKind(invoke.getCallee(), Kind::regexpFactory)) {
            const bool uppercase = arguments.size() == 2 &&
                                   ctjs::constantKey(arguments[0]) == "[A-Z]" &&
                                   ctjs::constantKey(arguments[1]) == "g";
            if (!hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() != 2 ||
                (!uppercase &&
                 (ctjs::constantKey(arguments[0]) != "^bs" || !emptyString(arguments[1])))) {
                refusal = "DOM prefix removal requires the original /^bs/ literal";
                return false;
            }
            unsigned uses = 0;
            for (mlir::OpOperand & use : invoke.getResult().getUses()) {
                if (!spend()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                if (!call || use.getOperandNumber() != 2 || call.getArgs().size() != 2) {
                    refusal = "DOM prefix RegExp escapes its single replacement";
                    return false;
                }
                ++uses;
            }
            if (uses != 1) {
                refusal = "DOM prefix RegExp requires one confined replacement";
                return false;
            }
            values[invoke.getResult()] = uppercase ? Kind::uppercaseRegExp : Kind::prefixRegExp;
            provedPrefixRegExps.push_back(invoke);
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::uriIntrinsic)) {
            auto parent = llvm::dyn_cast<ctjs::InvokeOp>(invoke->getParentOp());
            if (!parent || invoke->getParentRegion() != &parent.getBody() ||
                !hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() != 1 ||
                !hasKind(arguments[0], Kind::string)) {
                refusal = "URI call requires explicit identity, an undefined receiver, "
                          "one String and preserved failure continuation";
                return false;
            }
            provedCalls.push_back({invoke, HostDOMMethod::decodeURIComponent, arguments[0]});
            values[invoke.getResult()] = Kind::string;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::jsonParse)) {
            auto parent = llvm::dyn_cast<ctjs::InvokeOp>(invoke->getParentOp());
            auto method = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
            if (!parent || invoke->getParentRegion() != &parent.getBody() || !method ||
                method.getObject() != invoke.getReceiver() || arguments.size() != 1 ||
                !hasKind(arguments[0], Kind::string)) {
                refusal = "JSON.parse call requires the initial JSON receiver, one "
                          "String and preserved failure continuation";
                return false;
            }
            provedCalls.push_back({invoke, HostDOMMethod::jsonParse, arguments[0]});
            values[invoke.getResult()] = Kind::json;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::numberIntrinsic)) {
            if (!hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() != 1 ||
                (!hasKind(arguments[0], Kind::string) && !hasKind(arguments[0], Kind::null) &&
                 !hasKind(arguments[0], Kind::optionalString))) {
                refusal = "Number call requires its initial builtin, undefined "
                          "receiver and one String/null input";
                return false;
            }
            provedCalls.push_back({invoke, HostDOMMethod::number, arguments[0]});
            values[invoke.getResult()] = Kind::number;
            return true;
        }
        auto method = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (!method || method.getObject() != invoke.getReceiver()) {
            refusal = "DOM call does not preserve its proved method receiver";
            return false;
        }
        if (hasKind(invoke.getCallee(), Kind::charAt) || hasKind(invoke.getCallee(), Kind::slice)) {
            const bool first = hasKind(invoke.getCallee(), Kind::charAt);
            auto literal = arguments.size() == 1 ? arguments[0].getDefiningOp<ctjs::ConstantOp>()
                                                 : ctjs::ConstantOp{};
            auto index =
                literal ? llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue()) : ctjs::NumberAttr{};
            // ponytail: Bootstrap's exact indices avoid general
            // ToIntegerOrInfinity; broader slices need that proof.
            if (!index || index.getDouble() != (first ? 0 : 1)) {
                refusal = "DOM String indexing requires charAt(0) or slice(1)";
                return false;
            }
            provedCalls.push_back({invoke,
                                   first ? HostDOMMethod::stringCharAt : HostDOMMethod::stringSlice,
                                   invoke.getReceiver()});
            values[invoke.getResult()] = Kind::string;
            if (!spend()) { return false; }
            (first ? firstUnits : stringTails)[invoke.getResult()] = invoke.getReceiver();
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::lowercaseUnit) && arguments.empty()) {
            if (!spend()) { return false; }
            provedCalls.push_back(
                {invoke, HostDOMMethod::stringLowercaseUnit, invoke.getReceiver()});
            loweredFirstUnits[invoke.getResult()] = firstUnits.lookup(invoke.getReceiver());
            values[invoke.getResult()] = Kind::string;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::replacePrefix) && arguments.size() == 2 &&
            hasKind(arguments[0], Kind::uppercaseRegExp) &&
            hasKind(arguments[1], Kind::replacementCallback)) {
            auto closure = arguments[1].getDefiningOp<ctjs::CreateClosureOp>();
            auto callback = indexedCallbacks.lookup(static_cast<unsigned>(closure.getFunction()));
            // Every callback use is this exact pattern; its argument is
            // one ASCII uppercase unit, never an arbitrary String.
            provedCalls.push_back(
                {invoke, HostDOMMethod::replaceUppercase, invoke.getReceiver(), callback});
            values[invoke.getResult()] = Kind::string;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::replacePrefix) && arguments.size() == 2 &&
            hasKind(arguments[0], Kind::prefixRegExp) && emptyString(arguments[1])) {
            // ponytail: the exact ASCII /^bs/ with no flags needs no
            // regex engine. Broader patterns require their own proof.
            // Initial String/RegExp/factory identities fix @@replace,
            // exec and flag accessors; the census excludes mutation
            // and reentry, and the literal has no observable state.
            provedCalls.push_back(
                {invoke, HostDOMMethod::removeStringPrefix, invoke.getReceiver()});
            values[invoke.getResult()] = Kind::string;
            auto key = invoke.getReceiver().getDefiningOp<ctjs::GetPropertyOp>();
            if (!spend() || !spend()) { return false; }
            if (key && keyOrigins.contains(key.getResult()) &&
                prefixSnapshots.contains(key.getObject())) {
                // Removing a guaranteed prefix is injective. Keep this
                // authority separate from dataset membership: the new
                // key does not name the original dataset property.
                strippedAssignmentKeys[invoke.getResult()] = key;
            }
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::filterStrings) && arguments.size() == 1 &&
            hasKind(arguments[0], Kind::callback)) {
            if (!spend()) { return false; }
            if (auto found = snapshotOrigins.find(invoke.getReceiver());
                found != snapshotOrigins.end()) {
                if (!spend()) { return false; }
                const auto origin = found->second;
                snapshotOrigins.try_emplace(invoke.getResult(), origin);
            }
            auto closure = arguments[0].getDefiningOp<ctjs::CreateClosureOp>();
            auto callback = indexedCallbacks.lookup(static_cast<unsigned>(closure.getFunction()));
            if (!spend() || !spend()) { return false; }
            if (prefixCallbacks.contains(callback) ||
                prefixSnapshots.contains(invoke.getReceiver())) {
                prefixSnapshots.insert(invoke.getResult());
            }
            provedCalls.push_back(
                {invoke, HostDOMMethod::filterStrings, invoke.getReceiver(), callback});
            values[invoke.getResult()] = Kind::stringVector;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::startsWith) && arguments.size() == 1) {
            auto prefix = arguments[0].getDefiningOp<ctjs::ConstantOp>();
            auto text =
                prefix ? llvm::dyn_cast<ctjs::StringAttr>(prefix.getValue()) : ctjs::StringAttr{};
            // ponytail: ASCII prefixes make byte and UTF-16 prefix tests
            // equivalent. General prefixes need code-unit String proof.
            if (!text) {
                refusal = "DOM startsWith requires one constant ASCII prefix";
                return false;
            }
            for (unsigned char c : text.getValue()) {
                if (!spend()) { return false; }
                if (c > 127) {
                    refusal = "DOM startsWith requires one constant ASCII prefix";
                    return false;
                }
            }
            provedCalls.push_back({invoke, HostDOMMethod::startsWith, invoke.getReceiver()});
            values[invoke.getResult()] = Kind::boolean;
            if (callbackBody &&
                invoke.getReceiver() == block.getArgument(ctjs::implicit_arguments) &&
                text.getValue().starts_with("bs")) {
                prefixRequired.insert(invoke.getResult());
            }
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::objectKeys) && arguments.size() == 1 &&
            hasKind(arguments[0], Kind::dataset)) {
            if (datasetEpochs.lookup(arguments[0]) != mutationEpoch) {
                refusal = "DOM dataset enumeration crosses a source mutation";
                return false;
            }
            auto dataset = arguments[0].getDefiningOp<ctjs::GetPropertyOp>();
            if (!spend()) { return false; }
            snapshotOrigins.try_emplace(invoke.getResult(),
                                        DatasetOrigin{dataset.getObject(), mutationEpoch});
            provedCalls.push_back({invoke, HostDOMMethod::datasetKeys, dataset.getObject()});
            values[invoke.getResult()] = Kind::stringVector;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::numberToString) && arguments.empty()) {
            provedCalls.push_back({invoke, HostDOMMethod::numberToString, invoke.getReceiver()});
            values[invoke.getResult()] = Kind::string;
            return true;
        }
        const bool contains = hasKind(invoke.getCallee(), Kind::contains);
        const bool matches = hasKind(invoke.getCallee(), Kind::matches);
        const bool closest = hasKind(invoke.getCallee(), Kind::closest);
        if (arguments.size() == 1 &&
            ((contains && hasKind(arguments[0], Kind::element)) ||
             ((matches || closest) && hasKind(arguments[0], Kind::string)))) {
            provedCalls.push_back({invoke,
                                   contains  ? HostDOMMethod::contains
                                   : matches ? HostDOMMethod::matches
                                             : HostDOMMethod::closest,
                                   invoke.getReceiver()});
            values[invoke.getResult()] = closest ? Kind::nullableElement : Kind::boolean;
            return true;
        }
        if ((hasKind(invoke.getCallee(), Kind::toggle) ||
             hasKind(invoke.getCallee(), Kind::toggleAttribute)) &&
            (arguments.size() == 1 ||
             (arguments.size() == 2 &&
              (hasKind(arguments[1], Kind::boolean) || hasKind(arguments[1], Kind::undefined)))) &&
            hasKind(arguments[0], Kind::string)) {
            ++mutationEpoch;
            const bool classes = hasKind(invoke.getCallee(), Kind::toggle);
            auto element =
                classes ? invoke.getReceiver().getDefiningOp<ctjs::GetPropertyOp>().getObject()
                        : invoke.getReceiver();
            provedCalls.push_back(
                {invoke, classes ? HostDOMMethod::toggleClass : HostDOMMethod::toggleAttribute,
                 element});
            values[invoke.getResult()] = Kind::boolean;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::getAttribute) && arguments.size() == 1 &&
            hasKind(arguments[0], Kind::string)) {
            provedCalls.push_back({invoke, HostDOMMethod::getAttribute, invoke.getReceiver()});
            // Own String or null, with no prototype fallback.
            values[invoke.getResult()] = Kind::optionalString;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::hasAttribute) && arguments.size() == 1 &&
            hasKind(arguments[0], Kind::string)) {
            provedCalls.push_back({invoke, HostDOMMethod::hasAttribute, invoke.getReceiver()});
            values[invoke.getResult()] = Kind::boolean;
            return true;
        }
        const bool sets = hasKind(invoke.getCallee(), Kind::attribute);
        if ((sets && arguments.size() == 2 && hasKind(arguments[0], Kind::string) &&
             (hasKind(arguments[1], Kind::string) || hasKind(arguments[1], Kind::boolean) ||
              hasKind(arguments[1], Kind::optionalString))) ||
            (hasKind(invoke.getCallee(), Kind::removeAttribute) && arguments.size() == 1 &&
             hasKind(arguments[0], Kind::string))) {
            ++mutationEpoch;
            provedCalls.push_back(
                {invoke, sets ? HostDOMMethod::setAttribute : HostDOMMethod::removeAttribute,
                 invoke.getReceiver()});
            values[invoke.getResult()] = Kind::undefined;
            return true;
        }
        refusal = "DOM call arguments lack the supported primitive contract";
        return false;
    }
    return std::nullopt;
}

} // namespace ctcompile::ctnative::dom_entry_detail
