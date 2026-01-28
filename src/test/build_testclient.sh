
# * Copyright 2025 Comcast Cable Communications Management, LLC
#*
# * Licensed under the Apache License, Version 2.0 (the "License");
#* you may not use this file except in compliance with the License.
#* You may obtain a copy of the License at
#*
#* http://www.apache.org/licenses/LICENSE-2.0
#*
#* Unless required by applicable law or agreed to in writing, software
#* distributed under the License is distributed on an "AS IS" BASIS,
#* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#* See the License for the specific language governing permissions and
#* limitations under the License.
#*
#* SPDX-License-Identifier: Apache-2.0


#!/bin/bash

# Build Script for RDK Firmware Updater Test Client
# This script builds only the testClient for quick testing and development

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_FILE="${SCRIPT_DIR}/src/test/testClient.c"
OUTPUT_DIR="${SCRIPT_DIR}"
OUTPUT_BIN="${OUTPUT_DIR}/testClient"

print_info "RDK Firmware Updater Test Client Builder"
print_info "========================================"

# Check if source file exists
if [ ! -f "$SOURCE_FILE" ]; then
    print_error "Source file not found: $SOURCE_FILE"
    exit 1
fi

print_info "Source file: $SOURCE_FILE"
print_info "Output binary: $OUTPUT_BIN"

# Detect build environment
if [ -n "$CROSS_COMPILE" ] || [ -n "$CC" ]; then
    print_info "Cross-compilation environment detected"
    COMPILER="${CC:-arm-rdk-linux-gnueabi-gcc}"
    PKG_CONFIG="${PKG_CONFIG:-arm-rdk-linux-gnueabi-pkg-config}"
else
    print_info "Native compilation environment"
    COMPILER="${CC:-gcc}"
    PKG_CONFIG="${PKG_CONFIG:-pkg-config}"
fi

print_info "Using compiler: $COMPILER"
print_info "Using pkg-config: $PKG_CONFIG"

# Get GLib flags
print_info "Getting GLib compilation flags..."
if ! GLIB_CFLAGS=$($PKG_CONFIG --cflags glib-2.0 gio-2.0 2>/dev/null); then
    print_error "Failed to get GLib CFLAGS. Is glib-2.0-dev installed?"
    exit 1
fi

if ! GLIB_LIBS=$($PKG_CONFIG --libs glib-2.0 gio-2.0 2>/dev/null); then
    print_error "Failed to get GLib LIBS. Is glib-2.0-dev installed?"
    exit 1
fi

print_info "GLib CFLAGS: $GLIB_CFLAGS"
print_info "GLib LIBS: $GLIB_LIBS"

# Compilation flags
CFLAGS="-Wall -Wextra -g -O2"
INCLUDES="-I${SCRIPT_DIR}/src/include -I${SCRIPT_DIR}/src/dbus"

# Clean previous build
if [ -f "$OUTPUT_BIN" ]; then
    print_info "Cleaning previous build..."
    rm -f "$OUTPUT_BIN"
fi

# Compile testClient
print_info "Compiling testClient..."
COMPILE_CMD="$COMPILER $CFLAGS $INCLUDES $GLIB_CFLAGS -o $OUTPUT_BIN $SOURCE_FILE $GLIB_LIBS"

print_info "Compilation command:"
echo "  $COMPILE_CMD"

if $COMPILE_CMD; then
    print_success "testClient compiled successfully!"
    
    # Check if binary was created and is executable
    if [ -f "$OUTPUT_BIN" ] && [ -x "$OUTPUT_BIN" ]; then
        print_success "Binary is ready: $OUTPUT_BIN"
        
        # Show binary info
        print_info "Binary information:"
        ls -la "$OUTPUT_BIN"
        
        if command -v file >/dev/null 2>&1; then
            file "$OUTPUT_BIN"
        fi
        
        # Usage information
        echo ""
        print_info "Usage Examples:"
        echo "  $OUTPUT_BIN MyApp 1.0.0 basic"
        echo "  $OUTPUT_BIN MyApp 1.0.0 check" 
        echo "  $OUTPUT_BIN MyApp 1.0.0 signals"
        echo "  $OUTPUT_BIN MyApp 1.0.0 full"
        echo ""
        print_info "Signal Testing:"
        echo "  # Clear cache for cache-miss test:"
        echo "  sudo rm -f /tmp/xconf_response_thunder.txt"
        echo ""
        echo "  # Monitor D-Bus signals (in separate terminal):"
        echo "  dbus-monitor --system \"interface='org.rdkfwupdater.Interface'\""
        echo ""
        print_success "Build completed successfully!"
        
    else
        print_error "Binary was not created or is not executable"
        exit 1
    fi
else
    print_error "Compilation failed!"
    exit 1
fi

