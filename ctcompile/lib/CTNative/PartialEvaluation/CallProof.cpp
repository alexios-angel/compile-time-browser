#include "CallProof.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"

namespace ctcompile::ctnative::partial_eval {
namespace {
class equivalence {
public:
    equivalence(const snapshot & left, const snapshot & right, unsigned anchored, unsigned & steps,
                unsigned limit)
        : left(left), right(right), anchored(anchored), steps(steps), limit(limit) {}

    callProofResult check(value result, value counterpart) {
        if (left.heap.size() < anchored || right.heap.size() < anchored) {
            return callProofResult::different;
        }
        // All allocations that existed before the call retain their exact
        // identities, including aliases not used as explicit call operands.
        for (unsigned id = 0; id < anchored; ++id) {
            if (!values(value::reference(id), value::reference(id))) { return outcome(); }
        }
        if (!values(result, counterpart)) { return outcome(); }
        while (!pending.empty()) {
            auto [a, b] = pending.pop_back_val();
            if (!nodes(left.heap[a], right.heap[b])) { return outcome(); }
        }
        return callProofResult::equivalent;
    }

private:
    const snapshot &left, &right;
    unsigned anchored;
    unsigned & steps;
    unsigned limit;
    bool exhausted = false;
    llvm::DenseMap<unsigned, unsigned> forward, reverse;
    llvm::SmallVector<std::pair<unsigned, unsigned>> pending;

    bool step() {
        if (steps >= limit) {
            exhausted = true;
            return false;
        }
        ++steps;
        return true;
    }
    callProofResult outcome() const {
        return exhausted ? callProofResult::exhausted : callProofResult::different;
    }
    bool values(value a, value b) {
        if (!step() || a.tag != b.tag) { return false; }
        switch (a.tag) {
        case value::kind::unknown: return false;
        case value::kind::constant:
            // Exact attributes preserve tags, signed zero and NaN bit patterns.
            // JavaScript equality/SameValueZero is too weak for this proof.
            return a.constant && a.constant == b.constant;
        case value::kind::mapConstructor: return true;
        case value::kind::method:
            if (a.method != b.method) { return false; }
            [[fallthrough]];
        case value::kind::reference:
            if (a.node >= left.heap.size() || b.node >= right.heap.size() ||
                ((a.node < anchored || b.node < anchored) && a.node != b.node)) {
                return false;
            }
            if (const auto found = forward.find(a.node); found != forward.end()) {
                return found->second == b.node;
            }
            if (reverse.contains(b.node)) { return false; }
            forward[a.node] = b.node;
            reverse[b.node] = a.node;
            pending.emplace_back(a.node, b.node);
            return true;
        }
        llvm_unreachable("invalid partial-evaluation value");
    }
    bool nodes(const node & a, const node & b) {
        if (!step() || a.tag != b.tag || a.entries.size() != b.entries.size()) { return false; }
        if (a.tag == node::kind::cell &&
            (a.requiresWrite != b.requiresWrite || a.assigned != b.assigned ||
             !values(a.contents, b.contents))) {
            return false;
        }
        if (a.tag == node::kind::closure) {
            if (a.function != b.function || a.captures.size() != b.captures.size()) {
                return false;
            }
            for (auto [capture, other] : llvm::zip(a.captures, b.captures)) {
                if (!values(capture, other)) { return false; }
            }
        }
        // Ordered entries also preserve Map insertion order and object own-key
        // order; a key reference participates in the same alias bijection.
        for (auto [entry, other] : llvm::zip(a.entries, b.entries)) {
            if (!values(entry.first, other.first) || !values(entry.second, other.second)) {
                return false;
            }
        }
        return true;
    }
};
} // namespace

callProofResult equivalentCallState(const snapshot & candidate, value candidateResult,
                                    const snapshot & original, value originalResult,
                                    unsigned anchored, unsigned & steps, unsigned maxSteps) {
    return equivalence(candidate, original, anchored, steps, maxSteps)
        .check(candidateResult, originalResult);
}
} // namespace ctcompile::ctnative::partial_eval
