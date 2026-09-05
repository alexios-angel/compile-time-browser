# Optional removal of unreachable native helpers

`--ctnative-prune-unreachable` removes private CTJS function definitions after
specialization and partial evaluation have erased all their references. An
unused residual variant can otherwise reach native type inference without any
caller supplying parameter facts and make a fully evaluated initializer fail
native admission. Run the pass after PE and before native lowering. It is off
by default and does not replace type or ownership admission.

The pass constructs a graph from the current IR. Non-private functions, external
declarations, bytecode entry index zero and references outside function bodies
are roots. Every symbol reference in a reachable function retains its target,
including references nested in attributes. Every `ctjs.create_closure` retains
all definitions with its numeric function index. This includes unused closure
values, globally published functions and the original callee value retained by
specialized calls for boxed dispatch. The symbolic specialized target is a
separate edge; neither route can substitute for the other.

Reachability scans every block and nested control region conservatively. It
does not infer that a branch is dead, execute calls, drop effects, or remove
closure construction and global stores from surviving functions. A private
cycle with no path from a root can be removed as a group. Bodies reachable only
through another removed function can be removed in the same invocation.
Optimization provenance and earlier reachability annotations are never proof.

The initial contract covers flat CTJS modules with builtin, arith, SCF, CF and
UB support operations. Nested symbol tables/functions, unknown operations,
opaque target encodings and unresolved symbolic or numeric references retain
the complete module. Ambiguous numeric indices retain every matching target.
`max-steps` defaults to 100,000 graph operations; exhaustion also retains the
module. Analysis completes before any deletion, and repeated passes rederive
the graph. `report=true` reports removed and retained definitions; the module
summary is diagnostic data.

Published top-level helpers remain live even if PE removed every direct call.
Their `create_closure` and `store_global` operations still expose a callable
value. Removing these declarations needs a separate contract for external
observation and exports. This pass makes no such assumption.

The source fixture keeps an effectful runtime caller of a generic Map helper
and evaluates a separate initializer's literal calls. The resulting orphan
variants can disappear while the published helper and runtime write remain.
Focused IR tests cover symbol and numeric edges, public/external roots,
global publication, alternate boxed callee values, unreachable cycles, unknown
targets, unsupported scopes, budgets and repeated invocation.

Validation removes two orphan variants and
retains all four reachable source functions. Native and ctbrowser executions
agree on `staged=68`, `observed=17`, `effect=7` and `lifetime42=42`.
The six native CTests cover ordinary and deduced C++, GCC/Clang, numeric output
comparison, an altered-output rejection and absence of VM symbols. The boxed
specialization differential test also runs this pass and still requires
`observation=1020` and `effect=2`. All seven focused CTests and the reachability,
specialization and three PE lit cases pass. The native fixture pipeline requires
nonzero specialization, PE and reachability removal to avoid a vacuous result.

The integrated devbox gate passes 406/406 CTests, including 123/123 lit cases.
The generated reachability fixture also passes ASan/UBSan with leak detection
and agrees with all four interpreter observations.
