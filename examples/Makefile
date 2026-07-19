# THE compiler: the project's std::embed clang (see the root Makefile);
# no other compiler is supported. C++23 and up.
ifeq ($(origin CXX),default)
CXX := ../tools/clang-std-embed/bin/clang++
ifeq ($(wildcard $(CXX)),)
$(error ../tools/clang-std-embed/bin/clang++ not found - install the std::embed toolchain first)
endif
endif

# --embed-dir: asset paths in scripts are repo-root-relative
CONSTEXPR_FLAGS := -fconstexpr-steps=500000000 -fconstexpr-depth=1024 -fbracket-depth=16384 --embed-dir=$(CURDIR)/..

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

CXXFLAGS := -std=c++23 -I../include $(SUBMODULE_INCLUDES) $(SDL_CFLAGS) $(CONSTEXPR_FLAGS) -O2 -pedantic -Wall -Wextra -Werror -Wconversion

SOURCES := $(wildcard *.cpp)
TARGETS := $(SOURCES:%.cpp=%)

# reuse (or build) the grammar PCH from the repo root - the one-time
# slow step
PCH := ../ctbrowser.pch
PCH_USE = -include-pch $(PCH)

all: $(TARGETS)

$(PCH): ../include/ctbrowser.hpp
	@ulimit -s unlimited 2>/dev/null; $(CXX) $(filter-out $(SDL_CFLAGS),$(CXXFLAGS)) -x c++-header $< -o $@

# real .html pages become raw-string .inc literals (see tools/html-to-inc.py)
pong.inc: pong.html ../tools/html-to-inc.py
	python3 ../tools/html-to-inc.py $< > $@

pong: pong.inc

%: %.cpp $(PCH)
	@ulimit -s unlimited 2>/dev/null; $(CXX) $(CXXFLAGS) $(PCH_USE) $< -o $@ $(SDL_LIBS)

run: all
	@for t in $(TARGETS); do printf '== %s ==\n' "$$t"; ./$$t; echo; done

clean:
	rm -f $(TARGETS) pong.inc

.PHONY: all run clean
