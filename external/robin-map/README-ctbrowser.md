# tsl::robin-map, vendored

Upstream: https://github.com/Tessil/robin-map — MIT, header-only. `LICENSE` is
upstream's, unmodified; nothing in `include/` is patched.

## Why it is here

It backs `ctbrowser::string_flat_map`, which is the VM's property index — the
hottest map in the engine. Five open-addressing maps were measured on a real
Phaser frame rather than compared from their READMEs, because on paper they are
all the same shape:

| map | instructions | wall (min of 11) | peak RSS |
|---|---|---|---|
| `boost::unordered_flat_map` | 14.053 G | 1009 ms | 199.7 MB |
| `ankerl::unordered_dense` | 14.360 G | 989 ms | 182.5 MB |
| **`tsl::robin_map`** | **13.995 G** | **973 ms** | **182.4 MB** |
| `absl::flat_hash_map` | 14.593 G | 1005 ms | 199.4 MB |
| `gtl::flat_hash_map` | 14.340 G | 997 ms | 200.5 MB |

**-3.6% wall and -8.7% peak RSS against Boost's**, which had been the default.

The interesting part is that instruction count barely moved (-0.4%) while wall
clock did. This is a CACHE win, not fewer operations: robin-hood probing bounds
how far a key can sit from its ideal slot, so a lookup touches fewer lines.
Callgrind counts instructions and cannot see it — the two metrics disagreeing is
the finding, not noise.

Boost is still a dependency and still the fallback: `CTBROWSER_STRING_MAP` in
`core/containers.hpp` selects any of the five, so this is re-measurable rather
than a decision frozen into the tree.
