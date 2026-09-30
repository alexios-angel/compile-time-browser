#include "../../../lib/CTNative/HostContract/Provider/State.h"
#include "ctcompile/CTJS/IR/CTJSDialect.h"

#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include <bit>
#include <cstdint>
#include <cstdio>

#include "check.hpp"

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
    const auto value = state.lookup(map, key, unlimited);
    check(value && *value, "known entry is found");
    return value && *value ? **value : providerValue{};
}

unsigned create(providerState & state, providerMapProvenance provenance) {
    const auto id = state.create(provenance, unlimited);
    check(id.has_value(), "private Map allocation succeeds");
    return id.value_or(0);
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
    check(state.size(map, unlimited) == 13u,
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
    check(ordered.erase(orderedId, b, unlimited) == true, "deletion reports an existing entry");
    check(ordered.erase(orderedId, b, unlimited) == false, "failed deletion reports no mutation");
    check(ordered.set(orderedId, b, two, unlimited), "reinsertion succeeds");
    const auto & entries = ordered.get(orderedId)->entries;
    check(entries.size() == 3 && same(entries[0].key, a) && same(entries[1].key, c) &&
              same(entries[2].key, b),
          "deletion and reinsertion append after surviving entries");
    const auto missing = ordered.lookup(orderedId, string(context, "absent"), unlimited);
    check(missing && !*missing, "proved absence is separate from a stored undefined value");

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

    const auto empty = state.keys(map, unlimited);
    check(empty && empty->empty(), "empty Map snapshot is an empty sequence");
    check(!state.keys(0, unlimited) && !state.keys(999, unlimited),
          "invalid MapIds produce no snapshot");
    const auto noWork = [](unsigned) { return false; };
    check(!state.keys(map, noWork), "even an empty snapshot requires a successful budget check");

    const providerValue inserted[] = {a, b, c, firstObject, secondObject, negativeZero, nanA};
    for (auto key : inserted) {
        check(state.set(map, key, one, unlimited), "snapshot fixture key insertion succeeds");
    }
    check(state.set(map, b, two, unlimited) && state.set(map, firstObject, two, unlimited) &&
              state.set(map, zero, two, unlimited) && state.set(map, nanB, two, unlimited),
          "replacement and SameValueZero aliases preserve the snapshot's key positions");
    const providerValue before[] = {a, b, c, firstObject, secondObject, zero, nanA};
    auto snapshot = state.keys(map, unlimited).value_or(llvm::SmallVector<providerValue, 0>{});
    check(same(snapshot, before),
          "snapshot preserves insertion order, object identities, NaN identity and positive zero");
    llvm::SmallVector<providerValue, 8> another;
    if (auto keys = state.keys(map, unlimited)) { another = std::move(*keys); }
    check(same(another, before) && another.data() != snapshot.data(),
          "repeated snapshots own distinct key storage regardless of inline capacity");

    check(state.erase(map, b, unlimited) == true && state.set(map, b, one, unlimited),
          "Map deletion and reinsertion after snapshot creation succeed");
    const providerValue after[] = {a, c, firstObject, secondObject, zero, nanA, b};
    const auto updated = state.keys(map, unlimited);
    check(updated && same(*updated, after) && same(snapshot, before) && same(another, before),
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
        check(!state.lookup(first, key, unlimited),
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
    check(!state.create({}, unlimited) && same(state, saved),
          "missing allocation provenance cannot publish a MapId");
    check(!state.size(0, unlimited) && !state.get(0) && !state.get(999),
          "invalid MapIds produce no state or size proof");
    check(state.erase(second, edge, unlimited) == true &&
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
    const auto counted = initial.lookup(first, c, countWork);
    check(counted && *counted && charged >= 3,
          "lookup charges every compared key before the last entry");
    charged = 0;
    const auto countedKeys = initial.keys(first, countWork);
    check(countedKeys && countedKeys->size() == 3 && charged >= 3,
          "snapshot charges every copied key before publishing the complete sequence");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            const providerValue after[] = {a, b, c};
            const auto output = state.keys(first, spend);
            check(!output || same(*output, after), "a complete snapshot holds every key in order");
            return output.has_value();
        },
        "complete key snapshots have a finite completion budget");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            const auto output = state.lookup(first, c, spend);
            check(!output || (*output && same(**output, one)),
                  "lookup only publishes outputs after every required comparison");
            return output.has_value();
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
            const auto removed = state.erase(first, a, spend);
            check(!removed || *removed,
                  "deletion does not publish its result before mutation commits");
            return removed.has_value();
        },
        "deletion and entry shifts have a finite completion budget");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            const auto id = state.create(provenance, spend);
            check(!id || *id == 4, "allocation ID is withheld until allocation commits");
            return id.has_value();
        },
        "allocation has a finite completion budget");
    cutoffs(
        initial,
        [&](providerState & state, providerState::Spend spend) {
            const auto count = state.size(first, spend);
            check(!count || *count == 3, "size output is withheld on exhaustion");
            return count.has_value();
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
    if (ctbrowser_test_failures == 0) { std::puts("private provider Map state queries passed"); }
    return ctbrowser_test_failures == 0 ? 0 : 1;
}
