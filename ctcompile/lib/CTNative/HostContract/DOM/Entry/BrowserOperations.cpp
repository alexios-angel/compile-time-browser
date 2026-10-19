//===- BrowserOperations.cpp - DOM entry browser calls --------------------===//
//
// Body::browserOperation: the allow-list of DOM, String, Symbol, Number and JSON
// reads and calls a DOM entry function may use, and the proof each one needs
// before the native host may run it.
//
//===----------------------------------------------------------------------===//

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
/// Members proved by a table lookup alone: the receiver kind, the constant
/// key and the original intrinsic they need, if any. Reading `key` from a
/// value of kind `receiver` gives a proved method of that receiver.
struct MemberRow {
    Kind receiver;
    llvm::StringRef key, needs;
    Kind kind;
    HostDOMMethod method;
};
const MemberRow memberRows[]{
    {Kind::document, "querySelector", "", Kind::documentQuerySelector,
     HostDOMMethod::documentQuerySelector},
    {Kind::document, "querySelectorAll", "", Kind::documentQuerySelectorAll,
     HostDOMMethod::documentQuerySelectorAll},
    {Kind::symbol, "toString", "Symbol", Kind::symbolToString, HostDOMMethod::symbolToString},
    {Kind::symbol, "valueOf", "Symbol", Kind::symbolValueOf, HostDOMMethod::symbolValueOf},
    {Kind::stringVector, "filter", "Array", Kind::filterStrings, HostDOMMethod::filterStrings},
    {Kind::objectIntrinsic, "keys", "", Kind::objectKeys, HostDOMMethod::datasetKeys},
    {Kind::jsonIntrinsic, "parse", "", Kind::jsonParse, HostDOMMethod::jsonParse},
    {Kind::number, "toString", "Number", Kind::numberToString, HostDOMMethod::numberToString},
};

/// The Number a String index literal evaluates to: a Number constant (not
/// NaN), or null, a Boolean or a Number under one or two unary minus signs.
/// std::nullopt for anything else.
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
    // Negation converts null and Booleans to Numbers without running user
    // code. Unnegated null or Boolean arithmetic is refused; it would need
    // its own coercion proof.
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

/// Evaluates a String index expression at compile time: numericIndexLiteral
/// operands combined by at most \p arithmeticDepth levels of / - * % + or **.
/// std::nullopt when the expression has any other shape.
std::optional<double> stringIndex(mlir::Value value, unsigned arithmeticDepth = 2) {
    if (auto binary = value.getDefiningOp<ctjs::BinaryOp>();
        binary && ctjs::isArithmetic(binary.getKind())) {
        // ponytail: two binary levels bound this literal proof's work. Deeper
        // expressions need a separately budgeted origin analysis.
        if (!arithmeticDepth) { return std::nullopt; }
        const auto lhs = stringIndex(binary.getLhs(), arithmeticDepth - 1);
        const auto rhs = stringIndex(binary.getRhs(), arithmeticDepth - 1);
        if (!lhs || !rhs) { return std::nullopt; }
        // Both operands are already Numbers, so no coercion or user code can
        // run. Any other operand makes the recursion return std::nullopt.
        if (binary.getKind() == ctjs::BinaryKind::Div) { return *lhs / *rhs; }
        if (binary.getKind() == ctjs::BinaryKind::Sub) { return *lhs - *rhs; }
        if (binary.getKind() == ctjs::BinaryKind::Add) { return *lhs + *rhs; }
        if (binary.getKind() == ctjs::BinaryKind::Mod) { return std::fmod(*lhs, *rhs); }
        if (binary.getKind() == ctjs::BinaryKind::Pow) {
            // JavaScript (Number::exponentiate) gives NaN for |base| == 1 with
            // an infinite exponent, where std::pow gives 1.
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
    // Lowering clamps large and infinite indices before converting them to
    // unsigned. NaN has no Number-literal spelling, so it can only come from
    // the arithmetic above.
    return numericIndexLiteral(value);
}
} // namespace

std::optional<bool> Body::browserOperation(mlir::Operation & operation) {
    auto unary = llvm::dyn_cast<ctjs::UnaryOp>(operation);
    auto binary = llvm::dyn_cast<ctjs::BinaryOp>(operation);
    // Arithmetic on proved Numbers cannot coerce or call back into script.
    // Its result may also become attribute text; String-index uses still
    // need their own bounds proof below.
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
        (binary && ctjs::isArithmetic(binary.getKind()) &&
         (binary.getKind() != ctjs::BinaryKind::Add ||
          (binary.getLhs().getDefiningOp<ctjs::UnaryOp>() &&
           numericIndexLiteral(binary.getLhs())) ||
          (binary.getRhs().getDefiningOp<ctjs::UnaryOp>() &&
           numericIndexLiteral(binary.getRhs()))))) {
        if (!spend()) { return false; }
        const auto result = operation.getResult(0);
        if (!stringIndex(result)) {
            return fail(
                "DOM String indexing negation/division/subtraction/multiplication/remainder/power "
                "requires at most two arithmetic levels over Number or negated primitive literals");
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
                    // scalar Add takes its own path and skips this use scan.
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
                    return fail("DOM String indexing literal expression escapes its bounds");
                }
                ++bounds;
            }
            if (!bounds) { return fail("DOM String indexing literal expression has no bound use"); }
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
        // The DOM provider supplies the initial `undefined` binding, and the
        // whole-module scan of every operation has already ruled out
        // reassignment and script reentry.
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
        if (hasKind(read.getObject(), Kind::document) && key == "documentElement") {
            values[read.getResult()] = documentRootPresent ? Kind::element : Kind::nullableElement;
            proved.documentRoots.insert(read);
            return true;
        }
        for (const MemberRow & row : memberRows) {
            if (hasKind(read.getObject(), row.receiver) && key == row.key &&
                (row.needs.empty() || supplied(row.needs))) {
                values[read.getResult()] = row.kind;
                proved.methods.try_emplace(read, row.method);
                return true;
            }
        }
        // Bound lookup work by the longest well-known symbol name (18,
        // isConcatSpreadable).
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
                        // min(snapshot.length, 2^24) also bounds accesses to
                        // the snapshot. 2^24 is the VM's spread
                        // materialisation cap (see cappedLength in
                        // DOM/Snapshot/Bounds.h).
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
        // Bound hashing by the longest supported String member (11,
        // toLowerCase).
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
            // ponytail: accepts only keys read directly from a dataset
            // snapshot of the same element; refuses joined or transformed
            // keys until a separate provenance proof exists.
            if (!dataset || origin == keyOrigins.end() ||
                origin->second.element != dataset.getObject() ||
                origin->second.epoch != mutationEpoch ||
                datasetEpochs.lookup(read.getObject()) != mutationEpoch) {
                return fail("DOM dataset value requires a live member key from the same "
                            "element");
            }
            proved.datasetValues.try_emplace(read, dataset.getObject());
            values[read.getResult()] = Kind::string;
            proved.strings.push_back(read.getResult());
            return true;
        }
        if (hasKind(read.getObject(), Kind::element) && key == "classList") {
            values[read.getResult()] = Kind::tokenList;
            proved.tokenLists.push_back(read);
            return true;
        }
        // Skip keys longer than the longest supported member (16,
        // querySelectorAll) so unknown long keys cannot be hashed outside the
        // work budget.
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
        return fail("DOM property read lacks a proved receiver and supported member");
    }
    if (auto invoke = llvm::dyn_cast<ctjs::CallOp>(operation)) {
        auto arguments = invoke.getArgs();
        // Only two RegExps are supported, both matching Bootstrap's source:
        // /^bs/ with no flags (stripping the `bs` prefix of data-bs-* keys) and
        // /[A-Z]/g (one uppercase unit replaced by a callback, for camelCase
        // to kebab-case).
        if (hasKind(invoke.getCallee(), Kind::regexpFactory)) {
            const bool uppercase = arguments.size() == 2 &&
                                   ctjs::constantKey(arguments[0]) == "[A-Z]" &&
                                   ctjs::constantKey(arguments[1]) == "g";
            if (!hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() != 2 ||
                (!uppercase &&
                 (ctjs::constantKey(arguments[0]) != "^bs" || !emptyString(arguments[1])))) {
                return fail("DOM prefix removal requires the original /^bs/ literal");
            }
            unsigned uses = 0;
            for (mlir::OpOperand & use : invoke.getResult().getUses()) {
                if (!spend()) { return false; }
                if (llvm::isa<ctjs::RootOp>(use.getOwner())) { continue; }
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                if (!call || use.getOperandNumber() != 2 || call.getArgs().size() != 2) {
                    return fail("DOM prefix RegExp escapes its single replacement");
                }
                ++uses;
            }
            if (uses != 1) { return fail("DOM prefix RegExp requires one confined replacement"); }
            values[invoke.getResult()] = uppercase ? Kind::uppercaseRegExp : Kind::prefixRegExp;
            proved.stringPrefixRegExps.push_back(invoke);
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::uriIntrinsic)) {
            auto parent = llvm::dyn_cast<ctjs::InvokeOp>(invoke->getParentOp());
            if (!parent || invoke->getParentRegion() != &parent.getBody() ||
                !hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() != 1 ||
                !hasKind(arguments[0], Kind::string)) {
                return fail("URI call requires explicit identity, an undefined receiver, "
                            "one String and preserved failure continuation");
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
                return fail("JSON.parse call requires the initial JSON receiver, one "
                            "String and preserved failure continuation");
            }
            proved.calls.push_back({invoke, HostDOMMethod::jsonParse, arguments[0]});
            values[invoke.getResult()] = Kind::json;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::symbolIntrinsic)) {
            // No ToPrimitive/ToString hook can run for a String or undefined
            // description. Other descriptions are refused; they would need a
            // coercion and exception proof.
            if (!hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() > 1 ||
                (!arguments.empty() && !hasKind(arguments[0], {Kind::string, Kind::undefined}))) {
                return fail("Symbol call requires its initial builtin, undefined receiver "
                            "and at most one String/undefined description");
            }
            proved.calls.push_back({invoke, HostDOMMethod::symbol, {}});
            values[invoke.getResult()] = Kind::symbol;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::numberIntrinsic)) {
            if (!hasKind(invoke.getReceiver(), Kind::undefined) || arguments.size() != 1 ||
                !hasKind(arguments[0], {Kind::string, Kind::null, Kind::optionalString,
                                        Kind::boolean, Kind::number})) {
                return fail("Number call requires its initial builtin, undefined "
                            "receiver and one String/null/Boolean/Number input");
            }
            proved.calls.push_back({invoke, HostDOMMethod::number, arguments[0]});
            values[invoke.getResult()] = Kind::number;
            return true;
        }
        auto method = invoke.getCallee().getDefiningOp<ctjs::GetPropertyOp>();
        if (!method || method.getObject() != invoke.getReceiver()) {
            return fail("DOM call does not preserve its proved method receiver");
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
            // The selector call itself does the checked js_element_t
            // construction. A documentElement read may be absent; it proves
            // nothing about other uses, and earlier source effects still run.
            const bool checkedRoot =
                arguments.size() == 2 && hasKind(arguments[0], Kind::nullableElement) &&
                proved.documentRoots.contains(arguments[0].getDefiningOp<ctjs::GetPropertyOp>());
            if (arguments.size() != 2 || (!hasKind(arguments[0], Kind::element) && !checkedRoot) ||
                !hasKind(arguments[1], Kind::string)) {
                return fail("DOM prototype selector call requires an explicit Element or checked "
                            "document root and String");
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
                return fail("DOM String indexing permits an optional index and optional slice end");
            }
            for (mlir::Value argument : arguments) {
                if (!spend()) { return false; }
                auto literal = argument.getDefiningOp<ctjs::ConstantOp>();
                const bool primitive =
                    literal && llvm::isa<ctjs::NullAttr, ctjs::BooleanAttr>(literal.getValue());
                if (!primitive && !hasKind(argument, Kind::undefined) && !stringIndex(argument)) {
                    return fail("DOM String indexing requires primitive literals, proved undefined "
                                "or at most two arithmetic levels over Number or negated primitive "
                                "literals");
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
            // The /[A-Z]/g pattern hands the callback exactly one ASCII
            // uppercase character, never an arbitrary String.
            proved.calls.push_back(
                {invoke, HostDOMMethod::replaceUppercase, invoke.getReceiver(), callback});
            values[invoke.getResult()] = Kind::string;
            return true;
        }
        if (hasKind(invoke.getCallee(), Kind::replacePrefix) && arguments.size() == 2 &&
            hasKind(arguments[0], Kind::prefixRegExp) && emptyString(arguments[1])) {
            // ponytail: accepts only the ASCII /^bs/ with no flags, which needs
            // no regex engine; other patterns are refused until they have
            // their own proof. The original String, RegExp and factory fix
            // @@replace, exec and the flag accessors; the whole-module scan
            // has ruled out mutation and reentry, and the literal has no
            // observable state.
            proved.calls.push_back(
                {invoke, HostDOMMethod::removeStringPrefix, invoke.getReceiver()});
            values[invoke.getResult()] = Kind::string;
            auto key = invoke.getReceiver().getDefiningOp<ctjs::GetPropertyOp>();
            if (!spend() || !spend()) { return false; }
            if (key && keyOrigins.contains(key.getResult()) &&
                prefixSnapshots.contains(key.getObject())) {
                // Removing a prefix every key is known to have keeps keys
                // distinct. This is not dataset membership: the new key does
                // not name the original dataset property.
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
            // ponytail: accepts ASCII literal prefixes with at most two
            // concatenation levels, which keeps the proof bounded; refuses
            // other prefixes until they have origin and UTF-16 proofs.
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
                    return fail("DOM startsWith requires ASCII literals with at most two "
                                "concatenation levels");
                }
                for (unsigned char c : text.getValue()) {
                    if (!spend()) { return false; }
                    if (c > 127) { return fail("DOM startsWith requires an ASCII prefix"); }
                }
            }
            proved.calls.push_back({invoke, HostDOMMethod::startsWith, invoke.getReceiver()});
            values[invoke.getResult()] = Kind::boolean;
            auto text = ctjs::constantAttr<ctjs::StringAttr>(arguments[0]);
            // Only a literal "bs..." test on the callback's input marks the
            // result as requiring Bootstrap's data-bs-* prefix; computed
            // prefixes prove nothing about dataset keys.
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
                return fail("DOM dataset enumeration crosses a source mutation");
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
            const auto hostMethod = proved.methods.find(method)->second;
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
                return fail("DOM classList.contains requires one String");
            }
            for (mlir::Value argument : arguments) {
                if (!spend()) { return false; }
                if (!hasKind(argument, Kind::string)) {
                    return fail("DOM classList arguments require proved Strings");
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
            // getAttribute returns the attribute's String or null; it never
            // falls back to a prototype.
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
        return fail("DOM call arguments lack the supported primitive contract");
    }
    return std::nullopt;
}

} // namespace ctcompile::ctnative::dom_entry_detail
