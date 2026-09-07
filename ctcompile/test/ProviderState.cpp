#include "../lib/CTNative/HostContract/ProviderState.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <bit>
#include <cstdint>
#include <cstdio>

namespace {
namespace ctjs = ctcompile::ctjs;
using ctcompile::ctnative::host_detail::providerMapProvenance;
using ctcompile::ctnative::host_detail::providerState;
using ctcompile::ctnative::host_detail::providerValue;

constexpr const char * fixture = R"MLIR(
module {
  ctjs.func @script$0(%this: !ctjs.value, %new: !ctjs.value, %callee: !ctjs.value) -> !ctjs.value attributes {upvalue_count = 0 : i32} {
    %u = ctjs.constant #ctjs.undefined
    %map = ctjs.load_global "Map"
    %allocation = ctjs.construct %map(%map)
    %invoked = ctjs.call %callee(%u)
    ctjs.return %u
  }
}
)MLIR";

int failures = 0;
void check(bool value, const char * message) {
    if (value) { return; }
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}
constexpr auto unlimited = [](unsigned) { return true; };

bool same(providerValue a, providerValue b) {
    return a.kind == b.kind && a.id == b.id && a.literal == b.literal;
}

bool same(llvm::ArrayRef<providerValue> a, llvm::ArrayRef<providerValue> b) {
    if (a.size() != b.size()) { return false; }
    for (unsigned index = 0; index != a.size(); ++index) {
        if (!same(a[index], b[index])) { return false; }
    }
    return true;
}

bool same(const providerState & a, const providerState & b) {
    if (a.maps().size() != b.maps().size()) { return false; }
    for (unsigned index = 0; index != a.maps().size(); ++index) {
        const auto & left = a.maps()[index];
        const auto & right = b.maps()[index];
        if (left.provenance.allocation != right.provenance.allocation ||
            left.provenance.invocation != right.provenance.invocation ||
            left.provenance.factory != right.provenance.factory ||
            left.entries.size() != right.entries.size()) {
            return false;
        }
        for (unsigned entry = 0; entry != left.entries.size(); ++entry) {
            if (!same(left.entries[entry].key, right.entries[entry].key) ||
                !same(left.entries[entry].value, right.entries[entry].value)) {
                return false;
            }
        }
    }
    return true;
}

providerValue number(mlir::MLIRContext & context, double value) {
    return providerValue::constant(ctjs::NumberAttr::get(&context, std::bit_cast<uint64_t>(value)));
}

providerValue bits(mlir::MLIRContext & context, uint64_t value) {
    return providerValue::constant(ctjs::NumberAttr::get(&context, value));
}

providerValue string(mlir::MLIRContext & context, llvm::StringRef value) {
    return providerValue::constant(ctjs::StringAttr::get(&context, value));
}

providerValue read(const providerState & state, unsigned map, providerValue key) {
    bool found = false;
    providerValue value;
    check(state.lookup(map, key, found, value, unlimited) && found, "known entry is found");
    return value;
}

unsigned create(providerState & state, providerMapProvenance provenance) {
    unsigned id = 0;
    check(state.create(provenance, id, unlimited), "private Map allocation succeeds");
    return id;
}

// Sweep the externally observable completion boundary; no internal work formula
// is reproduced. Each failed operation must preserve all preexisting state.
template <typename Operation>
void cutoffs(const providerState & initial, Operation operation, const char * message) {
    unsigned completed = 0;
    for (unsigned budget = 0; budget != 4096; ++budget) {
        providerState trial;
        check(initial.cloneTo(trial, unlimited), "budget-test setup copy succeeds");
        unsigned remaining = budget;
        const auto spend = [&](unsigned amount) {
            if (amount > remaining) { return false; }
            remaining -= amount;
            return true;
        };
        if (operation(trial, spend)) {
            completed = budget;
            break;
        }
        check(same(trial, initial), "failed operation exposes no partial state");
    }
    check(completed != 0, message);
}

void keysAndOrder(mlir::MLIRContext & context, providerMapProvenance provenance) {
    providerState state;
    const unsigned map = create(state, provenance);
    const auto zero = number(context, 0.0), negativeZero = number(context, -0.0);
    const auto nanA = bits(context, UINT64_C(0x7ff8000000000001));
    const auto nanB = bits(context, UINT64_C(0xfff0000000000002));
    const auto one = number(context, 1.0), two = number(context, 2.0);
    check(state.set(map, negativeZero, one, unlimited) && state.set(map, zero, two, unlimited),
          "signed zero keys replace one entry");
    check(state.set(map, nanA, one, unlimited) && state.set(map, nanB, two, unlimited),
          "NaNs of different sign and payload replace one entry");
    check(same(read(state, map, negativeZero), two) && same(read(state, map, nanA), two),
          "SameValueZero lookup returns replacements");
    check(same(state.get(map)->entries.front().key, zero),
          "stored zero key is canonical positive zero");
    const providerValue distinct[] = {
        providerValue::constant(ctjs::BooleanAttr::get(&context, true)),
        one,
        string(context, "1"),
        providerValue::constant(ctjs::UndefinedAttr::get(&context)),
        providerValue::constant(ctjs::NullAttr::get(&context)),
        providerValue::object(0),
        providerValue::object(1),
        bits(context, UINT64_C(0x7ff0000000000000)),
        bits(context, UINT64_C(0xfff0000000000000)),
        string(context, llvm::StringRef("x\0y", 3)),
        string(context, "x")};
    unsigned ordinal = 0;
    for (providerValue key : distinct) {
        check(state.set(map, key, number(context, static_cast<double>(ordinal++)), unlimited),
              "tagged primitive or proved object key is accepted");
    }
    unsigned size = 0;
    check(state.size(map, size, unlimited) && size == 13,
          "primitive tags, object tokens, infinities and embedded NUL remain distinct");
    ordinal = 0;
    for (providerValue key : distinct) {
        check(same(read(state, map, key), number(context, static_cast<double>(ordinal++))),
              "each distinct key preserves its own value");
    }
    check(state.set(map, providerValue::object(0), negativeZero, unlimited),
          "object-key alias replaces its original entry");
    check(same(read(state, map, providerValue::object(0)), negativeZero),
          "numeric payload retains signed zero");

    providerState ordered;
    const unsigned orderedId = create(ordered, provenance);
    const auto a = string(context, "a"), b = string(context, "b"), c = string(context, "c");
    check(ordered.set(orderedId, a, one, unlimited) && ordered.set(orderedId, b, one, unlimited) &&
              ordered.set(orderedId, c, one, unlimited) &&
              ordered.set(orderedId, b, two, unlimited),
          "ordered fixture and replacement succeed");
    check(same(ordered.get(orderedId)->entries[1].key, b),
          "replacement preserves insertion position");
    bool removed = false;
    check(ordered.erase(orderedId, b, removed, unlimited) && removed,
          "deletion reports an existing entry");
    check(ordered.erase(orderedId, b, removed, unlimited) && !removed,
          "failed deletion reports no mutation");
    check(ordered.set(orderedId, b, two, unlimited), "reinsertion succeeds");
    const auto & entries = ordered.get(orderedId)->entries;
    check(entries.size() == 3 && same(entries[0].key, a) && same(entries[1].key, c) &&
              same(entries[2].key, b),
          "deletion and reinsertion append after surviving entries");
    bool found = true;
    providerValue missing = one;
    check(ordered.lookup(orderedId, string(context, "absent"), found, missing, unlimited) &&
              !found && same(missing, providerValue{}),
          "proved absence is separate from a stored undefined value");

    mlir::MLIRContext otherContext;
    otherContext.getOrLoadDialect<ctjs::CTJSDialect>();
    check(same(read(ordered, orderedId, string(otherContext, "b")), two),
          "literal equality does not depend on MLIR attribute identity");
}

void keySnapshots(mlir::MLIRContext & context, providerMapProvenance provenance) {
    providerState state;
    const unsigned map = create(state, provenance);
    const auto a = string(context, "a"), b = string(context, "b"), c = string(context, "c");
    const auto one = number(context, 1.0), two = number(context, 2.0);
    const auto zero = number(context, 0.0), negativeZero = number(context, -0.0);
    const auto nanA = bits(context, UINT64_C(0x7ff8000000000001));
    const auto nanB = bits(context, UINT64_C(0xfff0000000000002));
    const auto firstObject = providerValue::object(0), secondObject = providerValue::object(1);

    llvm::SmallVector<providerValue, 2> empty{one, two};
    check(state.keys(map, empty, unlimited) && empty.empty(),
          "empty Map snapshot replaces the previous output with an empty sequence");
    const providerValue keep[] = {one, secondObject};
    llvm::SmallVector<providerValue, 2> invalid{one, secondObject};
    check(!state.keys(0, invalid, unlimited) && same(invalid, keep) &&
              !state.keys(999, invalid, unlimited) && same(invalid, keep),
          "invalid MapIds cannot replace the previous snapshot output");
    const auto noWork = [](unsigned) { return false; };
    check(!state.keys(map, invalid, noWork) && same(invalid, keep),
          "even an empty snapshot requires a successful budget check");

    const providerValue inserted[] = {a, b, c, firstObject, secondObject, negativeZero, nanA};
    for (auto key : inserted) {
        check(state.set(map, key, one, unlimited), "snapshot fixture key insertion succeeds");
    }
    check(state.set(map, b, two, unlimited) && state.set(map, firstObject, two, unlimited) &&
              state.set(map, zero, two, unlimited) && state.set(map, nanB, two, unlimited),
          "replacement and SameValueZero aliases preserve the snapshot's key positions");
    const providerValue before[] = {a, b, c, firstObject, secondObject, zero, nanA};
    llvm::SmallVector<providerValue, 0> snapshot;
    check(state.keys(map, snapshot, unlimited) && same(snapshot, before),
          "snapshot preserves insertion order, object identities, NaN identity and positive zero");
    llvm::SmallVector<providerValue, 8> another;
    check(state.keys(map, another, unlimited) && same(another, before) &&
              another.data() != snapshot.data(),
          "repeated snapshots own distinct key storage regardless of inline capacity");

    bool removed = false;
    check(state.erase(map, b, removed, unlimited) && removed && state.set(map, b, one, unlimited),
          "Map deletion and reinsertion after snapshot creation succeed");
    const providerValue after[] = {a, c, firstObject, secondObject, zero, nanA, b};
    llvm::SmallVector<providerValue, 0> updated;
    check(state.keys(map, updated, unlimited) && same(updated, after) && same(snapshot, before) &&
              same(another, before),
          "later mutation updates fresh snapshots without changing existing snapshots");
    another.front() = c;
    check(same(snapshot, before) && same(read(state, map, a), one),
          "changing a snapshot's key records cannot mutate another snapshot or the source Map");
}

void graphAndRefusals(mlir::MLIRContext & context, providerMapProvenance provenance) {
    providerState state;
    const unsigned first = create(state, provenance), second = create(state, provenance);
    auto otherFactory = provenance;
    otherFactory.factory = 2;
    const unsigned third = create(state, otherFactory);
    check(first == 1 && second == 2 && third == 3,
          "repeated source allocations and separate factories receive distinct MapIds");
    check(state.get(first)->provenance.allocation == state.get(second)->provenance.allocation &&
              state.get(first)->provenance.factory == 1 &&
              state.get(third)->provenance.factory == 2,
          "source provenance is retained without conflating allocation instances");
    const auto edge = string(context, "edge"), one = number(context, 1.0);
    check(state.set(first, edge, providerValue::map(second), unlimited) &&
              state.set(second, edge, providerValue::map(third), unlimited),
          "private nested Map graph is accepted");
    providerState saved;
    check(state.cloneTo(saved, unlimited), "graph snapshot succeeds");
    check(!state.set(third, edge, providerValue::map(first), unlimited) && same(state, saved),
          "transitive resource cycle refuses without mutation");
    check(!state.set(second, edge, providerValue::map(second), unlimited) && same(state, saved),
          "self cycle refuses without replacing its prior edge");
    const providerValue badKeys[] = {
        providerValue{}, providerValue::map(first),
        providerValue::constant(ctjs::BigIntAttr::get(&context, "1")),
        providerValue::constant(mlir::StringAttr::get(&context, "not a JS literal"))};
    for (providerValue key : badKeys) {
        bool found = true;
        providerValue output = one;
        check(!state.lookup(first, key, found, output, unlimited) && found && same(output, one),
              "unsupported equality cannot be reported as absence");
        check(!state.set(first, key, one, unlimited) && same(state, saved),
              "unsupported key cannot change state");
    }
    const providerValue badValues[] = {
        providerValue{}, providerValue::map(0), providerValue::map(999),
        providerValue::constant(ctjs::BigIntAttr::get(&context, "1"))};
    for (providerValue value : badValues) {
        check(!state.set(first, edge, value, unlimited) && same(state, saved),
              "unknown or invalid resource payload refuses atomically");
    }
    providerState objectPayloads;
    check(state.cloneTo(objectPayloads, unlimited),
          "object payload transaction starts from a copy");
    check(objectPayloads.set(first, edge, providerValue::object(0), unlimited) &&
              same(read(objectPayloads, first, edge), providerValue::object(0)),
          "proved object payload retains token zero rather than becoming an unknown value");
    check(objectPayloads.set(first, edge, providerValue::object(1), unlimited) &&
              same(read(objectPayloads, first, edge), providerValue::object(1)) &&
              same(state, saved),
          "object payload replacement preserves identity and does not modify the original state");
    unsigned outputId = 77;
    check(!state.create({}, outputId, unlimited) && outputId == 77 && same(state, saved),
          "missing allocation provenance cannot publish a MapId");
    unsigned outputSize = 77;
    check(!state.size(0, outputSize, unlimited) && outputSize == 77 && !state.get(0) &&
              !state.get(999),
          "invalid MapIds produce no state or size proof");
    bool removed = false;
    check(state.erase(second, edge, removed, unlimited) && removed &&
              state.set(third, edge, providerValue::map(first), unlimited),
          "cycle checks use the current graph after deletion");
    check(saved.get(second)->entries.size() == 1,
          "transaction writes leave the independently copied committed state unchanged");
}

void budgets(mlir::MLIRContext & context, providerMapProvenance provenance) {
    providerState initial;
    const unsigned first = create(initial, provenance), second = create(initial, provenance);
    const unsigned third = create(initial, provenance);
    const auto a = string(context, "a"), b = string(context, "b"), c = string(context, "c");
    const auto one = number(context, 1.0), two = number(context, 2.0);
    check(initial.set(first, a, one, unlimited) && initial.set(first, b, one, unlimited) &&
              initial.set(first, c, one, unlimited) &&
              initial.set(second, a, providerValue::map(third), unlimited),
          "budget fixture has multiple entries and a nested resource edge");
    unsigned charged = 0;
    const auto countWork = [&](unsigned amount) {
        charged += amount;
        return true;
    };
    providerState countedCopy;
    check(initial.cloneTo(countedCopy, countWork) && charged >= 7,
          "copy charges at least three Map records and four stored entries");
    charged = 0;
    bool countedFound = false;
    providerValue countedValue;
    check(initial.lookup(first, c, countedFound, countedValue, countWork) && countedFound &&
              charged >= 3,
          "lookup charges every compared key before the last entry");
    charged = 0;
    llvm::SmallVector<providerValue, 0> countedKeys;
    check(initial.keys(first, countedKeys, countWork) && countedKeys.size() == 3 && charged >= 3,
          "snapshot charges every copied key before publishing the complete sequence");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            const providerValue before[] = {two, providerValue::object(99)};
            const providerValue after[] = {a, b, c};
            llvm::SmallVector<providerValue, 2> output{two, providerValue::object(99)};
            const auto * storage = output.data();
            const auto capacity = output.capacity();
            const bool success = state.keys(first, output, spend);
            check(success ? same(output, after)
                          : same(output, before) && output.data() == storage &&
                                output.capacity() == capacity,
                  "exhausted snapshots preserve every prior output key and its storage");
            return success;
        },
        "complete key snapshots have a finite completion budget");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            bool found = false;
            providerValue output = two;
            const bool success = state.lookup(first, c, found, output, spend);
            check(success ? found && same(output, one) : !found && same(output, two),
                  "lookup only publishes outputs after every required comparison");
            return success;
        },
        "lookup has a finite completion budget");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            return state.set(first, c, two, spend);
        },
        "replacement has a finite completion budget");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            return state.set(first, string(context, "nested"), providerValue::map(second), spend);
        },
        "insertion and resource traversal have a finite completion budget");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            bool removed = false;
            const bool success = state.erase(first, a, removed, spend);
            check(success == removed,
                  "deletion does not publish its result before mutation commits");
            return success;
        },
        "deletion and entry shifts have a finite completion budget");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            unsigned id = 77;
            const bool success = state.create(provenance, id, spend);
            check(success ? id == 4 : id == 77,
                  "allocation ID is withheld until allocation commits");
            return success;
        },
        "allocation has a finite completion budget");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            unsigned count = 77;
            const bool success = state.size(first, count, spend);
            check(success ? count == 3 : count == 77, "size output is unchanged on exhaustion");
            return success;
        },
        "size has a finite completion budget");
    providerState destination;
    create(destination, provenance);
    cutoffs(
        destination,
        [&](providerState & state, providerState::Spend spend) {
            const bool success = initial.cloneTo(state, spend);
            check(!success || same(state, initial),
                  "successful transaction copy contains all entries");
            return success;
        },
        "complete state copying has a finite completion budget");
    providerState transaction;
    check(initial.cloneTo(transaction, unlimited) && transaction.set(first, a, two, unlimited) &&
              !transaction.set(first, b, providerValue{}, unlimited),
          "transaction can refuse after an earlier supported mutation");
    check(same(read(initial, first, a), one),
          "discarding a failed transaction preserves the original committed value");
}
} // namespace

int main() {
    mlir::MLIRContext context;
    context.getOrLoadDialect<ctjs::CTJSDialect>();
    auto module = mlir::parseSourceString<mlir::ModuleOp>(fixture, &context);
    check(static_cast<bool>(module), "provider provenance fixture parses");
    if (!module) { return 1; }
    providerMapProvenance provenance;
    provenance.factory = 1;
    module->walk([&](ctjs::ConstructOp allocation) { provenance.allocation = allocation; });
    module->walk([&](ctjs::CallOp invocation) { provenance.invocation = invocation; });
    keysAndOrder(context, provenance);
    keySnapshots(context, provenance);
    graphAndRefusals(context, provenance);
    budgets(context, provenance);
    if (failures == 0) { std::puts("private provider Map state queries passed"); }
    return failures == 0 ? 0 : 1;
}
