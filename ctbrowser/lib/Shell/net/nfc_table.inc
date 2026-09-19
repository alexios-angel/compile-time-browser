// --- NFC (Unicode Annex #15), for UTS #46 step 2 -----------------
// Canonical decompositions, FULLY expanded so the run time needs no
// recursion. Hangul is algorithmic and is not in the table.
constexpr nfd_entry nfd_table[] = {
#include "nfc_table/nfd_table-01.inc"
#include "nfc_table/nfd_table-02.inc"
#include "nfc_table/nfd_table-03.inc"
};
constexpr char32_t nfd_data[] = {
#include "nfc_table/nfd_data-01.inc"
};

// Canonical_Combining_Class, nonzero only.
constexpr ccc_range ccc_ranges[] = {
#include "nfc_table/ccc_ranges-01.inc"
};

// The canonical pairs that RECOMPOSE - the composition exclusions
// are already gone, having been derived from NFC itself. Sorted by
// (starter, second) for the binary search.
constexpr compose_pair compose_pairs[] = {
#include "nfc_table/compose_pairs-01.inc"
#include "nfc_table/compose_pairs-02.inc"
};
