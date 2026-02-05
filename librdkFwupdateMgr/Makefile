# Makefile for librdkFwupdateMgr.so
# Production-grade C client library for RDK Firmware Update Manager

# Compiler and flags
CC := gcc
CFLAGS := -Wall -Wextra -Werror -fPIC -O2 -g -D_GNU_SOURCE
LDFLAGS := -shared
LIBS := -lpthread

# Directories
SRC_DIR := src
INC_DIR := include
TEST_DIR := test
BUILD_DIR := build
LIB_DIR := lib

# Target library
LIB_NAME := librdkFwupdateMgr.so
LIB_VERSION := 1.0.0
LIB_SONAME := $(LIB_NAME).1
LIB_FULL := $(LIB_NAME).$(LIB_VERSION)

# Source files
SRCS := $(SRC_DIR)/handle_mgr.c
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Test executable
TEST_SRCS := $(TEST_DIR)/test_handle.c
TEST_EXEC := $(BUILD_DIR)/test_handle

# Include paths
INCLUDES := -I$(INC_DIR) -I$(SRC_DIR)

# Phony targets
.PHONY: all clean test install dirs

# Default target
all: dirs $(LIB_DIR)/$(LIB_FULL) $(LIB_DIR)/$(LIB_SONAME) $(LIB_DIR)/$(LIB_NAME)

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(LIB_DIR)

# Build shared library with versioned name
$(LIB_DIR)/$(LIB_FULL): $(OBJS)
	@echo "Linking shared library: $@"
	$(CC) $(LDFLAGS) -Wl,-soname,$(LIB_SONAME) -o $@ $^ $(LIBS)
	@echo "Built: $@"

# Create soname symlink
$(LIB_DIR)/$(LIB_SONAME): $(LIB_DIR)/$(LIB_FULL)
	@echo "Creating symlink: $@ -> $(LIB_FULL)"
	@cd $(LIB_DIR) && ln -sf $(LIB_FULL) $(LIB_SONAME)

# Create development symlink
$(LIB_DIR)/$(LIB_NAME): $(LIB_DIR)/$(LIB_SONAME)
	@echo "Creating symlink: $@ -> $(LIB_SONAME)"
	@cd $(LIB_DIR) && ln -sf $(LIB_SONAME) $(LIB_NAME)

# Compile object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build test program
$(TEST_EXEC): $(TEST_SRCS) $(LIB_DIR)/$(LIB_NAME)
	@echo "Building test program: $@"
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(TEST_SRCS) -L$(LIB_DIR) -lrdkFwupdateMgr -lpthread -Wl,-rpath,'$$ORIGIN/../$(LIB_DIR)'
	@echo "Built test: $@"

# Build and run tests
test: $(TEST_EXEC)
	@echo "Running tests..."
	@LD_LIBRARY_PATH=$(LIB_DIR):$$LD_LIBRARY_PATH $(TEST_EXEC)

# Install library (optional)
install: all
	@echo "Installing library to /usr/local/lib..."
	@install -D $(LIB_DIR)/$(LIB_FULL) /usr/local/lib/$(LIB_FULL)
	@cd /usr/local/lib && ln -sf $(LIB_FULL) $(LIB_SONAME)
	@cd /usr/local/lib && ln -sf $(LIB_SONAME) $(LIB_NAME)
	@echo "Installing header to /usr/local/include..."
	@install -D $(INC_DIR)/rdkFwupdateMgr_client.h /usr/local/include/rdkFwupdateMgr_client.h
	@echo "Running ldconfig..."
	@ldconfig

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR) $(LIB_DIR)
	@echo "Clean complete."

# Display help
help:
	@echo "RDK Firmware Update Manager Client Library - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build shared library (default)"
	@echo "  test     - Build and run tests"
	@echo "  install  - Install library to system (requires sudo)"
	@echo "  clean    - Remove build artifacts"
	@echo "  help     - Display this help message"
	@echo ""
	@echo "Usage:"
	@echo "  make              # Build library"
	@echo "  make test         # Build and run tests"
	@echo "  sudo make install # Install to system"
	@echo "  make clean        # Clean build"
