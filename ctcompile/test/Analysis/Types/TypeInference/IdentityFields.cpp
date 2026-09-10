#include "Tests.h"

namespace ctcompile::test::type_inference {

// The identity field group is a storage schema, not an allocation identity.
// These rows deliberately give distinct objects the same schema. Presence is
// allowed to remove only implicit absence at this read; every explicit stored
// type, including Undefined on another object, still belongs to the join.
const std::string kIdentityFieldPrelude = R"mlir(
  %one = ctjs.constant #ctjs.number<4607182418800017408>
  %wide = ctjs.constant #ctjs.number<4609434218613702656>
  %nil = ctjs.constant #ctjs.undefined
  %yes = ctjs.constant #ctjs.boolean<true>
  %key = ctjs.constant #ctjs.string<"value">
  %different = ctjs.constant #ctjs.string<"different">
  %first = ctjs.create_object {ctnative.object_identity}
  %second = ctjs.create_object {ctnative.object_identity}
)mlir";

std::string identityFieldStore(llvm::StringRef object, llvm::StringRef value, llvm::StringRef key) {
    return "  ctjs.set_property " + object.str() + "[" + key.str() + "], " + value.str() +
           " {ctnative.object_field_group = 7 : i64}\n";
}

std::string identityFieldRead(llvm::StringRef object) {
    return "  %observed = ctjs.get_property " + object.str() +
           "[%key] {check, ctnative.object_field_group = 7 : i64}\n";
}

void checkIdentityFieldRows(mlir::MLIRContext & context) {
    const std::string store = identityFieldStore();
    const std::string read = identityFieldRead();
    const std::vector<row> rows = {
        {"an identity schema drops implicit absence only after this object's store",
         kIdentityFieldPrelude + store + read, "!ctnative.num<i32>", false, 1},
        {"an identity read before its store retains implicit absence",
         kIdentityFieldPrelude + read + store, "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a different allocation's store cannot initialize this instance's field",
         kIdentityFieldPrelude + identityFieldStore("%second") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a different field cannot initialize this instance's queried field",
         kIdentityFieldPrelude + identityFieldStore("%first", "%one", "%different") +
             identityFieldStore("%second") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"an identity field absent from every object reads Undefined", kIdentityFieldPrelude + read,
         "!ctnative.opt<!ctnative.bottom>", false, 0},
        {"a present identity field keeps the full schema's wider number join",
         kIdentityFieldPrelude + store + identityFieldStore("%second", "%wide") + read,
         "!ctnative.num<f64>", false, 1},
        {"a present identity field keeps an explicit Undefined from the same schema",
         kIdentityFieldPrelude + store + identityFieldStore("%second", "%nil") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 1},
        {"a present identity field keeps its explicit Undefined write",
         kIdentityFieldPrelude + identityFieldStore("%first", "%nil") + read,
         "!ctnative.opt<!ctnative.bottom>", false, 1},
        {"an identity field keeps a later explicit Undefined in its value join",
         kIdentityFieldPrelude + store + read + identityFieldStore("%first", "%nil"),
         "!ctnative.opt<!ctnative.num<i32>>", false, 1},
        {"identity field presence does not collapse different primitive schema values",
         kIdentityFieldPrelude + store + identityFieldStore("%second", "%yes") + read,
         "!ctnative.variant<!ctnative.bool, !ctnative.num<i32>>", false, 1},
        {"one conditional own-field store leaves an absent path",
         kIdentityFieldPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + store + "  }\n" +
             read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"both conditional own-field stores establish presence",
         kIdentityFieldPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + store +
             "  } else {\n" + identityFieldStore("%first", "%wide") + "  }\n" + read,
         "!ctnative.num<f64>", false, 1},
        {"stores to different allocations on two arms leave this field absent",
         kIdentityFieldPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + store +
             "  } else {\n" + identityFieldStore("%second") + "  }\n" + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a definite store survives an unrelated conditional field write",
         kIdentityFieldPrelude + store + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityFieldStore("%second", "%wide") + "  }\n" + read,
         "!ctnative.num<f64>", false, 1},
        {"an unknown incoming receiver cannot borrow another object's initialization",
         kIdentityFieldPrelude + store + identityFieldRead("%p"),
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a dynamic field mutation invalidates earlier instance initialization",
         kIdentityFieldPrelude + store + identityFieldStore("%second", "%one", "%p") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a loop-carried object origin remains conservative despite a schema store",
         kIdentityFieldPrelude + store + R"mlir(
  %bit = ctjs.truthy %p
  %carried = scf.while (%before = %first) : (!ctjs.value) -> !ctjs.value {
    scf.condition(%bit) %before : !ctjs.value
  } do {
  ^bb0(%after: !ctjs.value):
    scf.yield %after : !ctjs.value
  }
)mlir" + identityFieldRead("%carried"),
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
    };
    for (const row & r : rows) { check(context, r); }

    const std::string stringPrelude = kIdentityFieldPrelude + R"mlir(
  %text = ctjs.constant #ctjs.string<"owning field">
  %empty = ctjs.constant #ctjs.string<"">
  %null = ctjs.constant #ctjs.null
)mlir";
    const auto stringStore = identityFieldStore("%first", "%text");
    const std::vector<row> stringRows = {
        {"an initialized String field takes its type from the actual source store",
         stringPrelude + stringStore + read, "!ctnative.str<utf8>", false, 1},
        {"an initialized empty String field is still definitely present",
         stringPrelude + identityFieldStore("%first", "%empty") + read, "!ctnative.str<utf8>",
         false, 1},
        {"a later String store cannot initialize an earlier field read",
         stringPrelude + read + stringStore, "!ctnative.opt<!ctnative.str<utf8>>", false, 0},
        {"a String schema on another allocation cannot initialize the queried receiver",
         stringPrelude + identityFieldStore("%second", "%text") + read,
         "!ctnative.opt<!ctnative.str<utf8>>", false, 0},
        {"a different initialized String key cannot prove this key's presence",
         stringPrelude + identityFieldStore("%first", "%text", "%different") +
             identityFieldStore("%second", "%text") + read,
         "!ctnative.opt<!ctnative.str<utf8>>", false, 0},
        {"a String field keeps explicit Null from another allocation in its schema",
         stringPrelude + stringStore + identityFieldStore("%second", "%null") + read,
         "!ctnative.opt<!ctnative.str<utf8>>", false, 1},
        {"a String field keeps explicit Undefined from another allocation in its schema",
         stringPrelude + stringStore + identityFieldStore("%second", "%nil") + read,
         "!ctnative.opt<!ctnative.str<utf8>>", false, 1},
        {"a String read retains a later explicit Null store in the complete type join",
         stringPrelude + stringStore + read + identityFieldStore("%first", "%null"),
         "!ctnative.opt<!ctnative.str<utf8>>", false, 1},
        {"a String read retains a later explicit Undefined store in the complete type join",
         stringPrelude + stringStore + read + identityFieldStore("%first", "%nil"),
         "!ctnative.opt<!ctnative.str<utf8>>", false, 1},
        {"a last String store does not erase an earlier Number from the schema census",
         stringPrelude + store + stringStore + read,
         "!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>", false, 1},
        {"a saved String read does not erase a later Number from the storage schema",
         stringPrelude + stringStore + read + store,
         "!ctnative.variant<!ctnative.num<i32>, !ctnative.str<utf8>>", false, 1},
        {"a String field keeps a same-schema Boolean from another allocation",
         stringPrelude + stringStore + identityFieldStore("%second", "%yes") + read,
         "!ctnative.variant<!ctnative.bool, !ctnative.str<utf8>>", false, 1},
        {"a definitely initialized String field still joins an unknown incoming write",
         stringPrelude + stringStore + identityFieldStore("%second", "%p") + read,
         "!ctnative.boxed", false, 1},
        {"a one-arm String store leaves the uninitialized path visible",
         stringPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + stringStore + "  }\n" +
             read,
         "!ctnative.opt<!ctnative.str<utf8>>", false, 0},
        {"both String arms initialize one receiver without inventing a literal value",
         stringPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + stringStore +
             "  } else {\n" + identityFieldStore("%first", "%empty") + "  }\n" + read,
         "!ctnative.str<utf8>", false, 1},
        {"both String and Null arms initialize the field but retain its optional type",
         stringPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + stringStore +
             "  } else {\n" + identityFieldStore("%first", "%null") + "  }\n" + read,
         "!ctnative.opt<!ctnative.str<utf8>>", false, 1},
        {"different String receivers on the two arms do not establish presence",
         stringPrelude + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" + stringStore +
             "  } else {\n" + identityFieldStore("%second", "%text") + "  }\n" + read,
         "!ctnative.opt<!ctnative.str<utf8>>", false, 0},
        {"a dynamic write cannot preserve earlier String field initialization",
         stringPrelude + stringStore + identityFieldStore("%second", "%empty", "%p") + read,
         "!ctnative.opt<!ctnative.str<utf8>>", false, 0},
    };
    for (const row & r : stringRows) { check(context, r); }
    std::printf("owning String fields: %zu independent type and presence rows\n",
                stringRows.size());

    const std::string crossCall = R"mlir(
module {
  ctjs.func private @make(%receiver: !ctjs.value, %target: !ctjs.value,
                          %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %object = ctjs.create_object {ctnative.object_identity}
    %key = ctjs.constant #ctjs.string<"value">
    %number = ctjs.constant #ctjs.number<4607182418800017408>
    ctjs.set_property %object[%key], %number {ctnative.object_field_group = 7 : i64}
    ctjs.return %object
  }
  ctjs.func @read(%receiver: !ctjs.value, %target: !ctjs.value,
                  %callee: !ctjs.value) -> !ctjs.value
      attributes {upvalue_count = 0 : i32} {
    %nil = ctjs.constant #ctjs.undefined
    %object = ctjs.call_direct @make(%nil, %nil, %nil)
    %key = ctjs.constant #ctjs.string<"value">
    %observed = ctjs.get_property %object[%key]
        {check, ctnative.object_field_group = 7 : i64}
    ctjs.return %observed
  }
}
)mlir";
    check(context, {"a cross-call object origin cannot borrow a schema initialization", crossCall,
                    "!ctnative.opt<!ctnative.num<i32>>", true, 0});

    // Keep all annotations while moving the store to another live allocation.
    // A newly constructed solver and a cloned source module must both observe
    // each change. The original schema group remains deliberately unchanged.
    auto module = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + kIdentityFieldPrelude + store + read + "  ctjs.return %observed\n}\n",
        &context);
    if (!module) {
        std::printf("FAIL identity own-field live mutation fixture did not parse\n");
        ++failures;
        return;
    }
    ctcompile::ctjs::SetPropertyOp stored;
    ctcompile::ctjs::GetPropertyOp observed;
    llvm::SmallVector<ctcompile::ctjs::CreateObjectOp> objects;
    module->walk([&](ctcompile::ctjs::SetPropertyOp op) { stored = op; });
    module->walk([&](ctcompile::ctjs::GetPropertyOp op) { observed = op; });
    module->walk([&](ctcompile::ctjs::CreateObjectOp op) { objects.push_back(op); });
    if (!stored || !observed || objects.size() != 2) {
        std::printf("FAIL identity own-field live mutation fixture lost its exact operations\n");
        ++failures;
        return;
    }
    const auto liveAndFresh = [&](const char * what, const char * expected, bool assigned) {
        check(*module, what, expected);
        const auto proof = ctcompile::ctnative::queryNativeObjectFieldPresence(observed);
        if (proof.assigned != assigned || proof.exhausted) {
            std::printf("FAIL %s: stale source retained the wrong field presence\n", what);
            ++failures;
        }
        mlir::OwningOpRef<mlir::ModuleOp> fresh{llvm::cast<mlir::ModuleOp>(module->clone())};
        check(*fresh, what, expected);
        ctcompile::ctjs::GetPropertyOp freshRead;
        fresh->walk([&](ctcompile::ctjs::GetPropertyOp op) { freshRead = op; });
        const auto freshProof = ctcompile::ctnative::queryNativeObjectFieldPresence(freshRead);
        if (freshProof.assigned != assigned || freshProof.exhausted) {
            std::printf("FAIL %s: fresh source retained the wrong field presence\n", what);
            ++failures;
        }
    };
    liveAndFresh("identity field presence before live mutation", "!ctnative.num<i32>", true);
    stored->setOperand(0, objects[1].getResult());
    liveAndFresh("identity field rederives a changed store receiver",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    stored->setOperand(0, objects[0].getResult());
    stored->moveAfter(observed);
    liveAndFresh("identity field rederives a store moved after the read",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    stored->moveBefore(observed);
    liveAndFresh("identity field rebuilds presence after restoring its store", "!ctnative.num<i32>",
                 true);
    auto number = stored.getValue().getDefiningOp<ctcompile::ctjs::ConstantOp>();
    const auto original = number.getValue();
    number->setAttr("value", ctcompile::ctjs::UndefinedAttr::get(&context));
    liveAndFresh("identity field retains a newly explicit Undefined value",
                 "!ctnative.opt<!ctnative.bottom>", true);
    number->setAttr("value", original);
    liveAndFresh("identity field rebuilds its value join after restoring the source",
                 "!ctnative.num<i32>", true);
    for (const char * text : {"owned String", ""}) {
        number->setAttr("value", ctcompile::ctjs::StringAttr::get(&context, text));
        observed->setAttr("ctnative.object_schema", mlir::StringAttr::get(&context, "number"));
        liveAndFresh("a live String store supersedes a forged Number field report",
                     "!ctnative.str<utf8>", true);
        stored->setOperand(0, objects[1].getResult());
        liveAndFresh("a live String receiver change invalidates stale presence",
                     "!ctnative.opt<!ctnative.str<utf8>>", false);
        stored->setOperand(0, objects[0].getResult());
        stored->moveAfter(observed);
        liveAndFresh("a live String read before its store keeps implicit absence",
                     "!ctnative.opt<!ctnative.str<utf8>>", false);
        stored->moveBefore(observed);
        liveAndFresh("restoring a String store restores its exact source initialization",
                     "!ctnative.str<utf8>", true);
    }
    number->setAttr("value", ctcompile::ctjs::NullAttr::get(&context));
    liveAndFresh("changing String to Null retains the actual absent type",
                 "!ctnative.opt<!ctnative.bottom>", true);
    number->setAttr("value", original);
    liveAndFresh("restoring Number after String does not reuse its previous carrier",
                 "!ctnative.num<i32>", true);
}

const std::string kIdentityMapPrelude = kIdentityFieldPrelude + R"mlir(
  %slot = ctjs.constant #ctjs.string<"slot">
  %missing = ctjs.constant #ctjs.string<"missing">
  %setName = ctjs.constant #ctjs.string<"set">
  %getName = ctjs.constant #ctjs.string<"get">
  %deleteName = ctjs.constant #ctjs.string<"delete">
  %clearName = ctjs.constant #ctjs.string<"clear">
  %constructor = ctjs.load_global "Map" {ctnative.map_constructor}
  %map = ctjs.construct %constructor(%constructor)
      {ctnative.map_group = 3 : i64, ctnative.map_site}
  %setter = ctjs.get_property %map[%setName] {ctnative.map_method}
  %getter = ctjs.get_property %map[%getName] {ctnative.map_method}
  %eraser = ctjs.get_property %map[%deleteName] {ctnative.map_method}
  %clearer = ctjs.get_property %map[%clearName] {ctnative.map_method}
)mlir";

std::string identityMapSet(llvm::StringRef result, llvm::StringRef object) {
    return "  " + result.str() + " = ctjs.call %setter(%map, %slot, " + object.str() +
           ") {ctnative.map_action = \"set\", ctnative.map_group = 3 : i64}\n";
}

std::string identityMapGet(llvm::StringRef result, llvm::StringRef key) {
    // Even refusals carry map_present. It is an old report, not proof that
    // this exact read still names an initialized object in the current source.
    return "  " + result.str() + " = ctjs.call %getter(%map, " + key.str() +
           ") {ctnative.map_action = \"get\", ctnative.map_present}\n";
}

ctcompile::ctjs::GetPropertyOp checkedField(mlir::ModuleOp module) {
    ctcompile::ctjs::GetPropertyOp read;
    module.walk([&](ctcompile::ctjs::GetPropertyOp op) {
        if (op->hasAttr("check")) { read = op; }
    });
    return read;
}

void checkFieldBudgets(mlir::ModuleOp module, const char * what, bool assigned) {
    const auto read = checkedField(module);
    const auto complete = ctcompile::ctnative::queryNativeObjectFieldPresence(read);
    if (complete.assigned != assigned || complete.exhausted || complete.work == 0) {
        std::printf("FAIL %s: complete field-presence budget fixture disagrees\n", what);
        ++failures;
        return;
    }
    for (uint64_t limit = 0; limit < complete.work; ++limit) {
        const auto bounded = ctcompile::ctnative::queryNativeObjectFieldPresence(read, limit);
        if (bounded.assigned || !bounded.exhausted || bounded.work > limit) {
            std::printf("FAIL %s: field presence survived work cutoff %llu of %llu\n", what,
                        static_cast<unsigned long long>(limit),
                        static_cast<unsigned long long>(complete.work));
            ++failures;
            return;
        }
    }
    const auto exact = ctcompile::ctnative::queryNativeObjectFieldPresence(read, complete.work);
    if (exact.assigned != assigned || exact.exhausted || exact.work != complete.work) {
        std::printf("FAIL %s: exact field-presence budget did not reproduce the proof\n", what);
        ++failures;
    }
    std::printf("field presence %s: %llu exhaustive work cutoffs\n", what,
                static_cast<unsigned long long>(complete.work));
}

void checkIdentityMapFieldRows(mlir::MLIRContext & context) {
    const std::string store = identityFieldStore();
    const std::string save = identityMapSet("%put") + identityMapGet();
    const std::string read = identityFieldRead("%saved");
    const std::string erase =
        "  %removed = ctjs.call %eraser(%map, %slot) {ctnative.map_action = \"delete\"}\n";
    const std::string clear =
        "  %cleared = ctjs.call %clearer(%map) {ctnative.map_action = \"clear\"}\n";
    const std::vector<row> rows = {
        {"Map.get retains the exact initialized object from the reaching set",
         kIdentityMapPrelude + store + save + read, "!ctnative.num<i32>", false, 1},
        {"Map.get of a missing key cannot borrow the stored object's fields",
         kIdentityMapPrelude + store + identityMapSet("%put") +
             identityMapGet("%saved", "%missing") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"Map.get follows an overwrite to an uninitialized object in the same schema",
         kIdentityMapPrelude + store + identityMapSet("%put") +
             identityMapSet("%overwrite", "%second") + identityMapGet() + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a saved Map.get alias retains its original object after overwrite",
         kIdentityMapPrelude + store + save + identityMapSet("%overwrite", "%second") + read,
         "!ctnative.num<i32>", false, 1},
        {"a saved Map.get alias retains its original object after deletion",
         kIdentityMapPrelude + store + save + erase + read, "!ctnative.num<i32>", false, 1},
        {"a saved Map.get alias retains its original object after clearing the Map",
         kIdentityMapPrelude + store + save + clear + read, "!ctnative.num<i32>", false, 1},
        {"a fresh Map.get after deletion has no initialized object origin",
         kIdentityMapPrelude + store + identityMapSet("%put") + erase + identityMapGet() + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a field write through a Map.get alias initializes the original object",
         kIdentityMapPrelude + save + identityFieldStore("%saved") + identityFieldRead(),
         "!ctnative.num<i32>", false, 1},
        {"a field write through the original object initializes its saved Map.get alias",
         kIdentityMapPrelude + save + store + read, "!ctnative.num<i32>", false, 1},
        {"a saved alias field write after deletion initializes its retained object",
         kIdentityMapPrelude + save + erase + identityFieldStore("%saved") + identityFieldRead(),
         "!ctnative.num<i32>", false, 1},
        {"a saved alias field read keeps an explicitly stored Undefined",
         kIdentityMapPrelude + store + save + identityFieldStore("%saved", "%nil") + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 1},
        {"one branch writing through a saved alias leaves an absent path",
         kIdentityMapPrelude + save + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityFieldStore("%saved") + "  }\n" + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"both branches can initialize one object through different aliases",
         kIdentityMapPrelude + save + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityFieldStore("%saved") + "  } else {\n" + identityFieldStore("%first", "%wide") +
             "  }\n" + read,
         "!ctnative.num<f64>", false, 1},
        {"conditional Map writes preserve one independently known object origin",
         kIdentityMapPrelude + store + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityMapSet("%left") + "  } else {\n" + identityMapSet("%right") + "  }\n" +
             identityMapGet() + read,
         "!ctnative.num<i32>", false, 1},
        {"one conditional Map write cannot establish a reaching object",
         kIdentityMapPrelude + store + "  %bit = ctjs.truthy %p\n  scf.if %bit {\n" +
             identityMapSet("%left") + "  }\n" + identityMapGet() + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"an unknown call invalidates saved object field facts",
         kIdentityMapPrelude + store + save + "  %effect = ctjs.call %p(%nil)\n" + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"a same-schema second Map does not inherit the first Map's entry",
         kIdentityMapPrelude + store + identityMapSet("%put") + R"mlir(
  %otherMap = ctjs.construct %constructor(%constructor)
      {ctnative.map_group = 3 : i64, ctnative.map_site}
  %otherGetter = ctjs.get_property %otherMap[%getName] {ctnative.map_method}
  %saved = ctjs.call %otherGetter(%otherMap, %slot)
      {ctnative.map_action = "get", ctnative.map_present}
)mlir" + read,
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
        {"same-origin structured yields retain the saved object identity",
         kIdentityMapPrelude + store + save + R"mlir(
  %bit = ctjs.truthy %p
  %joined = scf.if %bit -> (!ctjs.value) {
    scf.yield %first : !ctjs.value
  } else {
    scf.yield %saved : !ctjs.value
  }
)mlir" + identityFieldRead("%joined"),
         "!ctnative.num<i32>", false, 1},
        {"different-origin structured yields cannot borrow one allocation's fields",
         kIdentityMapPrelude + store + save + R"mlir(
  %bit = ctjs.truthy %p
  %joined = scf.if %bit -> (!ctjs.value) {
    scf.yield %saved : !ctjs.value
  } else {
    scf.yield %second : !ctjs.value
  }
)mlir" + identityFieldRead("%joined"),
         "!ctnative.opt<!ctnative.num<i32>>", false, 0},
    };
    for (const row & r : rows) { check(context, r); }

    // The positive also passes actual preparation: raw annotations are not
    // required for this source to acquire the independently checked schema.
    const std::string text = prologue() + kIdentityMapPrelude + store + save +
                             identityMapSet("%overwrite", "%second") + read +
                             "  ctjs.return %observed\n}\n";
    auto module = mlir::parseSourceString<mlir::ModuleOp>(text, &context);
    if (!module) {
        std::printf("FAIL saved Map own-field preparation fixture did not parse\n");
        ++failures;
        return;
    }
    ctcompile::ctnative::prepareNativeMaps(*module);
    ctcompile::ctnative::prepareNativeObjectIdentities(*module);
    auto observed = checkedField(*module);
    if (!observed || ctcompile::ctnative::nativeObjectFieldGroup(observed) < 0) {
        std::printf("FAIL saved Map own-field fixture did not receive a live object schema\n");
        ++failures;
        return;
    }
    check(*module, "actual native preparation retains a saved object's initialized field",
          "!ctnative.num<i32>");
    checkFieldBudgets(*module, "prepared saved Map alias", true);

    // Retain prepared reports and alter the original reaching object. A saved
    // alias must never change identity just because a later Map set changes.
    llvm::SmallVector<ctcompile::ctjs::CallOp> puts;
    ctcompile::ctjs::CallOp saved;
    ctcompile::ctjs::SetPropertyOp fieldStore;
    module->walk([&](ctcompile::ctjs::CallOp call) {
        const auto action = ctcompile::ctnative::nativeMapAction(call);
        if (action == "set") { puts.push_back(call); }
        if (action == "get") { saved = call; }
    });
    module->walk([&](ctcompile::ctjs::SetPropertyOp op) { fieldStore = op; });
    if (puts.size() != 2 || !saved || !fieldStore) {
        std::printf("FAIL saved Map live mutation fixture lost exact source operations\n");
        ++failures;
        return;
    }
    const auto liveAndFresh = [&](const char * what, const char * expected, bool assigned) {
        check(*module, what, expected);
        const auto stale = ctcompile::ctnative::queryNativeObjectFieldPresence(observed);
        mlir::OwningOpRef<mlir::ModuleOp> fresh{llvm::cast<mlir::ModuleOp>(module->clone())};
        check(*fresh, what, expected);
        const auto rebuilt =
            ctcompile::ctnative::queryNativeObjectFieldPresence(checkedField(*fresh));
        if (stale.assigned != assigned || rebuilt.assigned != assigned || stale.exhausted ||
            rebuilt.exhausted) {
            std::printf("FAIL %s: live/fresh Map field presence disagrees\n", what);
            ++failures;
        }
    };
    const auto original = puts[0].getArgs()[1];
    puts[0]->setOperand(3, puts[1].getArgs()[1]);
    liveAndFresh("saved Map field rederives a changed reaching payload",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    puts[0]->setOperand(3, original);
    liveAndFresh("saved Map field restores its reaching payload", "!ctnative.num<i32>", true);
    const auto getKey = saved.getArgs()[0];
    auto missing = mlir::cast<ctcompile::ctjs::ConstantOp>(fieldStore.getKey().getDefiningOp());
    saved->setOperand(2, missing.getResult());
    liveAndFresh("saved Map field ignores stale presence after a lookup-key mutation",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    saved->setOperand(2, getKey);
    auto getter = saved.getCallee().getDefiningOp<ctcompile::ctjs::GetPropertyOp>();
    const auto originalCalleeKey = getter.getKey();
    getter->setOperand(1, missing.getResult());
    liveAndFresh("saved Map field rejects a stale action after a method-key mutation",
                 "!ctnative.opt<!ctnative.num<i32>>", false);
    getter->setOperand(1, originalCalleeKey);
    liveAndFresh("saved Map field restores its live method and lookup", "!ctnative.num<i32>", true);
    checkFieldBudgets(*module, "restored saved Map alias", true);

    // A live malformed source can retain valid reports. Query it directly,
    // without sending invalid SSA to the type solver: a then-only allocation
    // cannot become the receiver outside its region or in the sibling arm.
    for (bool sibling : {false, true}) {
        const std::string thenBody = R"mlir(
  %bit = ctjs.truthy %p
  scf.if %bit {
    %inside = ctjs.create_object {ctnative.object_identity, inside}
    ctjs.set_property %inside[%key], %one {ctnative.object_field_group = 7 : i64}
)mlir";
        const std::string body = kIdentityMapPrelude + store + save + thenBody +
                                 (sibling ? "  } else {\n" + read + "  }\n" : "  }\n" + read);
        auto scoped = mlir::parseSourceString<mlir::ModuleOp>(
            prologue() + body + "  ctjs.return %one\n}\n", &context);
        if (!scoped) {
            std::printf("FAIL own-field cross-scope fixture did not parse\n");
            ++failures;
            continue;
        }
        auto scopeRead = checkedField(*scoped);
        ctcompile::ctjs::CreateObjectOp inside;
        scoped->walk([&](ctcompile::ctjs::CreateObjectOp made) {
            if (made->hasAttr("inside")) { inside = made; }
        });
        if (!scopeRead || !inside ||
            !ctcompile::ctnative::queryNativeObjectFieldPresence(scopeRead).assigned) {
            std::printf("FAIL own-field cross-scope fixture lacks its initial live proof\n");
            ++failures;
            continue;
        }
        const auto receiver = scopeRead.getObject();
        scopeRead->setOperand(0, inside.getResult());
        mlir::OwningOpRef<mlir::ModuleOp> fresh{llvm::cast<mlir::ModuleOp>(scoped->clone())};
        if (ctcompile::ctnative::queryNativeObjectFieldPresence(scopeRead).assigned ||
            ctcompile::ctnative::queryNativeObjectFieldPresence(checkedField(*fresh)).assigned) {
            std::printf("FAIL own-field initialization crossed an invalid SSA scope\n");
            ++failures;
        }
        scopeRead->setOperand(0, receiver);
        if (!ctcompile::ctnative::queryNativeObjectFieldPresence(scopeRead).assigned) {
            std::printf("FAIL own-field initialization did not recover after a scope repair\n");
            ++failures;
        }
    }

    auto absentModule = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + kIdentityFieldPrelude + identityFieldStore("%second") + identityFieldRead() +
            "  ctjs.return %observed\n}\n",
        &context);
    if (!absentModule) {
        std::printf("FAIL absent own-field budget fixture did not parse\n");
        ++failures;
    } else {
        checkFieldBudgets(*absentModule, "different allocation refusal", false);
    }

    auto branchModule = mlir::parseSourceString<mlir::ModuleOp>(
        prologue() + rows[12].body + "  ctjs.return %observed\n}\n", &context);
    if (!branchModule) {
        std::printf("FAIL branch own-field budget fixture did not parse\n");
        ++failures;
    } else {
        checkFieldBudgets(*branchModule, "both-arm alias writes", true);
    }
}

} // namespace ctcompile::test::type_inference
