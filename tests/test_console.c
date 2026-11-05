#include "console.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static int history_is(const IsocadConsole *c, size_t index, const char *text) {
    if (index >= c->history_count) return 0;
    return strcmp(c->history[index], text) == 0;
}

static IsocadConsoleCommand submit(IsocadConsole *c, const char *text) {
    isocad_console_text_input(c, text);
    return isocad_console_enter(c);
}

static void test_seed_and_empty(void) {
    IsocadConsole c;
    isocad_console_init(&c, NULL);

    check(c.history_count == ISOCAD_CONSOLE_HISTORY_LINES,
          "three seeded history lines");
    check(history_is(&c, 0, "type help for commands"),
          "newest seed line mentions help");
    check(isocad_console_enter(&c) == ISOCAD_CONSOLE_CMD_NONE,
          "empty Enter submits nothing");
    check(c.history_count == ISOCAD_CONSOLE_HISTORY_LINES,
          "empty Enter does not touch history");

    submit(&c, "   ");
    check(c.input_len == 0, "blank line clears the input");
    check(c.history_count == ISOCAD_CONSOLE_HISTORY_LINES,
          "blank line does not touch history");

    isocad_console_free(&c);
}

static void test_known_commands(void) {
    IsocadConsole c;
    isocad_console_init(&c, NULL);

    check(submit(&c, "fit") == ISOCAD_CONSOLE_CMD_FIT, "fit command");
    check(c.input_len == 0, "fit clears input");
    check(history_is(&c, 0, "> fit"), "fit is echoed");

    check(submit(&c, "  FiT  ") == ISOCAD_CONSOLE_CMD_FIT,
          "commands are trimmed and case-insensitive");
    check(history_is(&c, 0, "> FiT"), "echo preserves case after trimming");

    check(submit(&c, "color") == ISOCAD_CONSOLE_CMD_TOGGLE_STYLE,
          "color command");
    check(submit(&c, "colour") == ISOCAD_CONSOLE_CMD_TOGGLE_STYLE,
          "colour alias");
    check(submit(&c, "colors") == ISOCAD_CONSOLE_CMD_TOGGLE_STYLE,
          "colors alias");
    check(submit(&c, "colours") == ISOCAD_CONSOLE_CMD_TOGGLE_STYLE,
          "colours alias");
    check(submit(&c, "thickness") == ISOCAD_CONSOLE_CMD_TOGGLE_STYLE,
          "thickness alias");
    check(submit(&c, "style") == ISOCAD_CONSOLE_CMD_TOGGLE_STYLE,
          "style alias");

    check(submit(&c, "quit") == ISOCAD_CONSOLE_CMD_QUIT, "quit command");
    check(submit(&c, "Exit") == ISOCAD_CONSOLE_CMD_QUIT, "exit alias");

    isocad_console_free(&c);
}

static void test_inline_commands(void) {
    IsocadConsole c;
    isocad_console_init(&c, NULL);

    check(submit(&c, "help") == ISOCAD_CONSOLE_CMD_NONE, "help returns none");
    check(history_is(&c, 0, "commands: fit color clear echo help quit"),
          "help lists the commands");

    check(submit(&c, "?") == ISOCAD_CONSOLE_CMD_NONE, "? alias");
    check(history_is(&c, 0, "commands: fit color clear echo help quit"),
          "? lists the commands");

    check(submit(&c, "nope") == ISOCAD_CONSOLE_CMD_NONE,
          "unknown command returns none");
    check(history_is(&c, 0, "unknown command: nope; type help"),
          "unknown command explains itself");

    check(submit(&c, "echo hello  world") == ISOCAD_CONSOLE_CMD_NONE,
          "echo returns none");
    check(history_is(&c, 0, "hello  world"),
          "echo prints its argument without a prompt");
    check(strcmp(c.history[1], "> echo hello  world") == 0,
          "echo command stays above its output");

    check(submit(&c, "echo") == ISOCAD_CONSOLE_CMD_NONE, "bare echo");
    check(history_is(&c, 0, "echo: missing text"), "bare echo reports");

    check(submit(&c, "clear") == ISOCAD_CONSOLE_CMD_NONE, "clear returns none");
    check(c.history_count == 0, "clear empties the visible history");

    isocad_console_print(&c, "direct output");
    check(history_is(&c, 0, "direct output"), "print adds a raw line");
    isocad_console_print(&c, NULL);
    check(c.history_count == 1, "print(NULL) is ignored");

    isocad_console_free(&c);
}

static void test_long_unknown(void) {
    IsocadConsole c;
    isocad_console_init(&c, NULL);

    char line[ISOCAD_CONSOLE_MAX_INPUT + 1];
    memset(line, 'a', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    check(submit(&c, line) == ISOCAD_CONSOLE_CMD_NONE,
          "long unknown command returns none");
    check(strncmp(c.history[0], "unknown command: aaaa", 21) == 0,
          "long unknown command is truncated in the message");
    check(strlen(c.history[0]) < ISOCAD_CONSOLE_LINE_MAX,
          "unknown message fits the line buffer");

    isocad_console_free(&c);
}

static void test_recall(void) {
    IsocadConsole c;
    isocad_console_init(&c, NULL);

    isocad_console_history_prev(&c);
    check(c.input_len == 0, "Up with no command history does nothing");

    submit(&c, "fit");
    submit(&c, "color");
    submit(&c, "help");

    isocad_console_history_prev(&c);
    check(strcmp(c.input, "help") == 0, "Up recalls the newest command");
    isocad_console_history_prev(&c);
    check(strcmp(c.input, "color") == 0, "Up recalls the previous command");
    isocad_console_history_prev(&c);
    check(strcmp(c.input, "fit") == 0, "Up recalls the oldest command");
    isocad_console_history_prev(&c);
    check(strcmp(c.input, "fit") == 0, "Up stops at the oldest command");

    isocad_console_history_next(&c);
    check(strcmp(c.input, "color") == 0, "Down moves towards newer commands");
    isocad_console_history_next(&c);
    check(strcmp(c.input, "help") == 0, "Down reaches the newest command");
    isocad_console_history_next(&c);
    check(c.input_len == 0, "Down past the newest restores the empty draft");
    isocad_console_history_next(&c);
    check(c.input_len == 0, "Down past the draft does nothing");

    isocad_console_text_input(&c, "dra");
    isocad_console_history_prev(&c);
    check(strcmp(c.input, "help") == 0, "Up saves the typed draft");
    isocad_console_history_next(&c);
    check(strcmp(c.input, "dra") == 0, "Down restores the typed draft");

    isocad_console_history_prev(&c);
    isocad_console_text_input(&c, "x");
    check(strcmp(c.input, "helpx") == 0, "typing edits the recalled line");
    isocad_console_history_prev(&c);
    check(strcmp(c.input, "help") == 0,
          "Up after editing saves the edited line as draft");
    isocad_console_history_next(&c);
    check(strcmp(c.input, "helpx") == 0, "Down restores the edited draft");

    isocad_console_backspace(&c);
    check(strcmp(c.input, "help") == 0, "Backspace edits the draft");
    check(c.recall_pos == 0, "Backspace leaves recall mode");

    isocad_console_clear_input(&c);
    check(c.input_len == 0, "clear_input empties the line");
    check(c.recall_pos == 0, "clear_input leaves recall mode");

    isocad_console_free(&c);
}

static void test_command_history(void) {
    IsocadConsole c;
    isocad_console_init(&c, NULL);

    submit(&c, "fit");
    submit(&c, "fit");
    check(c.cmd_history_count == 1,
          "consecutive duplicate commands are stored once");

    /* Clearing the visible history must not forget commands. */
    submit(&c, "clear");
    isocad_console_history_prev(&c);
    check(strcmp(c.input, "clear") == 0, "clear is itself recallable");
    isocad_console_history_prev(&c);
    check(strcmp(c.input, "fit") == 0, "visible clear keeps command history");

    isocad_console_free(&c);
}

static void test_command_history_ring(void) {
    IsocadConsole c;
    isocad_console_init(&c, NULL);

    for (int i = 0; i < 40; ++i) {
        char line[ISOCAD_CONSOLE_MAX_INPUT + 1];
        snprintf(line, sizeof(line), "echo %d", i);
        submit(&c, line);
    }

    check(c.cmd_history_count == ISOCAD_CONSOLE_CMD_HISTORY,
          "command history is capped");
    check(strcmp(c.cmd_history[0], "echo 39") == 0, "newest command is kept");

    for (size_t i = 0; i < ISOCAD_CONSOLE_CMD_HISTORY; ++i) {
        isocad_console_history_prev(&c);
    }
    check(strcmp(c.input, "echo 8") == 0,
          "the oldest retained command is 32 steps back");
    isocad_console_history_prev(&c);
    check(strcmp(c.input, "echo 8") == 0, "recall stops at the oldest retained");

    isocad_console_free(&c);
}

static void test_input_limits(void) {
    IsocadConsole c;
    isocad_console_init(&c, NULL);

    char line[128];
    memset(line, 'a', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    isocad_console_text_input(&c, line);
    check(c.input_len == ISOCAD_CONSOLE_MAX_INPUT,
          "input stops at the buffer limit");

    isocad_console_backspace(&c);
    check(c.input_len == ISOCAD_CONSOLE_MAX_INPUT - 1,
          "Backspace shortens the input");

    isocad_console_clear_input(&c);
    isocad_console_text_input(&c, "\n\t\xC3\xA9");
    check(c.input_len == 0, "control and non-ASCII bytes are ignored");

    isocad_console_text_input(&c, "abc");
    isocad_console_clear_input(&c);
    check(c.input_len == 0, "clear_input removes typed text");

    isocad_console_free(&c);
}

int main(void) {
    test_seed_and_empty();
    test_known_commands();
    test_inline_commands();
    test_long_unknown();
    test_recall();
    test_command_history();
    test_command_history_ring();
    test_input_limits();

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all console tests passed\n");
    return 0;
}
