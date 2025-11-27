# Makefile for RDK Firmware Updater Test Client
# Simple, standalone build for quick testing and development

# Detect compilation environment
ifdef CROSS_COMPILE
    CC ?= arm-rdk-linux-gnueabi-gcc
    PKG_CONFIG ?= arm-rdk-linux-gnueabi-pkg-config
else
    CC ?= gcc
    PKG_CONFIG ?= pkg-config
endif

# Source and target
SOURCE_FILE = src/test/testClient.c
TARGET_BIN = testClient

# Get GLib flags from pkg-config
GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 gio-2.0 2>/dev/null)
GLIB_LIBS := $(shell $(PKG_CONFIG) --libs glib-2.0 gio-2.0 2>/dev/null)

# Check if GLib flags were found
ifeq ($(GLIB_CFLAGS),)
    $(error GLib development packages not found. Install glib-2.0-dev)
endif

# Compilation flags
CFLAGS = -Wall -Wextra -g -O2 $(GLIB_CFLAGS)
LDFLAGS = $(GLIB_LIBS)
INCLUDES = -Isrc/include -Isrc/dbus

# Default target
all: $(TARGET_BIN)

# Build testClient
$(TARGET_BIN): $(SOURCE_FILE)
	@echo "🔨 Building testClient..."
	@echo "   Compiler: $(CC)"
	@echo "   Source: $(SOURCE_FILE)"
	@echo "   Target: $(TARGET_BIN)"
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET_BIN) $(SOURCE_FILE) $(LDFLAGS)
	@echo "✅ testClient built successfully!"
	@echo ""
	@echo "📖 Usage Examples:"
	@echo "   ./$(TARGET_BIN) MyApp 1.0.0 basic"
	@echo "   ./$(TARGET_BIN) MyApp 1.0.0 check"
	@echo "   ./$(TARGET_BIN) MyApp 1.0.0 signals"
	@echo "   ./$(TARGET_BIN) MyApp 1.0.0 full"
	@echo ""
	@echo "🧪 Signal Testing Tips:"
	@echo "   # Clear cache to test cache-miss scenario:"
	@echo "   sudo rm -f /tmp/xconf_response_thunder.txt"
	@echo ""
	@echo "   # Monitor D-Bus signals (separate terminal):"
	@echo "   dbus-monitor --system \"interface='org.rdkfwupdater.Interface'\""

# Clean target
clean:
	@echo "🧹 Cleaning testClient..."
	rm -f $(TARGET_BIN)
	@echo "✅ Clean completed"

# Install target (optional)
install: $(TARGET_BIN)
	@echo "📦 Installing testClient to /usr/local/bin..."
	sudo cp $(TARGET_BIN) /usr/local/bin/
	@echo "✅ testClient installed to /usr/local/bin/$(TARGET_BIN)"

# Uninstall target (optional)
uninstall:
	@echo "🗑️  Uninstalling testClient from /usr/local/bin..."
	sudo rm -f /usr/local/bin/$(TARGET_BIN)
	@echo "✅ testClient uninstalled"

# Test targets for quick validation
test-basic: $(TARGET_BIN)
	@echo "🧪 Running basic registration test..."
	./$(TARGET_BIN) TestApp 1.0.0 basic

test-signals: $(TARGET_BIN)
	@echo "🧪 Running signals test..."
	@echo "⚠️  Make sure rdkFwupdateMgr daemon is running first!"
	./$(TARGET_BIN) SignalTestApp 1.0.0 signals

test-cache-miss: $(TARGET_BIN)
	@echo "🧪 Testing cache miss scenario..."
	@echo "🗑️  Clearing XConf cache first..."
	sudo rm -f /tmp/xconf_response_thunder.txt
	@echo "⚠️  Make sure rdkFwupdateMgr daemon is running first!"
	./$(TARGET_BIN) CacheMissApp 1.0.0 signals

test-cache-hit: $(TARGET_BIN)
	@echo "🧪 Testing cache hit scenario..."
	@echo "ℹ️   Cache should exist from previous runs"
	@echo "⚠️  Make sure rdkFwupdateMgr daemon is running first!"
	./$(TARGET_BIN) CacheHitApp 1.0.0 signals

# Show build information
info:
	@echo "🔍 Build Information:"
	@echo "   Compiler: $(CC)"
	@echo "   PKG_CONFIG: $(PKG_CONFIG)" 
	@echo "   GLib CFLAGS: $(GLIB_CFLAGS)"
	@echo "   GLib LIBS: $(GLIB_LIBS)"
	@echo "   Source: $(SOURCE_FILE)"
	@echo "   Target: $(TARGET_BIN)"
	@echo "   Cross-compile: $(if $(CROSS_COMPILE),Yes ($(CROSS_COMPILE)),No)"

# Help target
help:
	@echo "🛠️  RDK Firmware Updater Test Client Makefile"
	@echo "=============================================="
	@echo ""
	@echo "Available targets:"
	@echo "  all           - Build testClient (default)"
	@echo "  clean         - Remove built binary"
	@echo "  install       - Install to /usr/local/bin"
	@echo "  uninstall     - Remove from /usr/local/bin"
	@echo "  info          - Show build configuration"
	@echo "  help          - Show this help"
	@echo ""
	@echo "Test targets:"
	@echo "  test-basic      - Run basic registration test"
	@echo "  test-signals    - Run D-Bus signal test"
	@echo "  test-cache-miss - Test cache miss scenario"
	@echo "  test-cache-hit  - Test cache hit scenario"
	@echo ""
	@echo "Quick Usage:"
	@echo "  make                    # Build testClient"
	@echo "  make test-signals       # Test complete signal flow"
	@echo "  make test-cache-miss    # Test cache miss + signals"

# Phony targets
.PHONY: all clean install uninstall test-basic test-signals test-cache-miss test-cache-hit info help

