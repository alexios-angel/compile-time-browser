//===- Vectors.cpp - Vectors of one proved element type -------------------===//
//
// The local proof behind arrays that lower to `std::vector<T>` for one proved
// element carrier T (String today): which values name one such array, which
// calls on it the native runtime implements, and what is stored into it.
// TypeInference types the array from the stores; admission and the emitter
// ask the same group query, so the three cannot disagree about a use.
//
// The dense numeric proof (Shapes.cpp, denseVectorAliases) is separate and
// unchanged. This one allows the Array and String methods css-dump.js uses
// (push, join, sort and split) and refuses writes through an index or to
// `length`, which the dense proof allows.
//
//===----------------------------------------------------------------------===//

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/TypeInference.h"

#include "mlir/Dialect/SCF/IR/SCF.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"

#include <cstddef>
#include <optional>
#include <string>

namespace ctcompile::ctnative {

namespace {

/// The text of a string constant, or empty when `value` is not one. Unlike
/// constantKey, this tells the empty string from a non-constant.
std::optional<llvm::StringRef> stringConstantText(mlir::Value value) {
    auto text = ctjs::constantAttr<ctjs::StringAttr>(value);
    if (!text) { return std::nullopt; }
    return text.getValue();
}

/// Whether `op` builds the regular expression `/\s+/` (no flags): a call of
/// the importer's literal factory with exactly those two constant strings.
bool isWhitespaceRegExp(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    if (!call || ctjs::calleeGlobalName(call) != "__ctbrowser_regexp" ||
        call.getArgs().size() != 2) {
        return false;
    }
    const auto pattern = stringConstantText(call.getArgs()[0]);
    const auto flags = stringConstantText(call.getArgs()[1]);
    return pattern && *pattern == "\\s+" && flags && flags->empty();
}

/// Whether `call` is `receiver.key(...)` with exactly that read as its callee
/// and nothing else reading the method.
bool isMethodCall(ctjs::CallOp call, llvm::StringRef key) {
    auto read = ctjs::receiverMethod(call);
    return read && read.getObject() == call.getReceiver() && ctjs::methodKey(call) == key &&
           read.getResult().hasOneUse();
}

} // namespace

TypeInference::VectorCall TypeInference::vectorCallOf(mlir::Operation * op) {
    auto call = llvm::dyn_cast_or_null<ctjs::CallOp>(op);
    if (!call) { return VectorCall::none; }
    if (ctjs::calleeGlobalName(call) == "__ctbrowser_regexp") { return VectorCall::regexp; }
    const std::size_t arity = call.getArgs().size();
    if (arity == 1 && isMethodCall(call, "push")) { return VectorCall::push; }
    if (arity <= 1 && isMethodCall(call, "join")) { return VectorCall::join; }
    if (arity == 0 && isMethodCall(call, "sort")) { return VectorCall::sort; }
    if (arity == 1 && isMethodCall(call, "split")) {
        mlir::Operation * separator = call.getArgs()[0].getDefiningOp();
        if (isWhitespaceRegExp(separator)) { return VectorCall::splitSpace; }
        // Any other RegExp separator is not a String; admission refuses the
        // RegExp itself by name.
        return VectorCall::split;
    }
    return VectorCall::none;
}

llvm::SmallVector<mlir::Value, 4> TypeInference::elementVectorAliases(mlir::Value array,
                                                                      std::string * why) {
    const auto fail = [&](const llvm::Twine & reason) {
        if (why) { *why = reason.str(); }
        return llvm::SmallVector<mlir::Value, 4>{};
    };
    if (!array || !array.getParentRegion()) { return fail("not a value in a function"); }
    auto function = array.getParentRegion()->getParentOfType<ctjs::FuncOp>();
    llvm::SmallVector<mlir::Value, 4> members{array};
    llvm::DenseSet<mlir::Value> seen{array};
    llvm::SmallVector<mlir::Operation *> owners;
    bool borrowed = false;
    std::size_t work = 0;
    const auto add = [&](mlir::Value value) {
        if (seen.insert(value).second) { members.push_back(value); }
    };
    // scf transport, as in denseVectorAliases: every value a member flows
    // into, and every value that flows into a member, names the same array.
    const auto before = [&](mlir::scf::WhileOp loop, unsigned index) {
        borrowed = true;
        add(loop.getInits()[index]);
        add(loop.getBeforeArguments()[index]);
        add(loop.getYieldOp().getOperand(index));
    };
    const auto after = [&](mlir::scf::WhileOp loop, unsigned index) {
        borrowed = true;
        add(loop.getConditionOp().getArgs()[index]);
        add(loop.getAfterArguments()[index]);
        add(loop.getResult(index));
    };
    for (std::size_t at = 0; at < members.size(); ++at) {
        // ponytail: one bounded walk per query, recomputed by every caller;
        // cache per solve if profiling shows it.
        if (++work > 65536) { return fail("an array whose alias group is too large to check"); }
        mlir::Value member = members[at];
        if (member.getParentRegion()->getParentOfType<ctjs::FuncOp>() != function) {
            return fail("an array that crosses into another function");
        }
        mlir::Operation * definition = member.getDefiningOp();
        if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(member)) {
            auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(argument.getOwner()->getParentOp());
            if (!loop) {
                return fail("an array passed as a parameter (not native for arrays of Strings "
                            "yet)");
            }
            if (&loop.getBefore() == argument.getOwner()->getParent()) {
                before(loop, argument.getArgNumber());
            } else {
                after(loop, argument.getArgNumber());
            }
        } else if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(definition)) {
            after(loop, llvm::cast<mlir::OpResult>(member).getResultNumber());
        } else if (auto selected = llvm::dyn_cast<mlir::scf::IfOp>(definition)) {
            borrowed = true;
            const unsigned index = llvm::cast<mlir::OpResult>(member).getResultNumber();
            add(selected.thenYield().getOperand(index));
            add(selected.elseYield().getOperand(index));
        } else if (llvm::isa<ctjs::CreateArrayOp>(definition)) {
            owners.push_back(definition);
        } else if (const VectorCall made = vectorCallOf(definition);
                   made == VectorCall::split || made == VectorCall::splitSpace) {
            owners.push_back(definition);
        } else if (made == VectorCall::sort) {
            // `a.sort()` answers `a` itself.
            add(llvm::cast<ctjs::CallOp>(definition).getReceiver());
        } else if (ctjs::isUndefinedConstant(member)) {
            // The importer threads a `var` through loops and branches with
            // its hoisted `undefined`, so an array made inside one meets
            // undefined at the join.
            return fail("an array created inside a branch or a loop whose variable is carried "
                        "out of it - its storage has to be one frame slot");
        } else {
            return fail("an array that is not a literal, a split result or a sort of one");
        }
        for (mlir::OpOperand & use : member.getUses()) {
            if (++work > 65536) { return fail("an array whose alias group is too large to check"); }
            mlir::Operation * user = use.getOwner();
            if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(user)) {
                before(loop, use.getOperandNumber());
                continue;
            }
            if (auto condition = llvm::dyn_cast<mlir::scf::ConditionOp>(user)) {
                auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(condition->getParentOp());
                if (!loop || use.getOperandNumber() == 0) {
                    return fail("an array used as a loop condition");
                }
                after(loop, use.getOperandNumber() - 1);
                continue;
            }
            if (auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(user)) {
                if (auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(yield->getParentOp())) {
                    before(loop, use.getOperandNumber());
                    continue;
                }
                auto selected = llvm::dyn_cast<mlir::scf::IfOp>(yield->getParentOp());
                if (!selected || use.getOperandNumber() >= selected.getNumResults()) {
                    return fail("an array yielded out of a region that is not an if or a loop");
                }
                borrowed = true;
                add(selected.getResult(use.getOperandNumber()));
                continue;
            }
            if (llvm::isa<ctjs::AppendOp>(user)) {
                // Operand 1 is the element: an array appended into another
                // array has escaped into it.
                if (use.getOperandNumber() != 0 || !member.getDefiningOp<ctjs::CreateArrayOp>()) {
                    return fail("an array stored into another array");
                }
                continue;
            }
            if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(user)) {
                if (use.getOperandNumber() != 0) { return fail("an array used as a property key"); }
                const llvm::StringRef key = ctjs::constantKey(read.getKey());
                // An empty key is an index, which TypeInference requires to be
                // a proved Number before it types the read.
                if (key.empty() || key == "length") { continue; }
                if (key == "push" || key == "join" || key == "sort") {
                    // The read has to be the callee of one call on this
                    // array; vectorCallOf checks that it has no other use.
                    if (!read.getResult().hasOneUse()) {
                        return fail(
                            ("an array whose method `" + key + "` is read as a value").str());
                    }
                    auto * call = *read.getResult().getUsers().begin();
                    const VectorCall kind = vectorCallOf(call);
                    if (kind == VectorCall::none ||
                        llvm::cast<ctjs::CallOp>(call).getReceiver() != member) {
                        return fail(("an array whose method `" + key +
                                     "` is called with arguments the native runtime does not "
                                     "implement")
                                        .str());
                    }
                    continue;
                }
                return fail(("an array read through the named property `" + key + "`").str());
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(user)) {
                const VectorCall kind = vectorCallOf(call);
                if (use.getOperandNumber() == 1 &&
                    (kind == VectorCall::push || kind == VectorCall::join ||
                     kind == VectorCall::sort)) {
                    if (kind == VectorCall::sort) { add(call.getResult()); }
                    continue;
                }
                return fail("an array passed to a call the native runtime does not implement");
            }
            if (llvm::isa<ctjs::CallDirectOp>(user)) {
                return fail("an array passed as an argument (not native for arrays of Strings "
                            "yet)");
            }
            if (llvm::isa<ctjs::ReturnOp>(user)) {
                return fail("an array returned from a function (not native for arrays of "
                            "Strings yet)");
            }
            if (llvm::isa<ctjs::SetPropertyOp>(user)) {
                return fail("an array written through an index or its `length`");
            }
            return fail(("an array that reaches `" + user->getName().getStringRef() + "`").str());
        }
    }
    // A value carried through scf control flow is the address of its owner, so
    // the owner has to live in the function's entry block. An array used only
    // inside the region that made it needs no such slot.
    if (borrowed) {
        if (!function) { return fail("an array outside a function"); }
        for (mlir::Operation * owner : owners) {
            if (owner->getBlock() != &function.getBody().front()) {
                return fail("an array created inside a branch or a loop and carried out of it - "
                            "its storage has to be one frame slot");
            }
        }
    }
    return members;
}

llvm::SmallVector<mlir::Value, 4> TypeInference::elementVectorStores(
    llvm::ArrayRef<mlir::Value> members) {
    llvm::SmallVector<mlir::Value, 4> stores;
    for (mlir::Value member : members) {
        if (auto literal = member.getDefiningOp<ctjs::CreateArrayOp>()) {
            llvm::append_range(stores, literal.getElements());
        }
        for (mlir::OpOperand & use : member.getUses()) {
            if (auto append = llvm::dyn_cast<ctjs::AppendOp>(use.getOwner());
                append && use.getOperandNumber() == 0) {
                stores.push_back(append.getElement());
            }
            if (auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                call && use.getOperandNumber() == 1 && vectorCallOf(call) == VectorCall::push) {
                stores.push_back(call.getArgs()[0]);
            }
        }
    }
    return stores;
}

bool TypeInference::splitsAString(llvm::ArrayRef<mlir::Value> members) {
    return llvm::any_of(members, [](mlir::Value member) {
        const VectorCall made = vectorCallOf(member.getDefiningOp());
        return made == VectorCall::split || made == VectorCall::splitSpace;
    });
}

} // namespace ctcompile::ctnative
