PLATFORM ?= $(UNION_PLATFORM)
ifeq (,$(PLATFORM))
	PLATFORM=linux
endif

PREFIX ?= /usr

WARNFLAGS := -pedantic-errors -Wall -Wextra
CXXFLAGS := -std=c++17 -O2
LDFLAGS  := -lstdc++

IS_CLANG := $(shell $(CROSS_COMPILE)c++ --version 2>/dev/null | grep -c clang)

ifeq ($(IS_CLANG),0)
# GCC before 9 keeps std::filesystem in a separate library.
LDFLAGS := $(LDFLAGS) -lstdc++fs
else
# Homebrew's zip.h annotates its API with clang's _Nullable/_Nonnull extension, which
# -pedantic-errors otherwise rejects. Our own code is still held to the strict setting.
WARNFLAGS := $(WARNFLAGS) -Wno-nullability-extension
endif

# FreeType. Present in the miyoomini sysroot (2.10.1), so no bundling is needed;
# on desktop, ask pkg-config.
ifeq ($(PLATFORM),miyoomini)
FREETYPE_CFLAGS := -I$(SYSROOT)$(PREFIX)/include/freetype2
FREETYPE_LIBS   := -lfreetype
else
FREETYPE_CFLAGS := $(shell pkg-config --cflags freetype2)
FREETYPE_LIBS   := $(shell pkg-config --libs freetype2)
endif

ifeq ($(PLATFORM),miyoomini)
CXXFLAGS := $(CXXFLAGS) \
	    -DPLATFORM_MIYOO_MINI=1 \
	    -marm -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -march=armv7ve+simd \
	    -Ideps/include/libxml2 \
	    -Ideps/include \
	    -Wno-psabi  # silence "parameter passing for argument of type '...' changed in GCC 7.1" warnings
LDFLAGS := $(LDFLAGS) \
	-Ldeps/lib \
	-L$(PREFIX)/lib \
	-Wl,-rpath-link,deps/lib
endif

CXX      := $(CROSS_COMPILE)c++

# Per-platform, and overridable, because builds must not share object files -- or, worse,
# the .d dependency files GCC writes, which record absolute include paths. Mixing them
# fails with "No rule to make target '/opt/miyoomini-toolchain/.../ft2build.h'", which
# says nothing about the real cause.
#
# PLATFORM alone is not enough: a build inside a Linux container and one on a Linux or
# macOS host are both PLATFORM=linux but have entirely different include paths. The
# containerised targets in Onion's Makefile therefore override this.
BUILD    ?= ./build/$(PLATFORM)
OBJ_DIR  := $(BUILD)/objects
APP_DIR  := $(BUILD)

# HarfBuzz is built from its own single-file amalgamation (third-party/harfbuzz/src/harfbuzz.cc,
# which #includes every other .cc). One translation unit, ~9s, no meson/autotools and no
# shared library to bundle. Backends we don't want (glib, icu, graphite, coretext, directwrite)
# self-disable because their HAVE_* macros are undefined.
# Defined before INCLUDE, which expands HB_DIR immediately.
#
# Overridable because bebook lives inside the Onion tree, where HarfBuzz is a submodule
# of the parent repo rather than of bebook.
HB_DIR      ?= ../../third-party/harfbuzz/src
HB_OBJECT   := $(OBJ_DIR)/harfbuzz_amalgam.o
HB_CXXFLAGS := -DHAVE_FREETYPE=1 -DHB_NO_MT -I$(HB_DIR) $(FREETYPE_CFLAGS)

# SQLite backs the dictionary lookups. Like HarfBuzz, it is compiled from its single-file
# amalgamation (third-party/sqlite/sqlite3.c) on every platform: one translation unit, no
# external dependency to bundle, identical on macOS host / Linux container / ARM device.
# Fetch it with tools/fetch_sqlite.sh (third-party/sqlite/ is gitignored). Overridable for
# the same reason HB_DIR is.
SQLITE_DIR    ?= third-party/sqlite
SQLITE_OBJECT := $(OBJ_DIR)/sqlite3_amalgam.o

# On the device everything lives at the default prefix, so plain -l flags resolve. On a
# desktop the libraries sit under an arbitrary prefix (e.g. Homebrew), so ask
# pkg-config for both the search paths and the include dirs.
#
# For SDL specifically, add the includedir itself rather than only pkg-config's
# --cflags: the latter points at <prefix>/include/SDL, but the sources spell their
# includes <SDL/SDL.h>.
ifeq ($(PLATFORM),miyoomini)
SDL_CFLAGS :=
SDL_LIBS   := -lSDL
# libzip and libxml2 are absent from the toolchain sysroot, so they come from deps/.
DEP_CFLAGS := -Ideps/include -Ideps/include/libxml2
DEP_LIBS   := -lzip -lxml2
else
SDL_CFLAGS := $(shell pkg-config --cflags sdl 2>/dev/null) $(addprefix -I,$(shell pkg-config --variable=includedir sdl 2>/dev/null))
SDL_LIBS   := $(shell pkg-config --libs sdl 2>/dev/null)
DEP_CFLAGS := $(shell pkg-config --cflags libzip libxml-2.0 2>/dev/null)
DEP_LIBS   := $(shell pkg-config --libs libzip libxml-2.0 2>/dev/null)
endif

# src/common is Onion's shared header tree, included for utils/hash.h alone: the
# Game Switcher names preview files by that hash, and a second copy here would be
# free to drift out of step with it. Safe to add wholesale -- bebook's own headers
# live under util/ and sys/, Onion's under utils/ and system/, so nothing shadows.
ONION_COMMON := ../common
INCLUDE  := -Isrc -I$(ONION_COMMON) $(DEP_CFLAGS) -I$(HB_DIR) -I$(SQLITE_DIR) $(FREETYPE_CFLAGS) $(SDL_CFLAGS)

# Appended only once every pkg-config-derived variable above exists. LDFLAGS is a :=
# variable, so referencing them any earlier would silently expand to nothing.
LDFLAGS := $(LDFLAGS) $(SDL_LIBS) $(FREETYPE_LIBS) $(DEP_LIBS)

ROTOZOOM_SRC := src/extern/rotozoom/SDL_rotozoom.c
COMMON_SRC   := $(filter-out src/reader/main.cpp, $(wildcard src/filetypes/*.cpp src/filetypes/txt/*.cpp src/filetypes/epub/*.cpp src/reader/*.cpp src/reader/views/*.cpp src/reader/views/token_view/*.cpp src/sys/*.cpp src/text/*.cpp src/util/*.cpp src/doc_api/*.cpp src/lexicon/*.cpp src/extern/hash-library/*.cpp))
READER_SRC   := $(COMMON_SRC) src/reader/main.cpp
# text_specimen.cpp has its own main() and is built as a separate target.
SANDBOX_SRC  := $(COMMON_SRC) $(filter-out src/sandbox/text_specimen.cpp, $(wildcard src/sandbox/*.cpp))

# Standalone type-specimen renderer. Depends only on the text engine, SDL and FreeType,
# deliberately: typography can then be iterated on without a working epub/zip/xml stack.
# sdl_pointer.cpp supplies the deleter behind surface_unique_ptr, which text/font.cpp
# uses; without it the link fails on SDL_Deleter::operator().
SPECIMEN_SRC     := src/sandbox/text_specimen.cpp $(wildcard src/text/*.cpp) src/util/sdl_pointer.cpp
SPECIMEN_OBJECTS := $(SPECIMEN_SRC:%.cpp=$(OBJ_DIR)/%.o) $(HB_OBJECT)
TEST_SRC     := $(COMMON_SRC) $(wildcard src/sys/tests/*.cpp src/reader/tests/*.cpp src/filetypes/epub/tests/*.cpp src/util/tests/*.cpp src/doc_api/tests/*.cpp src/text/tests/*.cpp src/lexicon/tests/*.cpp)

APP_READER_TARGET := bebook
APP_SANDBOX_TARGET := sandbox
APP_TEST_TARGET := test

ROTOZOOM_OBJECT := $(OBJ_DIR)/SDL_rotozoom.o
READER_OBJECTS  := $(READER_SRC:%.cpp=$(OBJ_DIR)/%.o) $(ROTOZOOM_OBJECT) $(HB_OBJECT) $(SQLITE_OBJECT)
SANDBOX_OBJECTS := $(SANDBOX_SRC:%.cpp=$(OBJ_DIR)/%.o) $(ROTOZOOM_OBJECT) $(HB_OBJECT) $(SQLITE_OBJECT)
TEST_OBJECTS    := $(TEST_SRC:%.cpp=$(OBJ_DIR)/%.o) $(ROTOZOOM_OBJECT) $(HB_OBJECT) $(SQLITE_OBJECT)

DEPENDENCIES := \
	    $(READER_OBJECTS:.o=.d)  \
	    $(SANDBOX_OBJECTS:.o=.d) \
	    $(TEST_OBJECTS:.o=.d)

all: build $(DICT) $(APP_DIR)/$(APP_READER_TARGET) $(APP_DIR)/$(APP_SANDBOX_TARGET)

# Just the reader. This is what the Onion build calls: the sandbox harnesses are
# developer tools and nothing ships them.
reader: build $(DICT) $(APP_DIR)/$(APP_READER_TARGET)

# The Italian dictionary DB ships compressed in git (resources/italian.sqlite.xz, ~16MB);
# the raw ~80MB .sqlite is gitignored and unpacked here on demand. Runtime + tests read the
# raw file relative to the working directory. Prefer the xz CLI, fall back to Python's lzma
# so the step works wherever either exists (dev host, test/toolchain containers).
DICT := resources/italian.sqlite
$(DICT): $(DICT).xz
	@if command -v xz >/dev/null 2>&1; then \
		xz -dkf $< ; \
	elif command -v python3 >/dev/null 2>&1; then \
		python3 -c "import lzma,shutil,sys; shutil.copyfileobj(lzma.open(sys.argv[1]), open(sys.argv[2],'wb'))" $< $@ ; \
	else \
		echo "error: need 'xz' or 'python3' to unpack $< -> $@" >&2 ; exit 1 ; \
	fi

$(ROTOZOOM_OBJECT): $(ROTOZOOM_SRC)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDE) -c $< -MMD -o $@

# Third-party: no WARNFLAGS (-pedantic-errors would reject it) and no -MMD (the
# amalgamation's dependency list is the entire HarfBuzz tree).
$(HB_OBJECT): $(HB_DIR)/harfbuzz.cc
	@if [ ! -f "$<" ]; then \
		echo "harfbuzz submodule missing at $(HB_DIR)."; \
		echo "Run: git submodule update --init --recursive third-party/harfbuzz"; \
		exit 1; \
	fi
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(HB_CXXFLAGS) -c $< -o $@

# SQLite amalgamation: a C translation unit, so compiled with cc, not the C++ driver. No
# WARNFLAGS (-pedantic-errors would reject it) and no -MMD (its dependency list is fixed).
# The defines trim the build to what a read-only, single-threaded, embedded lookup needs.
SQLITE_DEFINES := -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
                  -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_TEMP_STORE=3 -DSQLITE_OMIT_DEPRECATED
$(SQLITE_OBJECT): $(SQLITE_DIR)/sqlite3.c
	@if [ ! -f "$<" ]; then \
		echo "sqlite amalgamation missing at $(SQLITE_DIR). Run: tools/fetch_sqlite.sh"; \
		exit 1; \
	fi
	@mkdir -p $(@D)
	$(CROSS_COMPILE)cc $(CPPFLAGS) -O2 $(SQLITE_DEFINES) -I$(SQLITE_DIR) -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDE) $(WARNFLAGS) -c $< -MMD -o $@

$(APP_DIR)/$(APP_READER_TARGET): $(READER_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(APP_DIR)/$(APP_SANDBOX_TARGET): $(SANDBOX_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# libSDLmain (macOS/Windows only) exists to rename the app's main() to SDL_main. gtest
# supplies its own main and never sees SDL's header macro, so linking it leaves SDL_main
# undefined. The tests do not open a window, so drop it.
TEST_LDFLAGS := $(filter-out -lSDLmain,$(LDFLAGS))

$(APP_DIR)/$(APP_TEST_TARGET): $(TEST_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(TEST_LDFLAGS) -lgtest -lgtest_main

specimen: build $(APP_DIR)/specimen

$(APP_DIR)/specimen: $(SPECIMEN_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ $^ $(SDL_LIBS) $(FREETYPE_LIBS) -lstdc++

-include $(DEPENDENCIES)

.PHONY: all reader build clean debug release run_tests specimen

test: $(DICT) $(APP_DIR)/$(APP_TEST_TARGET)
	$(APP_DIR)/$(APP_TEST_TARGET)

# Cross-compilation happens in Onion's toolchain container; see the root Makefile's
# `toolchain` and `with-toolchain` targets.

build:
	@mkdir -p $(APP_DIR)
	@mkdir -p $(OBJ_DIR)

debug: CXXFLAGS += -g3 -O0
debug: all

debug_test: CXXFLAGS += -g
debug_test: test

clean:
	-@rm -rf $(BUILD)
