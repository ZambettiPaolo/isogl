PKG_CONFIG ?= pkg-config
CC ?= cc

BUILD := build
BIN := $(BUILD)/isogl-gtk4-host

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4) -lEGL -ldl
ISOGL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags isogl)
ISOGL_LIBS ?= $(shell $(PKG_CONFIG) --libs --static isogl)
ISOGL_LIB ?= $(shell $(PKG_CONFIG) --variable=libdir isogl)/libisogl.a

CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra

all: $(BIN)

$(BUILD)/main.o: main.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(ISOGL_CFLAGS) -c $< -o $@

$(BIN): $(BUILD)/main.o $(ISOGL_LIB)
	$(CC) $(CFLAGS) $(BUILD)/main.o $(ISOGL_LIBS) $(GTK_LIBS) -o $@

run: $(BIN)
	./$(BIN)

clean:
	$(RM) -r $(BUILD)

.PHONY: all run clean
