# Project Name and Directories
TARGET_NAME = loc
BUILD_DIR = build
BIN_DIR = bin
PROFILE_DIR = profiles
EXECUTABLE_SINGLE = $(BIN_DIR)/$(TARGET_NAME)-single
EXECUTABLE_MULTI = $(BIN_DIR)/$(TARGET_NAME)-multi

# --- OS Detection ---
# Detect if running on macOS to add specific headers
UNAME_S := $(shell uname -s)
DARWIN_FLAGS =
ifeq ($(UNAME_S),Darwin)
    DARWIN_FLAGS = -DDARWIN
endif

# --- Hardening Toggle ---
# Allows 'make HARDENED=1' for a hardened build. Defaults to 0 (Disabled).
HARDENED ?= 0

# Source and Header files
SRCS_SINGLE = src/mini-loc-single.c src/include/set.c
SRCS_MULTI = src/mini-loc-multi.c src/include/set.c
SRCS_ALL = $(SRCS_SINGLE) src/mini-loc-multi.c
HDRS = src/include/minicli.h src/include/set.h src/include/languages_data.h

# Object files
OBJS_COMMON = $(BUILD_DIR)/set.o
OBJ_SINGLE = $(BUILD_DIR)/mini-loc-single.o
OBJ_MULTI = $(BUILD_DIR)/mini-loc-multi.o

# Compiler
CC = gcc

# Strict compilation flags
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -pedantic  \
         -pedantic-errors -Wall -Wextra -Wformat=2 -Wformat-security -Wnull-dereference \
         -Wstack-protector -Wtrampolines -Walloca -Wvla \
         -Warray-bounds=2 -Wimplicit-fallthrough=3 -Wshift-overflow=2 -Wcast-qual \
         -Wcast-align=strict -Wconversion -Wsign-conversion -Wlogical-op -Wduplicated-cond -Wduplicated-branches \
         -Wrestrict -Wnested-externs -Winline -Wundef -Wstrict-prototypes -Wmissing-prototypes \
         -Wmissing-declarations -Wredundant-decls -Wshadow -Wwrite-strings \
         -Wfloat-equal -Wpointer-arith -Wbad-function-cast -Wold-style-definition -Isrc -Isrc/include \
         $(DARWIN_FLAGS)

# Security hardening flags
HARDENING_C = -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE -fstack-clash-protection -fcf-protection
HARDENING_L = -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,-z,separate-code -pie

# Optimization and PGO
OPTFLAGS = -O3 -march=native -flto
PGO_FLAGS =
PGO_GEN_FLAGS = -fprofile-generate
PGO_USE_FLAGS = -fprofile-use

# Conditional Flag Logic
ifeq ($(HARDENED),1)
    SELECTED_HARDENING_C = $(HARDENING_C)
    SELECTED_HARDENING_L = $(HARDENING_L)
else
    SELECTED_HARDENING_C =
    SELECTED_HARDENING_L =
endif

ALL_CFLAGS = $(CFLAGS) $(SELECTED_HARDENING_C) $(OPTFLAGS) $(PGO_FLAGS) -pthread
ALL_LDFLAGS = $(SELECTED_HARDENING_L) $(PGO_FLAGS) -flto

# Targets
.PHONY: all clean run format lint check directories install uninstall build-json single multi pgo-gen optimized

default: directories single multi
all: check build-json directories single multi
check: format lint

# PGO Targets
pgo-gen: build-json format clean directories
	@echo "Building instrumented binaries..."
	@mkdir -p $(PROFILE_DIR)
	$(MAKE) PGO_FLAGS="$(PGO_GEN_FLAGS)" single multi
	@echo "Run: ./bin/loc-multi -r <codebase> then move .gcda files to $(PROFILE_DIR)"

optimized: clean directories
	@echo "Building optimized binaries..."
	@if [ -d $(PROFILE_DIR) ] && [ "$$(ls -A $(PROFILE_DIR)/*.gcda 2>/dev/null)" ]; then \
		cp $(PROFILE_DIR)/*.gcda $(BUILD_DIR)/; \
	else \
		echo "Error: No profile data found. Run 'make pgo-gen' first."; exit 1; \
	fi
	$(MAKE) PGO_FLAGS="$(PGO_USE_FLAGS)" single multi

directories:
	@mkdir -p $(BIN_DIR) $(BUILD_DIR)

# Compilation Rules
$(BUILD_DIR)/%.o: src/%.c
	@echo "Compiling $< (Hardened: $(HARDENED))..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/set.o: src/include/set.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Linking Rules
single: $(OBJ_SINGLE) $(OBJS_COMMON)
	$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $(EXECUTABLE_SINGLE) $^

multi: $(OBJ_MULTI) $(OBJS_COMMON)
	$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $(EXECUTABLE_MULTI) $^ -pthread

build-json: src/include/languages_data.h

src/include/languages_data.h: assets/languages.json assets/convert_langs.py
	@python ./assets/convert_langs.py

clean:
	@echo "Cleaning artifacts..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

# Formatting and Linting (Restored mbake)
format:
	@echo "Formatting code and Makefile..."
	@clang-format -style=file:./.clang-format -i $(SRCS_ALL) $(HDRS)
	@mbake format --config ./.bake.toml Makefile
	@black -q ./assets/convert_langs.py

lint:
	@echo "Running analysis..."
	@clang-tidy -checks=-*,bugprone-*,clang-analyzer-*,performance-* $(SRCS_ALL) -- $(CFLAGS)
	mbake validate --config ./.bake.toml Makefile

# Installation
INSTALL_DIR = $(HOME)/.local/bin

install: check-binaries
	@printf "Install version [1-Multi, 2-Single]: "; \
	read choice; \
	if [ "$$choice" = "1" ]; then $(MAKE) install-multi; \
	elif [ "$$choice" = "2" ]; then $(MAKE) install-single; \
	else echo "Invalid choice."; exit 1; fi

check-binaries:
	@if [ ! -f $(EXECUTABLE_SINGLE) ] || [ ! -f $(EXECUTABLE_MULTI) ]; then \
		echo "Error: Binaries missing."; exit 1; fi

install-multi:
	@install -d $(INSTALL_DIR) && install -m 755 $(EXECUTABLE_MULTI) $(INSTALL_DIR)/$(TARGET_NAME)

install-single:
	@install -d $(INSTALL_DIR) && install -m 755 $(EXECUTABLE_SINGLE) $(INSTALL_DIR)/$(TARGET_NAME)

uninstall:
	@rm -f $(INSTALL_DIR)/$(TARGET_NAME)
