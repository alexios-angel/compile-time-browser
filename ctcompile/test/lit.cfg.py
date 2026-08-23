# The ctcompile lit configuration.
#
# WHAT THE PLAN SPECIFIES is the middle block: config.name, config.suffixes,
# config.test_format and the tool substitutions. The preamble and the two roots
# are standard LLVM out-of-tree convention, which the plan does not spell -
# noted here rather than presented as though it did.
import os

import lit.formats
import lit.llvm

lit.llvm.initialize(lit_config, config)
from lit.llvm import llvm_config  # noqa: E402  (must follow initialize)

config.name = "ctcompile"
config.suffixes = [".mlir", ".td", ".test"]
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)

config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.ctcompile_obj_root, "test")

# FileCheck, not, count - everything a RUN line reaches for that is not ours.
llvm_config.use_default_substitutions()

# A LIST OF DIRECTORIES, not one. The plan's snippet passes a single
# `config.ctcompile_tools_dir`, which assumes every tool lands in a shared bin/
# - and these do not: each is built where its own CMakeLists puts it, and moving
# them would change paths that documentation and other tests already name.
# add_tool_substitutions accepts a search list, so nothing has to move.
tools = ["ctjs-opt", "ctjs-translate", "ctcompile"]
llvm_config.add_tool_substitutions(tools, config.ctcompile_tools_dirs)
