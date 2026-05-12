# Project Name
TARGET_NAME = loc
BUILD_DIR = build
BIN_DIR = bin
PROFILE_DIR = profiles
EXECUTABLE_SINGLE = $(BIN_DIR)/$(TARGET_NAME)-single
EXECUTABLE_MULTI = $(BIN_DIR)/$(TARGET_NAME)-multi

# Source files
SRCS_SINGLE = src/mini-loc-single.c src/include/set.c
SRCS_MULTI = src/mini-loc-multi.c src/include/set.c
SRCS_ALL = $(SRCS_SINGLE) src/mini-loc-multi.c

# Header files
HDRS = src/include/minicli.h src/include/set.h src/include/languages_data.h

# Object files (placed in build/ directory)
OBJS_COMMON = $(BUILD_DIR)/set.o
OBJ_SINGLE = $(BUILD_DIR)/mini-loc-single.o
OBJ_MULTI = $(BUILD_DIR)/mini-loc-multi.o

# Compiler
CC = gcc

# Strict compilation flags
CFLAGS = -std=c99 \
         -D_POSIX_C_SOURCE=200809L \
         -D_XOPEN_SOURCE=700 \
         -D_DEFAULT_SOURCE \
         -pedantic \
         -pedantic-errors \
         -Wall \
         -Wextra \
         -Wformat=2 \
         -Wformat-security \
         -Wnull-dereference \
         -Wstack-protector \
         -Wtrampolines \
         -Walloca \
         -Wvla \
         -Warray-bounds=2 \
         -Wimplicit-fallthrough=3 \
         -Wshift-overflow=2 \
         -Wcast-qual \
         -Wcast-align=strict \
         -Wconversion \
         -Wsign-conversion \
         -Wlogical-op \
         -Wduplicated-cond \
         -Wduplicated-branches \
         -Wrestrict \
         -Wnested-externs \
         -Winline \
         -Wundef \
         -Wstrict-prototypes \
         -Wmissing-prototypes \
         -Wmissing-declarations \
         -Wredundant-decls \
         -Wshadow \
         -Wwrite-strings \
         -Wfloat-equal \
         -Wpointer-arith \
         -Wbad-function-cast \
         -Wold-style-definition \
         -Isrc -Isrc/include

# Security hardening flags
HARDENING = -D_FORTIFY_SOURCE=2 \
            -fstack-protector-strong \
            -fPIE \
            -fstack-clash-protection \
            -fcf-protection

# Linker hardening flags
LDFLAGS = -Wl,-z,relro \
          -Wl,-z,now \
          -Wl,-z,noexecstack \
          -Wl,-z,separate-code \
          -pie \
          -flto

# Optimization
OPTFLAGS = -O3 -march=native -flto

# PGO Support
PGO_FLAGS =
PGO_GEN_FLAGS = -fprofile-generate
PGO_USE_FLAGS = -fprofile-use

# Combine all flags
ALL_CFLAGS = $(CFLAGS) $(HARDENING) $(OPTFLAGS) $(PGO_FLAGS) -pthread
ALL_LDFLAGS = $(LDFLAGS) $(PGO_FLAGS)

# Targets
.PHONY: all clean run format lint check directories install uninstall build-json single multi pgo-gen optimized

default: directories single multi

all: check build-json directories single multi

# Analysis and Formatting
check: format lint

# PGO Targets
pgo-gen: build-json format clean directories
	@echo "Building instrumented binaries for profile generation..."
	@mkdir -p $(PROFILE_DIR)
	$(MAKE) PGO_FLAGS="$(PGO_GEN_FLAGS)" single multi
	@echo "Instrumented binaries built in $(BIN_DIR)/"
	@echo "Run: ./bin/loc-multi -r <codebase>"
	@echo "Then: cp $(BUILD_DIR)/*.gcda $(PROFILE_DIR)/"

optimized: clean directories
	@echo "Building optimized binaries using existing profiles..."
	@if [ -d $(PROFILE_DIR) ] && [ "$$(ls -A $(PROFILE_DIR)/*.gcda 2>/dev/null)" ]; then \
		cp $(PROFILE_DIR)/*.gcda $(BUILD_DIR)/; \
	else \
		echo "Error: No profile data in $(PROFILE_DIR)/. Run 'make pgo-gen' first."; \
		exit 1; \
	fi
	$(MAKE) PGO_FLAGS="$(PGO_USE_FLAGS)" single multi
	@echo "Optimized binaries built in $(BIN_DIR)/. Ready for 'make install'."

copy-optimized:
	@mkdir -p $(PROFILE_DIR)
	@cp $(BUILD_DIR)/*.gcda $(PROFILE_DIR)/;

# Create output directories if they don't exist
directories:
	@mkdir -p $(BIN_DIR) $(BUILD_DIR)

# Rules to compile .c files into .o files in the build/ directory
$(BUILD_DIR)/mini-loc-single.o: src/mini-loc-single.c src/include/languages_data.h
	@echo "Compiling $< ..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/mini-loc-multi.o: src/mini-loc-multi.c src/include/languages_data.h
	@echo "Compiling $< ..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/set.o: src/include/set.c
	@echo "Compiling $< ..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Rules to link the executables in the bin/ directory
single: $(OBJ_SINGLE) $(OBJS_COMMON)
	@echo "Linking $(EXECUTABLE_SINGLE) ..."
	$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $(EXECUTABLE_SINGLE) $^

multi: $(OBJ_MULTI) $(OBJS_COMMON)
	@echo "Linking $(EXECUTABLE_MULTI) ..."
	$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $(EXECUTABLE_MULTI) $^ -pthread

build-json: src/include/languages_data.h

src/include/languages_data.h: assets/languages.json assets/convert_langs.py
	@echo "Updating language header..."
	@python ./assets/convert_langs.py

# Rule to clean up build artifacts
clean:
	@echo "Cleaning up build artifacts..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

# Rule to run the executable (defaults to multi)
run: multi
	@echo "Running $(EXECUTABLE_MULTI) ..."
	./$(EXECUTABLE_MULTI)

# Format code using clang-format
format:
	@echo "Formatting code..."
	@echo "Clang-format"
	@clang-format -style=file:./.clang-format -i $(SRCS_ALL) $(HDRS)
	@echo "mbake"
	@mbake format --config ./.bake.toml Makefile
	@echo "black"
	@black ./assets/convert_langs.py
	@echo "Formatting done"

# Run static analysis with clang-tidy
CLANG_TIDY_CHECKS = -checks=-*,bugprone-*,clang-analyzer-*,performance-*
CLANG_TIDY_FLAGS = -std=c99 -pedantic -Wall -Wextra -Isrc -Isrc/include -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE

lint:
	@echo "Running static analysis..."
	@clang-tidy $(CLANG_TIDY_CHECKS) $(SRCS_ALL) -- $(CLANG_TIDY_FLAGS)
	mbake validate --config ./.bake.toml Makefile
	@echo "Analysis done"

# Installation directories
INSTALL_DIR = $(HOME)/.local/bin

# Install and uninstall targets
install: check-binaries
	@echo "Select version to install as '$(TARGET_NAME)':"
	@echo "1) Multi-threaded"
	@echo "2) Single-threaded"
	@printf "Choice [1-2]: "; \
	read choice; \
	if [ "$$choice" = "1" ]; then \
		$(MAKE) install-multi; \
	elif [ "$$choice" = "2" ]; then \
		$(MAKE) install-single; \
	else \
		echo "Invalid choice. Aborting."; \
		exit 1; \
	fi

# Verify binaries exist before installing
check-binaries:
	@if [ ! -f $(EXECUTABLE_SINGLE) ] || [ ! -f $(EXECUTABLE_MULTI) ]; then \
		echo "Error: Binaries not found in $(BIN_DIR)/. Please run 'make' or 'make optimized' first."; \
		exit 1; \
	fi

install-multi:
	@echo "Installing multi-threaded version to $(INSTALL_DIR)..."
	@install -d $(INSTALL_DIR)
	@install -m 755 $(EXECUTABLE_MULTI) $(INSTALL_DIR)/$(TARGET_NAME)
	@echo "$(TARGET_NAME) installed successfully (multi-threaded)."

install-single:
	@echo "Installing single-threaded version to $(INSTALL_DIR)..."
	@install -d $(INSTALL_DIR)
	@install -m 755 $(EXECUTABLE_SINGLE) $(INSTALL_DIR)/$(TARGET_NAME)
	@echo "$(TARGET_NAME) installed successfully (single-threaded)."

uninstall:
	@echo "Uninstalling $(TARGET_NAME) from $(INSTALL_DIR)..."
	@rm -f $(INSTALL_DIR)/$(TARGET_NAME)
	@echo "$(TARGET_NAME) uninstalled successfully."
