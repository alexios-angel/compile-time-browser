#include "Cases.hpp"

namespace ctcompile::test::escape::arrays::induction_detail {

void InductionCases::setup() {
    prefix =
        "  %zero = ctjs.constant #ctjs.number<0> {storage_test_id = \"zero\"}\n"
        "  %one = ctjs.constant #ctjs.number<4607182418800017408> {storage_test_id = \"one\"}\n"
        "  %two = ctjs.constant #ctjs.number<4611686018427387904> {storage_test_id = \"two\"}\n"
        "  %three = ctjs.constant #ctjs.number<4613937818241073152> {storage_test_id = \"three\"}\n"
        "  %a = ctjs.create_array [%one, %two, %three] {storage_test_id = \"a\"}\n";
    loop =
        "  cf.br ^header(%a, %zero, %zero : !ctjs.value, !ctjs.value, !ctjs.value)\n"
        "^header(%array: !ctjs.value, %index: !ctjs.value, %sum: !ctjs.value):\n"
        "  %key = ctjs.constant #ctjs.string<\"length\">\n"
        "  %length = ctjs.get_property %array[%key]\n"
        "  %less = ctjs.compare lt %index, %length\n"
        "  %flag = ctjs.truthy %less\n"
        "  cf.cond_br %flag, ^body(%array, %index, %sum : !ctjs.value, !ctjs.value, !ctjs.value), "
        "^exit(%sum : !ctjs.value)\n"
        "^body(%base: !ctjs.value, %i: !ctjs.value, %s: !ctjs.value):\n"
        "  %read = ctjs.get_property %base[%i]\n"
        "  %added = ctjs.binary add %s, %read {storage_test_id = \"added\"}\n"
        "  %step = ctjs.binary_static add %i, %one\n"
        "  cf.br ^header(%base, %step, %added : !ctjs.value, !ctjs.value, !ctjs.value)\n"
        "^exit(%result: !ctjs.value):\n"
        "  ctjs.return %result\n";
    original = prefix + loop;

    rows = 0;
    budgets = 0;
}

} // namespace ctcompile::test::escape::arrays::induction_detail
