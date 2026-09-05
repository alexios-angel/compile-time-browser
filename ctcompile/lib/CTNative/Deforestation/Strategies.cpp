#include "Strategies.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSwitch.h"

namespace ctcompile::ctnative::deforestation_detail {
namespace {

constexpr llvm::StringLiteral kVector = "std::vector<double>";
constexpr llvm::StringLiteral kScalar = "ctnative::nullable_scalar";

bool opaque(mlir::Type type, llvm::StringRef name) {
    auto value = llvm::dyn_cast<ec::OpaqueType>(type);
    return value && value.getValue() == name;
}

bool vector(mlir::Type type) {
    if (auto local = llvm::dyn_cast<ec::LValueType>(type)) { type = local.getValueType(); }
    return opaque(type, kVector);
}

bool scalar(mlir::Type type) {
    return llvm::isa<mlir::FloatType, mlir::IntegerType>(type) || opaque(type, kScalar);
}

bool ordinaryCall(ec::CallOpaqueOp call) {
    return !call.getArgs() && !call.getTemplateArgs();
}

bool emptyLocal(ec::VariableOp local) {
    auto initializer = llvm::dyn_cast<ec::OpaqueAttr>(local.getValue());
    return initializer && initializer.getValue().empty();
}

// These helpers are part of the native runtime ABI. Their implementations
// inspect scalar values or standard containers; none invokes JavaScript or
// writes a Map. Unknown direct/opaque calls are barriers even if no explicit
// Map operand is visible: a callee can retain an alias in its environment.
bool readOnlyCall(ec::CallOpaqueOp call) {
    if (!ordinaryCall(call)) { return false; }
    return llvm::StringSwitch<bool>(call.getCallee())
        .Cases({"ctnative::map_keys", "ctnative::map_values", "ctnative::map_size"}, true)
        .Cases({"ctnative::map_has", "ctnative::map_get", "ctnative::map_get_present"}, true)
        .Cases({"ctnative::vec_length", "ctnative::vec_at"}, true)
        .Cases({"ctnative::map_snapshot_at<true>", "ctnative::map_snapshot_at<false>"}, true)
        .Cases({"ctnative::to_number", "ctnative::to_nullable"}, true)
        .Cases({"ctnative::scalar_truthy", "ctnative::scalar_strict_equal"}, true)
        .Cases({"ctnative::scalar_equal", "ctnative::scalar_typeof"}, true)
        .Cases({"static_cast<double>", "static_cast<void>"}, true)
        .Cases({"std::fabs", "std::isfinite", "std::fmod", "std::pow"}, true)
        .Default(false);
}

bool readOnly(mlir::Operation * op) {
    if (op->getNumRegions() || op->getNumSuccessors()) { return false; }
    if (auto call = llvm::dyn_cast<ec::CallOpaqueOp>(op)) { return readOnlyCall(call); }
    if (auto constant = llvm::dyn_cast<ec::ConstantOp>(op)) {
        if (llvm::isa<mlir::FloatAttr, mlir::IntegerAttr>(constant.getValue())) { return true; }
        // An arbitrary opaque initializer is C++ code, not a pure constant.
        auto value = llvm::dyn_cast<ec::OpaqueAttr>(constant.getValue());
        return value && opaque(constant.getResult().getType(), kScalar) &&
               (value.getValue() == "ctnative::nullable_scalar{}" ||
                value.getValue() == "ctnative::nullable_scalar::null()");
    }
    if (auto local = llvm::dyn_cast<ec::VariableOp>(op)) {
        return vector(local.getResult().getType()) && emptyLocal(local);
    }
    if (auto assign = llvm::dyn_cast<ec::AssignOp>(op)) {
        // A numeric vector copy cannot modify the Map's pair-entry storage.
        // Writes to this candidate's own slots were checked by flow inference.
        return vector(assign.getVar().getType()) && vector(assign.getValue().getType());
    }
    if (auto load = llvm::dyn_cast<ec::LoadOp>(op)) {
        return scalar(load.getResult().getType()) || vector(load.getResult().getType());
    }
    if (llvm::isa<ec::AddOp, ec::SubOp, ec::MulOp, ec::DivOp, ec::RemOp, ec::CmpOp, ec::CastOp,
                  ec::LogicalAndOp, ec::LogicalOrOp, ec::LogicalNotOp, ec::UnaryMinusOp,
                  ec::UnaryPlusOp, ec::ConditionalOp, ec::BitwiseAndOp, ec::BitwiseOrOp,
                  ec::BitwiseXorOp, ec::BitwiseNotOp, ec::BitwiseLeftShiftOp,
                  ec::BitwiseRightShiftOp>(op)) {
        return llvm::all_of(op->getOperandTypes(), scalar) &&
               llvm::all_of(op->getResultTypes(), scalar);
    }
    return false;
}

struct constraints {
    ec::CallOpaqueOp producer;
    unsigned remaining;
    std::string & reason;
    llvm::SmallVector<mlir::Value> pending;
    llvm::DenseSet<mlir::Value> visited;
    llvm::SmallVector<mlir::Operation *> forwarding;
    llvm::SmallPtrSet<mlir::Operation *, 8> recorded;
    llvm::SmallVector<ec::CallOpaqueOp> consumers;

    bool spend() {
        if (remaining) {
            --remaining;
            return true;
        }
        reason = "fusion inference exceeded its scan budget";
        return false;
    }

    void forward(mlir::Operation * op) {
        if (recorded.insert(op).second) { forwarding.push_back(op); }
    }

    bool connect(mlir::Value value, mlir::Operation * at) {
        if (at->getBlock() != producer->getBlock() || !producer->isBeforeInBlock(at)) {
            reason = "snapshot flow crosses a control boundary";
            return false;
        }
        if (visited.insert(value).second) { pending.push_back(value); }
        return true;
    }

    bool slot(ec::AssignOp assignment) {
        auto local = assignment.getVar().getDefiningOp<ec::VariableOp>();
        if (!local || local->getBlock() != producer->getBlock() || !emptyLocal(local) ||
            !vector(local.getResult().getType())) {
            reason = "snapshot is stored outside a fresh local slot";
            return false;
        }
        for (mlir::Operation * user : local.getResult().getUsers()) {
            if (!spend()) { return false; }
            auto other = llvm::dyn_cast<ec::AssignOp>(user);
            if (other && other.getVar() == local.getResult() && other != assignment) {
                reason = "snapshot slot has conflicting producer bounds";
                return false;
            }
            // Flow and consumer checks reject users in other blocks. Within
            // this block, the producer must already be stored when observed.
            if (user != assignment.getOperation() && user->getBlock() == assignment->getBlock() &&
                !assignment->isBeforeInBlock(user)) {
                reason = "snapshot slot is observed before its producer assignment";
                return false;
            }
        }
        forward(local);
        forward(assignment);
        return connect(local.getResult(), assignment);
    }

    bool collect() {
        visited.insert(producer.getResult(0));
        pending.push_back(producer.getResult(0));
        while (!pending.empty()) {
            mlir::Value value = pending.pop_back_val();
            if (!spend()) { return false; }
            for (mlir::OpOperand & use : value.getUses()) {
                if (!spend()) { return false; }
                mlir::Operation * user = use.getOwner();
                if (auto assign = llvm::dyn_cast<ec::AssignOp>(user)) {
                    if (assign.getVar() == value) { continue; }
                    if (!slot(assign)) { return false; }
                    continue;
                }
                if (auto load = llvm::dyn_cast<ec::LoadOp>(user);
                    load && vector(load.getResult().getType())) {
                    forward(load);
                    if (!connect(load.getResult(), load)) { return false; }
                    continue;
                }
                auto call = llvm::dyn_cast<ec::CallOpaqueOp>(user);
                if (!call || !ordinaryCall(call) || use.getOperandNumber() != 0 ||
                    call.getNumResults() != 1) {
                    reason = "snapshot has an escaping or unsupported consumer bound";
                    return false;
                }
                const bool length = call.getCallee() == "ctnative::vec_length" &&
                                    call.getNumOperands() == 1 &&
                                    llvm::isa<mlir::Float64Type>(call.getResult(0).getType());
                const bool index = call.getCallee() == "ctnative::vec_at" &&
                                   call.getNumOperands() == 2 &&
                                   scalar(call.getOperand(1).getType()) &&
                                   opaque(call.getResult(0).getType(), kScalar);
                if (!length && !index) {
                    reason = "snapshot has an escaping or unsupported consumer bound";
                    return false;
                }
                consumers.push_back(call);
            }
        }
        return true;
    }
};

} // namespace

bool isSnapshot(ec::CallOpaqueOp call) {
    if (!ordinaryCall(call) || call.getNumOperands() != 1 || call.getNumResults() != 1 ||
        !opaque(call.getResult(0).getType(), kVector)) {
        return false;
    }
    auto map = llvm::dyn_cast<ec::OpaqueType>(call.getOperand(0).getType());
    if (!map || !map.getValue().ends_with(">>")) { return false; }
    if (call.getCallee() == "ctnative::map_values") {
        return map.getValue().starts_with("std::shared_ptr<ctnative::number_map<");
    }
    if (call.getCallee() == "ctnative::map_keys") {
        return map.getValue() == "std::shared_ptr<ctnative::number_map<double>>" ||
               map.getValue().starts_with("std::shared_ptr<ctnative::map_storage<double, ");
    }
    return false;
}

std::optional<strategy> inferStrategy(ec::CallOpaqueOp producer, unsigned maxScan,
                                      std::string & reason) {
    constraints flow{producer, maxScan, reason, {}, {}, {}, {}, {}};
    if (!flow.collect()) { return std::nullopt; }
    // Lumberhack §5.4: conflicting consumer upper bounds yield identity.
    // No producer duplication or eager evaluation of consumer code is used.
    if (flow.consumers.size() != 1) {
        reason = flow.consumers.empty() ? "snapshot has no scalar consumer strategy"
                                        : "snapshot has conflicting consumer strategies";
        return std::nullopt;
    }
    ec::CallOpaqueOp consumer = flow.consumers.front();
    if (consumer->getBlock() != producer->getBlock() || !producer->isBeforeInBlock(consumer)) {
        reason = "snapshot consumer crosses a control boundary";
        return std::nullopt;
    }
    for (mlir::Operation * op = producer->getNextNode(); op != consumer.getOperation();
         op = op->getNextNode()) {
        if (!flow.spend()) { return std::nullopt; }
        if (!readOnly(op)) {
            reason = "snapshot observation crosses an effect or control barrier";
            return std::nullopt;
        }
    }
    return strategy{producer, consumer,
                    consumer.getCallee() == "ctnative::vec_length" ? consumption::length
                                                                   : consumption::index,
                    producer.getCallee() == "ctnative::map_keys", std::move(flow.forwarding)};
}

} // namespace ctcompile::ctnative::deforestation_detail
