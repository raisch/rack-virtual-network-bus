RACK_DIR ?= ../LIBS/Rack-SDK-ARM64

SOURCES += $(wildcard src/*.cpp src/*/*.cpp)
SOURCES += $(wildcard src/*.cc src/*/*.cc)
FLAGS += -Isrc
DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk
