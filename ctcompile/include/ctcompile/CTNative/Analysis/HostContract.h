#pragma once

#include "ctcompile/CTJS/IR/CTJSOps.h"
#include "ctcompile/CTNative/Analysis/PrimitiveAlternatives.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/TypeID.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <optional>
#include <string>
#include <vector>

namespace ctcompile::ctnative {

struct HostRootRequest {
    std::string binding;
    std::vector<std::string> properties;
};

// Driver input, never an IR proof annotation. closed-source-v1 starts with
// ordinary own global bindings and unmodified intrinsic prototypes; all
// subsequent source mutations/calls still require the analysis below.
struct HostContract {
    std::string moduleSha256;
    std::string entry;
    std::vector<HostRootRequest> roots;
    std::vector<std::string> observations;
    std::vector<std::string> absentBindings;
    std::vector<std::string> undefinedBindings;
    // Explicit identities supplied by the embedding, not effect summaries.
    // Only standard Map/Array are supported. Source replacement/escape refuses.
    std::vector<std::string> initialIntrinsics;
    bool realmGlobalThis = false;
    // The embedding invokes this entry as a classic script, with its stable
    // realm receiver. This does not characterize an ordinary call's `this`.
    // Listed realm properties initially exist as writable own data slots;
    // reading/writing them cannot call JS. Their values remain unknown.
    bool classicScriptRealm = false;
    std::vector<std::string> realmOwnDataProperties;
};

llvm::Expected<HostContract> parseHostContract(llvm::StringRef json);
// Includes the whole driver/program IR. Presentation locations and previous
// host-contract reports are excluded; all semantic operations remain bound.
std::string hostContractFingerprint(mlir::ModuleOp module);
void clearHostContractReports(mlir::ModuleOp module);

struct HostSlotEdge {
    ctjs::SetPropertyOp write;
    ctjs::GetPropertyOp read;
};

// Finite primitive categories or empty object arguments, proved over the
// complete current call census. Object positions have no primitive alternatives;
// their actual allocation and allowed uses are checked independently. The
// captured Map may retain them as keys or payloads; they have no outgoing edges.
struct HostMethodParameters {
    ctjs::FuncOp function;
    std::vector<PrimitiveAlternatives> alternatives;
    std::vector<mlir::BlockArgument> objectKeys{};
    bool operator==(const HostMethodParameters & other) const {
        return function == other.function && alternatives == other.alternatives &&
               objectKeys == other.objectKeys;
    }
};

struct HostMethodArgument {
    mlir::BlockArgument parameter;
    mlir::Value actual;
    PrimitiveAlternatives alternatives;
    ctjs::CreateObjectOp object{};
};

// One immutable environment slot owns this exact standard Map, constructed
// empty. The complete live census of every closure sharing that slot permits
// primitive contents, fresh method-local leaves with fixed scalar fields, or
// checked empty caller leaves, fresh child Maps, and standard
// size/set/get/has/delete/clear effects. Child Maps cannot retain Maps.
// Caller leaves permit only key/payload uses; method-local leaves cannot be keys.
// No object escapes through a method result, field or unchecked use. Effects remain
// runtime; no startup value or result type is promised. Optional cell operations describe
// the original binding; after lifting, the call reads its environment value.
// Every handle is rederived from the current module, never from native markers.
struct HostCapturedMap {
    ctjs::LoadGlobalOp intrinsic;
    ctjs::ConstructOp allocation;
    ctjs::CreateCellOp cell;
    ctjs::CellSetOp initialization;
    std::vector<ctjs::CreateClosureOp> closures;
    std::vector<HostMethodParameters> parameters;
    // Complete family effects; argument below belongs to this actual call.
    std::vector<ctjs::LoadUpvalueOp> upvalues;
    std::vector<ctjs::GetPropertyOp> reads;
    std::vector<ctjs::CallOp> calls;
    std::vector<ctjs::CreateObjectOp> leafObjects;
    std::vector<ctjs::SetPropertyOp> leafWrites;
    std::vector<ctjs::GetPropertyOp> leafReads;
    ctjs::LoadUpvalueOp argument;
    std::vector<ctjs::ConstructOp> childMaps{};
    // Every outer set stores a checked fresh child, across the complete family.
    // This promises neither a returned child's identity nor its contents.
    bool childMapContents = false;
    std::vector<mlir::Value> returnedChildMaps{};
};

// Evidence for this actual call, not a promise about future exported callers
// or private visibility. The current own-data initializer stores this exact
// source closure. The original receiver and arguments remain live.
struct HostCallableEdge {
    mlir::Operation * call = nullptr;
    ctjs::GetPropertyOp read;
    ctjs::SetPropertyOp write;
    ctjs::CreateClosureOp closure;
    ctjs::FuncOp function;
    std::optional<HostCapturedMap> capturedMap;
    std::vector<HostMethodArgument> arguments;
};

// A proved Number, Boolean or String origin in an ordinary source global. Each read has
// one earlier entry store; dependencies are the completed published results
// encountered in its value expression, in traversal order (possibly repeated).
// The list is empty for constant-only scalar expressions; their source scope,
// initialization and complete environment proof remain mandatory.
// These are live source edges, not constant values or a native type promise.
struct HostScalarGlobalRead {
    ctjs::StoreGlobalOp initialization;
    ctjs::LoadGlobalOp read;
    mlir::Value value;
    PrimitiveAlternatives alternatives;
    std::vector<mlir::Value> dependencies;
};

// A fresh empty object stored once in an ordinary entry global, or an alias
// reached through a complete acyclic chain of those bindings. Initialization
// belongs to this read's binding; object remains the original allocation.
// Every read follows its store, and all uses are checked initializations or
// key/payload arguments in the completed captured-Map family. Loads/stores remain live.
struct HostObjectGlobalRead {
    ctjs::StoreGlobalOp initialization;
    ctjs::LoadGlobalOp read;
    ctjs::CreateObjectOp object;
};

struct HostSlotReport {
    std::string binding;
    std::string property;
    ctjs::CreateObjectOp owner;
    std::vector<ctjs::SetPropertyOp> writes;
    std::vector<ctjs::GetPropertyOp> reads;
    std::vector<HostSlotEdge> edges;
    std::string reason;
};

// A live, module-specific result. Diagnostic slot reports may expose partial
// evidence; property() returns a proof only when every contract obligation
// passed. Mutating the module invalidates this object. No native pass consumes
// the printed report attributes as authority.
class HostContractAnalysis {
public:
    HostContractAnalysis(mlir::ModuleOp module, const HostContract & contract,
                         unsigned maxSteps = 100000);

    [[nodiscard]] bool proved() const { return refusal.empty(); }
    [[nodiscard]] llvm::StringRef reason() const { return refusal; }
    // Bounded proof work, available to consumers sharing one analysis budget.
    [[nodiscard]] unsigned steps() const { return workSteps; }
    [[nodiscard]] bool exhausted() const { return budgetExhausted; }
    [[nodiscard]] llvm::ArrayRef<HostSlotReport> slots() const { return reports; }
    [[nodiscard]] llvm::ArrayRef<ctjs::StoreGlobalOp> observations() const { return observed; }
    [[nodiscard]] const HostSlotEdge * property(ctjs::GetPropertyOp read) const;
    [[nodiscard]] llvm::ArrayRef<HostCallableEdge> callables() const { return checkedCalls; }
    [[nodiscard]] const HostCallableEdge * callable(mlir::Operation * call) const;
    [[nodiscard]] llvm::ArrayRef<HostScalarGlobalRead> scalarReads() const {
        return checkedScalarReads;
    }
    [[nodiscard]] const HostScalarGlobalRead * scalarRead(ctjs::LoadGlobalOp read) const;
    [[nodiscard]] llvm::ArrayRef<HostObjectGlobalRead> objectReads() const {
        return checkedObjectReads;
    }
    [[nodiscard]] const HostObjectGlobalRead * objectRead(ctjs::LoadGlobalOp read) const;

private:
    std::string refusal;
    std::vector<HostSlotReport> reports;
    std::vector<ctjs::StoreGlobalOp> observed;
    std::vector<HostCallableEdge> checkedCalls;
    std::vector<HostScalarGlobalRead> checkedScalarReads;
    std::vector<HostObjectGlobalRead> checkedObjectReads;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
};

} // namespace ctcompile::ctnative
