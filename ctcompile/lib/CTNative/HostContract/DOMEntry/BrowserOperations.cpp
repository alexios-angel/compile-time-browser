#include "Body.hpp"

#include "llvm/ADT/StringMap.h"

#include <cmath>

namespace ctcompile::ctnative::dom_entry_detail {

namespace {
const llvm::StringSet<> wellKnownSymbols{
#define CTBROWSER_WELL_KNOWN_SYMBOL(name) #name,
#include "ctbrowser/core/well_known_symbols.def"
#undef CTBROWSER_WELL_KNOWN_SYMBOL
};
const llvm::StringMap<std::pair<Kind, HostDOMMethod>> stringMethods{
    {"startsWith", {Kind::startsWith, HostDOMMethod::startsWith}},
    {"replace", {Kind::replacePrefix, HostDOMMethod::removeStringPrefix}},
    {"charAt", {Kind::charAt, HostDOMMethod::stringCharAt}},
    {"slice", {Kind::slice, HostDOMMethod::stringSlice}},
    {"toLowerCase", {Kind::lowercaseUnit, HostDOMMethod::stringLowercaseUnit}},
};
const llvm::StringMap<std::pair<Kind, HostDOMMethod>> elementMethods{
    {"getAttribute", {Kind::getAttribute, HostDOMMethod::getAttribute}},
    {"setAttribute", {Kind::attribute, HostDOMMethod::setAttribute}},
    {"contains", {Kind::contains, HostDOMMethod::contains}},
    {"matches", {Kind::matches, HostDOMMethod::matches}},
    {"closest", {Kind::closest, HostDOMMethod::closest}},
    {"querySelector", {Kind::querySelector, HostDOMMethod::querySelector}},
    {"querySelectorAll", {Kind::querySelectorAll, HostDOMMethod::querySelectorAll}},
    {"toggleAttribute", {Kind::toggleAttribute, HostDOMMethod::toggleAttribute}},
    {"hasAttribute", {Kind::hasAttribute, HostDOMMethod::hasAttribute}},
    {"removeAttribute", {Kind::removeAttribute, HostDOMMethod::removeAttribute}},
};
const llvm::StringMap<std::pair<Kind, HostDOMMethod>> tokenMethods{
    {"toggle", {Kind::toggle, HostDOMMethod::toggleClass}},
    {"contains", {Kind::containsClass, HostDOMMethod::containsClass}},
    {"add", {Kind::addClass, HostDOMMethod::addClass}},
    {"remove", {Kind::removeClass, HostDOMMethod::removeClass}},
};

std::optional<double> stringIndex(mlir::Value value) {
    bool negative = false;
    if (auto unary = value.getDefiningOp<ctjs::UnaryOp>();
        unary && unary.getKind() == ctjs::UnaryKind::Neg) {
        negative = true;
        value = unary.getOperand();
    }
    auto literal = value.getDefiningOp<ctjs::ConstantOp>();
    auto number =
        literal ? llvm::dyn_cast<ctjs::NumberAttr>(literal.getValue()) : ctjs::NumberAttr{};
    if (!number) { return std::nullopt; }
    const double offset = negative ? -number.getDouble() : number.getDouble();
    // ponytail: uint32 magnitudes fit size_t on every native target; broader
    // inputs need their own ToIntegerOrInfinity and representability proof.
    if (!std::isfinite(offset) || offset < -4294967295.0 || offset > 4294967295.0 ||
        std::floor(offset) != offset) {
        return std::nullopt;
    }
    return offset;
}
} // namespace

std::optional<bool> Body::browserOperation(mlir::Operation & operation) {
    if (auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
        unary && unary.getKind() == ctjs::UnaryKind::Neg) {
        if (!spend()) { return false; }
        if (!stringIndex(unary.getResult())) {
            refusal = "DOM String indexing negation requires one bounded integer literal";
            return false;
        }
        unsigned bounds = 0;
        for (mlir::OpOperand & use : unary.getResult().getUses()) {
            if (!spend()) { return false; }
            if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            auto method = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                               : ctjs::GetPropertyOp{};
            auto key = method ? ctjs::constantKey(method.getKey()) : llvm::StringRef{};
            if (!method || method.getObject() != call.getReceiver() ||
                (key != "slice" && key != "charAt") || use.getOperandNumber() < 2 ||
                use.getOperandNumber() > (key == "charAt" ? 2U : 3U)) {
                refusal = "DOM String indexing literal negation escapes its bounds";
                return false;
            }
            ++bounds;
        }
        if (!bounds) {
            refusal = "DOM String indexing literal negation has no bound use";
            return false;
        }
        values[unary.getResult()] = Kind::number;
        return true;
    }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
        // The DOM provider fixes this initial binding. The complete
        // source census admits no replacement or script reentry.
        if (load.getName() == "undefined") {
            values[load.getResult()] = Kind::undefined;
            return true;
        }
        if (suppliedSymbol && load.getName() == "Symbol") {
            values[load.getResult()] = Kind::symbolIntrinsic;
            provedSymbolIntrinsics.insert(load);
            return true;
        }
        if (suppliedElement && load.getName() == "Element") {
            values[load.getResult()] = Kind::elementIntrinsic;
            provedElementIntrinsics.insert(load);
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
        // Bound catalog lookup work by the longest standard property name.
        if (hasKind(read.getObject(), Kind::symbolIntrinsic) && key.size() <= 18 &&
            wellKnownSymbols.contains(key)) {
            values[read.getResult()] = Kind::symbol;
            provedSymbols.try_emplace(read, key);
            return true;
        }
        if (suppliedSymbol && hasKind(read.getObject(), Kind::symbol) && key == "description") {
            values[read.getResult()] = Kind::undefinedString;
            provedSymbolDescriptions.insert(read);
            return true;
        }
        if (suppliedSymbol && hasKind(read.getObject(), Kind::symbol) &&
            (key == "toString" || key == "valueOf")) {
            const bool text = key == "toString";
            values[read.getResult()] = text ? Kind::symbolToString : Kind::symbolValueOf;
            provedMethods.try_emplace(read, text ? HostDOMMethod::symbolToString
                                                 : HostDOMMethod::symbolValueOf);
            return true;
        }
        if (hasKind(read.getObject(), Kind::elementIntrinsic) && key == "prototype") {
            values[read.getResult()] = Kind::elementPrototype;
            provedElementPrototypes.insert(read);
            return true;
        }
        if (hasKind(read.getObject(), Kind::elementPrototype) && key.size() <= 16) {
            auto method = elementMethods.find(key);
            if (method != elementMethods.end() &&
                (method->second.second == HostDOMMethod::querySelector ||
                 method->second.second == HostDOMMethod::querySelectorAll)) {
                values[read.getResult()] = Kind::prototypeSelector;
                provedMethods.try_emplace(read, method->second.second);
                return true;
            }
        }
        if (suppliedFunction && hasKind(read.getObject(), Kind::prototypeSelector) &&
            key == "call") {
            auto selector = read.getObject().getDefiningOp<ctjs::GetPropertyOp>();
            const auto method = provedMethods.find(selector);
            if (method != provedMethods.end()) {
                values[read.getResult()] = Kind::selectorCall;
                provedMethods.try_emplace(read, method->second);
                return true;
            }
        }
        const bool elements = hasKind(read.getObject(), Kind::elementVector);
        if ((elements || hasKind(read.getObject(), Kind::stringVector)) && key == "length") {
            values[read.getResult()] = Kind::number;
            if (elements) {
                provedElementVectorLengths.insert(read);
            } else {
                provedStringVectorLengths.push_back(read);
            }
            return true;
        }
        if ((elements || hasKind(read.getObject(), Kind::stringVector)) &&
            increasingIndices.contains(read.getKey())) {
            bool guarded = false;
            for (mlir::Operation * parent = read->getParentOp(); parent != function;
                 parent = parent->getParentOp()) {
                if (!spend()) { return false; }
                mlir::Value flag, index = read.getKey();
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(parent);
                    branch && branch.getThenRegion().isAncestor(read->getParentRegion())) {
                    flag = branch.getCondition();
                } else if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(parent);
                           loop && loop.getAfter().isAncestor(read->getParentRegion())) {
                    auto argument = llvm::dyn_cast<mlir::BlockArgument>(index);
                    if (!argument || argument.getOwner() != &loop.getAfter().front()) { continue; }
                    auto condition =
                        llvm::cast<mlir::scf::ConditionOp>(loop.getBefore().front().back());
                    flag = condition.getCondition();
                    index = condition.getArgs()[argument.getArgNumber()];
                }
                auto truth = flag ? flag.getDefiningOp<ctjs::TruthyOp>() : ctjs::TruthyOp{};
                auto compare =
                    truth ? truth.getValue().getDefiningOp<ctjs::CompareOp>() : ctjs::CompareOp{};
                auto length = compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>()
                                      : ctjs::GetPropertyOp{};
                guarded |= compare && compare.getKind() == ctjs::CompareKind::Lt &&
                           compare.getLhs() == index && length &&
                           length.getObject() == read.getObject() &&
                           (elements ? provedElementVectorLengths.contains(length)
                                     : llvm::is_contained(provedStringVectorLengths, length));
            }
            if (guarded) {
                if (!spend()) { return false; }
                if (auto origin = snapshotOrigins.find(read.getObject());
                    origin != snapshotOrigins.end()) {
                    if (!spend()) { return false; }
                    keyOrigins.try_emplace(read.getResult(), origin->second);
                }
                if (elements) {
                    values[read.getResult()] = Kind::element;
                    provedElementVectorIndices.insert(read);
                } else {
                    values[read.getResult()] = Kind::string;
                    provedStringVectorIndices.push_back(read);
                    provedStrings.push_back(read.getResult());
                }
                return true;
            }
        }
        if (suppliedArray && hasKind(read.getObject(), Kind::stringVector) && key == "filter") {
            values[read.getResult()] = Kind::filterStrings;
            provedMethods.try_emplace(read, HostDOMMethod::filterStrings);
            return true;
        }
        // Bound hashing by the longest supported String member.
        if (suppliedString && hasKind(read.getObject(), Kind::string) && key.size() <= 11) {
            if (auto method = stringMethods.find(key); method != stringMethods.end()) {
                const auto [kind, hostMethod] = method->second;
                const bool supported =
                    (kind != Kind::replacePrefix || suppliedRegExp) &&
                    (kind != Kind::lowercaseUnit || firstUnits.contains(read.getObject()));
                if (supported) {
                    values[read.getResult()] = kind;
                    provedMethods.try_emplace(read, hostMethod);
                    return true;
                }
            }
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
            provedDatasetValues.try_emplace(read, dataset.getObject());
            values[read.getResult()] = Kind::string;
            provedStrings.push_back(read.getResult());
            return true;
        }
        if (hasKind(read.getObject(), Kind::objectIntrinsic) && key == "keys") {
            values[read.getResult()] = Kind::objectKeys;
            provedMethods.try_emplace(read, HostDOMMethod::datasetKeys);
            return true;
        }
        if (hasKind(read.getObject(), Kind::jsonIntrinsic) && key == "parse") {
            values[read.getResult()] = Kind::jsonParse;
            provedMethods.try_emplace(read, HostDOMMethod::jsonParse);
            return true;
        }
        if (suppliedNumber && hasKind(read.getObject(), Kind::number) && key == "toString") {
            values[read.getResult()] = Kind::numberToString;
            provedMethods.try_emplace(read, HostDOMMethod::numberToString);
            return true;
        }
        if (hasKind(read.getObject(), Kind::element) && key == "classList") {
            values[read.getResult()] = Kind::tokenList;
            provedTokens.push_back(read);
            return true;
        }
        // Bound hashing by the longest supported member, as literal comparisons
        // did before the name tables. Unknown long keys must not bypass the work budget.
        if (hasKind(read.getObject(), Kind::element) && key.size() <= 16) {
            if (auto method = elementMethods.find(key); method != elementMethods.end()) {
                const auto [kind, hostMethod] = method->second;
                values[read.getResult()] = kind;
                provedMethods.try_emplace(read, hostMethod);
                return true;
            }
        }
        if (hasKind(read.getObject(), Kind::tokenList) && key.size() <= 8) {
            if (auto method = tokenMethods.find(key); method != tokenMethods.end()) {
                const auto [kind, hostMethod] = method->second;
                values[read.getResult()] = kind;
                provedMethods.try_emplace(read, hostMethod);
                return true;
            }
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
        if (hasKind(invoke.getCallee(), Kind::symbolIntrinsic)) {
            // No ToPrimitive/ToString hook can run for these exact input kinds.
            // Broader descriptions need their coercion and exception proof.
            if (!hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() > 1 ||
                (!arguments.empty() && !hasKind(arguments[0], Kind::string) &&
                 !hasKind(arguments[0], Kind::undefined))) {
                refusal = "Symbol call requires its initial builtin, undefined receiver "
                          "and at most one String/undefined description";
                return false;
            }
            provedCalls.push_back({invoke, HostDOMMethod::symbol, {}});
            values[invoke.getResult()] = Kind::symbol;
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
        if ((hasKind(invoke.getCallee(), Kind::symbolToString) ||
             hasKind(invoke.getCallee(), Kind::symbolValueOf)) &&
            arguments.empty()) {
            const bool text = hasKind(invoke.getCallee(), Kind::symbolToString);
            provedCalls.push_back(
                {invoke, text ? HostDOMMethod::symbolToString : HostDOMMethod::symbolValueOf,
                 invoke.getReceiver()});
            values[invoke.getResult()] = text ? Kind::string : Kind::symbol;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::selectorCall)) {
            if (arguments.size() != 2 || !hasKind(arguments[0], Kind::element) ||
                !hasKind(arguments[1], Kind::string)) {
                refusal = "DOM prototype selector call requires an explicit Element and String";
                return false;
            }
            const auto kind = provedMethods.find(method)->second;
            provedCalls.push_back({invoke, kind, arguments[0], {}, true});
            values[invoke.getResult()] = kind == HostDOMMethod::querySelectorAll
                                             ? Kind::elementVector
                                             : Kind::nullableElement;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::charAt) || hasKind(invoke.getCallee(), Kind::slice)) {
            const bool first = hasKind(invoke.getCallee(), Kind::charAt);
            if (arguments.empty() || arguments.size() > (first ? 1U : 2U)) {
                refusal = "DOM String indexing requires one index and an optional slice end";
                return false;
            }
            for (mlir::Value argument : arguments) {
                if (!spend()) { return false; }
                if (!stringIndex(argument)) {
                    refusal = "DOM String indexing requires bounded integer literals";
                    return false;
                }
            }
            provedCalls.push_back({invoke,
                                   first ? HostDOMMethod::stringCharAt : HostDOMMethod::stringSlice,
                                   invoke.getReceiver()});
            values[invoke.getResult()] = Kind::string;
            auto literal = arguments.front().getDefiningOp<ctjs::ConstantOp>();
            if (arguments.size() == 1 && literal &&
                *stringIndex(arguments.front()) == (first ? 0 : 1)) {
                if (!spend()) { return false; }
                (first ? firstUnits : stringTails)[invoke.getResult()] = invoke.getReceiver();
            }
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
        const bool query = hasKind(invoke.getCallee(), Kind::querySelector);
        const bool queryAll = hasKind(invoke.getCallee(), Kind::querySelectorAll);
        if (arguments.size() == 1 &&
            ((contains && hasKind(arguments[0], Kind::element)) ||
             ((matches || closest || query || queryAll) && hasKind(arguments[0], Kind::string)))) {
            const auto hostMethod =
                elementMethods.find(ctjs::constantKey(method.getKey()))->second.second;
            provedCalls.push_back({invoke, hostMethod, invoke.getReceiver()});
            if (queryAll) {
                values[invoke.getResult()] = Kind::elementVector;
            } else {
                values[invoke.getResult()] =
                    closest || query ? Kind::nullableElement : Kind::boolean;
            }
            return true;
        }
        const bool containsClass = hasKind(invoke.getCallee(), Kind::containsClass);
        const bool addClass = hasKind(invoke.getCallee(), Kind::addClass);
        const bool removeClass = hasKind(invoke.getCallee(), Kind::removeClass);
        if (containsClass || addClass || removeClass) {
            if (containsClass && arguments.size() != 1) {
                refusal = "DOM classList.contains requires one String";
                return false;
            }
            for (mlir::Value argument : arguments) {
                if (!spend()) { return false; }
                if (!hasKind(argument, Kind::string)) {
                    refusal = "DOM classList arguments require proved Strings";
                    return false;
                }
            }
            if (!containsClass) { ++mutationEpoch; }
            auto element = invoke.getReceiver().getDefiningOp<ctjs::GetPropertyOp>().getObject();
            const auto hostMethod =
                tokenMethods.find(ctjs::constantKey(method.getKey()))->second.second;
            provedCalls.push_back({invoke, hostMethod, element});
            values[invoke.getResult()] = containsClass ? Kind::boolean : Kind::undefined;
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
