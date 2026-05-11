# Project Name
TARGET_NAME = loc
BUILD_DIR = build
BIN_DIR = bin
EXECUTABLE = $(BIN_DIR)/$(TARGET_NAME)

# Source files
SRCS = src/mini-loc.c src/include/set.c
# Header files
HDRS = src/include/minicli.h src/include/set.h

# Object files (placed in build/ directory)
OBJS = $(BUILD_DIR)/mini-loc.o $(BUILD_DIR)/set.o

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

# Combine all flags
ALL_CFLAGS = $(CFLAGS) $(HARDENING) $(OPTFLAGS)

# Targets
.PHONY: all clean run format lint directories install uninstall build-json

all: format lint directories build-json $(EXECUTABLE)

# Create output directories if they don't exist
directories:
	@mkdir -p $(BIN_DIR) $(BUILD_DIR)

# Rule to compile .c files into .o files in the build/ directory
$(BUILD_DIR)/mini-loc.o: src/mini-loc.c
	@echo "Compiling $< ..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/set.o: src/include/set.c
	@echo "Compiling $< ..."
	$(CC) $(ALL_CFLAGS) -c $< -o $@

# Rule to link the executable in the bin/ directory
$(EXECUTABLE): $(OBJS)
	@echo "Linking $@ ..."
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $(EXECUTABLE) $(OBJS) -lm

build-json:
	@echo "Creating language header"
	@python ./assets/convert_langs.py
	@echo "Created new language header"

# Rule to clean up build artifacts
clean:
	@echo "Cleaning up build artifacts..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)

# Rule to run the executable
run: $(EXECUTABLE)
	@echo "Running $(EXECUTABLE) ..."
	./$(EXECUTABLE)

# Format code using clang-format
format:
	@echo "Formatting code..."
	@clang-format -style=file:./.clang-format -i $(SRCS) $(HDRS)
	mbake format --config ./.bake.toml Makefile

# Run static analysis with clang-tidy
CLANG_TIDY_CHECKS = -checks=-*,bugprone-*,clang-analyzer-*
CLANG_TIDY_FLAGS = -std=c99 -pedantic -Wall -Wextra -Isrc -Isrc/include -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE

lint:
	@echo "Running static analysis..."
	@clang-tidy $(CLANG_TIDY_CHECKS) $(SRCS) -- $(CLANG_TIDY_FLAGS)
	mbake validate --config ./.bake.toml Makefile

# Installation directories
INSTALL_DIR = $(HOME)/.local/bin

# Install and uninstall targets
install: $(EXECUTABLE)
	@echo "Installing $(TARGET_NAME) to $(INSTALL_DIR)..."
	@install -d $(INSTALL_DIR)
	@install -m 755 $(EXECUTABLE) $(INSTALL_DIR)/$(TARGET_NAME)
	@echo "$(TARGET_NAME) installed successfully."

uninstall:
	@echo "Uninstalling $(TARGET_NAME) from $(INSTALL_DIR)..."
	@rm -f $(INSTALL_DIR)/$(TARGET_NAME)
	@echo "$(TARGET_NAME) uninstalled successfully."
