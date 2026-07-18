.PHONY: default all run-tests clean pch

default: all

PYTHON := python3

# Earley at compile time needs more constexpr budget than the defaults
CXX_IS_CLANG := $(shell $(CXX) --version 2>/dev/null | grep -qi clang && echo yes)
ifeq ($(CXX_IS_CLANG),yes)
CONSTEXPR_FLAGS := -fconstexpr-steps=500000000 -fconstexpr-depth=1024 -fbracket-depth=2048
else
CONSTEXPR_FLAGS := -fconstexpr-ops-limit=3000000000 -fconstexpr-loop-limit=10000000 -fconstexpr-depth=1024
endif

# The three bricks come in as git submodules (run `git submodule update
# --init --recursive` once after cloning); every brick pins the SAME
# compile-time-lark, and the browser resolves ctlark/ctll through
# compile-time-html's copy so exactly one is in play.
LARK := external/compile-time-html/external/compile-time-lark
SUBMODULE_INCLUDES := \
	-Iexternal/compile-time-html/include \
	-Iexternal/compile-time-javascript/include \
	-Iexternal/compile-time-css/include \
	-I$(LARK)/include \
	-I$(LARK)/include/ctlark \
	-I$(LARK)/include/ctll

override CXXFLAGS := $(CXXFLAGS) -std=c++20 -Iinclude $(SUBMODULE_INCLUDES) $(CONSTEXPR_FLAGS) -O2 -pedantic -Wall -Wextra -Werror -Wconversion

# precompiled header: parsing the HTML + JavaScript + CSS grammars and
# compiling their Earley tables happens ONCE here - the JS grammar is
# the long pole (tens of minutes) - and every translation unit after
# starts from the baked result
ifeq ($(CXX_IS_CLANG),yes)
PCH := ctbrowser.pch
PCH_USE = -include-pch $(PCH)
else
PCH := include/ctbrowser.hpp.gch
PCH_USE =
endif

# engine tests are EXECUTABLES and run headless - no SDL needed; the
# page markup parses during compilation, the checks run after
TESTS := $(wildcard tests/*.cpp)
BINARIES := $(TESTS:%.cpp=%)
DEPENDENCY_FILES := $(TESTS:%.cpp=%.d)

all: run-tests

$(BINARIES): %: %.cpp $(PCH)
	$(CXX) $(CXXFLAGS) $(PCH_USE) -MMD $< -o $@

run-tests: $(BINARIES)
	@for t in $(BINARIES); do printf '== %s\n' "$$t"; ./$$t || exit 1; done

pch: $(PCH)

$(PCH): include/ctbrowser.hpp
	$(CXX) $(CXXFLAGS) -x c++-header $< -o $@

-include $(DEPENDENCY_FILES)

clean:
	rm -f $(BINARIES) $(DEPENDENCY_FILES) ctbrowser.pch include/ctbrowser.hpp.gch
