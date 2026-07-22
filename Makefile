.PHONY: default all run-tests clean pch

default: all

PYTHON := python3

# THE compiler: the project's own std::embed clang (built from the
# alexios-angel/llvm-project std-embed branch; installed into
# tools/clang-std-embed by the embed repo's sync-to-ctbrowser.sh, or
# fetched from the embed repo's GitHub release). No other compiler is
# supported - std::embed is load-bearing (assets.hpp). C++23 and up.
ifeq ($(origin CXX),default)
CXX := tools/clang-std-embed/bin/clang++
ifeq ($(wildcard $(CXX)),)
$(error tools/clang-std-embed/bin/clang++ not found - install the std::embed \
toolchain (embed repo: ./scripts/sync-to-ctbrowser.sh, or \
download the clang-std-embed release archive and unpack it there))
endif
endif

# Earley at compile time needs more constexpr budget than the defaults;
# --embed-dir: #embed/std::embed resource lookups resolve from the repo
# root (the auto-embedded assets of assets.hpp use script-literal paths
# like examples/assets/sprites.bmp)
CONSTEXPR_FLAGS := -fconstexpr-steps=500000000 -fconstexpr-depth=1024 -fbracket-depth=16384 --embed-dir=$(CURDIR)

# ctbrowser parses HTML+CSS+JS entirely BY VALUE at runtime, so it never uses
# the bricks' lark/Earley grammars: define *_NO_GRAMMAR everywhere to skip the
# grammar table builds (the bulk of the old PCH bake).
GRAMMAR_FREE := -DCTHTML_NO_GRAMMAR -DCTCSS_NO_GRAMMAR -DCTJS_NO_GRAMMAR

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

# GLM (header-only) powers the software 3D math in babylon.hpp;
# find the first include root that actually carries it.
GLM_ROOTS := /home/linuxbrew/.linuxbrew/include /opt/homebrew/include /usr/local/include /usr/include
GLM_INC_DIR := $(firstword $(foreach d,$(GLM_ROOTS),$(if $(wildcard $(d)/glm/glm.hpp),$(d))))
GLM_INCLUDE := $(if $(GLM_INC_DIR),-I$(GLM_INC_DIR),)

override CXXFLAGS := $(CXXFLAGS) -std=c++23 -Iinclude $(SUBMODULE_INCLUDES) $(GLM_INCLUDE) $(CONSTEXPR_FLAGS) $(GRAMMAR_FREE) -Wno-overlength-strings -O2 -pedantic -Wall -Wextra -Werror -Wconversion

# precompiled header: parsing the HTML + JavaScript + CSS grammars and
# compiling their Earley tables happens ONCE here - the JS grammar is
# the long pole (tens of minutes) - and every translation unit after
# starts from the baked result
PCH := ctbrowser.pch
PCH_USE = -include-pch $(PCH)

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

# ulimit: the Earley fold expressions at -fbracket-depth=16384 out-grow
# clang's default 8 MB stack during instantiation
tests/render: tests/render.cpp $(PCH)
	@ulimit -s unlimited 2>/dev/null; $(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $(PCH_USE) -MMD $< -o $@ $(SDL_LIBS)

$(filter-out tests/render,$(BINARIES)): %: %.cpp $(PCH)
	@ulimit -s unlimited 2>/dev/null; $(CXX) $(CXXFLAGS) $(PCH_USE) -MMD $< -o $@

# real .html pages enter the type system as generated raw-string
# literals, #include'd as the page<...> template argument
examples/pong.inc: examples/pong.html tools/html-to-inc.py
	$(PYTHON) tools/html-to-inc.py $< > $@

tests/pong: examples/pong.inc

run-tests: $(BINARIES)
	@for t in $(BINARIES); do printf '== %s\n' "$$t"; ./$$t || exit 1; done

pch: $(PCH)

# the PCH covers the ENGINE umbrella only - the shell-side headers
# (app/audio/screenshot/stb, SDL-dependent) are deliberately excluded
# so editing them never re-bakes the grammars
$(PCH): include/ctbrowser.hpp $(filter-out include/ctbrowser/app.hpp include/ctbrowser/audio.hpp include/ctbrowser/screenshot.hpp include/ctbrowser/stb_image_write.h,$(wildcard include/ctbrowser/*.hpp)) $(wildcard external/*/include/*.hpp) $(wildcard external/*/include/*/*.hpp)
	@ulimit -s unlimited 2>/dev/null; $(CXX) $(CXXFLAGS) -x c++-header $< -o $@

-include $(DEPENDENCY_FILES)

clean:
	rm -f $(BINARIES) $(DEPENDENCY_FILES) ctbrowser.pch examples/pong.inc
