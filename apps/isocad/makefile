PKG_CONFIG      ?= pkg-config
PKG_CONFIG_PATH ?= $(HOME)/.local/lib/pkgconfig
export PKG_CONFIG_PATH

CC      ?= cc
BUILD   := build
SRCS    := src/main.c src/cad.c src/console.c src/panels.c src/panel_deck.c
OBJS    := $(SRCS:src/%.c=$(BUILD)/%.o)
BIN     := $(BUILD)/isocad

CFLAGS   ?= -O2 -g
CFLAGS   += -std=c11 -Wall -Wextra -MMD -MP
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags isogl-sdl)
LDLIBS   := $(shell $(PKG_CONFIG) --libs --static isogl-sdl) -lm

all: $(BIN)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDLIBS) -o $@

run: $(BIN)
	./$(BIN)

clean:
	$(RM) -r $(BUILD)

-include $(OBJS:.o=.d)

.PHONY: all run clean
