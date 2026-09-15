#pragma once

#include "HostContractFixtures.h"
#include "mlir/IR/AsmState.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

namespace ctcompile::test::host_contract {

inline void checkFingerprint(mlir::MLIRContext & context) {
    auto module = mlir::parseSourceString<mlir::ModuleOp>(fixture, &context);
    check(static_cast<bool>(module), "fingerprint fixture parses");
    if (!module) { return; }
    const auto printed = [](mlir::ModuleOp source, bool locations) {
        std::string text;
        llvm::raw_string_ostream out(text);
        source.print(out, mlir::OpPrintingFlags().printGenericOpForm().enableDebugInfo(locations));
        return text;
    };
    const auto checked = [&] {
        const auto before = printed(*module, true);
        const auto result = hostContractFingerprint(*module);
        // Preserve the old always-clone algorithm as the compatibility oracle.
        mlir::OwningOpRef<mlir::ModuleOp> copy = module->clone();
        ctnative::clearHostContractReports(*copy);
        const auto text = printed(*copy, false);
        const auto digest = llvm::SHA256::hash(llvm::ArrayRef<std::uint8_t>(
            reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
        check(result == llvm::toHex(digest, true), "fingerprint matches the old canonical digest");
        check(printed(*module, true) == before, "fingerprinting never mutates source IR");
        return result;
    };
    const auto original = checked();
    auto function = module->lookupSymbol<ctjs::FuncOp>("publish$1");
    ctjs::ConstantOp constant;
    function.walk([&](ctjs::ConstantOp candidate) {
        if (llvm::isa<ctjs::StringAttr>(candidate.getValue())) { constant = candidate; }
    });
    check(static_cast<bool>(constant), "fingerprint fixture contains a nested source constant");
    if (!constant) { return; }
    mlir::Builder builder(&context);
    for (mlir::Operation * owner :
         {constant.getOperation(), function.getOperation(), module->getOperation()}) {
        owner->setAttr("ctnative.host_proved", builder.getBoolAttr(true));
        owner->setAttr("ctnative.host_", builder.getStringAttr("also ignored"));
        check(checked() == original, "reports at every operation depth leave the digest unchanged");
        owner->removeAttr("ctnative.host_proved");
        owner->removeAttr("ctnative.host_");
    }
    for (llvm::StringRef name : {"ctnative.host", "ctnative.hosted", "ctnative.unboxed"}) {
        constant->setAttr(name, builder.getUnitAttr());
        check(checked() != original, "nonreport attributes remain bound by the fingerprint");
        constant->removeAttr(name);
    }
    const auto sourceValue = constant.getValue();
    for (bool decorated : {false, true}) {
        if (decorated) { (*module)->setAttr("ctnative.host_proved", builder.getBoolAttr(true)); }
        constant.setValueAttr(ctjs::StringAttr::get(&context, "changed"));
        check(checked() != original, "both fingerprint paths bind current source mutations");
        constant.setValueAttr(sourceValue);
        check(checked() == original, "restoring source content restores the canonical digest");
    }
    (*module)->removeAttr("ctnative.host_proved");
    constant->setLoc(mlir::FileLineColLoc::get(&context, "moved.js", 17, 3));
    check(checked() == original, "presentation locations remain outside the source fingerprint");
}

} // namespace ctcompile::test::host_contract
