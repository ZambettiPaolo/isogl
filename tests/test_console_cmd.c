#include "console_cmd.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *message) {
    if (condition) {
        printf("ok   %s\n", message);
    } else {
        printf("FAIL %s\n", message);
        failures++;
    }
}

static IsocadCommand parse(const char *line) {
    IsocadCommand cmd;
    isocad_command_parse(line, &cmd);
    return cmd;
}

int main(void) {
    IsocadCommand cmd;

    cmd = parse("");
    check(cmd.kind == ISOCAD_CMD_NONE && cmd.recognized == 0,
          "empty line is not a command");

    cmd = parse("hello");
    check(cmd.kind == ISOCAD_CMD_NONE && cmd.recognized == 0,
          "ordinary text is not a command");

    cmd = parse("sym list");
    check(cmd.kind == ISOCAD_CMD_SYM_LIST && cmd.recognized == 1,
          "sym list parsed");

    cmd = parse("  sym   info   7 ");
    check(cmd.kind == ISOCAD_CMD_SYM_INFO && cmd.symbol == 7 &&
          cmd.has_symbol == 1,
          "sym info parses symbol id with extra whitespace");

    cmd = parse("sym points 2");
    check(cmd.kind == ISOCAD_CMD_SYM_POINTS && cmd.symbol == 2,
          "sym points parsed");

    cmd = parse("sym remove 4");
    check(cmd.kind == ISOCAD_CMD_SYM_REMOVE && cmd.symbol == 4,
          "sym remove parsed");

    cmd = parse("sym set 7 points 2 10 20");
    check(cmd.kind == ISOCAD_CMD_SYM_SET && cmd.symbol == 7 &&
          strcmp(cmd.key, "points") == 0 && cmd.arg_count == 3 &&
          strcmp(cmd.arg[0], "2") == 0 &&
          strcmp(cmd.arg[1], "10") == 0 &&
          strcmp(cmd.arg[2], "20") == 0,
          "sym set point tokens parsed");

    cmd = parse("sym set 3 texts 1 mm");
    check(cmd.kind == ISOCAD_CMD_SYM_SET &&
          strcmp(cmd.key, "texts") == 0 && cmd.arg_count == 2 &&
          strcmp(cmd.arg[0], "1") == 0 &&
          strcmp(cmd.rest_after_first, "mm") == 0,
          "sym set text keeps the text argument");

    cmd = parse("sym set 3 texts 1 hello world");
    check(cmd.kind == ISOCAD_CMD_SYM_SET &&
          cmd.arg_count == 3 &&
          strcmp(cmd.rest_after_first, "hello world") == 0,
          "sym set text preserves spaces in the rest of the line");

    cmd = parse("sym set 3 texts 0");
    check(cmd.kind == ISOCAD_CMD_SYM_SET && cmd.arg_count == 1 &&
          cmd.rest_after_first[0] == '\0',
          "sym set text allows an empty value");

    cmd = parse("sym set 3 texts 1  mm");
    check(cmd.kind == ISOCAD_CMD_SYM_SET &&
          strcmp(cmd.rest_after_first, " mm") == 0,
          "sym set text preserves an intentional leading space");

    cmd = parse("sym set 4 scale 2");
    check(cmd.kind == ISOCAD_CMD_SYM_SET &&
          strcmp(cmd.key, "scale") == 0 && cmd.arg_count == 1,
          "sym set read-only scale still parses");

    cmd = parse("save build/symbols.toml");
    check(cmd.kind == ISOCAD_CMD_SAVE && cmd.recognized == 1 &&
          strcmp(cmd.path, "build/symbols.toml") == 0,
          "save path parsed");

    cmd = parse("sym set 0 points 0 0 0");
    check(cmd.kind == ISOCAD_CMD_NONE && cmd.recognized == 1 &&
          cmd.error[0] != '\0',
          "zero symbol id rejected");

    cmd = parse("sym info abc");
    check(cmd.kind == ISOCAD_CMD_NONE && cmd.recognized == 1 &&
          cmd.error[0] != '\0',
          "non-numeric symbol id rejected");

    cmd = parse("sym");
    check(cmd.kind == ISOCAD_CMD_NONE && cmd.recognized == 1,
          "missing sym subcommand is a recognized error");

    cmd = parse("sym set 1");
    check(cmd.kind == ISOCAD_CMD_NONE && cmd.recognized == 1 &&
          cmd.error[0] != '\0',
          "missing parameter key rejected");

    cmd = parse("sym nope 1");
    check(cmd.kind == ISOCAD_CMD_NONE && cmd.recognized == 1,
          "unknown sym subcommand rejected");

    if (failures == 0) {
        printf("PASSED: console command parser\n");
        return 0;
    }
    printf("FAILED: %d console command check(s)\n", failures);
    return 1;
}
