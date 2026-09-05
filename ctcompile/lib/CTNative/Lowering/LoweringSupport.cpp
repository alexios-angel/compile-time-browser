#include "LoweringSupport.h"

namespace ctcompile::ctnative {
namespace pdll {

// THE KIND TEST FOR UnaryPlusIsIdentity.pdll. It is C++ rather than an
// attribute literal in the pattern because the literal does not work: mlir-pdll
// cannot parse `attr<"#ctjs.unary_kind<plus>">` without our dialect registered,
// and it drops the constraint and exits 0 rather than saying so.
//
// IT TAKES A ctjs::UnaryOp, NOT AN Operation *, and the pattern's
// `Op<ctjs.unary>` parameter is what does that. The reference's "Native
// Constraint Type Translations" says a NAMED operation constraint whose ODS has
// been included translates to the qualified C++ class rather than to
// `::mlir::Operation *`, and the generated wrapper here is
// `IsUnaryPlusPDLFn(::mlir::PatternRewriter &, ::ctcompile::ctjs::UnaryOp)`.
// The framework has already checked the type by then - ProcessDerivedPDLValue's
// verifyAsArg is a TypeSwitch that fails the constraint on a mismatch - so
// there is nothing left here to dyn_cast and nothing to null-check.
mlir::LogicalResult isUnaryPlus(mlir::PatternRewriter &, ctjs::UnaryOp o) {
    return mlir::success(o.getKind() == ctjs::UnaryKind::Plus);
}

} // namespace pdll
} // namespace ctcompile::ctnative

namespace ctcompile::ctnative::lowering_detail {

#include "UnaryPlusIsIdentity.h.inc"

// What C++ type carries a value of this ctnative type, per the table above.
// `none` is "not representable here", and is the reason for a refusal.
carrier carrierOf(mlir::Type type) {
    if (type == nullptr) { return carrier::none; }
    if (llvm::isa<BoolType>(type)) { return carrier::boolean; }
    if (llvm::isa<NumType>(type)) { return carrier::number; }
    if (llvm::isa<ClosureType>(type)) { return carrier::closure; }
    if (llvm::isa<MethodTableType>(type)) { return carrier::methodTable; }
    if (llvm::isa<ObjectIdentityType>(type)) { return carrier::objectIdentity; }
    if (auto string = llvm::dyn_cast<StrType>(type);
        string && string.getEncoding() == StrEncoding::UTF8) {
        return carrier::string;
    }
    if (auto opt = llvm::dyn_cast<OptType>(type)) {
        if (llvm::isa<BottomType, NumType, BoolType>(opt.getElementType())) {
            return carrier::nullable;
        }
    }
    if (auto map = llvm::dyn_cast<MapType>(type)) {
        const auto key = map.getKeyType();
        const auto string = llvm::dyn_cast<StrType>(key);
        const bool supportedKey =
            llvm::isa<BottomType, NumType, BoolType, ObjectIdentityType>(key) ||
            (string && string.getEncoding() == StrEncoding::UTF8);
        const auto value = map.getValueType();
        const bool ownedValue = llvm::isa<BottomType, NumType>(value) ||
                                (llvm::isa<MapType>(value) && carrierOf(value) == carrier::map);
        return supportedKey && ownedValue ? carrier::map : carrier::none;
    }
    // PHASE 57A: A DENSE ARRAY IS A `std::vector<double>` AND NOTHING ELSE
    // YET. The element carrier decides: `vector<bool>` is a bit-packed
    // specialisation whose `operator[]` returns a proxy that aliases the
    // container and converts differently from `bool` (part 24 Stage 57A says
    // so by name), and a vector of anything with no carrier has none either.
    // So only a numeric element has a representation here, and the refusal
    // for the rest is named at the literal.
    if (auto elements = llvm::dyn_cast<VecType>(type)) {
        auto element = elements.getElementType();
        if (auto opt = llvm::dyn_cast<OptType>(element)) { element = opt.getElementType(); }
        return llvm::isa<BottomType, NumType>(element) ? carrier::vector : carrier::none;
    }
    return carrier::none;
}

bool isScalarCarrier(carrier value) {
    return value == carrier::number || value == carrier::boolean || value == carrier::nullable;
}

bool isNullableCarrier(mlir::Type type) {
    if (!type) { return false; }
    if (auto value = llvm::dyn_cast<ec::LValueType>(type)) { type = value.getValueType(); }
    auto opaque = llvm::dyn_cast<ec::OpaqueType>(type);
    return opaque && opaque.getValue() == kNullableType;
}

// The one C++ type a dense array lowers to. Spelled once: the emitted
// declaration, the helper signatures and the lit test all have to agree, and
// three copies of a string is how they stop agreeing.
mlir::Type vectorCarrierType(mlir::MLIRContext * c) {
    return ec::LValueType::get(ec::OpaqueType::get(c, kVectorType));
}

llvm::StringRef mapKeySpelling(mlir::Type type) {
    if (llvm::isa<BottomType, NumType>(type)) { return "double"; }
    if (llvm::isa<BoolType>(type)) { return "bool"; }
    if (llvm::isa<StrType>(type)) { return "std::string"; }
    if (llvm::isa<ObjectIdentityType>(type)) { return kObjectIdentityType; }
    llvm::report_fatal_error("native Map key has no carrier; admission should refuse it");
}

std::string mapValueSpelling(mlir::Type type) {
    if (llvm::isa<BottomType, NumType>(type)) { return "double"; }
    if (auto map = llvm::dyn_cast<MapType>(type)) {
        return llvm::cast<ec::OpaqueType>(mapCarrierType(map)).getValue().str();
    }
    llvm::report_fatal_error("native Map value has no carrier; admission should refuse it");
}

bool mapNeedsString(MapType type) {
    return llvm::isa<StrType>(type.getKeyType()) ||
           (llvm::isa<MapType>(type.getValueType()) &&
            mapNeedsString(llvm::cast<MapType>(type.getValueType())));
}

mlir::Type mapCarrierType(MapType type) {
    const auto key = mapKeySpelling(type.getKeyType());
    const auto value = type.getValueType();
    const std::string body =
        llvm::isa<MapType>(value)
            ? ("ctnative::map_storage<" + key + ", " + mapValueSpelling(value) + ">").str()
            : ("ctnative::number_map<" + key + ">").str();
    return ec::OpaqueType::get(type.getContext(), "std::shared_ptr<" + body + ">");
}

mlir::Type closureCarrierType(ClosureType type) {
    return ec::OpaqueType::get(type.getContext(),
                               "ctnative::ctn_env_" + cIdentifier(type.getTarget()));
}

mlir::Type methodTableCarrierType(MethodTableType type) {
    return ec::OpaqueType::get(type.getContext(), "std::shared_ptr<ctnative::method_" +
                                                      cIdentifier(type.getSite()) + ">");
}

// Opt includes null or undefined, retained by the tagged scalar carrier.
bool mayBeUndefined(mlir::Type type) {
    return llvm::isa<OptType>(type);
}

mlir::Type carrierType(mlir::MLIRContext * c, carrier which) {
    // `none` HAS NO REPRESENTATION, and returning f64 for it was a silent
    // guess at the one thing this tier exists not to guess at. A value with no
    // proved carrier must be refused by admission long before it gets here;
    // reaching this point means a rule let one through, and a crash naming
    // that is worth far more than a double that happens to verify.
    switch (which) {
    case carrier::nullable: return ec::OpaqueType::get(c, kNullableType);
    case carrier::objectIdentity: return ec::OpaqueType::get(c, kObjectIdentityType);
    case carrier::methodTable:
        llvm::report_fatal_error("method table carrier needs its proved schema");
    case carrier::boolean: return mlir::IntegerType::get(c, 1);
    case carrier::number: return mlir::Float64Type::get(c);
    case carrier::string:
        return ec::OpaqueType::get(c, StrType::get(c, StrEncoding::UTF8).cppCarrier());
    case carrier::structure:
    case carrier::closure:
    case carrier::map:
    case carrier::vector:
    case carrier::none: break;
    }
    llvm::report_fatal_error("ctnative lowering: asked for the C++ carrier of a value that has "
                             "none - admission should have refused it");
}

std::string printed(mlir::Type type) {
    std::string out;
    llvm::raw_string_ostream os{out};
    if (type == nullptr) {
        os << "<unvisited>";
    } else {
        os << type;
    }
    return out;
}

// --- PHASE 59 SLICE 1: A CLOSURE CARRIES BY LIFTING, NOT BY ALLOCATING ----------
//
// A JavaScript closure is a function plus the bindings it captured. The obvious
// C++ for it is a lambda with a capture list, or a `std::function` where the
// callee is not known - and both of those own storage, which is the one thing a
// tier with no collector has to be most careful about. So this slice does not
// build a closure at all. It LIFTS: the captured values become extra LEADING
// parameters of the target function, `ctjs.load_upvalue i` inside it becomes a
// reference to parameter i, `ctjs.create_closure` lowers to nothing exactly as a
// declaration closure already does, and each call passes the captured values as
// ordinary arguments. Zero allocation, no functor, no ownership question.
//
// IT IS AN IR REWRITE THAT RUNS BEFORE THE SOLVE, and that is what makes it
// cheap rather than a second dataflow analysis. Once a closure call is a
// `ctjs.call_direct`, every piece of machinery this file already has works
// unchanged: MLIR's CallOpInterface makes the target reachable to
// DeadCodeAnalysis (an uncalled private function is dead and its types read
// `<unvisited>`), TypeInference propagates each capture's proved type into the
// leading parameter it became, and the call-graph fixpoint in runOnOperation
// closes over the new edge in both directions. The lowering below needed no new
// arm for the call and no new carrier.
//
// WHAT THE IR ACTUALLY DOES, AND WHERE THE BRIEF FOR THIS WORK WAS WRONG.
// "Captures are parent-frame VALUES at construction" is not what the bytecode
// emits: `compiler_impl::is_captured` sets `local::boxed` for a local MENTIONED
// inside a nested function, mutated or not, and `op::new_cell` then boxes it -
// so EVERY from_parent_local capture operand is a `ctjs.create_cell` result and
// none of them has a carrier. Taken literally the admission rule "every
// capture's value has a carrier" would lift nothing at all. What is true is the
// sentence after it: a cell nothing ever writes is a constant box, so this
// unboxes exactly those - every use a `ctjs.cell_get` or a capture of a lifted
// closure, no `ctjs.cell_set`, and no `ctjs.store_upvalue` anywhere the cell can
// reach - and the carrier check then applies to what the cell HOLDS.
//
// AND A CAPTURE THAT IS NOT A CELL COMES THROUGH THE ENCLOSING CLOSURE - PHASE
// 59 SLICE 1b. For a descriptor that is not from_parent_local the VM copies the
// enclosing closure's upvalue into the slot (context::make_closure); the
// operand the importer writes beside it is an `undefined` placeholder nothing
// reads, and WHICH upvalue is on the closure's `enclosing_indices` attribute,
// parallel to the capture list (BytecodeImport.cpp, op::closure). While the
// enclosing function is unlifted this tier does not carry that closure at all,
// and the capture is refused - naming the enclosing closure's own reason,
// because that is the obstacle. Once the enclosing function IS lifted, lift()
// has turned its upvalue k into its capture parameter: the entry-block argument
// 3 + k, in [3, 3 + ctnative.captures), holding the initial of a cell some outer
// frame proved constant. The index names a CELL in the enclosing closure and
// the argument holds the VALUE every read of that cell yields (run_loop.cpp,
// VM_CASE(get_upvalue): `reg = cell->slot`), which is what a lifted call wants:
// a nested closure whose capture is that argument captures the same constant,
// and passing the argument on is exact for exactly the reason slice 1 is. The
// classify-then-lift loop is therefore a FIXPOINT: an outer closure lifts in one
// round and the closure nested in it is judged again in the next, when its
// enclosing function carries `ctnative.captures`. This is what a UMD bundle is
// made of - the module body is the factory's frame, so every closure inside a
// nested function reaches a module binding this way - and it was 15 of the 19
// callees a direct call reaches in bootstrap before this slice.
//
// AND A CELL WITH ONE DOMINATING WRITE IS CONSTANT TOO - PHASE 59 SLICE 2 STEP
// 1. `compiler_impl::predeclare_locals` boxes every `var`/`let`/`const` of a
// body up front holding `undefined`, so a declaration is a ctjs.cell_set into
// an already-built box and slice 1 reads every local binding in the language as
// reassigned. A binding written ONCE is constant after that write, and the
// carrier is then the store's operand rather than the cell's initial. The four
// conditions, and why the initial stops being observable, are stated beside
// `closureLifter::writtenOnce`; `constantValueOf` is the one function that
// picks between the two values, because the admission, the call-site rewrite
// and the unboxing have to agree or a lifted call prints `undefined`.

// The function index the importer put after the last `$` of the symbol. The
// same reading ResolveGlobals does, and the only link there is between a
// `ctjs.create_closure`'s `$function` attribute and the `ctjs.func` it names.
std::optional<unsigned> functionIndexOf(ctjs::FuncOp fn) {
    const llvm::StringRef name = fn.getSymName();
    const std::size_t dollar = name.rfind('$');
    if (dollar == llvm::StringRef::npos) { return std::nullopt; }
    unsigned index = 0;
    if (name.substr(dollar + 1).getAsInteger(10, index)) { return std::nullopt; }
    return index;
}

// Where a `ctjs.create_closure`'s captures start: after $enclosing_closure and
// $enclosing_this, which are operands and not attributes.

bool isUndefinedConstant(mlir::Value v) {
    auto k = v.getDefiningOp<ctjs::ConstantOp>();
    return k && llvm::isa<ctjs::UndefinedAttr>(k.getValue());
}

// The constant string a property key operand carries, or empty. The same
// reading TypeInference and admission::keyOf make; spelled here because the
// lift runs before either of them is available.
llvm::StringRef constantKeyOf(mlir::Value key) {
    auto constant = key.getDefiningOp<ctjs::ConstantOp>();
    if (!constant) { return {}; }
    auto str = llvm::dyn_cast<ctjs::StringAttr>(constant.getValue());
    return str ? str.getValue() : llvm::StringRef{};
}

// What the rewrite did, for the `report` remark. Pass statistics are compiled
// out of the LLVM package this builds against, so a counter that is asserted
// has to be printed.
// Every value that names the same object as `v`, `v` included. A literal
// nothing is lifted onto is a group of one, so callers need no special case.
llvm::SmallVector<mlir::Value, 2> aliasesOf(const receiverGroups * groups, mlir::Value v) {
    if (groups != nullptr) {
        const auto entry = groups->find(v);
        if (entry != groups->end()) { return entry->second; }
    }
    return {v};
}

// PART 24 PHASE 63 STEP 7: a provenance comment above every generated
// definition, so a C++ diagnostic on generated code maps back to the
// JavaScript site. The importer fuses a NameLoc with the FileLineColLoc of
// the site; the first file location found, at any depth, is the site.
std::string siteOf(mlir::Location loc) {
    std::string site;
    loc->walk([&](mlir::Location l) {
        if (auto file = llvm::dyn_cast<mlir::FileLineColLoc>(l)) {
            site = file.getFilename().str() + ":" + std::to_string(file.getLine()) + ":" +
                   std::to_string(file.getColumn());
            return mlir::WalkResult::interrupt();
        }
        return mlir::WalkResult::advance();
    });
    return site.empty() ? std::string{"<no source location>"} : site;
}
// The importer gives a ctjs.func no location of its own; its first located
// operation is the function's site.
std::string siteOfFunction(ctjs::FuncOp fn) {
    std::string site = siteOf(fn.getLoc());
    if (site != "<no source location>") { return site; }
    fn.getBody().walk([&](mlir::Operation * o) {
        const std::string here = siteOf(o->getLoc());
        if (here == "<no source location>") { return mlir::WalkResult::advance(); }
        site = here;
        return mlir::WalkResult::interrupt();
    });
    return site;
}

std::string cIdentifier(llvm::StringRef symbol) {
    std::string name = symbol.str();
    for (char & ch : name) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) { ch = '_'; }
    }
    return name;
}

// THE DECLARATIVE RULE'S PATTERN SET, BUILT ONCE. Freezing a PDL pattern
// compiles its bytecode, which is not something to redo per function - and
// FrozenRewritePatternSet is what both greedy entry points take.
mlir::FrozenRewritePatternSet declarativePatterns(mlir::MLIRContext * context) {
    mlir::RewritePatternSet patterns(context);
    populateGeneratedPDLLPatterns(patterns);
    return patterns;
}

} // namespace ctcompile::ctnative::lowering_detail
