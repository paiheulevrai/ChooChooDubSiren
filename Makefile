CXX ?= g++
PKG_CONFIG ?= pkg-config
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)
BUILD ?= build/linux

.PHONY: all test
all: $(BUILD)/dubsiren

$(BUILD)/dubsiren: src/main.cpp src/siren.cpp src/siren.h src/font12x16.cpp
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) src/main.cpp src/siren.cpp src/font12x16.cpp $(LDFLAGS) $(SDL_LIBS) -o $@

$(BUILD)/siren-test: tests/siren_test.cpp src/siren.cpp src/siren.h
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) tests/siren_test.cpp src/siren.cpp -o $@

$(BUILD)/app-test: tests/app_test.cpp src/main.cpp src/siren.cpp src/siren.h src/font12x16.cpp
	mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) tests/app_test.cpp src/siren.cpp src/font12x16.cpp $(SDL_LIBS) -o $@

test: $(BUILD)/siren-test $(BUILD)/app-test
	$(BUILD)/siren-test
	SDL_AUDIODRIVER=dummy $(BUILD)/app-test $(BUILD)/app-test-data
