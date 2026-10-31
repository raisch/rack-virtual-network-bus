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
X86_64_RACK_DIR := ../LIBS/Rack-SDK-X64
X86_64_PLUGIN_DIR := $(HOME)/Library/Application Support/Rack2/plugins-mac-x64

SOURCES += $(wildcard src/*.cpp src/*/*.cpp)
SOURCES += $(wildcard src/*.cc src/*/*.cc)
FLAGS += -Isrc
DISTRIBUTABLES += res

.PHONY: install-arm64 install-x86_64

install-arm64:
	$(MAKE) RACK_DIR=$(ARM64_RACK_DIR) clean
	$(MAKE) RACK_DIR=$(ARM64_RACK_DIR) dist
	mkdir -p "$(ARM64_PLUGIN_DIR)"
	cp dist/*.vcvplugin "$(ARM64_PLUGIN_DIR)/"

install-x86_64:
	$(MAKE) RACK_DIR=$(X86_64_RACK_DIR) \
		CROSS_COMPILE=x86_64-apple-darwin clean
	$(MAKE) RACK_DIR=$(X86_64_RACK_DIR) \
		CROSS_COMPILE=x86_64-apple-darwin dist
	mkdir -p "$(X86_64_PLUGIN_DIR)"
	cp dist/*.vcvplugin "$(X86_64_PLUGIN_DIR)/"

include $(RACK_DIR)/plugin.mk
