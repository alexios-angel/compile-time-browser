//===- Analysis.h - Host entry prefix interpreter state ---------*- C++ -*-===//
//
// Internal state of HostEntryPrefixAnalysis (HostPrefix.h): prefixValue, the
// abstract value of an SSA value; prefixObject, an object the prefix built;
// and prefixAnalysis, the interpreter whose members are defined across the
// .cpp files in this directory.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../Analysis.h"
#include "../Provider/State.h"
#include "ctcompile/CTNative/Analysis/HostPrefix.h"
#include "llvm/ADT/StringSet.h"

namespace ctcompile::ctnative::host_detail {

/// The abstract value of one SSA value during prefix interpretation.
/// `literal` holds a primitive's constant and `made` a closure's creating op.
/// `object` is an index whose meaning depends on `kind`: into
/// prefixAnalysis::objects for objects, into the owning factory's resources
/// for resources.
struct prefixValue {
    enum class Kind {
        unknown,
        primitive,
        object,
        realm,
        resource,
        resourceMethod,
        mapConstructor,
        arrayConstructor,
        arrayFrom,
        mapKeysIterator,
        keySnapshot,
        closure,
        absent
    } kind = Kind::unknown;
    mlir::Attribute literal;
    ctjs::CreateClosureOp made;
    unsigned object = 0;
    /// One-based index into prefixAnalysis::factories for a closure a factory
    /// returned; zero when that is not proved.
    unsigned invocation = 0;

    static prefixValue constant(mlir::Attribute value) { return {Kind::primitive, value, {}, 0}; }
    static prefixValue of(Kind kind, unsigned object) { return {kind, {}, {}, object}; }
};

/// An object literal the prefix allocated, with the fields it has seen
/// written, and the factory call that allocated it, if any.
struct prefixObject {
    llvm::StringMap<prefixValue> fields;
    ctjs::CreateObjectOp allocation;
    mlir::Operation * invocation = nullptr;
    bool retained = false;
    bool published = false;
};

/// Symbolically interprets a module's entry function up to the first
/// operation whose effect it cannot model, recording the branches, calls,
/// factories, publications and provider reads that HostEntryPrefixAnalysis
/// reports. `remaining` is the step budget (`exhausted` once spent); the
/// follow* flags come from HostPrefixFollow and opt in to modelling each kind
/// of provider interaction. `refusal` rejects the whole module, while
/// `boundary` only marks where interpretation stopped. Results borrow the IR
/// and are invalid after any mutation.
struct prefixAnalysis {
    mlir::ModuleOp module;
    const HostContract & contract;
    unsigned remaining;
    bool followPublication;
    bool followProviderReads;
    bool followProviderMutations;
    bool followProviderDiagnostics;
    bool followProviderCallbacks;
    bool followProviderObjects;
    unsigned operationCount = 0;
    bool exhausted = false;
    /// Set when anything could observe or re-bind a function outside its
    /// known call sites (eval, Function, Reflect, `arguments`, computed or
    /// reflective property keys, skipped ops); uniqueContext() then refuses
    /// every call.
    bool reflective = false;
    std::string refusal, boundary;
    std::string providerBoundary;
    std::vector<HostPrefixBranch> branches;
    std::vector<HostPrefixCall> calls;
    std::vector<HostPrefixFactory> factories;
    std::vector<HostPrefixPublication> publications;
    std::vector<HostPrefixReadSummary> reads;
    std::vector<HostPrefixProviderSummary> providerCalls;
    providerState providers;
    std::vector<llvm::SmallVector<unsigned>> providerRoots;
    llvm::DenseSet<mlir::Operation *> discoveryOnly;
    llvm::DenseMap<unsigned, unsigned> factoryTables;
    llvm::DenseSet<mlir::Operation *> factoryClosures;
    llvm::DenseMap<unsigned, ctjs::FuncOp> functions;
    llvm::DenseMap<mlir::Operation *, llvm::SmallVector<ctjs::CallDirectOp>> callers;
    llvm::DenseMap<mlir::Operation *, unsigned> creations;
    llvm::DenseSet<mlir::Operation *> visited;
    llvm::DenseSet<mlir::Operation *> pendingNewTarget;
    llvm::StringMap<prefixValue> globals;
    llvm::StringSet<> declaredGlobals;
    llvm::StringMap<llvm::SmallVector<ctjs::StoreGlobalOp>> initializers;
    mlir::DominanceInfo dominance;
    llvm::DenseMap<mlir::Value, prefixValue> observedValues;
    std::vector<prefixObject> objects;
    using environment = llvm::DenseMap<mlir::Value, prefixValue>;

    struct completion {
        enum class Kind {
            stopped,
            returned,
            yielded
        } kind = Kind::stopped;
        llvm::SmallVector<prefixValue> values;
    };

    prefixAnalysis(mlir::ModuleOp module, const HostContract & contract, unsigned maxSteps,
                   HostPrefixFollow follow);
    bool step();
    bool spend(uint64_t count);
    /// One step per character of a copied or compared name.
    bool spendText(llvm::StringRef text);
    prefixValue stop(mlir::Operation * operation, llvm::StringRef reason);
    /// The capture of `factory` a summarized closure's upvalue `load` reads, one
    /// step per capture examined; null for another slot or on budget failure.
    const HostPrefixCapture * capture(const HostPrefixFactory & factory, prefixValue closure,
                                      ctjs::FuncOp function, ctjs::LoadUpvalueOp load);
    ctjs::FuncOp target(prefixValue value);
    bool uniqueContext(ctjs::FuncOp function, ctjs::CallDirectOp call,
                       ctjs::CreateClosureOp producer);
    bool initializedGlobal(ctjs::LoadGlobalOp load);
    bool identitySafeFunction(ctjs::FuncOp function, llvm::DenseSet<mlir::Operation *> & stack);
    bool identitySafeRegion(mlir::Region & region, llvm::DenseSet<mlir::Operation *> & stack);
    prefixValue factory(ctjs::CallOp call, ctjs::FuncOp function);
    /// The factory summary whose captured closure `closure` is, when `call` in
    /// the entry invokes a one-block `function` and Map is an intrinsic;
    /// `charged` spends one step per capture first.
    const HostPrefixFactory * summarizedOwner(ctjs::CallOp call, ctjs::FuncOp function,
                                              prefixValue closure, bool charged);
    prefixValue providerRead(ctjs::CallOp call, ctjs::FuncOp function, prefixValue closure,
                             environment & values);
    prefixValue providerMutation(ctjs::CallOp call, ctjs::FuncOp function, prefixValue closure,
                                 environment & values);
    void publication(ctjs::SetPropertyOp write, prefixValue owner, prefixValue value);
    completion function(ctjs::FuncOp function, llvm::ArrayRef<prefixValue> arguments,
                        unsigned depth);
    completion region(mlir::Region & region, environment & values, unsigned depth);
    prefixValue operation(mlir::Operation * operation, environment & values, unsigned depth);
};

std::optional<bool> prefixTruth(prefixValue value);
/// A constant's value or a truth test's Boolean; nullopt for any other
/// operation.
std::optional<prefixValue> prefixScalar(mlir::Operation * operation,
                                        const prefixAnalysis::environment & values,
                                        mlir::MLIRContext * context);
prefixValue prefixUnary(ctjs::UnaryKind kind, prefixValue operand, mlir::MLIRContext * context);
prefixValue prefixCompare(ctjs::CompareKind kind, prefixValue left, prefixValue right,
                          mlir::MLIRContext * context);
prefixValue prefixObjectCompare(ctjs::CompareKind kind, prefixValue left, prefixValue right,
                                mlir::MLIRContext * context);
bool prefixPrimitive(prefixValue value);

} // namespace ctcompile::ctnative::host_detail
