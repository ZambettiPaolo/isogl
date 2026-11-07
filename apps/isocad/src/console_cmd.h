#ifndef ISOCAD_CONSOLE_CMD_H
#define ISOCAD_CONSOLE_CMD_H

#include <stddef.h>
#include <stdint.h>

/*
 * Pure parser for the isocad console prototype.  It has no GL, SDL or CAD
 * dependency: it only splits a line into a command kind and generic argument
 * tokens.  Value interpretation is metadata-driven and lives in cad_commands.
 */

#define ISOCAD_CMD_MAX_ARGS 8
#define ISOCAD_CMD_ARG_MAX  48
#define ISOCAD_CMD_KEY_MAX  32
#define ISOCAD_CMD_PATH_MAX 64
#define ISOCAD_CMD_REST_MAX 96
#define ISOCAD_CMD_ERROR_MAX 64

typedef enum {
    ISOCAD_CMD_NONE = 0,
    ISOCAD_CMD_SYM_LIST,
    ISOCAD_CMD_SYM_INFO,
    ISOCAD_CMD_SYM_SET,
    ISOCAD_CMD_SYM_POINTS,
    ISOCAD_CMD_SYM_REMOVE,
    ISOCAD_CMD_SAVE
} IsocadCommandKind;

typedef struct {
    IsocadCommandKind kind;

    /* 1 when the first word is a known namespace (`sym` or `save`).  A false
     * value means the line was ordinary console text, not a malformed
     * command, so callers can stay silent instead of printing an error. */
    int recognized;

    uint32_t symbol;              /* SYM_INFO / SET / POINTS / REMOVE */
    int has_symbol;
    char key[ISOCAD_CMD_KEY_MAX]; /* SYM_SET parameter key */
    char path[ISOCAD_CMD_PATH_MAX];
    char rest[ISOCAD_CMD_REST_MAX];             /* raw text after key/path */
    char rest_after_first[ISOCAD_CMD_REST_MAX]; /* TEXTS: text after index */

    char arg[ISOCAD_CMD_MAX_ARGS][ISOCAD_CMD_ARG_MAX];
    size_t arg_count;

    char error[ISOCAD_CMD_ERROR_MAX];
} IsocadCommand;

void isocad_command_init(IsocadCommand *cmd);

/* Parse `line` into `cmd`.  Returns cmd->kind; ISOCAD_CMD_NONE means the line
 * is not a command or is malformed, with the reason in cmd->error.  Value
 * counts and kinds are intentionally not validated here. */
IsocadCommandKind isocad_command_parse(const char *line, IsocadCommand *cmd);

#endif /* ISOCAD_CONSOLE_CMD_H */
