//===- Vectors.cpp - Vectors of one proved element type -------------------===//
//
// The local proof behind arrays that lower to `std::vector<T>` for one proved
// element carrier T (a String or a two-element row today): which values name
// one such array, which calls on it the native runtime implements, and what is
// stored into it. Also the tuple literal (a row) and the bounded-loop index
// read.
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

#include <cmath>
#include <cstddef>
#include <cstdint>
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
    if (isArrayCallbackCall(call)) { return VectorCall::callback; }
    if (builtinStaticOf(call)) { return VectorCall::builtinStatic; }
    if (ctjs::calleeGlobalName(call) == "__ctbrowser_regexp") { return VectorCall::regexp; }
    const std::size_t arity = call.getArgs().size();
    if (arity == 1 && isMethodCall(call, "push")) { return VectorCall::push; }
    if (arity <= 1 && isMethodCall(call, "join")) { return VectorCall::join; }
    if (arity == 0 && isMethodCall(call, "sort")) { return VectorCall::sort; }
    if (arity == 1 && isMethodCall(call, "includes")) { return VectorCall::includes; }
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
    // A parameter is a borrow of the caller's array: nothing here may return
    // it or mix it with an array this function owns.
    bool parameter = false, writes = false, returned = false;
    // An element's `children` is an HTMLCollection, which has no push, join
    // or sort, and which the native runtime snapshots (Browser.hpp): it may
    // only be read.
    bool collection = false, joined = false, lent = false;
    // A call the array is lent to; one lent twice to the same call would be
    // two borrows of one vector, and a push through one would move the other.
    llvm::DenseSet<mlir::Operation *> lentTo;
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
        // Operands 0-2 of a function are the receiver, new.target and callee;
        // a declared parameter comes after them.
        auto entryArgument = llvm::dyn_cast<mlir::BlockArgument>(member);
        if (entryArgument && function && entryArgument.getOwner() == &function.getBody().front() &&
            entryArgument.getArgNumber() >= 3) {
            parameter = true;
        } else if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(member)) {
            auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(argument.getOwner()->getParentOp());
            if (!loop) { return fail("an array carried by a block argument that is not a loop's"); }
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
        } else if (auto cases = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(definition)) {
            // A loop's exits, as the importer structures an early return
            // from inside it: one arm per exit.
            borrowed = true;
            const unsigned index = llvm::cast<mlir::OpResult>(member).getResultNumber();
            for (mlir::Region & arm : cases->getRegions()) {
                add(llvm::cast<mlir::scf::YieldOp>(arm.front().getTerminator()).getOperand(index));
            }
        } else if (llvm::isa<ctjs::CreateArrayOp, ctjs::CallDirectOp>(definition)) {
            // A literal, or the array a direct call returned by value.
            owners.push_back(definition);
        } else if (auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(definition);
                   read && ctjs::constantKey(read.getKey()) == "children") {
            collection = true;
            owners.push_back(definition);
        } else if (vectorCallOf(definition) == VectorCall::builtinStatic) {
            // querySelectorAll's snapshot (Inference/Elements.cpp): a NodeList,
            // which has no push, join or sort.
            collection = true;
            owners.push_back(definition);
        } else if (llvm::isa<ctjs::IterableOp>(definition)) {
            add(definition->getOperand(0));
        } else if (spreadCopySource(definition)) {
            // `[].concat(...a)`: a new array (Inference/Elements.cpp).
            owners.push_back(definition);
        } else if (const VectorCall made = vectorCallOf(definition);
                   made == VectorCall::split || made == VectorCall::splitSpace ||
                   (made == VectorCall::callback && arrayCallbackMakesArray(definition))) {
            owners.push_back(definition);
        } else if (made == VectorCall::sort || arrayCallbackSorts(definition)) {
            // `a.sort()` and `a.sort(f)` answer `a` itself.
            add(llvm::cast<ctjs::CallOp>(definition).getReceiver());
        } else if (ctjs::isUndefinedConstant(member)) {
            // The importer threads a `var` through loops and branches with
            // its hoisted `undefined`, so an array made inside one meets
            // undefined at the join.
            return fail("an array created inside a branch or a loop whose variable is carried "
                        "out of it - its storage has to be one frame slot");
        } else {
            return fail("an array that is not a literal, a split result, a sort of one, an "
                        "element's children, a parameter or a call result");
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
                if (auto cases = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(yield->getParentOp())) {
                    borrowed = true;
                    add(cases.getResult(use.getOperandNumber()));
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
            // The spread loop's iterator over the array, and the copy
            // `[].concat(...a)` makes of it (Inference/Elements.cpp).
            if (llvm::isa<ctjs::IterableOp>(user)) {
                add(user->getResult(0));
                continue;
            }
            if (spreadCopySource(user) == member) { continue; }
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
                if (key == "push" || key == "join" || key == "sort" || key == "includes" ||
                    isArrayCallbackMethod(key)) {
                    // The read has to be the callee of one call on this
                    // array; vectorCallOf checks that it has no other use.
                    if (!read.getResult().hasOneUse()) {
                        return fail(
                            ("an array whose method `" + key + "` is read as a value").str());
                    }
                    auto * call = *read.getResult().getUsers().begin();
                    const VectorCall kind = vectorCallOf(call);
                    writes |= kind == VectorCall::push || kind == VectorCall::sort ||
                              arrayCallbackSorts(call);
                    joined |= kind == VectorCall::join;
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
                // `a.sort(f)` sorts its receiver in place and answers it; a
                // capture of the comparator is only lent, as for the others.
                if (use.getOperandNumber() == 1 && arrayCallbackSorts(call)) {
                    writes = true;
                    add(call.getResult());
                    continue;
                }
                if (kind == VectorCall::callback &&
                    arrayCallbackBorrows(call, use.getOperandNumber())) {
                    continue;
                }
                if (use.getOperandNumber() == 1 &&
                    (kind == VectorCall::push || kind == VectorCall::join ||
                     kind == VectorCall::sort || kind == VectorCall::includes)) {
                    if (kind == VectorCall::sort) { add(call.getResult()); }
                    continue;
                }
                return fail("an array passed to a call the native runtime does not implement");
            }
            // An argument is borrowed by the callee, whose own proof makes the
            // parameter read-only; a refused callee refuses this caller too.
            if (auto call = llvm::dyn_cast<ctjs::CallDirectOp>(user);
                call && use.getOperandNumber() >= 3) {
                if (!lentTo.insert(call).second) {
                    return fail("an array passed twice to one call");
                }
                lent = true;
                continue;
            }
            // An array is an object, so always truthy.
            if (llvm::isa<ctjs::TruthyOp>(user)) { continue; }
            if (auto negated = llvm::dyn_cast<ctjs::UnaryOp>(user);
                negated && negated.getKind() == ctjs::UnaryKind::Not) {
                continue;
            }
            // Returned by value: the owner is moved out as the function ends.
            if (llvm::isa<ctjs::ReturnOp>(user)) {
                returned = true;
                continue;
            }
            if (llvm::isa<ctjs::SetPropertyOp>(user)) {
                return fail("an array written through an index or its `length`");
            }
            return fail(("an array that reaches `" + user->getName().getStringRef() + "`").str());
        }
    }
    // Lent or returned, it would meet a callee's or caller's Array methods.
    if (collection && (writes || joined || lent || returned || owners.size() != 1)) {
        return fail(owners.size() != 1 ? "an element's children mixed with another array"
                    : lent || returned ? "an element's children passed to or returned from "
                                         "a function"
                                       : "an element's children called with an Array method");
    }
    // A parameter borrows the caller's array: read-only as a span, or, when
    // the function pushes to it or sorts it, by address (elementParameterWritten).
    if (parameter && (!owners.empty() || returned)) {
        return fail(!owners.empty() ? "a parameter array mixed with an array this function owns"
                                    : "a parameter array returned from the function");
    }
    // A value carried through scf control flow is the address of its owner, so
    // the owner has to live in the function's entry block. An array used only
    // inside the region that made it needs no such slot.
    if (borrowed && !parameter) {
        if (!function) { return fail("an array outside a function"); }
        for (mlir::Operation * owner : owners) {
            // An empty literal in a branch that runs at most once per call
            // (`return [i]` and `return []` in a loop's exit arms) can take
            // its frame slot at the function's entry: making an empty array
            // has no effect to move.
            auto literal = llvm::dyn_cast<ctjs::CreateArrayOp>(owner);
            const bool hoistable = literal && literal.getElements().empty() &&
                                   !owner->getParentOfType<mlir::scf::WhileOp>() &&
                                   !owner->getParentOfType<mlir::scf::ForOp>();
            if (owner->getBlock() != &function.getBody().front() && !hoistable) {
                return fail("an array created inside a branch or a loop and carried out of it - "
                            "its storage has to be one frame slot");
            }
        }
    }
    return members;
}

llvm::SmallVector<mlir::Value, 4> TypeInference::elementVectorStores(
    llvm::ArrayRef<mlir::Value> group) {
    llvm::SmallVector<mlir::Value, 4> stores;
    // The array's own members, then the parameter groups of every callee it is
    // lent to: a push there stores into this array too.
    llvm::SmallVector<mlir::Value, 8> members(group);
    llvm::DenseSet<mlir::Value> seen(group.begin(), group.end());
    for (std::size_t at = 0; at < members.size(); ++at) {
        mlir::Value member = members[at];
        for (mlir::OpOperand & use : member.getUses()) {
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
            ctjs::FuncOp callee = call ? call.getTarget() : ctjs::FuncOp{};
            if (!callee || use.getOperandNumber() < 3 || callee.getBody().empty()) { continue; }
            mlir::Value parameter = callee.getBody().front().getArgument(use.getOperandNumber());
            if (seen.contains(parameter)) { continue; }
            for (mlir::Value borrowed : elementVectorAliases(parameter)) {
                if (seen.insert(borrowed).second) { members.push_back(borrowed); }
            }
        }
    }
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

int64_t TypeInference::constantIndex(mlir::Value key) {
    auto number = ctjs::constantAttr<ctjs::NumberAttr>(key);
    if (!number) { return -1; }
    const double value = number.getDouble();
    if (!(value >= 0.0) || value > 1e9 || value != std::floor(value)) { return -1; }
    return static_cast<int64_t>(value);
}

llvm::SmallVector<mlir::Value, 2> TypeInference::tupleParts(mlir::Value array) {
    llvm::SmallVector<mlir::Value, 2> parts;
    auto literal = array.getDefiningOp<ctjs::CreateArrayOp>();
    if (!literal) { return parts; }
    llvm::append_range(parts, literal.getElements());
    // Appends run in block order; isTupleLiteral requires them all in the
    // literal's block, so block order is the position order.
    for (mlir::Operation & op : *literal->getBlock()) {
        auto append = llvm::dyn_cast<ctjs::AppendOp>(op);
        if (append && append.getArray() == array) { parts.push_back(append.getElement()); }
    }
    return parts;
}

bool TypeInference::isTupleLiteral(mlir::Value array) {
    auto literal = array.getDefiningOp<ctjs::CreateArrayOp>();
    if (!literal) { return false; }
    mlir::Block * block = literal->getBlock();
    mlir::Operation * push = nullptr;
    unsigned appends = 0;
    for (mlir::OpOperand & use : array.getUses()) {
        mlir::Operation * user = use.getOwner();
        if (user->getBlock() != block) { return false; }
        if (llvm::isa<ctjs::AppendOp>(user) && use.getOperandNumber() == 0) {
            ++appends;
            continue;
        }
        // Operand 2 is the call's first argument (callee, receiver, args...).
        if (use.getOperandNumber() == 2 && vectorCallOf(user) == VectorCall::push && !push) {
            push = user;
            continue;
        }
        return false;
    }
    if (!push || literal.getElements().size() + appends != 2) { return false; }
    // Every append has to run before the push copies the tuple out.
    return llvm::all_of(array.getUsers(), [&](mlir::Operation * user) {
        return user == push || user->isBeforeInBlock(push);
    });
}

namespace {

/// Whether every value `counter`, a before-region argument of an scf.while,
/// can hold is an integer at least 0: following the loop's back edge and the
/// structured results it passes through, each value that reaches it is a
/// constant index at least 0, `counter + 1`, or `ub.poison`, which the
/// structuring pass yields only on the path that leaves the loop.
bool nonNegativeCounter(mlir::BlockArgument counter, mlir::scf::WhileOp loop) {
    llvm::SmallVector<mlir::Value> work{loop.getInits()[counter.getArgNumber()],
                                        loop.getYieldOp().getOperand(counter.getArgNumber())};
    llvm::DenseSet<mlir::Value> seen;
    while (!work.empty()) {
        const mlir::Value value = work.pop_back_val();
        if (!seen.insert(value).second) { continue; }
        if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value)) {
            // The after-region receives what the condition forwards.
            if (argument.getOwner()->getParent() != &loop.getAfter()) { return false; }
            work.push_back(loop.getConditionOp().getArgs()[argument.getArgNumber()]);
            continue;
        }
        mlir::Operation * producer = value.getDefiningOp();
        const unsigned index = llvm::cast<mlir::OpResult>(value).getResultNumber();
        if (llvm::isa<mlir::scf::IfOp, mlir::scf::IndexSwitchOp>(producer)) {
            for (mlir::Region & region : producer->getRegions()) {
                if (region.empty()) { return false; }
                work.push_back(region.front().getTerminator()->getOperand(index));
            }
            continue;
        }
        if (producer->getName().getStringRef() == "ub.poison" ||
            TypeInference::constantIndex(value) >= 0) {
            continue;
        }
        auto add = llvm::dyn_cast<ctjs::BinaryStaticOp>(producer);
        if (!add || add.getKind() != ctjs::BinaryKind::Add || add.getLhs() != counter ||
            TypeInference::constantIndex(add.getRhs()) != 1) {
            return false;
        }
    }
    return true;
}

} // namespace

bool TypeInference::isBoundedIndexRead(mlir::Operation * op) {
    auto read = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(op);
    auto counter = read ? llvm::dyn_cast<mlir::BlockArgument>(read.getKey()) : nullptr;
    if (!counter) { return false; }
    auto loop = llvm::dyn_cast<mlir::scf::WhileOp>(counter.getOwner()->getParentOp());
    // A folded for-of (Lowering/ForOf.cpp) reads in the before-region, under
    // `counter < array.length`, with the counter stepped through the
    // structured exits of its body.
    if (loop && counter.getOwner()->getParent() == &loop.getBefore()) {
        for (mlir::Operation * at = read->getParentOp(); at != loop; at = at->getParentOp()) {
            auto branch = llvm::dyn_cast<mlir::scf::IfOp>(at);
            if (!branch || !branch.getThenRegion().isAncestor(read->getParentRegion())) {
                continue;
            }
            auto truthy = branch.getCondition().getDefiningOp<ctjs::TruthyOp>();
            auto compare = truthy ? truthy.getValue().getDefiningOp<ctjs::CompareOp>() : nullptr;
            auto length = compare ? compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>() : nullptr;
            if (compare && compare.getKind() == ctjs::CompareKind::Lt &&
                compare.getLhs() == counter && length && length.getObject() == read.getObject() &&
                ctjs::constantKey(length.getKey()) == "length") {
                return nonNegativeCounter(counter, loop);
            }
        }
        return false;
    }
    if (!loop || counter.getOwner()->getParent() != &loop.getAfter()) { return false; }
    const unsigned index = counter.getArgNumber();
    // The condition forwards the before-region counter unchanged ...
    mlir::scf::ConditionOp condition = loop.getConditionOp();
    mlir::Value before = loop.getBeforeArguments()[index];
    if (condition.getArgs()[index] != before) { return false; }
    // ... and holds only while `counter < array.length` for the read's array.
    auto truthy = condition.getCondition().getDefiningOp<ctjs::TruthyOp>();
    auto compare = truthy ? truthy.getValue().getDefiningOp<ctjs::CompareOp>() : nullptr;
    if (!compare || compare.getKind() != ctjs::CompareKind::Lt || compare.getLhs() != before) {
        return false;
    }
    auto length = compare.getRhs().getDefiningOp<ctjs::GetPropertyOp>();
    if (!length || length.getObject() != read.getObject() ||
        ctjs::constantKey(length.getKey()) != "length") {
        return false;
    }
    // It starts at a non-negative integer and every iteration adds 1, so it
    // is always an integer at least 0.
    if (constantIndex(loop.getInits()[index]) < 0) { return false; }
    auto step = loop.getYieldOp().getOperand(index).getDefiningOp();
    const auto addsOne = [&](mlir::Value lhs, mlir::Value rhs) {
        return lhs == counter && constantIndex(rhs) == 1;
    };
    if (auto add = llvm::dyn_cast_or_null<ctjs::BinaryStaticOp>(step)) {
        return add.getKind() == ctjs::BinaryKind::Add && addsOne(add.getLhs(), add.getRhs());
    }
    if (auto add = llvm::dyn_cast_or_null<ctjs::BinaryOp>(step)) {
        return add.getKind() == ctjs::BinaryKind::Add && addsOne(add.getLhs(), add.getRhs());
    }
    return false;
}

namespace {
/// Whether `op` is a built-in call (TypeInference::builtinStaticOf) that takes
/// `value` as an Element argument.
bool builtinElementUse(mlir::Operation * op, mlir::Value value) {
    const TypeInference::BuiltinStatic * row = TypeInference::builtinStaticOf(op);
    if (!row) { return false; }
    auto args = llvm::cast<ctjs::CallOp>(op).getArgs();
    for (unsigned i = 0; i < args.size() && i < row->arity; ++i) {
        if (args[i] == value && row->params[i] == TypeInference::BuiltinStatic::Param::element) {
            return true;
        }
    }
    return false;
}
} // namespace

llvm::StringRef TypeInference::firstPropertyRead(mlir::Value value) {
    llvm::DenseSet<mlir::Value> seen;
    while (value && seen.insert(value).second) {
        auto fn = value.getParentRegion()->getParentOfType<ctjs::FuncOp>();
        if (!fn) { return {}; }
        // ponytail: text order, not execution order; a read in a branch that
        // does not run can name the wrong key. Only the error message depends
        // on it.
        mlir::Operation * first = nullptr;
        fn.walk<mlir::WalkOrder::PreOrder>([&](mlir::Operation * op) {
            if (first) { return mlir::WalkResult::interrupt(); }
            auto read = llvm::dyn_cast<ctjs::GetPropertyOp>(op);
            auto call = llvm::dyn_cast<ctjs::CallDirectOp>(op);
            auto yield = llvm::dyn_cast<mlir::scf::YieldOp>(op);
            if ((read && read.getObject() == value && !ctjs::constantKey(read.getKey()).empty()) ||
                (call && llvm::is_contained(call->getOperands().drop_front(3), value)) ||
                (yield && llvm::is_contained(yield->getOperands(), value)) ||
                builtinElementUse(op, value)) {
                first = op;
                return mlir::WalkResult::interrupt();
            }
            return mlir::WalkResult::advance();
        });
        if (auto read = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(first)) {
            return ctjs::constantKey(read.getKey());
        }
        // A built-in that takes it as an Element reads it as its receiver,
        // `Element.prototype.querySelectorAll.call(root, s)`.
        if (first && builtinElementUse(first, value)) { return builtinStaticOf(first)->member; }
        // Out of a branch it is yielded from, as a default parameter's value
        // `e = document.documentElement` is, into the branch's result.
        if (auto yield = llvm::dyn_cast_or_null<mlir::scf::YieldOp>(first)) {
            mlir::Operation * branch = yield->getParentOp();
            const auto at = static_cast<unsigned>(llvm::find(yield->getOperands(), value) -
                                                  yield->getOperands().begin());
            value = at < branch->getNumResults() ? branch->getResult(at) : mlir::Value{};
            continue;
        }
        auto call = llvm::dyn_cast_or_null<ctjs::CallDirectOp>(first);
        ctjs::FuncOp callee = call ? call.getTarget() : ctjs::FuncOp{};
        if (!callee || callee.getBody().empty()) { return {}; }
        // Into the callee, at the first argument position that passes it.
        const mlir::Value passed = value;
        value = {};
        for (unsigned i = 3; i < call->getNumOperands(); ++i) {
            if (call->getOperand(i) == passed) {
                value = callee.getBody().front().getArgument(i);
                break;
            }
        }
    }
    return {};
}

bool TypeInference::isRowRead(mlir::Operation * op) {
    auto read = llvm::dyn_cast_or_null<ctjs::GetPropertyOp>(op);
    if (!read || !ctjs::constantKey(read.getKey()).empty() || read.getResult().use_empty()) {
        return false;
    }
    return llvm::all_of(read.getResult().getUses(), [](mlir::OpOperand & use) {
        auto position = llvm::dyn_cast<ctjs::GetPropertyOp>(use.getOwner());
        if (!position || use.getOperandNumber() != 0) { return false; }
        const int64_t index = constantIndex(position.getKey());
        return index == 0 || index == 1;
    });
}

bool TypeInference::elementParameterWritten(mlir::Value parameter) {
    llvm::SmallVector<mlir::Value, 4> pending{parameter};
    llvm::DenseSet<mlir::Value> seen{parameter};
    while (!pending.empty()) {
        for (mlir::Value member : elementVectorAliases(pending.pop_back_val())) {
            for (mlir::OpOperand & use : member.getUses()) {
                const VectorCall kind = vectorCallOf(use.getOwner());
                if (use.getOperandNumber() == 1 &&
                    (kind == VectorCall::push || kind == VectorCall::sort ||
                     arrayCallbackSorts(use.getOwner()))) {
                    return true;
                }
                // Lent on to a callee that writes it.
                auto call = llvm::dyn_cast<ctjs::CallDirectOp>(use.getOwner());
                ctjs::FuncOp callee = call ? call.getTarget() : ctjs::FuncOp{};
                if (callee && use.getOperandNumber() >= 3 && !callee.getBody().empty()) {
                    mlir::Value next = callee.getBody().front().getArgument(use.getOperandNumber());
                    if (seen.insert(next).second) { pending.push_back(next); }
                }
            }
        }
    }
    return false;
}

bool TypeInference::elementVectorParameter(llvm::ArrayRef<mlir::Value> members) {
    return llvm::any_of(members, [](mlir::Value member) {
        auto argument = llvm::dyn_cast<mlir::BlockArgument>(member);
        return argument && argument.getOwner()->isEntryBlock() &&
               llvm::isa<ctjs::FuncOp>(argument.getOwner()->getParentOp()) &&
               argument.getArgNumber() >= 3;
    });
}

bool TypeInference::isUndefinedGlobal(mlir::Operation * op) {
    return isRealmGlobal(op, "undefined");
}

bool TypeInference::isRealmGlobal(mlir::Operation * op, llvm::StringRef name) {
    auto load = llvm::dyn_cast_or_null<ctjs::LoadGlobalOp>(op);
    if (!load || load.getName() != name) { return false; }
    // ponytail: one module walk per query; index the stores per solve if a
    // large program makes this slow.
    auto module = op->getParentOfType<mlir::ModuleOp>();
    bool stored = false;
    module.walk([&](ctjs::StoreGlobalOp store) { stored |= store.getName() == name; });
    return !stored;
}

} // namespace ctcompile::ctnative
