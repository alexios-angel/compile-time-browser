//===- CTNativeInterfaces.td - "this operation has a proved static type" --===//
//
// ONE INTERFACE, AND IT IS THE SEAM. Part 24's Stage 53C: "Every native op
// implements it. Nothing else in the pipeline has to know about the analysis."
//
// That sentence is the design. Phase 54 runs a dataflow analysis; Phases 55-60
// emit C++ from its conclusions. If the emitters asked the ANALYSIS what a
// value's type is, every one of them would have to be able to run it, and the
// project would be back at part 24's own warning - two places computing "what
// type is this". Instead the analysis writes its conclusion into the IR as an
// operation's result type, and this interface is how anything downstream reads
// it back without knowing that an analysis was ever involved.
//
//===----------------------------------------------------------------------===//
#ifndef CTCOMPILE_CTNATIVE_INTERFACES
#define CTCOMPILE_CTNATIVE_INTERFACES

include "mlir/IR/OpBase.td"

def CTNative_StaticTyped : OpInterface<"StaticTypedOpInterface"> {
  let cppNamespace = "::ctcompile::ctnative";
  let description = [{
    Implemented by every operation whose value has a PROVED static type -
    one a later phase may emit a concrete C++ declaration for rather than a
    boxed `script::value`.

    THE CLAIM IS "PROVED", NOT "GUESSED". An operation implementing this
    interface is asserting that Phase 54's inference reached a conclusion
    Phase 54B's oracle would accept: never narrower than what the
    interpreter observes at run time. `!ctnative.boxed` is a perfectly good
    answer here and means the proof came out at the top of the lattice; what
    is NOT a good answer is a `!ctjs.value`, which means no proof was
    attempted at all and the operation should not have claimed the
    interface.

    `hasProvedNativeType()` is how a caller tells those two apart, and it is
    a SHARED declaration rather than an interface method on purpose:
    everything it does is read `getStaticType()` and look at which dialect
    owns the answer, so an implementation that overrode it could only make
    it lie.
  }];

  let methods = [
    InterfaceMethod<
      /*desc=*/[{
        The proved static type of the value this operation produces.

        THE DEFAULT IS THE SINGLE RESULT'S TYPE, which is the whole answer
        for the great majority of native operations: the analysis wrote its
        conclusion there, and re-deriving it would be the second computation
        this interface exists to prevent. Operations with several results,
        or with a result whose MLIR type is not the type the C++ backend
        should declare, override it.

        A null Type is a legal return and means "this operation does not
        have one static type". It is not an error and must not be treated
        as one; `getNumResults() != 1` is the ordinary way to get it.
      }],
      /*retTy=*/"::mlir::Type",
      /*methodName=*/"getStaticType",
      /*args=*/(ins),
      /*methodBody=*/"",
      /*defaultImpl=*/[{
        ::mlir::Operation * self = $_op.getOperation();
        if (self->getNumResults() != 1) { return ::mlir::Type(); }
        return self->getResult(0).getType();
      }]>,
  ];

  let extraSharedClassDeclaration = [{
    /// Does `getStaticType()` name a type from THIS dialect?
    ///
    /// The distinction that matters downstream is not "is there a type" -
    /// there always is - but "did anything prove it". A `!ctjs.value` or a
    /// builtin `i32` reaching here means the operation claimed a proof it
    /// does not have, and Phase 63 records that as a refusal rather than
    /// emitting native C++ for it.
    bool hasProvedNativeType() {
      ::mlir::Type proved = (*this).getStaticType();
      if (!proved) { return false; }
      return ::llvm::isa<::ctcompile::ctnative::CTNativeDialect>(&proved.getDialect());
    }
  }];
}

#endif // CTCOMPILE_CTNATIVE_INTERFACES
