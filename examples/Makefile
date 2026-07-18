CXX ?= c++

CXX_IS_CLANG := $(shell $(CXX) --version 2>/dev/null | grep -qi clang && echo yes)
ifeq ($(CXX_IS_CLANG),yes)
CONSTEXPR_FLAGS := -fconstexpr-steps=500000000 -fconstexpr-depth=1024 -fbracket-depth=2048
else
CONSTEXPR_FLAGS := -fconstexpr-ops-limit=3000000000 -fconstexpr-loop-limit=10000000 -fconstexpr-depth=1024
endif

LARK := ../external/compile-time-html/external/compile-time-lark
SUBMODULE_INCLUDES := \
	-I../external/compile-time-html/include \
	-I../external/compile-time-javascript/include \
	-I../external/compile-time-css/include \
	-I$(LARK)/include \
	-I$(LARK)/include/ctlark \
	-I$(LARK)/include/ctll

SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS := $(shell pkg-config --libs sdl3)

CXXFLAGS := -std=c++20 -I../include $(SUBMODULE_INCLUDES) $(SDL_CFLAGS) $(CONSTEXPR_FLAGS) -O2 -pedantic -Wall -Wextra -Werror -Wconversion

SOURCES := $(wildcard *.cpp)
TARGETS := $(SOURCES:%.cpp=%)

# reuse (or build) the grammar PCH from the repo root - the one-time
# slow step; gcc picks up ../include/ctbrowser.hpp.gch automatically
ifeq ($(CXX_IS_CLANG),yes)
PCH := ../ctbrowser.pch
PCH_USE = -include-pch $(PCH)
else
PCH := ../include/ctbrowser.hpp.gch
PCH_USE =
endif

all: $(TARGETS)

$(PCH): ../include/ctbrowser.hpp
	$(CXX) $(filter-out $(SDL_CFLAGS),$(CXXFLAGS)) -x c++-header $< -o $@

%: %.cpp $(PCH)
	$(CXX) $(CXXFLAGS) $(PCH_USE) $< -o $@ $(SDL_LIBS)

run: all
	@for t in $(TARGETS); do printf '== %s ==\n' "$$t"; ./$$t; echo; done

clean:
	rm -f $(TARGETS)

.PHONY: all run clean
