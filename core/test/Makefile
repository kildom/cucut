CXX ?= g++
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -pthread
CPPFLAGS += \
	-Igtest/googletest/include \
	-Igtest/googlemock/include \
	-Igtest/googletest \
	-Igtest/googlemock
LDLIBS += -pthread

TEST_BINARY := build/core_tests
TEST_SOURCES := \
	src/binjson_test.cpp \
	src/buffer_test.cpp \
	gtest/googletest/src/gtest-all.cc \
	gtest/googlemock/src/gmock-all.cc \
	gtest/googlemock/src/gmock_main.cc

.PHONY: all run clean

all: $(TEST_BINARY)

run: $(TEST_BINARY)
	./$(TEST_BINARY)

$(TEST_BINARY): $(TEST_SOURCES) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TEST_SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

build:
	mkdir -p $@

clean:
	rm -rf build
