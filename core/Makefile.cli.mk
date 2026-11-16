MAKEFILE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CC = gcc
CXX = g++
CFLAGS += -Wall -Wextra -Wpedantic
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic

ifeq ($(DEBUG),1)
BUILD_MODE := debug
CFLAGS += -O0 -g -DDEBUG -D_DEBUG -DNRELEASE -D_NRELEASE
CXXFLAGS += -O0 -g -DDEBUG -D_DEBUG -DNRELEASE -D_NRELEASE
else
BUILD_MODE := release
CFLAGS += -O3 -DNDEBUG -D_NDEBUG -DRELEASE -D_RELEASE
CXXFLAGS += -O3 -DNDEBUG -D_NDEBUG -DRELEASE -D_RELEASE
endif

BUILD_DIR := $(MAKEFILE_DIR)build
OBJ_DIR := $(BUILD_DIR)/obj
CLI_BINARY := $(BUILD_DIR)/cucut-cli
BUILD_CONFIG := $(OBJ_DIR)/.build-config

C_SOURCES := $(wildcard $(MAKEFILE_DIR)*.c)
CPP_SOURCES := $(filter-out $(MAKEFILE_DIR)wasm.cpp,$(wildcard $(MAKEFILE_DIR)*.cpp))
COMMAND_CPP_SOURCES := $(wildcard $(MAKEFILE_DIR)commands/*.cpp)
C_OBJECTS := $(patsubst $(MAKEFILE_DIR)%.c,$(OBJ_DIR)/%.o,$(C_SOURCES))
CPP_OBJECTS := $(patsubst $(MAKEFILE_DIR)%.cpp,$(OBJ_DIR)/%.o,$(CPP_SOURCES))
COMMAND_CPP_OBJECTS := $(patsubst $(MAKEFILE_DIR)commands/%.cpp,$(OBJ_DIR)/commands/%.o,$(COMMAND_CPP_SOURCES))
OBJECTS := $(C_OBJECTS) $(CPP_OBJECTS) $(COMMAND_CPP_OBJECTS)
DEPENDENCIES := $(OBJECTS:.o=.d)

.PHONY: all clean FORCE

all: $(CLI_BINARY)

$(CLI_BINARY): $(OBJECTS) | $(BUILD_DIR)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(MAKEFILE_DIR)%.c $(BUILD_CONFIG) | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(OBJ_DIR)/%.o: $(MAKEFILE_DIR)%.cpp $(BUILD_CONFIG) | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

$(OBJ_DIR)/commands/%.o: $(MAKEFILE_DIR)commands/%.cpp $(BUILD_CONFIG) | $(OBJ_DIR)/commands
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF $(@:.o=.d) -MT $@ -c $< -o $@

BUILD_CONFIG_CONTENT := $(shell cat "$(BUILD_CONFIG)" 2>/dev/null)
ifneq ($(BUILD_CONFIG_CONTENT),$(BUILD_MODE))
$(BUILD_CONFIG): FORCE | $(OBJ_DIR)
	@printf '%s\n' '$(BUILD_MODE)' > $@.tmp
	@mv $@.tmp $@
endif

FORCE:

$(BUILD_DIR) $(OBJ_DIR) $(OBJ_DIR)/commands:
	mkdir -p $@

clean:
	rm -f $(CLI_BINARY) $(OBJECTS) $(DEPENDENCIES)
	rmdir $(OBJ_DIR)/commands $(OBJ_DIR) $(BUILD_DIR) 2>/dev/null || true

-include $(DEPENDENCIES)