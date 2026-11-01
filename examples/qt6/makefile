PKG_CONFIG ?= pkg-config
CXX ?= c++

BUILD := build
BIN := $(BUILD)/isogl-qt6-host

QT_CFLAGS := $(shell $(PKG_CONFIG) --cflags Qt6Widgets Qt6OpenGLWidgets)
QT_LIBS := $(shell $(PKG_CONFIG) --libs Qt6Widgets Qt6OpenGLWidgets)
ISOGL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags isogl)
ISOGL_LIBS ?= $(shell $(PKG_CONFIG) --libs --static isogl)
ISOGL_LIB ?= $(shell $(PKG_CONFIG) --variable=libdir isogl)/libisogl.a

CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra

all: $(BIN)

$(BUILD)/main.o: main.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(QT_CFLAGS) $(ISOGL_CFLAGS) -c $< -o $@

$(BIN): $(BUILD)/main.o $(ISOGL_LIB)
	$(CXX) $(CXXFLAGS) $(BUILD)/main.o $(ISOGL_LIBS) $(QT_LIBS) -o $@

run: $(BIN)
	./$(BIN)

clean:
	$(RM) -r $(BUILD)

.PHONY: all run clean
