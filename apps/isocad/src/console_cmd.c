#include "console_cmd.h"

#include <stdio.h>
#include <string.h>

static const char *cmd_skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *cmd_token_end(const char *s) {
    while (*s && *s != ' ' && *s != '\t') s++;
    return s;
}

static int cmd_word_is(const char *begin, const char *end, const char *word) {
    size_t length = strlen(word);
    return (size_t)(end - begin) == length &&
           memcmp(begin, word, length) == 0;
}

static void cmd_copy(char *dst, size_t size,
                     const char *begin, const char *end) {
    if (!dst || size == 0) return;
    size_t length = (size_t)(end - begin);
    if (length >= size) length = size - 1;
    memcpy(dst, begin, length);
    dst[length] = '\0';
}

static int cmd_parse_u32(const char *begin, const char *end, uint32_t *out) {
    if (begin == end) return -1;
    uint64_t value = 0;
    for (const char *p = begin; p < end; ++p) {
        if (*p < '0' || *p > '9') return -1;
        value = value * 10u + (uint64_t)(*p - '0');
        if (value > UINT32_MAX) return -1;
    }
    if (value == 0) return -1;
    *out = (uint32_t)value;
    return 0;
}

static void cmd_set_error(IsocadCommand *cmd, const char *message) {
    if (!cmd) return;
    cmd->kind = ISOCAD_CMD_NONE;
    snprintf(cmd->error, sizeof(cmd->error), "%s", message);
}

static void cmd_split_rest(IsocadCommand *cmd, const char *rest) {
    snprintf(cmd->rest, sizeof(cmd->rest), "%s", rest);
    cmd->arg_count = 0;

    const char *p = cmd_skip_ws(rest);
    const char *first_end = NULL;
    while (*p) {
        const char *end = cmd_token_end(p);
        if (cmd->arg_count < ISOCAD_CMD_MAX_ARGS) {
            cmd_copy(cmd->arg[cmd->arg_count], ISOCAD_CMD_ARG_MAX, p, end);
        }
        cmd->arg_count++;
        if (!first_end && cmd->arg_count == 1) first_end = end;
        p = cmd_skip_ws(end);
    }

    cmd->rest_after_first[0] = '\0';
    if (first_end) {
        /* Preserve the rest of the line except the one separator after the
           index.  Additional spaces are meaningful text (e.g. " mm"). */
        const char *after = first_end;
        if (*after == ' ' || *after == '\t') after++;
        snprintf(cmd->rest_after_first, sizeof(cmd->rest_after_first), "%s",
                 after);
    }
}

void isocad_command_init(IsocadCommand *cmd) {
    if (!cmd) return;
    memset(cmd, 0, sizeof(*cmd));
}

IsocadCommandKind isocad_command_parse(const char *line, IsocadCommand *cmd) {
    if (!cmd) return ISOCAD_CMD_NONE;
    isocad_command_init(cmd);
    if (!line) {
        cmd_set_error(cmd, "empty command");
        return ISOCAD_CMD_NONE;
    }

    const char *p = cmd_skip_ws(line);
    if (!*p) {
        cmd_set_error(cmd, "empty command");
        return ISOCAD_CMD_NONE;
    }

    const char *word_end = cmd_token_end(p);

    if (cmd_word_is(p, word_end, "save")) {
        cmd->recognized = 1;
        const char *path = cmd_skip_ws(word_end);
        if (!*path) {
            cmd_set_error(cmd, "usage: save <path>");
            return ISOCAD_CMD_NONE;
        }
        size_t length = strlen(path);
        if (length >= sizeof(cmd->path)) {
            cmd_set_error(cmd, "path too long");
            return ISOCAD_CMD_NONE;
        }
        snprintf(cmd->path, sizeof(cmd->path), "%s", path);

        /* Trim trailing whitespace from the path. */
        size_t n = strlen(cmd->path);
        while (n > 0 && (cmd->path[n - 1] == ' ' || cmd->path[n - 1] == '\t')) {
            cmd->path[--n] = '\0';
        }
        if (n == 0) {
            cmd_set_error(cmd, "usage: save <path>");
            return ISOCAD_CMD_NONE;
        }
        cmd->kind = ISOCAD_CMD_SAVE;
        return cmd->kind;
    }

    if (!cmd_word_is(p, word_end, "sym")) {
        cmd_set_error(cmd, "unknown command");
        return ISOCAD_CMD_NONE;
    }
    cmd->recognized = 1;

    p = cmd_skip_ws(word_end);
    if (!*p) {
        cmd_set_error(cmd, "usage: sym list|info|set|points|remove");
        return ISOCAD_CMD_NONE;
    }
    const char *sub_end = cmd_token_end(p);

    if (cmd_word_is(p, sub_end, "list")) {
        cmd->kind = ISOCAD_CMD_SYM_LIST;
        return cmd->kind;
    }

    IsocadCommandKind kind;
    if (cmd_word_is(p, sub_end, "info")) {
        kind = ISOCAD_CMD_SYM_INFO;
    } else if (cmd_word_is(p, sub_end, "points")) {
        kind = ISOCAD_CMD_SYM_POINTS;
    } else if (cmd_word_is(p, sub_end, "remove")) {
        kind = ISOCAD_CMD_SYM_REMOVE;
    } else if (cmd_word_is(p, sub_end, "set")) {
        kind = ISOCAD_CMD_SYM_SET;
    } else {
        cmd_set_error(cmd, "unknown sym subcommand");
        return ISOCAD_CMD_NONE;
    }

    p = cmd_skip_ws(sub_end);
    if (!*p) {
        cmd_set_error(cmd, "missing symbol id");
        return ISOCAD_CMD_NONE;
    }
    const char *id_end = cmd_token_end(p);
    if (cmd_parse_u32(p, id_end, &cmd->symbol) != 0) {
        cmd_set_error(cmd, "invalid symbol id");
        return ISOCAD_CMD_NONE;
    }
    cmd->has_symbol = 1;

    if (kind != ISOCAD_CMD_SYM_SET) {
        cmd->kind = kind;
        return cmd->kind;
    }

    p = cmd_skip_ws(id_end);
    if (!*p) {
        cmd_set_error(cmd, "usage: sym set <id> <key> [args]");
        return ISOCAD_CMD_NONE;
    }
    const char *key_end = cmd_token_end(p);
    if ((size_t)(key_end - p) >= sizeof(cmd->key)) {
        cmd_set_error(cmd, "parameter key too long");
        return ISOCAD_CMD_NONE;
    }
    cmd_copy(cmd->key, sizeof(cmd->key), p, key_end);

    cmd_split_rest(cmd, cmd_skip_ws(key_end));
    cmd->kind = ISOCAD_CMD_SYM_SET;
    return cmd->kind;
}
