#include "Body.hpp"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSwitch.h"

#include <cmath>
#include <limits>

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

std::optional<double> numericIndexLiteral(mlir::Value value) {
    bool negative = false;
    unsigned negations = 0;
    // ponytail: two literal negations; deeper chains need a budgeted origin proof.
    for (; negations < 2; ++negations) {
        auto unary = value.getDefiningOp<ctjs::UnaryOp>();
        if (!unary || unary.getKind() != ctjs::UnaryKind::Neg) { break; }
        negative = !negative;
        value = unary.getOperand();
    }
    auto literal = value.getDefiningOp<ctjs::ConstantOp>();
    // Negation has already converted these exact primitives to Numbers.
    // Bare null/Boolean arithmetic still needs a separate coercion proof.
    if (negations && literal) {
        if (llvm::isa<ctjs::NullAttr>(literal.getValue())) { return negative ? -0.0 : 0.0; }
        if (auto flag = llvm::dyn_cast<ctjs::BooleanAttr>(literal.getValue())) {
            const double number = flag.getValue() ? 1.0 : 0.0;
            return negative ? -number : number;
        }
    }
    auto number = ctjs::constantAttr<ctjs::NumberAttr>(value);
    if (!number || std::isnan(number.getDouble())) { return std::nullopt; }
    return negative ? -number.getDouble() : number.getDouble();
}

bool emptyString(mlir::Value value) {
    auto string = ctjs::constantAttr<ctjs::StringAttr>(value);
    return string && string.getValue().empty();
}

std::optional<double> stringIndex(mlir::Value value, unsigned arithmeticDepth = 2) {
    if (auto binary = value.getDefiningOp<ctjs::BinaryOp>();
        binary &&
        (binary.getKind() == ctjs::BinaryKind::Div || binary.getKind() == ctjs::BinaryKind::Sub ||
         binary.getKind() == ctjs::BinaryKind::Mul || binary.getKind() == ctjs::BinaryKind::Mod ||
         binary.getKind() == ctjs::BinaryKind::Add || binary.getKind() == ctjs::BinaryKind::Pow)) {
        // ponytail: two binary levels bound this literal proof's work. Deeper
        // expressions need a separately budgeted origin analysis.
        if (!arithmeticDepth) { return std::nullopt; }
        const auto lhs = stringIndex(binary.getLhs(), arithmeticDepth - 1);
        const auto rhs = stringIndex(binary.getRhs(), arithmeticDepth - 1);
        if (!lhs || !rhs) { return std::nullopt; }
        // Both operands are already Numbers; no coercion or user code can run.
        // Globals and coercing operands still need their own origin proof.
        if (binary.getKind() == ctjs::BinaryKind::Div) { return *lhs / *rhs; }
        if (binary.getKind() == ctjs::BinaryKind::Sub) { return *lhs - *rhs; }
        if (binary.getKind() == ctjs::BinaryKind::Add) { return *lhs + *rhs; }
        if (binary.getKind() == ctjs::BinaryKind::Mod) { return std::fmod(*lhs, *rhs); }
        if (binary.getKind() == ctjs::BinaryKind::Pow) {
            // Number::exponentiate differs from pow for |base|=1 and infinite exponent.
            if (std::abs(*lhs) == 1 && !std::isfinite(*rhs)) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            return std::pow(*lhs, *rhs);
        }
        return *lhs * *rhs;
    }
    if (auto unary = value.getDefiningOp<ctjs::UnaryOp>();
        unary && unary.getKind() == ctjs::UnaryKind::Neg) {
        if (unary.getOperand().getDefiningOp<ctjs::BinaryOp>()) {
            const auto offset = stringIndex(unary.getOperand(), arithmeticDepth);
            return offset ? std::optional<double>{-*offset} : std::nullopt;
        }
    }
    // Size and infinity clamp before unsigned conversion in lowering. NaN has
    // no Number-literal source spelling; only the arithmetic above proves its origin.
    return numericIndexLiteral(value);
}
} // namespace

std::optional<bool> Body::browserOperation(mlir::Operation & operation) {
    auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
    auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation);
    // Exact Number operands cannot coerce or reenter. Their scalar arithmetic
    // also supplies attribute text; String-index consumers retain their own bounds proof.
    const auto numericBinary = [&](auto op) {
        return op && op.getKind() != ctjs::BinaryKind::Concat &&
               hasKind(op.getLhs(), Kind::number) && hasKind(op.getRhs(), Kind::number);
    };
    if ((unary &&
         (unary.getKind() == ctjs::UnaryKind::Neg || unary.getKind() == ctjs::UnaryKind::Plus ||
          unary.getKind() == ctjs::UnaryKind::BitNot) &&
         hasKind(unary.getOperand(), Kind::number)) ||
        numericBinary(binary) || numericBinary(llvm::dyn_cast<ctjs::BinaryStaticOp>(operation))) {
        if (!spend()) { return false; }
        values[operation.getResult(0)] = Kind::number;
        return true;
    }
    if ((unary && unary.getKind() == ctjs::UnaryKind::Neg) ||
        (binary &&
         (binary.getKind() == ctjs::BinaryKind::Div || binary.getKind() == ctjs::BinaryKind::Sub ||
          binary.getKind() == ctjs::BinaryKind::Mul || binary.getKind() == ctjs::BinaryKind::Mod ||
          binary.getKind() == ctjs::BinaryKind::Pow ||
          (binary.getKind() == ctjs::BinaryKind::Add &&
           ((binary.getLhs().getDefiningOp<ctjs::UnaryOp>() &&
             numericIndexLiteral(binary.getLhs())) ||
            (binary.getRhs().getDefiningOp<ctjs::UnaryOp>() &&
             numericIndexLiteral(binary.getRhs()))))))) {
        if (!spend()) { return false; }
        const auto result = operation.getResult(0);
        if (!stringIndex(result)) {
            refusal =
                "DOM String indexing negation/division/subtraction/multiplication/remainder/power "
                "requires at most two arithmetic levels over Number or negated primitive literals";
            return false;
        }
        llvm::SmallVector<mlir::Value> pending{result};
        while (!pending.empty()) {
            unsigned bounds = 0;
            for (mlir::OpOperand & use : pending.pop_back_val().getUses()) {
                if (!spend()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                if (auto arithmetic = llvm::dyn_cast<ctjs::BinaryOp>(use.getOwner());
                    arithmetic && stringIndex(arithmetic.getResult())) {
                    // Follow all intermediate uses, including Add: ordinary
                    // scalar Add has its own path and does not run this census.
                    pending.push_back(arithmetic.getResult());
                    ++bounds;
                    continue;
                }
                if (auto negation = llvm::dyn_cast<ctjs::UnaryOp>(use.getOwner());
                    negation && negation.getKind() == ctjs::UnaryKind::Neg &&
                    stringIndex(negation.getResult())) {
                    pending.push_back(negation.getResult());
                    ++bounds;
                    continue;
                }
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                auto method = call ? call.getCallee().getDefiningOp<ctjs::GetPropertyOp>()
                                   : ctjs::GetPropertyOp{};
                auto key = method ? ctjs::constantKey(method.getKey()) : llvm::StringRef{};
                if (!method || method.getObject() != call.getReceiver() ||
                    (key != "slice" && key != "charAt") || use.getOperandNumber() < 2 ||
                    use.getOperandNumber() > (key == "charAt" ? 2U : 3U)) {
                    refusal = "DOM String indexing literal expression escapes its bounds";
                    return false;
                }
                ++bounds;
            }
            if (!bounds) {
                refusal = "DOM String indexing literal expression has no bound use";
                return false;
            }
        }
        values[result] = Kind::number;
        return true;
    }
    if (auto load = llvm::dyn_cast<ctjs::LoadGlobalOp>(operation)) {
        if (documentParameter && load.getName() == "document") {
            values[load.getResult()] = Kind::document;
            proved.documentLoads.insert(load);
            return true;
        }
        // The DOM provider fixes this initial binding. The complete
        // source census admits no replacement or script reentry.
        if (load.getName() == "undefined") {
            values[load.getResult()] = Kind::undefined;
            return true;
        }
        const auto kind = llvm::StringSwitch<std::optional<Kind>>(load.getName())
                              .Case("Symbol", Kind::symbolIntrinsic)
                              .Case("Element", Kind::elementIntrinsic)
                              .Case("__ctbrowser_regexp", Kind::regexpFactory)
                              .Case("decodeURIComponent", Kind::uriIntrinsic)
                              .Case("Number", Kind::numberIntrinsic)
                              .Case("Object", Kind::objectIntrinsic)
                              .Case("JSON", Kind::jsonIntrinsic)
                              .Default(std::nullopt);
        // The RegExp factory also needs the original String and RegExp.
        if (kind && supplied(load.getName()) &&
            (*kind != Kind::regexpFactory || (supplied("String") && supplied("RegExp")))) {
            values[load.getResult()] = *kind;
            proved.intrinsics.insert(load);
            return true;
        }
    }
    if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(operation)) {
        const auto key = ctjs::constantKey(read.getKey());
        if (hasKind(read.getObject(), Kind::document)) {
            if (key == "documentElement") {
                values[read.getResult()] =
                    documentRootPresent ? Kind::element : Kind::nullableElement;
                proved.documentRoots.insert(read);
                return true;
            }
            if (key == "querySelector" || key == "querySelectorAll") {
                const bool all = key == "querySelectorAll";
                values[read.getResult()] =
                    all ? Kind::documentQuerySelectorAll : Kind::documentQuerySelector;
                proved.methods.try_emplace(read, all ? HostDOMMethod::documentQuerySelectorAll
                                                     : HostDOMMethod::documentQuerySelector);
                return true;
            }
        }
        // Bound catalog lookup work by the longest standard property name.
        if (hasKind(read.getObject(), Kind::symbolIntrinsic) && key.size() <= 18 &&
            wellKnownSymbols.contains(key)) {
            values[read.getResult()] = Kind::symbol;
            proved.symbols.try_emplace(read, key);
            return true;
        }
        if (supplied("Symbol") && hasKind(read.getObject(), Kind::symbol) && key == "description") {
            values[read.getResult()] = Kind::undefinedString;
            proved.symbolDescriptions.insert(read);
            return true;
        }
        if (supplied("Symbol") && hasKind(read.getObject(), Kind::symbol) &&
            (key == "toString" || key == "valueOf")) {
            const bool text = key == "toString";
            values[read.getResult()] = text ? Kind::symbolToString : Kind::symbolValueOf;
            proved.methods.try_emplace(read, text ? HostDOMMethod::symbolToString
                                                  : HostDOMMethod::symbolValueOf);
            return true;
        }
        if (hasKind(read.getObject(), Kind::elementIntrinsic) && key == "prototype") {
            values[read.getResult()] = Kind::elementPrototype;
            proved.elementPrototypes.insert(read);
            return true;
        }
        if (hasKind(read.getObject(), Kind::elementPrototype) && key.size() <= 16) {
            auto method = elementMethods.find(key);
            if (method != elementMethods.end() &&
                (method->second.second == HostDOMMethod::matches ||
                 method->second.second == HostDOMMethod::closest ||
                 method->second.second == HostDOMMethod::querySelector ||
                 method->second.second == HostDOMMethod::querySelectorAll)) {
                values[read.getResult()] = Kind::prototypeSelector;
                proved.methods.try_emplace(read, method->second.second);
                return true;
            }
        }
        if (supplied("Function") && hasKind(read.getObject(), Kind::prototypeSelector) &&
            key == "call") {
            auto selector = read.getObject().getDefiningOp<ctjs::GetPropertyOp>();
            const auto method = proved.methods.find(selector);
            if (method != proved.methods.end()) {
                values[read.getResult()] = Kind::selectorCall;
                proved.methods.try_emplace(read, method->second);
                return true;
            }
        }
        const bool elements = hasKind(read.getObject(), Kind::elementVector);
        if ((elements || hasKind(read.getObject(), Kind::stringVector)) && key == "length") {
            values[read.getResult()] = Kind::number;
            if (elements) {
                proved.elementVectorLengths.insert(read);
            } else {
                proved.stringVectorLengths.push_back(read);
            }
            return true;
        }
        if ((elements || hasKind(read.getObject(), Kind::stringVector)) &&
            increasingIndices.contains(read.getKey())) {
            bool guarded = false;
            for (mlir::Operation * parent = read->getParentOp(); parent != function;
                 parent = parent->getParentOp()) {
                if (!spend()) { return false; }
                auto [compare, index] = indexGuard(parent, read);
                mlir::Value bound = compare ? compare.getRhs() : mlir::Value{};
                if (auto capped =
                        bound ? bound.getDefiningOp<mlir::scf::IfOp>() : mlir::scf::IfOp{};
                    capped && capped.getNumResults() == 1 && capped.getThenRegion().hasOneBlock() &&
                    capped.getElseRegion().hasOneBlock() &&
                    capped.getThenRegion().front().getOperations().size() == 1 &&
                    capped.getElseRegion().front().getOperations().size() == 1) {
                    if (!spend()) { return false; }
                    auto less = truthyComparison(capped.getCondition());
                    auto number = less ? ctjs::constantAttr<ctjs::NumberAttr>(less.getRhs())
                                       : ctjs::NumberAttr{};
                    auto yes =
                        llvm::dyn_cast<mlir::scf::YieldOp>(capped.getThenRegion().front().back());
                    auto no =
                        llvm::dyn_cast<mlir::scf::YieldOp>(capped.getElseRegion().front().back());
                    if (less && less.getKind() == ctjs::CompareKind::Lt && number &&
                        number.getDouble() == double(1U << 24) && yes && no &&
                        yes.getNumOperands() == 1 && no.getNumOperands() == 1 &&
                        yes.getOperand(0) == less.getLhs() && no.getOperand(0) == less.getRhs()) {
                        // min(snapshot.length, spread cap) is also an upper
                        // bound for accesses to that immutable snapshot.
                        bound = less.getLhs();
                    }
                }
                auto length =
                    bound ? bound.getDefiningOp<ctjs::GetPropertyOp>() : ctjs::GetPropertyOp{};
                guarded |= compare && compare.getKind() == ctjs::CompareKind::Lt &&
                           compare.getLhs() == index && length &&
                           length.getObject() == read.getObject() &&
                           (elements ? proved.elementVectorLengths.contains(length)
                                     : llvm::is_contained(proved.stringVectorLengths, length));
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
                    proved.elementVectorIndices.insert(read);
                } else {
                    values[read.getResult()] = Kind::string;
                    proved.stringVectorIndices.push_back(read);
                    proved.strings.push_back(read.getResult());
                }
                return true;
            }
        }
        if (supplied("Array") && hasKind(read.getObject(), Kind::stringVector) && key == "filter") {
            values[read.getResult()] = Kind::filterStrings;
            proved.methods.try_emplace(read, HostDOMMethod::filterStrings);
            return true;
        }
        // Bound hashing by the longest supported String member.
        if (supplied("String") && hasKind(read.getObject(), Kind::string) && key.size() <= 11) {
            if (auto method = stringMethods.find(key); method != stringMethods.end()) {
                const auto [kind, hostMethod] = method->second;
                const bool supported =
                    (kind != Kind::replacePrefix || suppliedRegExp()) &&
                    (kind != Kind::lowercaseUnit || firstUnits.contains(read.getObject()));
                if (supported) {
                    values[read.getResult()] = kind;
                    proved.methods.try_emplace(read, hostMethod);
                    return true;
                }
            }
        }
        if (hasKind(read.getObject(), Kind::element) && key == "dataset" &&
            llvm::is_contained(proved.datasetElements, read.getObject())) {
            values[read.getResult()] = Kind::dataset;
            datasetEpochs[read.getResult()] = mutationEpoch;
            proved.datasets.push_back(read);
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
            proved.datasetValues.try_emplace(read, dataset.getObject());
            values[read.getResult()] = Kind::string;
            proved.strings.push_back(read.getResult());
            return true;
        }
        if (hasKind(read.getObject(), Kind::objectIntrinsic) && key == "keys") {
            values[read.getResult()] = Kind::objectKeys;
            proved.methods.try_emplace(read, HostDOMMethod::datasetKeys);
            return true;
        }
        if (hasKind(read.getObject(), Kind::jsonIntrinsic) && key == "parse") {
            values[read.getResult()] = Kind::jsonParse;
            proved.methods.try_emplace(read, HostDOMMethod::jsonParse);
            return true;
        }
        if (supplied("Number") && hasKind(read.getObject(), Kind::number) && key == "toString") {
            values[read.getResult()] = Kind::numberToString;
            proved.methods.try_emplace(read, HostDOMMethod::numberToString);
            return true;
        }
        if (hasKind(read.getObject(), Kind::element) && key == "classList") {
            values[read.getResult()] = Kind::tokenList;
            proved.tokenLists.push_back(read);
            return true;
        }
        // Bound hashing by the longest supported member, as literal comparisons
        // did before the name tables. Unknown long keys must not bypass the work budget.
        if (hasKind(read.getObject(), Kind::element) && key.size() <= 16) {
            if (auto method = elementMethods.find(key); method != elementMethods.end()) {
                const auto [kind, hostMethod] = method->second;
                values[read.getResult()] = kind;
                proved.methods.try_emplace(read, hostMethod);
                return true;
            }
        }
        if (hasKind(read.getObject(), Kind::tokenList) && key.size() <= 8) {
            if (auto method = tokenMethods.find(key); method != tokenMethods.end()) {
                const auto [kind, hostMethod] = method->second;
                values[read.getResult()] = kind;
                proved.methods.try_emplace(read, hostMethod);
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
            proved.stringPrefixRegExps.push_back(invoke);
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
            proved.calls.push_back({invoke, HostDOMMethod::decodeURIComponent, arguments[0]});
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
            proved.calls.push_back({invoke, HostDOMMethod::jsonParse, arguments[0]});
            values[invoke.getResult()] = Kind::json;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::symbolIntrinsic)) {
            // No ToPrimitive/ToString hook can run for these exact input kinds.
            // Broader descriptions need their coercion and exception proof.
            if (!hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() > 1 ||
                (!arguments.empty() && !hasKind(arguments[0], {Kind::string, Kind::undefined}))) {
                refusal = "Symbol call requires its initial builtin, undefined receiver "
                          "and at most one String/undefined description";
                return false;
            }
            proved.calls.push_back({invoke, HostDOMMethod::symbol, {}});
            values[invoke.getResult()] = Kind::symbol;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::numberIntrinsic)) {
            if (!hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() != 1 ||
                !hasKind(arguments[0], {Kind::string, Kind::null, Kind::optionalString,
                                        Kind::boolean, Kind::number})) {
                refusal = "Number call requires its initial builtin, undefined "
                          "receiver and one String/null/Boolean/Number input";
                return false;
            }
            proved.calls.push_back({invoke, HostDOMMethod::number, arguments[0]});
            values[invoke.getResult()] = Kind::number;
            return true;
        }
        auto method = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (!method || method.getObject() != invoke.getReceiver()) {
            refusal = "DOM call does not preserve its proved method receiver";
            return false;
        }
        const bool documentAll = hasKind(invoke.getCallee(), Kind::documentQuerySelectorAll);
        if ((documentAll || hasKind(invoke.getCallee(), Kind::documentQuerySelector)) &&
            arguments.size() == 1 && hasKind(arguments[0], Kind::string)) {
            proved.calls.push_back({invoke,
                                    documentAll ? HostDOMMethod::documentQuerySelectorAll
                                                : HostDOMMethod::documentQuerySelector,
                                    documentParameter});
            values[invoke.getResult()] = documentAll ? Kind::elementVector : Kind::nullableElement;
            return true;
        }
        if (hasKind(invoke.getCallee(), {Kind::symbolToString, Kind::symbolValueOf}) &&
            arguments.empty()) {
            const bool text = hasKind(invoke.getCallee(), Kind::symbolToString);
            proved.calls.push_back(
                {invoke, text ? HostDOMMethod::symbolToString : HostDOMMethod::symbolValueOf,
                 invoke.getReceiver()});
            values[invoke.getResult()] = text ? Kind::string : Kind::symbol;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::selectorCall)) {
            // The selector's checked js_element_t construction stays at this
            // call. A root read may be absent; it grants no presence fact to
            // other uses, and preceding source effects must still run.
            const bool checkedRoot =
                arguments.size() == 2 && hasKind(arguments[0], Kind::nullableElement) &&
                proved.documentRoots.contains(arguments[0].getDefiningOp<ctjs::GetPropertyOp>());
            if (arguments.size() != 2 || (!hasKind(arguments[0], Kind::element) && !checkedRoot) ||
                !hasKind(arguments[1], Kind::string)) {
                refusal = "DOM prototype selector call requires an explicit Element or checked "
                          "document root and String";
                return false;
            }
            const auto kind = proved.methods.find(method)->second;
            proved.calls.push_back({invoke, kind, arguments[0], {}, true});
            values[invoke.getResult()] = kind == HostDOMMethod::querySelectorAll
                                             ? Kind::elementVector
                                             : Kind::nullableElement;
            if (kind == HostDOMMethod::matches) { values[invoke.getResult()] = Kind::boolean; }
            return true;
        }
        if (hasKind(invoke.getCallee(), {Kind::charAt, Kind::slice})) {
            const bool first = hasKind(invoke.getCallee(), Kind::charAt);
            if (arguments.size() > (first ? 1U : 2U)) {
                refusal = "DOM String indexing permits an optional index and optional slice end";
                return false;
            }
            for (mlir::Value argument : arguments) {
                if (!spend()) { return false; }
                auto literal = argument.getDefiningOp<ctjs::ConstantOp>();
                const bool primitive =
                    literal && llvm::isa<ctjs::NullAttr, ctjs::BooleanAttr>(literal.getValue());
                if (!primitive && !hasKind(argument, Kind::undefined) && !stringIndex(argument)) {
                    refusal = "DOM String indexing requires primitive literals, proved undefined "
                              "or at most two arithmetic levels over Number or negated primitive "
                              "literals";
                    return false;
                }
            }
            proved.calls.push_back(
                {invoke, first ? HostDOMMethod::stringCharAt : HostDOMMethod::stringSlice,
                 invoke.getReceiver()});
            values[invoke.getResult()] = Kind::string;
            auto literal = arguments.empty() ? ctjs::ConstantOp{}
                                             : arguments.front().getDefiningOp<ctjs::ConstantOp>();
            if (arguments.size() == 1 && literal && hasKind(arguments.front(), Kind::number) &&
                *stringIndex(arguments.front()) == (first ? 0 : 1)) {
                if (!spend()) { return false; }
                (first ? firstUnits : stringTails)[invoke.getResult()] = invoke.getReceiver();
            }
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::lowercaseUnit) && arguments.empty()) {
            if (!spend()) { return false; }
            proved.calls.push_back(
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
            proved.calls.push_back(
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
            proved.calls.push_back(
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
            proved.calls.push_back(
                {invoke, HostDOMMethod::filterStrings, invoke.getReceiver(), callback});
            values[invoke.getResult()] = Kind::stringVector;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::startsWith) && arguments.size() == 1) {
            llvm::SmallVector<std::pair<mlir::Value, unsigned>, 4> pieces{{arguments[0], 0}};
            // ponytail: two concatenation levels keep the ASCII proof bounded.
            // General prefixes need separate origin and UTF-16 proofs.
            while (!pieces.empty()) {
                const auto [piece, depth] = pieces.pop_back_val();
                if (!spend()) { return false; }
                if (auto add = piece.getDefiningOp<ctjs::BinaryOp>();
                    add && add.getKind() == ctjs::BinaryKind::Add && depth < 2) {
                    pieces.emplace_back(add.getLhs(), depth + 1);
                    pieces.emplace_back(add.getRhs(), depth + 1);
                    continue;
                }
                auto text = ctjs::constantAttr<ctjs::StringAttr>(piece);
                if (!text) {
                    refusal = "DOM startsWith requires ASCII literals with at most two "
                              "concatenation levels";
                    return false;
                }
                for (unsigned char c : text.getValue()) {
                    if (!spend()) { return false; }
                    if (c > 127) {
                        refusal = "DOM startsWith requires an ASCII prefix";
                        return false;
                    }
                }
            }
            proved.calls.push_back({invoke, HostDOMMethod::startsWith, invoke.getReceiver()});
            values[invoke.getResult()] = Kind::boolean;
            auto text = ctjs::constantAttr<ctjs::StringAttr>(arguments[0]);
            // Computed prefixes gain no dataset membership or key-transform authority.
            if (callbackBody && text &&
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
            proved.calls.push_back({invoke, HostDOMMethod::datasetKeys, dataset.getObject()});
            values[invoke.getResult()] = Kind::stringVector;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::numberToString) && arguments.empty()) {
            proved.calls.push_back({invoke, HostDOMMethod::numberToString, invoke.getReceiver()});
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
            proved.calls.push_back({invoke, hostMethod, invoke.getReceiver()});
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
            proved.calls.push_back({invoke, hostMethod, element});
            values[invoke.getResult()] = containsClass ? Kind::boolean : Kind::undefined;
            return true;
        }
        if (hasKind(invoke.getCallee(), {Kind::toggle, Kind::toggleAttribute}) &&
            (arguments.size() == 1 ||
             (arguments.size() == 2 && hasKind(arguments[1], {Kind::boolean, Kind::undefined}))) &&
            hasKind(arguments[0], Kind::string)) {
            ++mutationEpoch;
            const bool classes = hasKind(invoke.getCallee(), Kind::toggle);
            auto element =
                classes ? invoke.getReceiver().getDefiningOp<ctjs::GetPropertyOp>().getObject()
                        : invoke.getReceiver();
            proved.calls.push_back(
                {invoke, classes ? HostDOMMethod::toggleClass : HostDOMMethod::toggleAttribute,
                 element});
            values[invoke.getResult()] = Kind::boolean;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::getAttribute) && arguments.size() == 1 &&
            hasKind(arguments[0], Kind::string)) {
            proved.calls.push_back({invoke, HostDOMMethod::getAttribute, invoke.getReceiver()});
            // Own String or null, with no prototype fallback.
            values[invoke.getResult()] = Kind::optionalString;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::hasAttribute) && arguments.size() == 1 &&
            hasKind(arguments[0], Kind::string)) {
            proved.calls.push_back({invoke, HostDOMMethod::hasAttribute, invoke.getReceiver()});
            values[invoke.getResult()] = Kind::boolean;
            return true;
        }
        const bool sets = hasKind(invoke.getCallee(), Kind::attribute);
        if ((sets && arguments.size() == 2 && hasKind(arguments[0], Kind::string) &&
             hasKind(arguments[1], {Kind::string, Kind::boolean, Kind::optionalBoolean,
                                    Kind::number, Kind::optionalNumber, Kind::optionalString})) ||
            (hasKind(invoke.getCallee(), Kind::removeAttribute) && arguments.size() == 1 &&
             hasKind(arguments[0], Kind::string))) {
            ++mutationEpoch;
            proved.calls.push_back(
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
