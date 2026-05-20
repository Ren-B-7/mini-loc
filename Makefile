# Project Name and Directories
TARGET_NAME = loc
BUILD_DIR = build
BIN_DIR = bin
PROFILE_DIR = profiles

# --- OS / Platform Detection ---
# On Windows this is always set by the environment (works in MinGW/MSYS2 too).
# uname -s is available inside MinGW, so we use it to distinguish macOS/Linux.
ifeq ($(OS),Windows_NT)
    PLATFORM   := windows
    EXE_SUFFIX := .exe
    # MinGW ships python3; fall back to plain python if not found
    PYTHON     := $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)
    # Default install location under the user profile (forward-slashes work in MinGW)
    INSTALL_DIR ?= $(USERPROFILE)/.local/bin
else
    UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)
    ifeq ($(UNAME_S),Darwin)
        PLATFORM := darwin
    else
        PLATFORM := linux
    endif
    EXE_SUFFIX :=
    PYTHON     := $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null || echo python3)
    INSTALL_DIR ?= $(HOME)/.local/bin
endif

EXECUTABLE_SINGLE = $(BIN_DIR)/$(TARGET_NAME)-single$(EXE_SUFFIX)
EXECUTABLE_MULTI = $(BIN_DIR)/$(TARGET_NAME)-multi$(EXE_SUFFIX)

# --- Hardening Toggle ---
# Allows 'make HARDENED=1' for a hardened build. Defaults to 0 (Disabled).
HARDENED ?= 0

# Source and Header files
JSON_ASSETS = assets/languages.json
SRCS_SHARED = src/include/set.c src/fs.c src/count.c src/cli.c src/languages.c src/languages_data.c
SRCS_SINGLE = src/mini-loc-single.c
SRCS_MULTI = src/mini-loc-multi.c src/threading.c
SRCS_ALL = $(SRCS_SINGLE) $(SRCS_MULTI) $(SRCS_SHARED)
HDRS = src/include/cli.h src/include/count.h src/include/fs.h \
              src/include/languages_data.h src/include/languages.h \
              src/include/minicli.h src/include/output.h src/include/set.h \
              src/include/threading.h src/include/types.h

# Object files
OBJS_COMMON = $(BUILD_DIR)/set.o $(BUILD_DIR)/cli.o $(BUILD_DIR)/count.o \
              $(BUILD_DIR)/fs.o $(BUILD_DIR)/languages.o $(BUILD_DIR)/threading.o \
              $(BUILD_DIR)/languages_data.o
OBJ_SINGLE = $(BUILD_DIR)/mini-loc-single.o
OBJ_MULTI = $(BUILD_DIR)/mini-loc-multi.o

# --- Compiler Detection ---
CC = gcc
IS_GCC := $(shell $(CC) -v 2>&1 | grep -q "gcc" && echo 1 || echo 0)

# macOS-specific compiler flag
ifeq ($(PLATFORM),darwin)
    DARWIN_FLAGS = -DDARWIN
else
    DARWIN_FLAGS =
endif

# MinGW/Windows: POSIX source macros can cause warnings with the MinGW headers;
# they are not needed since MinGW provides its own POSIX-compatible layer.
ifeq ($(PLATFORM),windows)
    POSIX_FLAGS =
else
    POSIX_FLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE
endif

# cJSON detection — covers Homebrew (Apple Silicon & Intel), system, and local installs
CJSON_INC = -I/usr/include/cjson -I/usr/local/include -I/usr/local/include/cjson \
            -I/opt/homebrew/include -I/opt/homebrew/include/cjson
CJSON_LIB = -L/usr/local/lib -L/opt/homebrew/lib

# Strict compilation flags
CFLAGS = -std=c99 $(POSIX_FLAGS) -pedantic -pedantic-errors \
         -Wall -Wextra -Wformat=2 -Wformat-security -Wnull-dereference \
         -Isrc -Isrc/include $(CJSON_INC) $(DARWIN_FLAGS)

# GCC-specific warnings (MinGW ships GCC, so these will apply on Windows too)
ifeq ($(IS_GCC),1)
    GCC_FLAGS = -Wstack-protector -Wtrampolines -Walloca -Wvla \
                -Warray-bounds=2 -Wimplicit-fallthrough=3 -Wshift-overflow=2 \
                -Wcast-qual -Wcast-align=strict -Wconversion -Wsign-conversion \
                -Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wrestrict \
                -Wnested-externs -Winline -Wundef -Wstrict-prototypes \
                -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls \
                -Wshadow -Wwrite-strings -Wfloat-equal -Wpointer-arith \
                -Wbad-function-cast -Wold-style-definition
    CFLAGS += $(GCC_FLAGS)
endif

# Hardening flags — GNU ld linker options; not available on macOS (Apple ld)
# or MinGW (which uses a PE linker). Applied on Linux only.
ifeq ($(PLATFORM),linux)
    HARDENING_C = -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE \
                  -fstack-clash-protection -fcf-protection
    HARDENING_L = -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack \
                  -Wl,-z,separate-code -pie
else
    HARDENING_C =
    HARDENING_L =
endif

ifeq ($(HARDENED),1)
    SELECTED_HARDENING_C = $(HARDENING_C)
    SELECTED_HARDENING_L = $(HARDENING_L)
else
    SELECTED_HARDENING_C =
    SELECTED_HARDENING_L =
endif

# Optimization and PGO
OPTFLAGS = -O3 -march=native -flto
PGO_FLAGS =
PGO_GEN_FLAGS = -fprofile-generate
PGO_USE_FLAGS = -fprofile-use

ALL_CFLAGS = $(CFLAGS) $(SELECTED_HARDENING_C) $(OPTFLAGS) $(PGO_FLAGS) -pthread
LD_FLAGS = $(SELECTED_HARDENING_L) $(PGO_FLAGS)
LD_LIBS = $(CJSON_LIB) -lcjson

# ---------------------------------------------------------------------------
# Phony Targets
# ---------------------------------------------------------------------------
.PHONY: all clean format format-c format-python format-makefile format-ci lint lint-c lint-makefile check directories install uninstall \
        build-json build-json-force single multi pgo-gen optimized install-multi bench \
        install-single default check-binaries check-tools

default: directories single multi
all: check-tools build-json-force format lint directories single multi
check: check-tools format lint

# ---------------------------------------------------------------------------
# Tool Availability Guards
# These run before format/lint so missing tools produce a clear error instead
# of a cryptic "command not found" mid-build.
# ---------------------------------------------------------------------------
check-tools:
	@$(PYTHON) --version > /dev/null 2>&1 || \
	    { echo "ERROR: Python not found. Install Python 3 and ensure it is on PATH."; exit 1; }
	@command -v clang-format > /dev/null 2>&1 || \
	    { echo "ERROR: clang-format not found. Install LLVM and ensure it is on PATH."; exit 1; }
	@command -v clang-tidy > /dev/null 2>&1 || \
	    { echo "ERROR: clang-tidy not found. Install LLVM and ensure it is on PATH."; exit 1; }
	@command -v mbake > /dev/null 2>&1 || \
	    { echo "ERROR: mbake not found. Install mbake and ensure it is on PATH."; exit 1; }
	@command -v black > /dev/null 2>&1 || \
	    { echo "ERROR: black not found. Run: pip install black"; exit 1; }

# ---------------------------------------------------------------------------
# Directory Creation
# mkdir -p works in MinGW bash on Windows, macOS, and Linux.
# ---------------------------------------------------------------------------
directories:
	mkdir -p $(BIN_DIR) $(BUILD_DIR)

# ---------------------------------------------------------------------------
# Code Generation
# ---------------------------------------------------------------------------
build-json: src/include/languages_data.h src/languages_data.c

build-json-force: assets/languages.json assets/convert_langs.py
	$(PYTHON) assets/convert_langs.py

src/include/languages_data.h: assets/languages.json assets/convert_langs.py
	$(PYTHON) assets/convert_langs.py

src/languages_data.c: assets/languages.json assets/convert_langs.py
	$(PYTHON) assets/convert_langs.py

# ---------------------------------------------------------------------------
# Compilation Rules
# All object files depend on the generated header so that 'make -j8' will
# never start compiling before the Python code-generation step finishes.
# ---------------------------------------------------------------------------
GENERATED_HDR = src/include/languages_data.h

$(BUILD_DIR)/%.o: src/%.c $(GENERATED_HDR)
	@echo "Compiling $< (Hardened: $(HARDENED), Platform: $(PLATFORM))..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/include/%.c $(GENERATED_HDR)
	@echo "Compiling $< (Hardened: $(HARDENED), Platform: $(PLATFORM))..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# Linking Rules
# ---------------------------------------------------------------------------
single: build-json $(OBJ_SINGLE) $(OBJS_COMMON)
	$(CC) $(ALL_CFLAGS) $(LD_FLAGS) -o $(EXECUTABLE_SINGLE) $(OBJ_SINGLE) $(OBJS_COMMON) $(LD_LIBS)

multi: build-json $(OBJ_MULTI) $(OBJS_COMMON)
	$(CC) $(ALL_CFLAGS) $(LD_FLAGS) -o $(EXECUTABLE_MULTI) $(OBJ_MULTI) $(OBJS_COMMON) -pthread $(LD_LIBS)

# ---------------------------------------------------------------------------
# PGO Targets
# GCC profile flags work under MinGW, but .gcda output paths can be tricky
# on Windows — documented with a note below.
# ---------------------------------------------------------------------------
pgo-gen: build-json format clean directories
	@echo "Building instrumented binaries..."
	@mkdir -p $(PROFILE_DIR)
	$(MAKE) PGO_FLAGS="$(PGO_GEN_FLAGS)" single multi
	@echo "Run: ./$(EXECUTABLE_MULTI) -r <codebase>"
	@echo "Then move the generated .gcda files into $(PROFILE_DIR)/ and run 'make optimized'."
	@echo "Note: on Windows/MinGW .gcda files are written relative to the build directory."

optimized: clean directories
	@echo "Building optimized binaries using profile data..."
	@if [ -d "$(PROFILE_DIR)" ] && ls $(PROFILE_DIR)/*.gcda > /dev/null 2>&1; then \
		cp $(PROFILE_DIR)/*.gcda $(BUILD_DIR)/; \
	else \
		echo "ERROR: No .gcda profile data found in $(PROFILE_DIR)/. Run 'make pgo-gen' first."; \
		exit 1; \
	fi
	$(MAKE) PGO_FLAGS="$(PGO_USE_FLAGS)" single multi

# ---------------------------------------------------------------------------
# Clean
# rm -rf works in MinGW bash on all three platforms.
# ---------------------------------------------------------------------------
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# ---------------------------------------------------------------------------
# Benchmark
# Both NAME and COUNT must be supplied: make bench NAME=multi COUNT=5
# ---------------------------------------------------------------------------
bench:
ifndef NAME
	$(error NAME is required. Usage: make bench NAME=multi COUNT=5)
endif
ifndef COUNT
	$(error COUNT is required. Usage: make bench NAME=multi COUNT=5)
endif
	@echo "Running benchmark on ./bin/loc-$(NAME)$(EXE_SUFFIX)..."
	$(PYTHON) assets/bench.py ./bin/loc-$(NAME)$(EXE_SUFFIX) $(COUNT)

# ---------------------------------------------------------------------------
# Formatting and Linting
# ---------------------------------------------------------------------------
format: format-c format-makefile format-python
format-ci: format-c-ci format-makefile-ci format-python-ci

format-c:
	@echo "Formatting C source files"
	clang-format -style=file:./.clang-format -i $(SRCS_ALL) $(HDRS) $(JSON_ASSETS)

format-c-ci:
	@echo "Checking C source file formats"
	clang-format --dry-run -style=file:./.clang-format -Werror $(SRCS_ALL) $(HDRS) $(JSON_ASSETS)

format-makefile:
	@echo "Formatting Makefile"
	mbake format --config ./.bake.toml Makefile

format-makefile-ci:
	@echo "Checking Makefile format"
	mbake format --config ./.bake.toml --check Makefile

format-python:
	@echo "Formatting Python files"
	black -q ./assets/**.py

format-python-ci:
	@echo "Checking Python Format"
	black --check ./assets/**.py

lint: lint-c lint-makefile

lint-c:
	@echo "Running clang-tidy analysis"
	clang-tidy -checks=-*,bugprone-*,clang-analyzer-*,performance-* \
	$(SRCS_ALL) -- $(CFLAGS)

lint-makefile:
	@echo "Running Makefile analysis"
	mbake validate --config ./.bake.toml Makefile

# ---------------------------------------------------------------------------
# Installation
# install(1) is available in MinGW/MSYS2 (via coreutils), so we use it
# uniformly. INSTALL_DIR defaults to ~/.local/bin on all three platforms;
# on Windows that resolves under %USERPROFILE%.
# ---------------------------------------------------------------------------
check-binaries:
	@if [ ! -f "$(EXECUTABLE_SINGLE)" ] || [ ! -f "$(EXECUTABLE_MULTI)" ]; then \
		echo "ERROR: One or both binaries are missing. Run 'make' first."; \
		echo "  Expected: $(EXECUTABLE_SINGLE)"; \
		echo "  Expected: $(EXECUTABLE_MULTI)"; \
		exit 1; \
	fi

# Interactive prompt — printf + read work in MinGW bash on all platforms.
install: check-binaries
	@printf "Install version [1-Multi, 2-Single]: "; \
	read choice; \
	if [ "$$choice" = "1" ]; then \
		$(MAKE) install-multi; \
	elif [ "$$choice" = "2" ]; then \
		$(MAKE) install-single; \
	else \
		echo "Invalid choice '$$choice'. Please enter 1 or 2."; exit 1; \
	fi

install-multi: check-binaries
	mkdir -p $(INSTALL_DIR)
	install -m 755 $(EXECUTABLE_MULTI) $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)
	@echo "Installed $(TARGET_NAME) (multi) to $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)"

install-single: check-binaries
	mkdir -p $(INSTALL_DIR)
	install -m 755 $(EXECUTABLE_SINGLE) $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)
	@echo "Installed $(TARGET_NAME) (single) to $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)"

uninstall:
	rm -f $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)
	@echo "Uninstalled $(TARGET_NAME) from $(INSTALL_DIR)"
