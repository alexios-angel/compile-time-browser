//===- ClosureLifting/ClassInheritance.cpp - Flattening `extends` ---------===//
//
// A class that extends another class of the same module becomes a class that
// extends nothing: every member it inherits is copied onto it as a member of
// its own, and its `super(...)` and `super.m(...)` become method calls on its
// own instance. Each concrete class then has its own copy of every function
// that runs on its instances, so `this` in a copy names exactly one class,
// an overridden method called through `this` resolves to one function at
// compile time, and the class census (Classes.cpp) and the constructor,
// method and static-member rules see an ordinary class.
//
// For `class D extends P`, after P itself is flattened:
//  - each prototype method and each static method or static getter of P that
//    D does not define becomes a member of D, backed by a copy of P's
//    function named `<function>_<D>`;
//  - P's constructor is copied the same way and stored on D's prototype as
//    the method "super:<P's function>:constructor", and every `super.m`
//    method D's own methods call is copied as "super:<P's function>:m". A
//    flattened P already carries its own "super:..." members, which D
//    inherits as ordinary members, so a `super` chain several classes deep
//    names the right function at every level;
//  - in D's constructor, `super(a, b)` becomes `this["super:...:constructor"]
//    (a, b)`, the flag the importer keeps to check that `super` runs once and
//    before `this` is read folds to a constant, and the constructor returns
//    undefined, which `new` reads as "the instance", as it read `this`;
//  - a constructor D does not write (the importer's `constructor(...args) {
//    super(...args); }`) is replaced by a copy of P's constructor named after
//    D, which receives the same arguments;
//  - the `__ctbrowser_class_heritage` call goes, so D extends nothing.
//
// A parent that nothing uses any more once its children carry their own
// copies (never constructed, its statics never read, its binding never read)
// is erased with its members, as are functions no closure names any more.
// The module's ctnative.inheritance_summary counts the copies and erasures;
// ctnative.copies maps every copy to the source function it stands for, and
// ctnative.inheritance_erased lists the source functions erased, so a count
// of source functions can be rebuilt after later passes prune functions.
//
// A class is left as it is, with the reason on its constructor closure
// (kNativeInheritanceReason), when its parent is not one class of this module
// or is not flattened, when it or a class in its chain declares fields (the
// importer's `__fields`) or a prototype accessor, when its constructor reads
// `super` other than as one `super(...)` call or reaches `this` from a nested
// function, when a member reads `super` other than as a `super.m(...)` call
// on `this`, when a constructor in the chain reads new.target or may return
// an object (which `super(...)` would make `this`), or when an
// `instanceof` test names a class of the chain, which a flattened class
// would answer differently. A class that flows anywhere as a value (stored,
// passed, chosen by a condition) is flattened all the same: native code has
// no carrier for a class value, so the rule that meets that use refuses it,
// and no native function can observe the chain the rewrite removed.
//
//===----------------------------------------------------------------------===//

#include "ClassInheritance.h"

#include "../LoweringSupport.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Interfaces/LoopLikeInterface.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringSet.h"

#include <optional>
#include <string>

namespace ctcompile::ctnative::lowering_detail {
namespace {

using Reason = std::optional<std::string>;

/// Whether `call` calls the engine helper `name` (Classes.cpp).
bool callsIntrinsic(ctjs::CallOp call, llvm::StringRef name) {
    auto load = call.getCallee().getDefiningOp<ctjs::LoadGlobalOp>();
    return load && load.getName() == name;
}

/// The constructor closure `read` names when every store into its binding
/// stores that one closure (Classes.cpp keeps the same rule), or null.
ctjs::CreateClosureOp boundConstructor(mlir::Value read) {
    auto get = read.getDefiningOp<ctjs::CellGetOp>();
    auto cell = get ? get.getCell().getDefiningOp<ctjs::CreateCellOp>() : ctjs::CreateCellOp{};
    if (!cell) { return read.getDefiningOp<ctjs::CreateClosureOp>(); }
    ctjs::CreateClosureOp held;
    for (mlir::Operation * user : cell.getResult().getUsers()) {
        auto set = llvm::dyn_cast<ctjs::CellSetOp>(user);
        if (!set || set.getCell() != cell.getResult()) { continue; }
        auto made = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
        if (!made || (held && held != made)) { return {}; }
        held = made;
    }
    return held;
}

/// The function name without the importer's `$N` index.
llvm::StringRef stemOf(ctjs::FuncOp fn) {
    llvm::StringRef name = fn.getSymName();
    return name.substr(0, name.rfind('$'));
}

/// A class as the importer spells it (Classes.cpp's file header).
struct jsClassShape {
    ctjs::CreateClosureOp constructor;
    ctjs::CallOp defined;
    ctjs::CallOp heritage;
    ctjs::CreateObjectOp prototype;
    ctjs::CreateClosureOp parent;
    /// Members by key: prototype methods, static methods, static getters.
    llvm::MapVector<llvm::StringRef, ctjs::CreateClosureOp> methods, statics, staticGetters;
    bool flat = false;
    bool refused = false;
    /// A class flattened from this one copied its members.
    bool parentOfFlattened = false;
    bool erased = false;
};

/// A `super.m` read: `load_home`, `get_proto` and a constant-key read.
struct superRead {
    ctjs::LoadHomeOp home;
    ctjs::GetProtoOp proto;
    ctjs::GetPropertyOp read;
    llvm::StringRef key;
};

struct flattener {
    mlir::ModuleOp module;
    mlir::MLIRContext * context;
    llvm::MapVector<mlir::Operation *, jsClassShape> classes;
    llvm::DenseMap<unsigned, ctjs::FuncOp> byIndex;
    unsigned nextIndex = 0;
    /// Functions this run added as copies and erased as unreachable.
    int64_t copies = 0, erased = 0;
    /// Each copy this run made, by symbol: the source function it stands for.
    llvm::MapVector<llvm::StringRef, mlir::StringAttr> copiesOf;
    /// The source functions (not copies) this run erased.
    llvm::SmallVector<mlir::Attribute> erasedSources;

    explicit flattener(mlir::ModuleOp m) : module(m), context(m.getContext()) {}

    /// The name a copy is named after: the function it was first copied
    /// from, without the class names earlier copies appended.
    llvm::DenseMap<mlir::Operation *, std::string> originalStem;

    std::string stemFor(ctjs::FuncOp fn) {
        auto found = originalStem.find(fn.getOperation());
        return found == originalStem.end() ? stemOf(fn).str() : found->second;
    }

    ctjs::FuncOp targetOf(ctjs::CreateClosureOp c) {
        return byIndex.lookup(static_cast<unsigned>(c.getFunction()));
    }

    void index() {
        module.walk([&](ctjs::FuncOp fn) {
            if (auto at = ctjs::functionIndex(fn)) {
                byIndex.try_emplace(*at, fn);
                nextIndex = std::max(nextIndex, *at + 1);
            }
        });
    }

    /// Finds every class and its members. A member the shape does not have
    /// (an accessor with a setter, a write that is not a member definition)
    /// is left for the class census to refuse.
    void census() {
        module.walk([&](ctjs::CallOp call) {
            if (!callsIntrinsic(call, "__ctbrowser_class_defined") || call.getArgs().size() != 1) {
                return;
            }
            if (auto ctor = call.getArgs()[0].getDefiningOp<ctjs::CreateClosureOp>()) {
                jsClassShape & made = classes[ctor.getOperation()];
                made.constructor = ctor;
                made.defined = call;
            }
        });
        for (auto & [key, cls] : classes) {
            (void)key;
            for (mlir::OpOperand & use : cls.constructor.getResult().getUses()) {
                mlir::Operation * user = use.getOwner();
                if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                    set && use.getOperandNumber() == 0) {
                    const llvm::StringRef name = ctjs::constantKey(set.getKey());
                    auto member = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                    if (name == "prototype") {
                        cls.prototype = set.getValue().getDefiningOp<ctjs::CreateObjectOp>();
                    } else if (!name.empty() && name != "__home" && member) {
                        cls.statics[name] = member;
                    }
                } else if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(user);
                           accessor && use.getOperandNumber() == 0) {
                    if (auto getter = accessor.getGetter().getDefiningOp<ctjs::CreateClosureOp>()) {
                        cls.staticGetters[accessor.getName()] = getter;
                    }
                } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(user);
                           call && callsIntrinsic(call, "__ctbrowser_class_heritage") &&
                           call.getArgs().size() == 3 && call.getArgs()[0] == cls.constructor) {
                    cls.heritage = call;
                    cls.parent = boundConstructor(call.getArgs()[1]);
                }
            }
            if (!cls.prototype) { continue; }
            for (mlir::Operation * user : cls.prototype.getResult().getUsers()) {
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
                if (!set || set.getObject() != cls.prototype.getResult()) { continue; }
                const llvm::StringRef name = ctjs::constantKey(set.getKey());
                auto member = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                if (!name.empty() && name != "constructor" && member) {
                    cls.methods[name] = member;
                }
            }
        }
    }

    jsClassShape * classOf(ctjs::CreateClosureOp ctor) {
        auto found = classes.find(ctor.getOperation());
        return found == classes.end() ? nullptr : &found->second;
    }

    /// Why `cls` declares something this rewrite cannot copy: fields, or a
    /// prototype accessor.
    static Reason unsupportedMembers(jsClassShape & cls, llvm::StringRef who) {
        if (cls.statics.contains("__fields")) { return (who + " declares class fields").str(); }
        for (mlir::Operation * user : cls.prototype.getResult().getUsers()) {
            if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(user);
                accessor && accessor.getTarget() == cls.prototype.getResult()) {
                return (who + " has the accessor `" + accessor.getName() + "`").str();
            }
        }
        return std::nullopt;
    }

    /// Every `load_home` in `fn` read as `super.<key>`, or why one is not.
    /// `constructorRead` admits `super.constructor`, which is how the
    /// importer spells the callee of `super(...)`.
    static Reason superReads(ctjs::FuncOp fn, llvm::SmallVectorImpl<superRead> & out) {
        Reason why;
        fn.walk([&](ctjs::LoadHomeOp home) {
            if (why) { return; }
            // A home read by __ctbrowser_init_fields is the field initialiser
            // call, which unsupportedMembers proved has nothing to do.
            if (home.getResult().hasOneUse()) {
                if (auto call = llvm::dyn_cast<ctjs::CallOp>(*home.getResult().getUsers().begin());
                    call && callsIntrinsic(call, "__ctbrowser_init_fields")) {
                    return;
                }
            }
            auto proto =
                home.getResult().hasOneUse()
                    ? llvm::dyn_cast<ctjs::GetProtoOp>(*home.getResult().getUsers().begin())
                    : ctjs::GetProtoOp{};
            auto read =
                proto && proto.getResult().hasOneUse()
                    ? llvm::dyn_cast<ctjs::GetPropertyOp>(*proto.getResult().getUsers().begin())
                    : ctjs::GetPropertyOp{};
            const llvm::StringRef key = read && read.getObject() == proto.getResult()
                                            ? ctjs::constantKey(read.getKey())
                                            : llvm::StringRef{};
            if (key.empty()) {
                why = "`" + stemOf(fn).str() +
                      "` uses `super` other than to call a method of the parent";
                return;
            }
            out.push_back({home, proto, read, key});
        });
        return why;
    }

    /// Why a call through `read` is not `super.m(...)` on `this`.
    static Reason superCallsOnThis(ctjs::FuncOp fn, superRead read) {
        const mlir::Value self = fn.getBody().front().getArgument(0);
        for (mlir::OpOperand & use : read.read.getResult().getUses()) {
            auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            if (!call || use.getOperandNumber() != 0 || call.getReceiver() != self) {
                return "`" + stemOf(fn).str() + "` uses `super." + read.key.str() +
                       "` other than as a call";
            }
        }
        return std::nullopt;
    }

    /// A copy of `fn` named `<stem>$<new index>`, placed after it. Every
    /// function its body makes a closure of is copied too, named for `owner`,
    /// so each function stays named by one ctjs.create_closure, as the
    /// closure lift requires.
    std::pair<ctjs::FuncOp, unsigned> copyFunction(ctjs::FuncOp fn, llvm::StringRef stem,
                                                   llvm::StringRef owner) {
        const unsigned at = nextIndex++;
        auto copy = llvm::cast<ctjs::FuncOp>(fn->clone());
        copy.setSymName((stem + "$" + llvm::Twine(at)).str());
        mlir::OpBuilder b(fn);
        b.setInsertionPointAfter(fn);
        b.insert(copy);
        byIndex[at] = copy;
        ++copies;
        // Every copy names the source function it was first copied from.
        if (!copy->hasAttr(kNativeCopyOf)) {
            copy->setAttr(kNativeCopyOf, mlir::StringAttr::get(context, fn.getSymName()));
        }
        copiesOf[copy.getSymName()] = llvm::cast<mlir::StringAttr>(copy->getAttr(kNativeCopyOf));
        llvm::SmallVector<ctjs::CreateClosureOp> inner;
        copy.walk([&](ctjs::CreateClosureOp made) { inner.push_back(made); });
        for (ctjs::CreateClosureOp made : inner) {
            ctjs::FuncOp nested = targetOf(made);
            if (!nested) { continue; }
            const std::string nestedStem = stemFor(nested);
            auto [nestedCopy, index] =
                copyFunction(nested, (nestedStem + "_" + owner).str(), owner);
            originalStem[nestedCopy.getOperation()] = nestedStem;
            made.setFunctionAttr(b.getI32IntegerAttr(static_cast<int32_t>(index)));
        }
        return {copy, at};
    }

    mlir::Value text(mlir::OpBuilder & at, mlir::Location where, llvm::StringRef value) {
        return ctjs::ConstantOp::create(at, where, ctjs::ValueType::get(context),
                                        ctjs::StringAttr::get(context, value));
    }

    mlir::Value undefinedAt(mlir::OpBuilder & at, mlir::Location where) {
        return ctjs::ConstantOp::create(at, where, ctjs::ValueType::get(context),
                                        ctjs::UndefinedAttr::get(context));
    }

    /// A closure of a copy of `member`'s function for class `owner`, built
    /// where `at` points, and stored on `object` under `key` with its
    /// `__home` edge. The copy is named `<function>_<owner>` unless `name`
    /// is given. Returns the copy.
    ctjs::FuncOp copyMember(mlir::OpBuilder & at, ctjs::CreateClosureOp member,
                            llvm::StringRef owner, mlir::Value object, llvm::StringRef key,
                            bool getter = false) {
        ctjs::FuncOp original = targetOf(member);
        const std::string stem = stemFor(original);
        auto [copy, index] = copyFunction(original, stem + "_" + owner.str(), owner);
        originalStem[copy.getOperation()] = stem;
        auto closure = llvm::cast<ctjs::CreateClosureOp>(at.clone(*member));
        closure.setFunctionAttr(at.getI32IntegerAttr(static_cast<int32_t>(index)));
        const mlir::Location where = member.getLoc();
        if (getter) {
            ctjs::DefineAccessorOp::create(at, where, object, key, closure.getResult(),
                                           undefinedAt(at, where));
        } else {
            ctjs::SetPropertyOp::create(at, where, object, text(at, where, key),
                                        closure.getResult());
        }
        ctjs::SetPropertyOp::create(at, where, closure.getResult(), text(at, where, "__home"),
                                    object);
        return copy;
    }

    /// The C++ class name of `cls` (Classes.cpp's classNameOf, without its
    /// sanitising, which the copies' names do not need: the emitter spells
    /// function names itself).
    llvm::StringRef nameOf(jsClassShape & cls) { return stemOf(targetOf(cls.constructor)); }

    Reason check(jsClassShape & cls) {
        if (!cls.parent) { return std::string("its parent is not one class this module defines"); }
        jsClassShape * parent = classOf(cls.parent);
        if (!parent || !parent->prototype) {
            return std::string("its parent is not one class this module defines");
        }
        if (parent->refused) { return std::string("its parent class is not flattened"); }
        if (!cls.prototype) { return std::string("its prototype is not an object literal"); }
        if (cls.heritage->getBlock() != parent->defined->getBlock() ||
            !parent->defined->isBeforeInBlock(cls.heritage)) {
            return std::string("its parent is not defined earlier in the same block");
        }
        if (auto why = unsupportedMembers(cls, "it")) { return why; }
        if (auto why = unsupportedMembers(*parent, "its parent")) { return why; }
        // A class of the chain named by instanceof: a flattened instance
        // answers false where JavaScript answers true.
        Reason why;
        module.walk([&](ctjs::InstanceOfOp test) {
            for (mlir::Value side : test->getOperands()) {
                auto named = boundConstructor(side);
                for (jsClassShape * at = &cls; !why && named && at;
                     at = at->parent ? classOf(at->parent) : nullptr) {
                    if (named == at->constructor) {
                        why = "`instanceof` tests a class of its chain";
                    }
                }
            }
        });
        if (why) { return why; }
        // `super(...)` evaluates to what the parent's constructor returns
        // when that is an object, and the rewrite reads it as `this`. Every
        // return must give a primitive, as the constructor rule requires of
        // a class `new` builds (Constructors.cpp, isNotObjectLike).
        Reason returned;
        ctjs::FuncOp parentFn = targetOf(parent->constructor);
        parentFn.walk([&](ctjs::ReturnOp ret) {
            llvm::DenseSet<mlir::Value> seen;
            if (returned ||
                thisOrPrimitive(ret.getValue(), parentFn.getBody().front().getArgument(0), seen)) {
                return;
            }
            returned = "its parent's constructor may return an object, which would replace `this`";
        });
        if (returned) { return returned; }
        // new.target is the class `new` named; a copy of the parent's
        // constructor called as a method would read undefined.
        if (!targetOf(parent->constructor).getBody().front().getArgument(1).use_empty()) {
            return std::string("its parent's constructor reads new.target");
        }
        return std::nullopt;
    }

    /// A nested function of a constructor that captures the importer's flag
    /// for `this`: its closure, the operand that captures the flag, and the
    /// reads of that capture in its body.
    struct capturedFlag {
        ctjs::CreateClosureOp closure;
        unsigned operand;
        llvm::SmallVector<ctjs::LoadUpvalueOp> reads;
    };

    /// Why the flag `use`, a capture by `closure`, cannot be read as set;
    /// records the site otherwise. The closure must be made where
    /// `super(...)` has run, be the only closure of its function, and its
    /// function must only read the capture (never write it or hand it to a
    /// closure of its own).
    Reason flagCapture(ctjs::CreateClosureOp closure, mlir::OpOperand & use, ctjs::CellSetOp raised,
                       mlir::DominanceInfo & dominance, llvm::SmallVectorImpl<capturedFlag> & out) {
        const std::string before = "its constructor's `this` is reached from a nested function "
                                   "that may run before `super(...)`";
        const unsigned first = closure.getUpvalues().getBeginOperandIndex();
        if (use.getOperandNumber() < first ||
            !dominance.dominates(raised.getOperation(), closure.getOperation())) {
            return before;
        }
        const unsigned slot = use.getOperandNumber() - first;
        if (auto indices = closure.getEnclosingIndicesAttr();
            indices && indices.asArrayRef()[slot] != -1) {
            return before;
        }
        ctjs::FuncOp target = targetOf(closure);
        if (!target) { return before; }
        unsigned named = 0;
        module.walk([&](ctjs::CreateClosureOp other) { named += targetOf(other) == target; });
        if (named != 1) {
            return std::string("its constructor's `this` is reached from a function made by "
                               "more than one closure");
        }
        capturedFlag site{closure, use.getOperandNumber(), {}};
        Reason why;
        target.walk([&](mlir::Operation * op) {
            if (auto read = llvm::dyn_cast<ctjs::LoadUpvalueOp>(op)) {
                if (read.getIndex() == slot) { site.reads.push_back(read); }
            } else if (auto write = llvm::dyn_cast<ctjs::StoreUpvalueOp>(op);
                       write && write.getIndex() == slot) {
                why =
                    std::string("a nested function of its constructor writes its flag for `this`");
            } else if (auto inner = llvm::dyn_cast<ctjs::CreateClosureOp>(op)) {
                if (auto indices = inner.getEnclosingIndicesAttr();
                    indices &&
                    llvm::is_contained(indices.asArrayRef(), static_cast<int32_t>(slot))) {
                    why = std::string(
                        "a function nested twice in its constructor reaches its `this`");
                }
            }
        });
        if (why) { return why; }
        out.push_back(std::move(site));
        return std::nullopt;
    }

    /// Rewrites `super(...)` in the constructor `fn` of a class whose parent
    /// constructor is stored under `superKey`, or says why it cannot.
    Reason flattenConstructor(ctjs::FuncOp fn, llvm::StringRef superKey,
                              llvm::ArrayRef<superRead> reads) {
        mlir::Block & entry = fn.getBody().front();
        const mlir::Value self = entry.getArgument(0);
        // The one `super(...)`: a call of `super.constructor` on `this`,
        // right after the importer's pass_new_target.
        ctjs::CallOp superCall;
        for (superRead read : reads) {
            if (read.key != "constructor") { continue; }
            for (mlir::OpOperand & use : read.read.getResult().getUses()) {
                if (llvm::isa<ctjs::CallSpreadOp>(use.getOwner())) {
                    return std::string("its constructor calls `super(...)` with a spread argument");
                }
                auto call = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
                if (!call || use.getOperandNumber() != 0 || call.getReceiver() != self ||
                    superCall) {
                    return std::string(
                        "its constructor uses `super` other than as one `super(...)` call");
                }
                superCall = call;
            }
        }
        if (!superCall) {
            return std::string("its constructor calls `super(...)` with a spread or not at all");
        }
        auto passes = llvm::dyn_cast_or_null<ctjs::PassNewTargetOp>(superCall->getPrevNode());
        if (!passes) { return std::string("its `super(...)` call is not the importer's spelling"); }
        // The flag the importer sets right after the call (a constant true,
        // then the cell_set), and the reads that check it.
        ctjs::CellSetOp raised;
        mlir::Operation * next = superCall->getNextNode();
        for (int step = 0; next && step < 2 && !raised; ++step, next = next->getNextNode()) {
            if (auto set = llvm::dyn_cast<ctjs::CellSetOp>(next)) { raised = set; }
        }
        auto flag =
            raised ? raised.getCell().getDefiningOp<ctjs::CreateCellOp>() : ctjs::CreateCellOp{};
        if (!flag || flag->getParentOp() != fn) {
            return std::string("its `super(...)` call is not the importer's spelling");
        }
        mlir::DominanceInfo dominance(fn);
        llvm::SmallVector<std::pair<ctjs::CellGetOp, bool>> checks;
        llvm::SmallVector<capturedFlag> captured;
        for (mlir::OpOperand & use : flag.getResult().getUses()) {
            mlir::Operation * user = use.getOwner();
            if (user == raised.getOperation()) { continue; }
            if (auto closure = llvm::dyn_cast<ctjs::CreateClosureOp>(user)) {
                if (auto why = flagCapture(closure, use, raised, dominance, captured)) {
                    return why;
                }
                continue;
            }
            auto get = llvm::dyn_cast<ctjs::CellGetOp>(user);
            if (!get) {
                return "its constructor's flag for `this` is used by `" +
                       user->getName().getStringRef().str() + "`";
            }
            if (dominance.dominates(raised.getOperation(), get.getOperation())) {
                checks.emplace_back(get, true);
            } else if (dominance.properlyDominates(get.getOperation(), raised.getOperation())) {
                checks.emplace_back(get, false);
            } else {
                return std::string("its constructor does not call `super(...)` on every path");
            }
        }
        if (raised->getParentOfType<mlir::LoopLikeOpInterface>()) {
            return std::string("its constructor calls `super(...)` inside a loop");
        }
        // The binding and field helpers have nothing left to do once the
        // flag folds; their results are never read.
        llvm::SmallVector<ctjs::CallOp> helpers;
        fn.walk([&](ctjs::CallOp call) {
            if (callsIntrinsic(call, "__ctbrowser_bind_this") ||
                callsIntrinsic(call, "__ctbrowser_init_fields")) {
                helpers.push_back(call);
            }
        });
        for (ctjs::CallOp call : helpers) {
            if (!call.getResult().use_empty()) {
                return std::string("its constructor uses the result of a class helper");
            }
        }

        // `super(a, b)` is `this["super:P:constructor"](a, b)`, and it
        // evaluates to `this`: the parent's constructor returns no object
        // (the constructor rule checks every constructor for that).
        mlir::OpBuilder at(superCall);
        auto method =
            ctjs::GetPropertyOp::create(at, superCall.getLoc(), ctjs::ValueType::get(context), self,
                                        text(at, superCall.getLoc(), superKey));
        superCall->setOperand(0, method.getResult());
        for (mlir::OpOperand & use : llvm::make_early_inc_range(superCall.getResult().getUses())) {
            auto bind = llvm::dyn_cast<ctjs::CallOp>(use.getOwner());
            if (bind && callsIntrinsic(bind, "__ctbrowser_bind_this")) { continue; }
            use.set(self);
        }
        passes.erase();
        for (ctjs::CallOp call : helpers) { eraseWithFeeders(call); }
        // The flag folds: true wherever `super(...)` has run, false before.
        for (auto [get, ran] : checks) {
            for (mlir::OpOperand & use : llvm::make_early_inc_range(get.getResult().getUses())) {
                mlir::OpBuilder here(use.getOwner());
                if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(use.getOwner())) {
                    truthy.getResult().replaceAllUsesWith(
                        mlir::arith::ConstantOp::create(here, truthy.getLoc(),
                                                        here.getBoolAttr(ran))
                            .getResult());
                    truthy.erase();
                    continue;
                }
                use.set(ctjs::ConstantOp::create(here, get.getLoc(), ctjs::ValueType::get(context),
                                                 ctjs::BooleanAttr::get(context, ran)));
            }
            get.erase();
        }
        // A nested function made after `super(...)` sees the flag set: its
        // reads are true, and its closure stops capturing the flag.
        for (capturedFlag & site : captured) {
            for (ctjs::LoadUpvalueOp read : site.reads) {
                for (mlir::OpOperand & use :
                     llvm::make_early_inc_range(read.getResult().getUses())) {
                    mlir::OpBuilder here(use.getOwner());
                    if (auto truthy = llvm::dyn_cast<ctjs::TruthyOp>(use.getOwner())) {
                        truthy.getResult().replaceAllUsesWith(
                            mlir::arith::ConstantOp::create(here, truthy.getLoc(),
                                                            here.getBoolAttr(true))
                                .getResult());
                        truthy.erase();
                        continue;
                    }
                    use.set(ctjs::ConstantOp::create(here, read.getLoc(),
                                                     ctjs::ValueType::get(context),
                                                     ctjs::BooleanAttr::get(context, true)));
                }
                read.erase();
            }
            mlir::OpBuilder at(site.closure);
            site.closure->setOperand(site.operand, undefinedAt(at, site.closure.getLoc()));
            foldConstantBranches(targetOf(site.closure));
        }
        raised.erase();
        eraseWithFeeders(flag);
        foldConstantBranches(fn);
        return std::nullopt;
    }

    /// Erases `op` and each constant or global load only it used.
    static void eraseWithFeeders(mlir::Operation * op) {
        llvm::SmallVector<mlir::Operation *> feeders;
        for (mlir::Value operand : op->getOperands()) {
            if (mlir::Operation * made = operand.getDefiningOp();
                llvm::isa_and_nonnull<ctjs::ConstantOp, ctjs::LoadGlobalOp, ctjs::LoadHomeOp>(
                    made)) {
                feeders.push_back(made);
            }
        }
        op->erase();
        for (mlir::Operation * made : feeders) {
            if (made->use_empty()) { made->erase(); }
        }
    }

    static bool isUndefined(mlir::Value value) {
        auto constant = value.getDefiningOp<ctjs::ConstantOp>();
        return constant && llvm::isa<ctjs::UndefinedAttr>(constant.getValue());
    }

    /// The integer `value` holds when it is a constant, through index casts.
    static std::optional<int64_t> constantInt(mlir::Value value) {
        mlir::Operation * made = value.getDefiningOp();
        if (auto constant = llvm::dyn_cast_or_null<mlir::arith::ConstantOp>(made)) {
            if (auto integer = llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())) {
                return integer.getValue().getZExtValue();
            }
            return std::nullopt;
        }
        if (llvm::isa_and_nonnull<mlir::arith::IndexCastUIOp, mlir::arith::IndexCastOp>(made)) {
            return constantInt(made->getOperand(0));
        }
        return std::nullopt;
    }

    /// Replaces `op` by the operations of `region`'s one block, and its
    /// results by what that block yields.
    static void inlineRegion(mlir::Operation * op, mlir::Region & region) {
        if (region.empty()) {
            op->erase();
            return;
        }
        mlir::Block & body = region.front();
        mlir::Operation * yield = body.getTerminator();
        llvm::SmallVector<mlir::Value> results(yield->getOperands());
        op->getBlock()->getOperations().splice(op->getIterator(), body.getOperations(),
                                               body.begin(), yield->getIterator());
        op->replaceAllUsesWith(results);
        op->erase();
    }

    /// Folds the branches the flag decided, and the ones their results then
    /// decide: an scf.if, scf.index_switch, arith.select, cf.switch or
    /// cf.cond_br on a constant takes its one arm, and blocks nothing reaches
    /// any more go. Written out rather than with the upstream patterns, which
    /// read ctjs.constant operations as constants of their own.
    void foldConstantBranches(ctjs::FuncOp fn) {
        for (bool changed = true; changed;) {
            changed = false;
            fn.walk([&](mlir::Operation * op) {
                if (changed) { return mlir::WalkResult::interrupt(); }
                if (auto branch = llvm::dyn_cast<mlir::scf::IfOp>(op)) {
                    if (auto taken = constantInt(branch.getCondition())) {
                        inlineRegion(op, *taken ? branch.getThenRegion() : branch.getElseRegion());
                        changed = true;
                    }
                } else if (auto choice = llvm::dyn_cast<mlir::scf::IndexSwitchOp>(op)) {
                    if (auto taken = constantInt(choice.getArg())) {
                        mlir::Region * region = &choice.getDefaultRegion();
                        for (auto [value, arm] :
                             llvm::zip(choice.getCases(), choice.getCaseRegions())) {
                            if (value == *taken) { region = &arm; }
                        }
                        inlineRegion(op, *region);
                        changed = true;
                    }
                }
                if (changed) { return mlir::WalkResult::interrupt(); }
                // A result every arm yields the same outer value for is that
                // value.
                if (llvm::isa<mlir::scf::IfOp, mlir::scf::IndexSwitchOp>(op)) {
                    for (mlir::OpResult result : op->getResults()) {
                        if (result.use_empty()) { continue; }
                        mlir::Value same;
                        bool agree = true;
                        for (mlir::Region & region : op->getRegions()) {
                            if (region.empty()) {
                                agree = false;
                                break;
                            }
                            mlir::Value given = region.front().getTerminator()->getOperand(
                                result.getResultNumber());
                            if ((same && given != same) ||
                                (given.getParentRegion() &&
                                 op->isAncestor(given.getParentRegion()->getParentOp()))) {
                                agree = false;
                                break;
                            }
                            same = given;
                        }
                        if (agree && same) {
                            result.replaceAllUsesWith(same);
                            changed = true;
                        }
                    }
                }
                if (changed) { return mlir::WalkResult::interrupt(); }
                if (auto select = llvm::dyn_cast<mlir::arith::SelectOp>(op)) {
                    if (auto taken = constantInt(select.getCondition())) {
                        select.getResult().replaceAllUsesWith(*taken ? select.getTrueValue()
                                                                     : select.getFalseValue());
                        select.erase();
                        changed = true;
                    }
                } else if (auto jump = llvm::dyn_cast<mlir::cf::SwitchOp>(op)) {
                    if (auto taken = constantInt(jump.getFlag())) {
                        mlir::Block * dest = jump.getDefaultDestination();
                        mlir::ValueRange operands = jump.getDefaultOperands();
                        if (auto values = jump.getCaseValues()) {
                            for (auto [index, value] :
                                 llvm::enumerate(values->getValues<llvm::APInt>())) {
                                if (value.getZExtValue() == static_cast<uint64_t>(*taken)) {
                                    dest = jump.getCaseDestinations()[index];
                                    operands = jump.getCaseOperands(static_cast<unsigned>(index));
                                }
                            }
                        }
                        mlir::OpBuilder at(jump);
                        mlir::cf::BranchOp::create(at, jump.getLoc(), dest, operands);
                        jump.erase();
                        changed = true;
                    }
                } else if (auto jump = llvm::dyn_cast<mlir::cf::CondBranchOp>(op)) {
                    if (auto taken = constantInt(jump.getCondition())) {
                        mlir::OpBuilder at(jump);
                        mlir::cf::BranchOp::create(
                            at, jump.getLoc(), *taken ? jump.getTrueDest() : jump.getFalseDest(),
                            *taken ? jump.getTrueDestOperands() : jump.getFalseDestOperands());
                        jump.erase();
                        changed = true;
                    }
                }
                return changed ? mlir::WalkResult::interrupt() : mlir::WalkResult::advance();
            });
        }
        // Blocks no branch reaches any more, last to first so a chain of them
        // goes in one pass.
        for (bool changed = true; changed;) {
            changed = false;
            for (mlir::Block & block :
                 llvm::make_early_inc_range(llvm::drop_begin(fn.getBody().getBlocks()))) {
                if (!block.hasNoPredecessors()) { continue; }
                for (mlir::Operation & op : llvm::reverse(block)) { op.dropAllUses(); }
                block.dropAllDefinedValueUses();
                block.erase();
                changed = true;
            }
        }
        // A block entered only by an unconditional branch joins its
        // predecessor, so a return of `this` through it is a return of
        // `this` again.
        for (bool changed = true; changed;) {
            changed = false;
            for (mlir::Block & block : llvm::drop_begin(fn.getBody().getBlocks())) {
                mlir::Block * from = block.getSinglePredecessor();
                auto jump = from ? llvm::dyn_cast<mlir::cf::BranchOp>(from->getTerminator())
                                 : mlir::cf::BranchOp{};
                if (!jump || jump.getDest() != &block) { continue; }
                for (auto [argument, operand] :
                     llvm::zip(block.getArguments(), jump.getDestOperands())) {
                    argument.replaceAllUsesWith(operand);
                }
                jump.erase();
                from->getOperations().splice(from->end(), block.getOperations());
                block.dropAllDefinedValueUses();
                block.erase();
                changed = true;
                break;
            }
        }
        // What the folds left unused (constants, casts, poison).
        llvm::SmallVector<mlir::Operation *> dead;
        fn.walk<mlir::WalkOrder::PostOrder>([&](mlir::Operation * op) {
            if (op != fn.getOperation() && mlir::isOpTriviallyDead(op)) { dead.push_back(op); }
        });
        for (mlir::Operation * op : llvm::reverse(dead)) {
            if (mlir::isOpTriviallyDead(op)) { op->erase(); }
        }
    }

    /// The returns of a flattened constructor give undefined where they gave
    /// `this`: `new` and `super(...)` evaluate to the instance either way.
    void returnUndefined(ctjs::FuncOp fn) {
        const mlir::Value self = fn.getBody().front().getArgument(0);
        fn.walk([&](ctjs::ReturnOp returned) {
            llvm::DenseSet<mlir::Value> seen;
            if (!thisOrPrimitive(returned.getValue(), self, seen)) { return; }
            mlir::OpBuilder at(returned);
            returned->setOperand(0, undefinedAt(at, returned.getLoc()));
        });
        // The structured results and block arguments that carried `this` to
        // those returns are unused now; pass undefined through them instead,
        // so `this` is used only where the body uses it.
        for (bool changed = true; changed;) {
            changed = false;
            fn.walk([&](mlir::Operation * op) {
                if (!llvm::isa<mlir::scf::IfOp, mlir::scf::IndexSwitchOp>(op)) { return; }
                for (mlir::OpResult result : op->getResults()) {
                    if (!result.use_empty()) { continue; }
                    for (mlir::Region & region : op->getRegions()) {
                        if (region.empty()) { continue; }
                        mlir::Operation * yield = region.front().getTerminator();
                        mlir::OpOperand & carried = yield->getOpOperand(result.getResultNumber());
                        if (!llvm::isa<ctjs::ValueType>(carried.get().getType()) ||
                            isUndefined(carried.get())) {
                            continue;
                        }
                        mlir::OpBuilder at(yield);
                        carried.set(undefinedAt(at, yield->getLoc()));
                        changed = true;
                    }
                }
            });
            for (mlir::Block & block : llvm::drop_begin(fn.getBody().getBlocks())) {
                for (mlir::BlockArgument argument : block.getArguments()) {
                    if (!argument.use_empty()) { continue; }
                    for (mlir::Block * from : block.getPredecessors()) {
                        auto branch =
                            llvm::dyn_cast<mlir::BranchOpInterface>(from->getTerminator());
                        if (!branch) { continue; }
                        for (unsigned i = 0; i < branch->getNumSuccessors(); ++i) {
                            if (branch->getSuccessor(i) != &block) { continue; }
                            mlir::OperandRange forwarded =
                                branch.getSuccessorOperands(i).getForwardedOperands();
                            const unsigned at = argument.getArgNumber();
                            if (at >= forwarded.size() ||
                                !llvm::isa<ctjs::ValueType>(forwarded[at].getType()) ||
                                isUndefined(forwarded[at])) {
                                continue;
                            }
                            mlir::OpBuilder before(branch);
                            branch->setOperand(forwarded.getBeginOperandIndex() + at,
                                               undefinedAt(before, branch.getLoc()));
                            changed = true;
                        }
                    }
                }
            }
        }
    }

    /// Whether every value `value` can hold is `self` or a primitive,
    /// followed through scf results and block arguments. A poison value is
    /// one no path that reaches the use produces.
    static bool thisOrPrimitive(mlir::Value value, mlir::Value self,
                                llvm::DenseSet<mlir::Value> & seen) {
        if (!seen.insert(value).second || value == self) { return true; }
        if (auto argument = llvm::dyn_cast<mlir::BlockArgument>(value)) {
            mlir::Block * block = argument.getOwner();
            if (block->isEntryBlock()) { return false; }
            for (mlir::Block * from : block->getPredecessors()) {
                auto branch = llvm::dyn_cast<mlir::BranchOpInterface>(from->getTerminator());
                if (!branch) { return false; }
                for (unsigned i = 0; i < branch->getNumSuccessors(); ++i) {
                    if (branch->getSuccessor(i) != block) { continue; }
                    mlir::Value passed = branch.getSuccessorOperands(i)[argument.getArgNumber()];
                    if (!passed || !thisOrPrimitive(passed, self, seen)) { return false; }
                }
            }
            return true;
        }
        mlir::Operation * made = value.getDefiningOp();
        if (made->getName().getStringRef() == "ub.poison") { return true; }
        if (llvm::isa<mlir::scf::IfOp, mlir::scf::IndexSwitchOp>(made)) {
            const unsigned index = llvm::cast<mlir::OpResult>(value).getResultNumber();
            for (mlir::Region & region : made->getRegions()) {
                if (region.empty()) { continue; }
                mlir::Operation * yield = region.front().getTerminator();
                if (!thisOrPrimitive(yield->getOperand(index), self, seen)) { return false; }
            }
            return true;
        }
        return llvm::isa<ctjs::ConstantOp, ctjs::BinaryOp, ctjs::BinaryStaticOp, ctjs::UnaryOp,
                         ctjs::CompareOp, ctjs::TruthyOp, ctjs::FromBoolOp, ctjs::InstanceOfOp,
                         ctjs::HasPropertyOp>(made);
    }

    /// Whether `fn` is the importer's implicit derived constructor:
    /// `constructor(...args) { super(...args); }`, which only forwards.
    static bool forwardsOnly(ctjs::FuncOp fn) {
        bool spread = false, other = false;
        fn.walk([&](mlir::Operation * op) {
            if (llvm::isa<ctjs::CallSpreadOp>(op)) {
                spread = true;
            } else if (llvm::isa<ctjs::SetPropertyOp, ctjs::CreateClosureOp, ctjs::CallDirectOp,
                                 ctjs::ConstructOp, ctjs::CellSetOp, ctjs::StoreGlobalOp,
                                 ctjs::StoreUpvalueOp>(op)) {
                other = true;
            } else if (auto call = llvm::dyn_cast<ctjs::CallOp>(op);
                       call && !callsIntrinsic(call, "__ctbrowser_bind_this") &&
                       !callsIntrinsic(call, "__ctbrowser_init_fields")) {
                other = true;
            }
        });
        return spread && !other;
    }

    Reason flatten(jsClassShape & cls) {
        if (auto why = check(cls)) { return why; }
        jsClassShape & parent = *classOf(cls.parent);
        ctjs::FuncOp ctorFn = targetOf(cls.constructor);
        const std::string name = nameOf(cls).str();
        const std::string superStem = ("super:" + targetOf(parent.constructor).getSymName()).str();

        // Read every `super` use before anything changes.
        llvm::SmallVector<superRead> ctorReads;
        const bool implicit = forwardsOnly(ctorFn);
        if (!implicit) {
            if (auto why = superReads(ctorFn, ctorReads)) { return why; }
            for (superRead read : ctorReads) {
                if (read.key == "constructor") { continue; }
                if (auto why = superCallsOnThis(ctorFn, read)) { return why; }
            }
        }
        llvm::MapVector<ctjs::FuncOp, llvm::SmallVector<superRead>> memberReads;
        llvm::StringSet<> superKeys;
        for (const auto & entry : cls.methods) {
            ctjs::FuncOp fn = targetOf(entry.second);
            llvm::SmallVector<superRead> reads;
            if (auto why = superReads(fn, reads)) { return why; }
            for (superRead read : reads) {
                if (auto why = superCallsOnThis(fn, read)) { return why; }
                superKeys.insert(read.key);
            }
            memberReads[fn] = std::move(reads);
        }
        for (superRead read : ctorReads) {
            if (read.key != "constructor") { superKeys.insert(read.key); }
        }
        for (const auto * group : {&cls.statics, &cls.staticGetters}) {
            for (const auto & entry : *group) {
                llvm::SmallVector<superRead> reads;
                ctjs::FuncOp fn = targetOf(entry.second);
                if (fn && (superReads(fn, reads) || !reads.empty())) {
                    return "its static member `" + entry.first.str() + "` reads `super`";
                }
            }
        }
        for (const auto & key : superKeys) {
            if (!parent.methods.contains(key.getKey())) {
                return "`super." + key.getKey().str() + "` names no method of its parent";
            }
        }
        // The constructor: an explicit one keeps its body.
        if (!implicit) {
            if (auto why = flattenConstructor(ctorFn, superStem + ":constructor", ctorReads)) {
                return why;
            }
            for (superRead read : ctorReads) {
                if (read.key != "constructor") { redirectSuper(read, superStem); }
            }
            eraseSuperReads(ctorReads);
            returnUndefined(ctorFn);
        }
        for (auto & [fn, reads] : memberReads) {
            for (superRead read : reads) { redirectSuper(read, superStem); }
            eraseSuperReads(reads);
        }

        // Inherited members, then the `super` targets, before D is defined.
        mlir::OpBuilder at(cls.defined);
        const mlir::Value proto = cls.prototype.getResult();
        const mlir::Value ctor = cls.constructor.getResult();
        for (const auto & entry : parent.methods) {
            if (cls.methods.contains(entry.first)) { continue; }
            cls.methods[entry.first] = {};
            copyMember(at, entry.second, name, proto, entry.first);
        }
        for (const auto & key : superKeys) {
            copyMember(at, parent.methods.lookup(key.getKey()), name, proto,
                       superStem + ":" + key.getKey().str());
        }
        if (!implicit) {
            copyMember(at, parent.constructor, name, proto, superStem + ":constructor");
        }
        for (const auto & entry : parent.statics) {
            if (cls.statics.contains(entry.first) || cls.staticGetters.contains(entry.first)) {
                continue;
            }
            copyMember(at, entry.second, name, ctor, entry.first);
        }
        for (const auto & entry : parent.staticGetters) {
            if (cls.statics.contains(entry.first) || cls.staticGetters.contains(entry.first)) {
                continue;
            }
            copyMember(at, entry.second, name, ctor, entry.first, /*getter=*/true);
        }
        // A constructor that only forwards is the parent's, named after D.
        if (implicit) {
            auto [copy, index] = copyFunction(targetOf(parent.constructor), name, name);
            // It stands for the forwarding constructor, which goes.
            copy->setAttr(kNativeCopyOf, mlir::StringAttr::get(context, ctorFn.getSymName()));
            copiesOf[copy.getSymName()] = mlir::StringAttr::get(context, ctorFn.getSymName());
            cls.constructor.setFunctionAttr(at.getI32IntegerAttr(static_cast<int32_t>(index)));
            // The closure now names the parent's code, so it captures what
            // the parent's closure captures: its upvalue operands follow the
            // descriptors of the function it names. Both closures are made in
            // the same frame, the parent's first.
            llvm::SmallVector<mlir::Value> operands{cls.constructor.getEnclosingClosure(),
                                                    cls.constructor.getEnclosingThis()};
            llvm::append_range(operands, parent.constructor.getUpvalues());
            cls.constructor->setOperands(operands);
            if (auto indices = parent.constructor.getEnclosingIndicesAttr()) {
                cls.constructor.setEnclosingIndicesAttr(indices);
            } else {
                cls.constructor.removeEnclosingIndicesAttr();
            }
            // Nothing else names the forwarding function now.
            eraseIfUnnamed(ctorFn);
        }
        eraseWithFeeders(cls.heritage);
        cls.heritage = {};
        // The census re-reads members from the IR: refresh this class's.
        cls.methods.clear();
        cls.statics.clear();
        cls.staticGetters.clear();
        rereadMembers(cls);
        return std::nullopt;
    }

    void rereadMembers(jsClassShape & cls) {
        for (mlir::OpOperand & use : cls.constructor.getResult().getUses()) {
            if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                set && use.getOperandNumber() == 0) {
                const llvm::StringRef name = ctjs::constantKey(set.getKey());
                auto member = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                if (!name.empty() && name != "prototype" && name != "__home" && member) {
                    cls.statics[name] = member;
                }
            } else if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(use.getOwner());
                       accessor && use.getOperandNumber() == 0) {
                if (auto getter = accessor.getGetter().getDefiningOp<ctjs::CreateClosureOp>()) {
                    cls.staticGetters[accessor.getName()] = getter;
                }
            }
        }
        for (mlir::Operation * user : cls.prototype.getResult().getUsers()) {
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            if (!set || set.getObject() != cls.prototype.getResult()) { continue; }
            const llvm::StringRef name = ctjs::constantKey(set.getKey());
            auto member = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
            if (!name.empty() && name != "constructor" && member) { cls.methods[name] = member; }
        }
    }

    /// `super.m(...)` becomes `this["super:P:m"](...)`.
    void redirectSuper(superRead read, llvm::StringRef superStem) {
        mlir::OpBuilder at(read.read);
        auto fn = read.read->getParentOfType<ctjs::FuncOp>();
        auto method = ctjs::GetPropertyOp::create(
            at, read.read.getLoc(), ctjs::ValueType::get(context),
            fn.getBody().front().getArgument(0),
            text(at, read.read.getLoc(), (superStem + ":" + read.key).str()));
        read.read.getResult().replaceAllUsesWith(method.getResult());
    }

    static void eraseSuperReads(llvm::ArrayRef<superRead> reads) {
        for (superRead read : reads) {
            if (read.read && read.read.getResult().use_empty()) { eraseWithFeeders(read.read); }
            if (read.proto && read.proto.getResult().use_empty()) { read.proto.erase(); }
            if (read.home && read.home.getResult().use_empty()) { read.home.erase(); }
        }
    }

    /// Erases the definition of `cls`, a parent of a flattened class, when
    /// nothing can reach the class any more: its constructor is used only by
    /// its own setup, its members and a binding nobody reads, and its members
    /// only by their definitions. Its children carry their own copies, so
    /// what it defined can no longer run. Returns whether it erased it.
    bool eraseIfUnreachable(jsClassShape & cls) {
        if (!cls.prototype || cls.heritage) { return false; }
        const mlir::Value ctor = cls.constructor.getResult();
        const mlir::Value proto = cls.prototype.getResult();
        llvm::SetVector<mlir::Operation *> doomed;
        llvm::SmallVector<ctjs::CreateClosureOp> members;
        const auto memberOnly = [&](ctjs::CreateClosureOp member, mlir::Value home) {
            for (mlir::OpOperand & use : member.getResult().getUses()) {
                auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(use.getOwner());
                auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(use.getOwner());
                const bool definition = (set && use.getOperandNumber() == 2 &&
                                         (set.getObject() == proto || set.getObject() == ctor)) ||
                                        (accessor && use.getOperandNumber() == 1);
                const bool homeEdge = set && use.getOperandNumber() == 0 &&
                                      ctjs::constantKey(set.getKey()) == "__home" &&
                                      set.getValue() == home;
                if (!definition && !homeEdge) { return false; }
                doomed.insert(use.getOwner());
            }
            members.push_back(member);
            return true;
        };
        for (mlir::OpOperand & use : ctor.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (user == cls.defined.getOperation()) {
                doomed.insert(user);
            } else if (auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user)) {
                const llvm::StringRef key = ctjs::constantKey(set.getKey());
                if (use.getOperandNumber() == 0 && (key == "prototype" || key == "__home")) {
                    doomed.insert(user);
                } else if (use.getOperandNumber() == 2 && set.getObject() == proto &&
                           key == "constructor") {
                    doomed.insert(user);
                } else if (use.getOperandNumber() == 2 && key == "__home") {
                    // A static member's `__home` edge: memberOnly claims it
                    // with its member, and the check below sees that it did.
                    continue;
                } else if (auto member = set.getValue().getDefiningOp<ctjs::CreateClosureOp>();
                           use.getOperandNumber() == 0 && member) {
                    doomed.insert(user);
                    if (!memberOnly(member, ctor)) { return false; }
                } else {
                    return false;
                }
            } else if (auto accessor = llvm::dyn_cast<ctjs::DefineAccessorOp>(user);
                       accessor && use.getOperandNumber() == 0) {
                doomed.insert(user);
                auto getter = accessor.getGetter().getDefiningOp<ctjs::CreateClosureOp>();
                if (!getter || !ctjs::isUndefinedConstant(accessor.getSetter()) ||
                    !memberOnly(getter, ctor)) {
                    return false;
                }
            } else if (auto store = llvm::dyn_cast<ctjs::CellSetOp>(user);
                       store && use.getOperandNumber() == 1) {
                for (mlir::Operation * reader : store.getCell().getUsers()) {
                    if (!llvm::isa<ctjs::CellSetOp>(reader)) { return false; }
                }
                doomed.insert(user);
            } else {
                return false;
            }
        }
        for (mlir::OpOperand & use : proto.getUses()) {
            mlir::Operation * user = use.getOwner();
            if (doomed.contains(user)) { continue; }
            auto set = llvm::dyn_cast<ctjs::SetPropertyOp>(user);
            // A member's `__home` edge: memberOnly claims it with its member.
            if (set && use.getOperandNumber() == 2 && ctjs::constantKey(set.getKey()) == "__home") {
                continue;
            }
            auto member = set && use.getOperandNumber() == 0
                              ? set.getValue().getDefiningOp<ctjs::CreateClosureOp>()
                              : ctjs::CreateClosureOp{};
            if (!member) { return false; }
            doomed.insert(user);
            if (!memberOnly(member, proto)) { return false; }
        }
        for (mlir::Value value : {ctor, proto}) {
            for (mlir::Operation * user : value.getUsers()) {
                if (!doomed.contains(user)) { return false; }
            }
        }
        // The constants, global loads and binding cells only the definition
        // used go with it.
        llvm::SetVector<mlir::Operation *> feeders;
        for (mlir::Operation * op : doomed) {
            for (mlir::Value operand : op->getOperands()) {
                if (mlir::Operation * made = operand.getDefiningOp();
                    llvm::isa_and_nonnull<ctjs::ConstantOp, ctjs::LoadGlobalOp, ctjs::CreateCellOp>(
                        made)) {
                    feeders.insert(made);
                }
            }
        }
        for (mlir::Operation * op : doomed) { op->dropAllReferences(); }
        for (mlir::Operation * op : doomed) { op->erase(); }
        llvm::SmallVector<ctjs::FuncOp> functions;
        for (ctjs::CreateClosureOp member : members) {
            functions.push_back(targetOf(member));
            member.erase();
        }
        functions.push_back(targetOf(cls.constructor));
        cls.constructor.erase();
        cls.prototype.erase();
        for (mlir::Operation * made : feeders) {
            if (made->use_empty()) { made->erase(); }
        }
        for (ctjs::FuncOp fn : functions) { eraseIfUnnamed(fn); }
        return true;
    }

    /// Erases `fn` when no closure names it any more.
    void eraseIfUnnamed(ctjs::FuncOp fn) {
        if (!fn) { return; }
        bool named = false;
        module.walk([&](ctjs::CreateClosureOp other) { named = named || targetOf(other) == fn; });
        if (named) { return; }
        llvm::SmallVector<ctjs::FuncOp> nested;
        fn.walk([&](ctjs::CreateClosureOp made) { nested.push_back(targetOf(made)); });
        byIndex.erase(*ctjs::functionIndex(fn));
        if (!fn->hasAttr(kNativeCopyOf)) {
            erasedSources.push_back(mlir::StringAttr::get(context, fn.getSymName()));
        }
        fn.erase();
        ++erased;
        for (ctjs::FuncOp inner : nested) { eraseIfUnnamed(inner); }
    }

    void run() {
        index();
        census();
        // Parents first: a class waits until its parent is flat or refused.
        for (bool changed = true; changed;) {
            changed = false;
            for (auto & [key, cls] : classes) {
                (void)key;
                if (cls.flat || cls.refused) { continue; }
                if (!cls.heritage) {
                    cls.flat = true;
                    changed = true;
                    continue;
                }
                jsClassShape * parent = cls.parent ? classOf(cls.parent) : nullptr;
                if (parent && !parent->flat && !parent->refused) { continue; }
                if (auto why = flatten(cls)) {
                    cls.refused = true;
                    cls.constructor->setAttr(kNativeInheritanceReason,
                                             mlir::StringAttr::get(context, *why));
                } else {
                    cls.flat = true;
                    parent->parentOfFlattened = true;
                }
                changed = true;
            }
        }
        // A parent nothing can reach once its children are flattened goes,
        // children first so a whole chain can go.
        for (bool changed = true; changed;) {
            changed = false;
            for (auto & [key, cls] : classes) {
                (void)key;
                if (!cls.parentOfFlattened || cls.erased) { continue; }
                if (eraseIfUnreachable(cls)) {
                    cls.erased = true;
                    changed = true;
                }
            }
        }
        // How many functions the module gained and lost, added to an earlier
        // run's counts, so a tool comparing function counts before and after
        // lowering can account for them.
        if (copies || erased) {
            auto earlier = module->getAttrOfType<mlir::DictionaryAttr>(kNativeInheritanceSummary);
            const auto count = [&](llvm::StringRef key) -> int64_t {
                auto value = earlier ? earlier.getAs<mlir::IntegerAttr>(key) : mlir::IntegerAttr{};
                return value ? value.getInt() : 0;
            };
            mlir::Builder b(context);
            module->setAttr(
                kNativeInheritanceSummary,
                b.getDictionaryAttr({
                    b.getNamedAttr("copies", b.getI64IntegerAttr(count("copies") + copies)),
                    b.getNamedAttr("erased", b.getI64IntegerAttr(count("erased") + erased)),
                }));
        }
        module.walk([](ctjs::FuncOp fn) { fn->removeAttr(kNativeCopyOf); });
        if (!copiesOf.empty()) {
            llvm::SmallVector<mlir::NamedAttribute> all;
            if (auto earlier = module->getAttrOfType<mlir::DictionaryAttr>(kNativeCopies)) {
                all.append(earlier.begin(), earlier.end());
            }
            for (auto & [copy, source] : copiesOf) {
                all.emplace_back(mlir::StringAttr::get(context, copy), source);
            }
            module->setAttr(kNativeCopies, mlir::DictionaryAttr::get(context, all));
        }
        if (!erasedSources.empty()) {
            llvm::SmallVector<mlir::Attribute> all;
            if (auto earlier = module->getAttrOfType<mlir::ArrayAttr>(kNativeErasedSources)) {
                all.append(earlier.begin(), earlier.end());
            }
            all.append(erasedSources);
            module->setAttr(kNativeErasedSources, mlir::ArrayAttr::get(context, all));
        }
        // A cycle of parents never becomes ready.
        for (auto & [key, cls] : classes) {
            (void)key;
            if (!cls.flat && !cls.refused && !cls.erased) {
                cls.constructor->setAttr(
                    kNativeInheritanceReason,
                    mlir::StringAttr::get(context, "its parent chain does not end"));
            }
        }
    }
};

} // namespace

void flattenClassInheritance(mlir::ModuleOp module) {
    flattener(module).run();
}

} // namespace ctcompile::ctnative::lowering_detail
