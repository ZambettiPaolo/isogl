PREFIX      ?= /usr/local
LIBDIR      ?= $(PREFIX)/lib
INCLUDEDIR  ?= $(PREFIX)/include
DATADIR     ?= $(PREFIX)/share/isogl
BINDIR      ?= $(PREFIX)/bin

VERSION     := 0.2.0
PKG_CONFIG  ?= pkg-config
CC          ?= cc
AR          ?= ar

SDL2_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL2_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)

CFLAGS      ?= -O2 -g
CFLAGS      += -std=c11 -Wall -Wextra -MMD -MP -fPIC
CPPFLAGS    += -Iinclude -Iinclude/isogl -Ithird_party/tomlc99 \
               -Iapps/isocad/src -Iexamples/demo -Ihosts \
               -DISOGL_DEFAULT_ASSET_DIR=\"$(DATADIR)\"
LDLIBS      := -ldl -lm

ifeq ($(SANITIZE),1)
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer -g
LDFLAGS += -fsanitize=address,undefined
endif

BUILD       := build
LIB         := $(BUILD)/libisogl.a
SDL_HOST_LIB := $(BUILD)/libisogl_sdl.a
DEMO        := $(BUILD)/isogl-demo
TEST        := $(BUILD)/test-core
TEST_SYMBOL_GEOMETRY := $(BUILD)/test-symbol-geometry
SYMBOL_GEOMETRY_TEST_OBJS := $(BUILD)/tests/test_symbol_geometry.o \
                             $(BUILD)/src/isogl_symbol_geometry.o
TEST_PANEL_LAYOUT := $(BUILD)/test-panel-layout
PANEL_LAYOUT_TEST_OBJS := $(BUILD)/tests/test_panel_layout.o \
                          $(BUILD)/src/isogl_panel.o
TEST_PANEL_DECK := $(BUILD)/test-panel-deck
PANEL_DECK_TEST_OBJS := $(BUILD)/tests/test_panel_deck.o \
                        $(BUILD)/apps/isocad/src/panel_deck.o \
                        $(BUILD)/src/isogl_panel.o
TEST_SYMBOLS := $(BUILD)/test-symbols
TEST_SYMBOLS_SRCS := tests/test_symbols.c tests/fake_cad.c
TEST_SYMBOLS_OBJS := $(TEST_SYMBOLS_SRCS:%.c=$(BUILD)/%.o)
TEST_VIEWPORT := $(BUILD)/test-viewport
TEST_VIEWPORT_SRCS := tests/test_viewport.c
TEST_VIEWPORT_OBJS := $(TEST_VIEWPORT_SRCS:%.c=$(BUILD)/%.o)
TEST_OVERLAY := $(BUILD)/test-overlay
TEST_OVERLAY_SRCS := tests/test_overlay.c
TEST_OVERLAY_OBJS := $(TEST_OVERLAY_SRCS:%.c=$(BUILD)/%.o)
TEST_DRAWING_LOAD := $(BUILD)/test-drawing-load
DRAWING_LOAD_TEST_OBJS := $(BUILD)/tests/test_drawing_load.o \
                          $(BUILD)/examples/demo/drawing_load.o
TEST_SYMBOL_PARAMS := $(BUILD)/test-symbol-params
SYMBOL_PARAMS_TEST_OBJS := $(BUILD)/tests/test_symbol_params.o
TEST_CAD_SYMBOLS := $(BUILD)/test-cad-symbols
CAD_SYMBOLS_TEST_OBJS := $(BUILD)/tests/test_cad_symbols.o \
                         $(BUILD)/apps/isocad/src/cad.o \
                         $(BUILD)/apps/isocad/src/cad_commands.o \
                         $(BUILD)/apps/isocad/src/console.o \
                         $(BUILD)/apps/isocad/src/console_cmd.o \
                         $(BUILD)/examples/demo/drawing_load.o
TEST_CONSOLE_CMD := $(BUILD)/test-console-cmd
CONSOLE_CMD_TEST_OBJS := $(BUILD)/tests/test_console_cmd.o \
                         $(BUILD)/apps/isocad/src/console_cmd.o

SYMBOL_GEOMETRY_SRC := src/isogl_symbol_geometry.c
SYMBOL_SRC := src/isogl_symbol.c
LIB_SRCS    := src/isogl.c src/isogl_font.c src/isogl_style.c \
               src/isogl_overlay.c src/isogl_panel.c src/isogl_panel_emit.c \
               $(SYMBOL_GEOMETRY_SRC) $(SYMBOL_SRC) \
               third_party/glad/glad.c third_party/tomlc99/toml.c
SDL_HOST_SRCS := hosts/isogl-sdl/isogl_sdl.c
DEMO_SRCS   := examples/demo/main.c examples/demo/drawing_load.c
TEST_SRCS   := tests/test_core.c apps/isocad/src/cad.c

LIB_OBJS    := $(LIB_SRCS:%.c=$(BUILD)/%.o)
SDL_HOST_OBJS := $(SDL_HOST_SRCS:%.c=$(BUILD)/%.o)
DEMO_OBJS   := $(DEMO_SRCS:%.c=$(BUILD)/%.o)
TEST_OBJS   := $(TEST_SRCS:%.c=$(BUILD)/%.o)

all: $(LIB) $(SDL_HOST_LIB) $(DEMO)

lib: $(LIB)

sdl-host: $(SDL_HOST_LIB)

# Only the SDL host, the demo and the GL tests need SDL headers.  The core
# library objects are compiled without them: a stray SDL include fails the
# build by construction.
$(SDL_HOST_OBJS) $(DEMO_OBJS) $(TEST_OBJS) $(TEST_SYMBOLS_OBJS) \
    $(TEST_VIEWPORT_OBJS) $(TEST_OVERLAY_OBJS) $(DRAWING_LOAD_TEST_OBJS) \
    $(SYMBOL_PARAMS_TEST_OBJS) $(CAD_SYMBOLS_TEST_OBJS): \
    TARGET_CFLAGS := $(SDL2_CFLAGS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TARGET_CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(SDL_HOST_LIB): $(SDL_HOST_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(DEMO): $(DEMO_OBJS) $(SDL_HOST_LIB) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEMO_OBJS) $(SDL_HOST_LIB) $(LIB) $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

$(TEST): $(TEST_OBJS) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(TEST_OBJS) $(LIB) $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

$(TEST_SYMBOL_GEOMETRY): $(SYMBOL_GEOMETRY_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SYMBOL_GEOMETRY_TEST_OBJS) -lm $(LDFLAGS) -o $@

test-symbol-geometry: $(TEST_SYMBOL_GEOMETRY)
	./$(TEST_SYMBOL_GEOMETRY)

$(TEST_PANEL_LAYOUT): $(PANEL_LAYOUT_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(PANEL_LAYOUT_TEST_OBJS) -lm $(LDFLAGS) -o $@

test-panel-layout: $(TEST_PANEL_LAYOUT)
	./$(TEST_PANEL_LAYOUT)

$(TEST_PANEL_DECK): $(PANEL_DECK_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(PANEL_DECK_TEST_OBJS) -lm $(LDFLAGS) -o $@

test-panel-deck: $(TEST_PANEL_DECK)
	./$(TEST_PANEL_DECK)

$(TEST_SYMBOLS): $(TEST_SYMBOLS_OBJS) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(TEST_SYMBOLS_OBJS) $(LIB) $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

test-symbols: $(TEST_SYMBOLS)
	./$(TEST_SYMBOLS) . tests/fixtures/test_drawing.toml

$(TEST_VIEWPORT): $(TEST_VIEWPORT_OBJS) $(SDL_HOST_LIB) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(TEST_VIEWPORT_OBJS) $(SDL_HOST_LIB) $(LIB) \
	    $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

test-viewport: $(TEST_VIEWPORT)
	./$(TEST_VIEWPORT) .

$(TEST_OVERLAY): $(TEST_OVERLAY_OBJS) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(TEST_OVERLAY_OBJS) $(LIB) \
	    $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

test-overlay: $(TEST_OVERLAY)
	./$(TEST_OVERLAY) .

$(TEST_DRAWING_LOAD): $(DRAWING_LOAD_TEST_OBJS) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DRAWING_LOAD_TEST_OBJS) $(LIB) \
	    $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

test-drawing-load: $(TEST_DRAWING_LOAD)
	./$(TEST_DRAWING_LOAD) .

$(TEST_SYMBOL_PARAMS): $(SYMBOL_PARAMS_TEST_OBJS) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SYMBOL_PARAMS_TEST_OBJS) $(LIB) \
	    $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

test-symbol-params: $(TEST_SYMBOL_PARAMS)
	./$(TEST_SYMBOL_PARAMS) .

$(TEST_CAD_SYMBOLS): $(CAD_SYMBOLS_TEST_OBJS) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CAD_SYMBOLS_TEST_OBJS) $(LIB) \
	    $(SDL2_LIBS) $(LDLIBS) $(LDFLAGS) -o $@

test-cad-symbols: $(TEST_CAD_SYMBOLS)
	./$(TEST_CAD_SYMBOLS) .

$(TEST_CONSOLE_CMD): $(CONSOLE_CMD_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CONSOLE_CMD_TEST_OBJS) -lm $(LDFLAGS) -o $@

test-console-cmd: $(TEST_CONSOLE_CMD)
	./$(TEST_CONSOLE_CMD)

run: $(DEMO)
	./$(DEMO)

example-qt6: lib
	@$(PKG_CONFIG) --exists Qt6Widgets Qt6OpenGLWidgets || \
	    (echo "error: Qt6 Widgets/OpenGLWidgets development files not found"; exit 1)
	$(MAKE) -C examples/qt6 BUILD="$(CURDIR)/build/examples/qt6" \
	    ISOGL_LIB="$(CURDIR)/build/libisogl.a" \
	    ISOGL_CFLAGS="-I$(CURDIR)/include -I$(CURDIR)/include/isogl" \
	    ISOGL_LIBS="$(CURDIR)/build/libisogl.a -ldl -lm"

example-gtk4: lib
	@$(PKG_CONFIG) --exists gtk4 || \
	    (echo "error: GTK4 development files not found (libgtk-4-dev)"; exit 1)
	$(MAKE) -C examples/gtk4 BUILD="$(CURDIR)/build/examples/gtk4" \
	    ISOGL_LIB="$(CURDIR)/build/libisogl.a" \
	    ISOGL_CFLAGS="-I$(CURDIR)/include -I$(CURDIR)/include/isogl" \
	    ISOGL_LIBS="$(CURDIR)/build/libisogl.a -ldl -lm"

test: test-symbol-geometry test-panel-layout test-panel-deck test-overlay \
      test-drawing-load test-symbol-params test-console-cmd test-cad-symbols \
      test-symbols test-viewport $(TEST)
	./$(TEST) .

install: all
	install -d $(DESTDIR)$(INCLUDEDIR)/isogl
	install -d $(DESTDIR)$(INCLUDEDIR)/isogl-sdl
	install -d $(DESTDIR)$(LIBDIR)/pkgconfig
	install -d $(DESTDIR)$(DATADIR)/shaders
	install -d $(DESTDIR)$(DATADIR)/fonts
	install -d $(DESTDIR)$(DATADIR)/drawings
	install -d $(DESTDIR)$(BINDIR)
	install -m644 include/isogl/*.h $(DESTDIR)$(INCLUDEDIR)/isogl/
	install -m644 hosts/isogl-sdl/isogl_sdl.h $(DESTDIR)$(INCLUDEDIR)/isogl-sdl/
	install -m644 $(LIB) $(DESTDIR)$(LIBDIR)/
	install -m644 $(SDL_HOST_LIB) $(DESTDIR)$(LIBDIR)/
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@VERSION@|$(VERSION)|g' \
	    isogl.pc.in > $(DESTDIR)$(LIBDIR)/pkgconfig/isogl.pc
	sed -e 's|@PREFIX@|$(PREFIX)|g' -e 's|@VERSION@|$(VERSION)|g' \
	    isogl-sdl.pc.in > $(DESTDIR)$(LIBDIR)/pkgconfig/isogl-sdl.pc
	install -m644 shaders/*.glsl $(DESTDIR)$(DATADIR)/shaders/
	install -m644 fonts/ReliefSingleLineLinear.toml $(DESTDIR)$(DATADIR)/fonts/
	install -m644 examples/demo/architecture.toml $(DESTDIR)$(DATADIR)/drawings/
	install -m755 $(DEMO) $(DESTDIR)$(BINDIR)/isogl-demo

uninstall:
	$(RM) $(DESTDIR)$(LIBDIR)/libisogl.a
	$(RM) $(DESTDIR)$(LIBDIR)/libisogl_sdl.a
	$(RM) $(DESTDIR)$(LIBDIR)/pkgconfig/isogl.pc
	$(RM) $(DESTDIR)$(LIBDIR)/pkgconfig/isogl-sdl.pc
	$(RM) $(DESTDIR)$(BINDIR)/isogl-demo
	$(RM) $(DESTDIR)$(INCLUDEDIR)/isogl/*.h
	$(RM) $(DESTDIR)$(INCLUDEDIR)/isogl-sdl/isogl_sdl.h
	$(RM) $(DESTDIR)$(DATADIR)/shaders/*.glsl
	$(RM) $(DESTDIR)$(DATADIR)/fonts/ReliefSingleLineLinear.toml
	$(RM) $(DESTDIR)$(DATADIR)/drawings/architecture.toml

check-sdl-free: lib
	@! grep -RIn --include='*.c' --include='*.h' \
	    -E '#include[[:space:]]*[<"]SDL|SDL_[A-Za-z]' src include/isogl || \
	    (echo "error: SDL reference in the core" && exit 1)
	@! nm -u $(LIB) | grep -qi SDL || \
	    (echo "error: SDL symbol in $(LIB)" && exit 1)
	@echo "core is SDL-free"

clean:
	$(RM) -r $(BUILD)

-include $(LIB_OBJS:.o=.d) $(SDL_HOST_OBJS:.o=.d) $(DEMO_OBJS:.o=.d) \
         $(TEST_OBJS:.o=.d) $(BUILD)/tests/test_symbol_geometry.d \
         $(TEST_SYMBOLS_OBJS:.o=.d) $(TEST_VIEWPORT_OBJS:.o=.d) \
         $(SYMBOL_PARAMS_TEST_OBJS:.o=.d) $(CAD_SYMBOLS_TEST_OBJS:.o=.d) \
         $(CONSOLE_CMD_TEST_OBJS:.o=.d)

.PHONY: all lib sdl-host run test test-symbol-geometry test-panel-layout \
        test-panel-deck test-overlay test-drawing-load test-symbol-params \
        test-console-cmd test-cad-symbols test-symbols test-viewport \
        check-sdl-free example-qt6 example-gtk4 install uninstall clean
