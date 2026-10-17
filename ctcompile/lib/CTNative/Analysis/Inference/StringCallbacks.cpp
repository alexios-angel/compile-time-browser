//===- StringCallbacks.cpp - String.prototype.replace with a function ----===//
//
// `text.replace(/pattern/g, fn)`: closure lifting
// (Lowering/ClosureLifting/StringCallbacks.cpp) specialises the closure for
// its one call, which then names the callback by kNativeStringCallback and
// passes the callback's captured values after the RegExp literal. No
// ctjs.call_direct reaches the callback, so its parameters are typed here by
// the call: its captures from the call's operands, then the matched text and
// each group, all Strings.
//
// `text.toLowerCase()` on a proved String is recognised here too, since the
// replacement functions this serves (Bootstrap's normalizeDataKey) call it.
//
// The pattern check is shared by the lift and admission: the native runtime
// runs the pattern with std::regex (Runtime/RegExp.hpp), and only a subset of
// patterns means the same there as in JavaScript over WTF-8 text.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"
#include "ctcompile/CTNative/IR/CTNativeDialect.h"

#include "mlir/IR/SymbolTable.h"

#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

namespace ctcompile::ctnative {

namespace {

/// The callback a specialised call names, or null.
ctjs::FuncOp callbackOf(ctjs::CallOp call) {
    auto name = call->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeStringCallback);
    return name ? mlir::SymbolTable::lookupNearestSymbolFrom<ctjs::FuncOp>(call, name)
                : ctjs::FuncOp{};
}

/// The text of a constant String operand, or none.
std::optional<std::string> constantText(mlir::Value value) {
    auto text = ctjs::constantAttr<ctjs::StringAttr>(value);
    if (!text) { return std::nullopt; }
    return text.getValue().str();
}

/// The pattern and flags of the RegExp `value` makes when it is a literal or
/// `new RegExp(source)` of the realm's RegExp with a constant String source
/// (no flags), and when its only uses are the read of `test` and the call of
/// it, `call`. None otherwise.
std::optional<std::pair<std::string, std::string>> testedRegExp(mlir::Value value,
                                                                ctjs::CallOp call) {
    if (!llvm::all_of(value.getUsers(), [&](mlir::Operation * user) {
            return user == call.getOperation() || user == call.getCallee().getDefiningOp();
        })) {
        return std::nullopt;
    }
    if (auto literal = TypeInference::regExpLiteral(value)) { return literal; }
    auto made = value.getDefiningOp<ctjs::ConstructOp>();
    if (!made || ctjs::calleeGlobalName(made) != "RegExp" || made.getArgs().size() != 1 ||
        !TypeInference::isRealmGlobal(made.getCallee().getDefiningOp(), "RegExp")) {
        return std::nullopt;
    }
    auto source = constantText(made.getArgs()[0]);
    if (!source) { return std::nullopt; }
    return std::pair{*source, std::string{}};
}

/// The marked `test` call that consumes the RegExp `value`, or null.
ctjs::CallOp testCallOf(mlir::Value value) {
    for (mlir::Operation * user : value.getUsers()) {
        auto call = llvm::dyn_cast<ctjs::CallOp>(user);
        if (call && call.getReceiver() == value && TypeInference::isStringMethodCall(call) &&
            call->getAttrOfType<mlir::StringAttr>(kNativeStringMethod) == "test") {
            return call;
        }
    }
    return {};
}

} // namespace

std::optional<std::pair<std::string, std::string>> TypeInference::regExpLiteral(mlir::Value value) {
    auto call = value.getDefiningOp<ctjs::CallOp>();
    if (!call || ctjs::calleeGlobalName(call) != "__ctbrowser_regexp" ||
        call.getArgs().size() != 2) {
        return std::nullopt;
    }
    auto pattern = constantText(call.getArgs()[0]);
    auto flags = constantText(call.getArgs()[1]);
    if (!pattern || !flags) { return std::nullopt; }
    return std::pair{*pattern, *flags};
}

std::string TypeInference::replacePatternProblem(llvm::StringRef pattern, llvm::StringRef flags,
                                                 unsigned declared) {
    for (char flag : flags) {
        if (flag != 'g') {
            return std::string{"with the flag `"} + flag +
                   "` - only `g` means the same to std::regex";
        }
    }
    unsigned groups = 0;
    bool alternation = false;
    bool inClass = false;
    char previous = '|'; // An alternative may not start empty.
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (static_cast<unsigned char>(ch) >= 0x80) {
            return "with a non-ASCII pattern - std::regex reads it byte by byte";
        }
        // A control escape (\t, \n, \v, \f, \r) is one ASCII character to both
        // engines, inside a class or out.
        const bool controlEscape = ch == '\\' && i + 1 < pattern.size() &&
                                   llvm::StringRef{"tnvfr"}.contains(pattern[i + 1]);
        if (controlEscape) {
            previous = pattern[++i];
            continue;
        }
        if (inClass) {
            if (ch == '\\') { return "with an escape inside a class"; }
            if (ch == ']') { inClass = false; }
            previous = ch;
            continue;
        }
        switch (ch) {
        case '\\': {
            const char next = i + 1 < pattern.size() ? pattern[i + 1] : '\0';
            if (std::isalnum(static_cast<unsigned char>(next)) || next == '\0') {
                return std::string{"with `\\"} + next +
                       "` - its meaning depends on Unicode in JavaScript and on the locale in "
                       "std::regex";
            }
            ++i;
            previous = next;
            continue;
        }
        case '.': return "with `.` - it matches one UTF-16 unit in JavaScript and one byte here";
        case '$':
            // At the end, after an atom, `$` is the end of the input to both
            // engines (no `m` flag) and the atom before it keeps the pattern
            // from matching the empty string.
            if (i + 1 == pattern.size() && previous != '|' && previous != '(') {
                previous = 'a';
                continue;
            }
            [[fallthrough]];
        case '^':
            return std::string{"with the anchor `"} + ch + "` - it can match the empty string";
        case '*':
        case '?':
        case '{':
            return std::string{"with `"} + ch +
                   "` - a pattern that can match the empty string advances differently";
        case '[':
            if (i + 1 < pattern.size() && pattern[i + 1] == '^') {
                return "with a negated class - it matches one byte of a multibyte character";
            }
            inClass = true;
            break;
        case '(':
            if (i + 1 < pattern.size() && pattern[i + 1] == '?') {
                if (pattern.substr(i, 3) != "(?:") { return "with a lookaround or named group"; }
                i += 2;
            } else {
                ++groups;
            }
            break;
        case '|':
        case ')':
            if (previous == '|' || previous == '(') {
                return "with an empty alternative - it matches the empty string";
            }
            alternation |= ch == '|';
            break;
        default: break;
        }
        previous = ch;
    }
    if (pattern.empty() || previous == '|') {
        return "with a pattern that matches the empty string";
    }
    if (declared > 1 + groups) {
        return "whose callback declares " + std::to_string(declared) +
               " parameters - only the match and its groups are passed, since the position "
               "counts UTF-16 units";
    }
    if (declared > 1 && alternation) {
        return "whose callback reads a group of a pattern with alternatives - a group may not "
               "take part in a match";
    }
    return {};
}

std::string TypeInference::matchGroupProblem(llvm::StringRef pattern, llvm::StringRef flags,
                                             unsigned group) {
    if (!flags.empty() && flags != "i") {
        return "with the flags `" + flags.str() + "` - only `i` means the same to std::regex";
    }
    // The text is ASCII, so std::regex reads each class, escape and `i` as
    // JavaScript does; what is left is whether the group takes part in every
    // match, which an alternative or an optional quantifier breaks.
    unsigned groups = 0;
    bool inClass = false;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char ch = pattern[i];
        if (static_cast<unsigned char>(ch) >= 0x80) {
            return "with a non-ASCII pattern - std::regex reads it byte by byte";
        }
        if (ch == '\\') {
            ++i;
            continue;
        }
        if (inClass) {
            inClass = ch != ']';
            continue;
        }
        if (ch == '[') {
            inClass = true;
        } else if (ch == '(') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '?') {
                return "with a lookaround or a group that does not capture";
            }
            ++groups;
        } else if (ch == '|' || ch == '?' || ch == '*' || ch == '{') {
            return std::string{"with `"} + ch + "` - a group may not take part in a match";
        }
    }
    if (group < 1 || group > groups) {
        return "reading group " + std::to_string(group) + " of a pattern with " +
               std::to_string(groups);
    }
    return {};
}

bool TypeInference::isStringCallbackCall(mlir::Operation * op) {
    return llvm::isa_and_nonnull<ctjs::CallOp>(op) &&
           op->getAttrOfType<mlir::FlatSymbolRefAttr>(kNativeStringCallback);
}

void TypeInference::markStringMethodCalls(mlir::Operation * top) {
    // `Object.prototype.toString.call(x).match(/re/flags)[k]`, Bootstrap's
    // toType: the match and its RegExp literal are used for nothing else.
    top->walk([](ctjs::GetPropertyOp group) {
        auto index = ctjs::constantAttr<ctjs::NumberAttr>(group.getKey());
        auto match = group.getObject().getDefiningOp<ctjs::CallOp>();
        if (!index || !match || !match.getResult().hasOneUse() ||
            ctjs::methodKey(match) != "match" || match.getArgs().size() != 1 ||
            !match.getArgs()[0].hasOneUse()) {
            return;
        }
        auto read = ctjs::receiverMethod(match);
        const auto * row = builtinStaticOf(match.getReceiver().getDefiningOp());
        const auto literal = regExpLiteral(match.getArgs()[0]);
        const double k = index.getDouble();
        if (!read || read.getObject() != match.getReceiver() || !read.getResult().hasOneUse() ||
            !row || row->global != "Object" || row->member != "toString" || !literal ||
            !(k >= 1 && k <= 9 && k == std::floor(k))) {
            return;
        }
        mlir::MLIRContext * c = group.getContext();
        group->setAttr(kNativeMatchGroup, index);
        group->setAttr("ctnative.replace_pattern", mlir::StringAttr::get(c, literal->first));
        group->setAttr("ctnative.replace_flags", mlir::StringAttr::get(c, literal->second));
    });
    top->walk([](ctjs::CallOp call) {
        auto read = ctjs::receiverMethod(call);
        const llvm::StringRef name = ctjs::methodKey(call);
        if (read && read.getObject() == call.getReceiver() && read.getResult().hasOneUse() &&
            call.getArgs().empty() && (name == "toLowerCase" || name == "trim")) {
            call->setAttr(kNativeStringMethod, mlir::StringAttr::get(call.getContext(), name));
        }
        // `text.replace(/pattern/, replacement)` with a replacement that is
        // not a function: closure lifting specialises the function case.
        if (read && read.getObject() == call.getReceiver() && read.getResult().hasOneUse() &&
            name == "replace" && call.getArgs().size() == 2 &&
            !call->hasAttr(kNativeStringCallback) &&
            !call.getArgs()[1].getDefiningOp<ctjs::CreateClosureOp>() &&
            call.getArgs()[0].hasOneUse()) {
            if (const auto literal = regExpLiteral(call.getArgs()[0])) {
                // The source, kept for the emitter: the literal's constant
                // operands are lowered before the call is.
                mlir::MLIRContext * c = call.getContext();
                call->setAttr(kNativeStringMethod, mlir::StringAttr::get(c, name));
                call->setAttr("ctnative.replace_pattern", mlir::StringAttr::get(c, literal->first));
                call->setAttr("ctnative.replace_flags", mlir::StringAttr::get(c, literal->second));
            }
        }
        // `new RegExp(source).test(text)` with a constant source, as Config's
        // type check tests a type name against its table's pattern once the
        // table loop is unrolled (Inference/TableLoops.cpp).
        if (read && read.getObject() == call.getReceiver() && read.getResult().hasOneUse() &&
            name == "test" && call.getArgs().size() == 1) {
            if (const auto made = testedRegExp(call.getReceiver(), call)) {
                mlir::MLIRContext * c = call.getContext();
                call->setAttr(kNativeStringMethod, mlir::StringAttr::get(c, name));
                call->setAttr("ctnative.replace_pattern", mlir::StringAttr::get(c, made->first));
                call->setAttr("ctnative.replace_flags", mlir::StringAttr::get(c, made->second));
            }
        }
    });
}

bool TypeInference::isStringMethodCall(mlir::Operation * op) {
    return llvm::isa_and_nonnull<ctjs::CallOp>(op) && op->hasAttr(kNativeStringMethod);
}

bool TypeInference::consumedByStringCallback(mlir::Operation * op) {
    if (!op || op->getNumResults() != 1) { return false; }
    // The RegExp a marked `test` call runs, and the RegExp global it is made
    // from (the constructor's callee and new.target, or the literal factory).
    if (testCallOf(op->getResult(0))) { return true; }
    if (llvm::isa<ctjs::LoadGlobalOp>(op) && !op->getResult(0).use_empty() &&
        llvm::all_of(op->getResult(0).getUses(), [](mlir::OpOperand & use) {
            mlir::Operation * user = use.getOwner();
            return (llvm::isa<ctjs::ConstructOp>(user) || use.getOperandNumber() == 0) &&
                   user->getNumResults() == 1 && testCallOf(user->getResult(0));
        })) {
        return true;
    }
    if (!op->getResult(0).hasOneUse()) { return false; }
    mlir::OpOperand & use = *op->getResult(0).getUses().begin();
    mlir::Operation * user = use.getOwner();
    // A match whose group read is marked, and the method read and RegExp
    // literal it consumes; its receiver lowers on its own.
    if (user->hasAttr(kNativeMatchGroup)) { return llvm::isa<ctjs::CallOp>(op); }
    if (auto match = llvm::dyn_cast<ctjs::CallOp>(user);
        match && match.getResult().hasOneUse() &&
        match.getResult().getUsers().begin()->hasAttr(kNativeMatchGroup)) {
        return use.getOperandNumber() == 0 || use.getOperandNumber() == 2;
    }
    // The method read is operand 0 and the RegExp literal operand 2 of the
    // specialised call; the literal factory's global is operand 0 of the
    // literal.
    if (isStringCallbackCall(user)) {
        return use.getOperandNumber() == 0 || use.getOperandNumber() == 2;
    }
    // A marked method call consumes its method read, and replace its RegExp
    // literal too.
    if (isStringMethodCall(user)) {
        return use.getOperandNumber() == 0 ||
               (use.getOperandNumber() == 2 &&
                user->getAttrOfType<mlir::StringAttr>(kNativeStringMethod) == "replace");
    }
    return llvm::isa<ctjs::LoadGlobalOp>(op) && use.getOperandNumber() == 0 &&
           consumedByStringCallback(user);
}

bool TypeInference::isStringCallbackParameter(mlir::Value value) {
    auto argument = llvm::dyn_cast<mlir::BlockArgument>(value);
    if (!argument || !argument.getOwner()->isEntryBlock() ||
        argument.getArgNumber() < ctjs::implicit_arguments) {
        return false;
    }
    auto function = llvm::dyn_cast<ctjs::FuncOp>(argument.getOwner()->getParentOp());
    return function && function->hasAttr(kNativeStringCallback);
}

mlir::Type TypeInference::stringCallbackType(mlir::Operation * op,
                                             llvm::ArrayRef<const TypeLattice *> operands,
                                             bool & pending) {
    mlir::MLIRContext * c = op->getContext();
    auto call = llvm::cast<ctjs::CallOp>(op);
    ctjs::FuncOp callback = callbackOf(call);
    if (!callback || callback.getBody().empty()) { return {}; }
    mlir::Block & entry = callback.getBody().front();
    const auto pass = [&](unsigned index, mlir::Type type) {
        if (!type || index >= entry.getNumArguments()) { return; }
        TypeLattice * lattice = getLatticeElement(entry.getArgument(index));
        propagateIfChanged(lattice, lattice->join(TypeValue{type}));
    };
    // Operands 0, 1 and 2 are the method, the receiver and the RegExp.
    const auto captures = static_cast<unsigned>(call.getArgs().size()) - 1;
    for (unsigned i = 0; i < captures; ++i) {
        pass(ctjs::implicit_arguments + i, operands[3 + i]->getValue().getType());
    }
    const mlir::Type receiver = operands[1]->getValue().getType();
    if (!receiver) {
        pending = true;
        return {};
    }
    const bool text = llvm::isa<StrType, StrViewType>(receiver);
    for (unsigned i = ctjs::implicit_arguments + captures; i < entry.getNumArguments(); ++i) {
        pass(i, text ? mlir::Type(defaultStringType(c)) : mlir::Type(BoxedType::get(c)));
    }
    // Not a String receiver: admission refuses the call by its type.
    return text ? mlir::Type(defaultStringType(c)) : mlir::Type{};
}

} // namespace ctcompile::ctnative
