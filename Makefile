HOST_ARCH := $(shell uname -m)
ifeq ($(HOST_ARCH),arm64)
DEFAULT_RACK_DIR := ../LIBS/Rack-SDK-ARM64
else ifeq ($(HOST_ARCH),x86_64)
DEFAULT_RACK_DIR := ../LIBS/Rack-SDK-X64
else
$(error Unsupported host architecture: $(HOST_ARCH))
endif

RACK_DIR ?= $(DEFAULT_RACK_DIR)
ifeq ($(HOST_ARCH),x86_64)
CROSS_COMPILE ?= x86_64-apple-darwin
endif
ARM64_RACK_DIR := ../LIBS/Rack-SDK-ARM64
ARM64_PLUGIN_DIR := $(HOME)/Library/Application Support/Rack2/plugins-mac-arm64
ARM64_PACKAGE := target/arm64/virtual-network-bus-2.0.0-mac-arm64.vcvplugin
X86_64_RACK_DIR := ../LIBS/Rack-SDK-X64
X86_64_PLUGIN_DIR := $(HOME)/Library/Application Support/Rack2/plugins-mac-x64
X86_64_PACKAGE := target/x86_64/virtual-network-bus-2.0.0-mac-x64.vcvplugin

SOURCES += $(wildcard src/*.cpp src/*/*.cpp)
SOURCES += $(wildcard src/*.cc src/*/*.cc)
FLAGS += -Isrc
DISTRIBUTABLES += res

.PHONY: install-arm64 install-x86_64

install-arm64:
	@test -f "$(ARM64_PACKAGE)" || \
		(echo "Missing $(ARM64_PACKAGE); build the ARM64 package first." >&2; exit 1)
	mkdir -p "$(ARM64_PLUGIN_DIR)"
	cp "$(ARM64_PACKAGE)" "$(ARM64_PLUGIN_DIR)/"

install-x86_64:
	@test -f "$(X86_64_PACKAGE)" || \
		(echo "Missing $(X86_64_PACKAGE); build the x86_64 package first." >&2; exit 1)
	mkdir -p "$(X86_64_PLUGIN_DIR)"
	cp "$(X86_64_PACKAGE)" "$(X86_64_PLUGIN_DIR)/"

include $(RACK_DIR)/plugin.mk

# Override Rack SDK's build-and-install target with package-only installation.
install:
	@case "$$(uname -m)" in \
		arm64) $(MAKE) install-arm64 ;; \
		x86_64) $(MAKE) install-x86_64 ;; \
		*) echo "Unsupported host architecture: $$(uname -m)" >&2; exit 1 ;; \
	esac
