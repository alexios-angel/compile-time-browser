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

// Finite primitive categories proved over the complete current call census.
// They describe possible values, never the value of an earlier invocation.
struct HostMethodParameters {
    ctjs::FuncOp function;
    std::vector<PrimitiveAlternatives> alternatives;
    bool operator==(const HostMethodParameters & other) const {
        return function == other.function && alternatives == other.alternatives;
    }
};

struct HostPrimitiveArgument {
    mlir::BlockArgument parameter;
    mlir::Value actual;
    PrimitiveAlternatives alternatives;
};

// One immutable environment slot owns this exact standard Map, constructed
// empty. The complete live census of every closure sharing that slot permits
// primitive contents or fresh method-local leaf objects with fixed scalar
// fields, and standard size/set/get/has/delete effects. Object payloads cannot
// escape through a method result, field, key or unchecked use. Effects remain
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
    ctjs::LoadUpvalueOp argument;
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
    std::vector<HostPrimitiveArgument> arguments;
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

private:
    std::string refusal;
    std::vector<HostSlotReport> reports;
    std::vector<ctjs::StoreGlobalOp> observed;
    std::vector<HostCallableEdge> checkedCalls;
    unsigned workSteps = 0;
    bool budgetExhausted = false;
};

} // namespace ctcompile::ctnative
