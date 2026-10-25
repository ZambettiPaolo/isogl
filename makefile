PREFIX      ?= /usr/local
LIBDIR      ?= $(PREFIX)/lib
INCLUDEDIR  ?= $(PREFIX)/include
DATADIR     ?= $(PREFIX)/share/isogl
BINDIR      ?= $(PREFIX)/bin

VERSION     := 0.1.0
PKG_CONFIG  ?= pkg-config
CC          ?= cc
AR          ?= ar

SDL2_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL2_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)

CFLAGS      ?= -O2 -g
CFLAGS      += -std=c11 -Wall -Wextra -MMD -MP -fPIC
CPPFLAGS    += -Iinclude/isogl -Ithird_party/tomlc99 \
               -DISOGL_DEFAULT_ASSET_DIR=\"$(DATADIR)\"
LDLIBS      := -ldl -lm

ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer -g
LDFLAGS += -fsanitize=address,undefined
endif

BUILD       := build
LIB         := $(BUILD)/libisogl.a
DEMO        := $(BUILD)/isogl-demo
TEST        := $(BUILD)/test-core

LIB_SRCS    := src/isogl.c src/isogl_font.c src/isogl_style.c \
               third_party/glad/glad.c third_party/tomlc99/toml.c
DEMO_SRCS   := examples/demo/main.c
TEST_SRCS   := tests/test_core.c

LIB_OBJS    := $(LIB_SRCS:%.c=$(BUILD)/%.o)
DEMO_OBJS   := $(DEMO_SRCS:%.c=$(BUILD)/%.o)
TEST_OBJS   := $(TEST_SRCS:%.c=$(BUILD)/%.o)

all: $(LIB) $(DEMO)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL2_CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(DEMO): $(DEMO_OBJS) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEMO_OBJS) $(LIB) $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

$(TEST): $(TEST_OBJS) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(TEST_OBJS) $(LIB) $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

run: $(DEMO)
	./$(DEMO)

test: $(TEST)
	./$(TEST) .

install: all
	install -d $(DESTDIR)$(INCLUDEDIR)/isogl
	install -d $(DESTDIR)$(LIBDIR)/pkgconfig
	install -d $(DESTDIR)$(DATADIR)/shaders
	install -d $(DESTDIR)$(DATADIR)/fonts
	install -d $(DESTDIR)$(BINDIR)
	install -m644 include/isogl/*.h $(DESTDIR)$(INCLUDEDIR)/isogl/
	install -m644 $(LIB) $(DESTDIR)$(LIBDIR)/
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@VERSION@|$(VERSION)|g' \
	    isogl.pc.in > $(DESTDIR)$(LIBDIR)/pkgconfig/isogl.pc
	install -m644 shaders/*.glsl $(DESTDIR)$(DATADIR)/shaders/
	install -m644 fonts/ReliefSingleLineLinear.toml $(DESTDIR)$(DATADIR)/fonts/
	install -m755 $(DEMO) $(DESTDIR)$(BINDIR)/isogl-demo

uninstall:
	$(RM) $(DESTDIR)$(LIBDIR)/libisogl.a
	$(RM) $(DESTDIR)$(LIBDIR)/pkgconfig/isogl.pc
	$(RM) $(DESTDIR)$(BINDIR)/isogl-demo
	$(RM) $(DESTDIR)$(INCLUDEDIR)/isogl/*.h
	$(RM) $(DESTDIR)$(DATADIR)/shaders/*.glsl
	$(RM) $(DESTDIR)$(DATADIR)/fonts/ReliefSingleLineLinear.toml

clean:
	$(RM) -r $(BUILD)

-include $(LIB_OBJS:.o=.d) $(DEMO_OBJS:.o=.d) $(TEST_OBJS:.o=.d)

.PHONY: all run test install uninstall clean
