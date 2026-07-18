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

# engine tests are EXECUTABLES and run headless with no SDL; the
# RENDER test links SDL3 (found via pkg-config) and drives the real
# shell under the dummy video driver - skipped when SDL3 is absent
SDL_AVAILABLE := $(shell pkg-config --exists sdl3 2>/dev/null && echo yes)
TESTS := $(filter-out tests/render.cpp,$(wildcard tests/*.cpp))
ifeq ($(SDL_AVAILABLE),yes)
TESTS += tests/render.cpp
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS := $(shell pkg-config --libs sdl3)
# the SDL3 satellite libraries light up richer paths when installed:
# SDL3_image (PNG/JPG sprites), SDL3_mixer (real mixing, OGG/MP3),
# SDL3_ttf (TrueType page text); absent, the built-in fallbacks serve
ifeq ($(shell pkg-config --exists sdl3-image 2>/dev/null && echo yes),yes)
SDL_CFLAGS += -DCTBROWSER_WITH_IMAGE $(shell pkg-config --cflags sdl3-image)
SDL_LIBS += $(shell pkg-config --libs sdl3-image)
endif
ifeq ($(shell pkg-config --exists sdl3-mixer 2>/dev/null && echo yes),yes)
SDL_CFLAGS += -DCTBROWSER_WITH_MIXER $(shell pkg-config --cflags sdl3-mixer)
SDL_LIBS += $(shell pkg-config --libs sdl3-mixer)
endif
ifeq ($(shell pkg-config --exists sdl3-ttf 2>/dev/null && echo yes),yes)
SDL_CFLAGS += -DCTBROWSER_WITH_TTF $(shell pkg-config --cflags sdl3-ttf)
SDL_LIBS += $(shell pkg-config --libs sdl3-ttf)
endif
endif
BINARIES := $(TESTS:%.cpp=%)
DEPENDENCY_FILES := $(TESTS:%.cpp=%.d)

all: run-tests

tests/render: tests/render.cpp $(PCH)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(PCH_USE) -MMD $< -o $@ $(SDL_LIBS)

$(filter-out tests/render,$(BINARIES)): %: %.cpp $(PCH)
	$(CXX) $(CXXFLAGS) $(PCH_USE) -MMD $< -o $@

run-tests: $(BINARIES)
	@for t in $(BINARIES); do printf '== %s\n' "$$t"; ./$$t || exit 1; done

pch: $(PCH)

# the PCH covers the ENGINE umbrella only - the shell-side headers
# (app/audio/screenshot/stb, SDL-dependent) are deliberately excluded
# so editing them never re-bakes the grammars
$(PCH): include/ctbrowser.hpp $(filter-out include/ctbrowser/app.hpp include/ctbrowser/audio.hpp include/ctbrowser/screenshot.hpp include/ctbrowser/stb_image_write.h,$(wildcard include/ctbrowser/*.hpp)) $(wildcard external/*/include/*.hpp) $(wildcard external/*/include/*/*.hpp)
	$(CXX) $(CXXFLAGS) -x c++-header $< -o $@

-include $(DEPENDENCY_FILES)

clean:
	rm -f $(BINARIES) $(DEPENDENCY_FILES) ctbrowser.pch include/ctbrowser.hpp.gch
