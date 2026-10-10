//===- Setup.cpp - Dense array length base fragments ----------------------===//
//
// Initialises the IR fragments shared by every dense array `length` row:
// an array %a holding one object %x, a `length` read and a return.
//
//===----------------------------------------------------------------------===//

#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::length_detail {

/// Builds the base fragments every later group uses and resets the counters.
void LengthCases::setup() {
    values = "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
             "  %key = ctjs.constant #ctjs.string<\"length\"> {storage_test_id = \"key\"}\n"
             "  %x = ctjs.create_object {storage_test_id = \"x\"}\n"
             "  %a = ctjs.create_array [%x] {storage_test_id = \"a\"}\n";
    read = "  %length = ctjs.get_property %a[%key] {storage_test_id = \"length\"}\n";
    overwrite = "  ctjs.set_property %a[%zero], %zero\n";
    done = "  ctjs.return %length\n";
    rows = 0;
    budgets = 0;
}

} // namespace ctcompile::test::escape::arrays::length_detail
