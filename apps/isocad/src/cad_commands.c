#include "cad_commands.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *cmd_kind_name(isogl_symbol_param_kind kind) {
    switch (kind) {
        case ISOGL_SYM_PARAM_POINTS:   return "POINTS";
        case ISOGL_SYM_PARAM_TEXTS:    return "TEXTS";
        case ISOGL_SYM_PARAM_SEGMENTS: return "SEGMENTS";
        case ISOGL_SYM_PARAM_SCALE:    return "SCALE";
        case ISOGL_SYM_PARAM_NUMBER:   return "NUMBER";
        case ISOGL_SYM_PARAM_INTEGER:  return "INTEGER";
        case ISOGL_SYM_PARAM_BOOLEAN:  return "BOOLEAN";
        case ISOGL_SYM_PARAM_ENUM:     return "ENUM";
        case ISOGL_SYM_PARAM_COLOR:    return "COLOR";
        default:                       return "?";
    }
}

static int find_param_index(const isogl_symbol_params *params,
                            const char *key) {
    if (!params || !key) return -1;
    for (size_t i = 0; i < params->param_count; ++i) {
        if (params->params[i].desc &&
            strcmp(params->params[i].desc->key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int cmd_fail(IsocadConsole *console, const char *message) {
    printf("isocad: %s\n", message);
    isocad_console_echo(console, message);
    return -1;
}

static int parse_double_token(const char *token, double *out) {
    if (!token || !out) return -1;
    errno = 0;
    char *end = NULL;
    double value = strtod(token, &end);
    if (end == token || *end != '\0' || errno == ERANGE || !isfinite(value)) {
        return -1;
    }
    *out = value;
    return 0;
}

static int parse_long_token(const char *token, long *out) {
    if (!token || !out) return -1;
    errno = 0;
    char *end = NULL;
    long value = strtol(token, &end, 10);
    if (end == token || *end != '\0' || errno == ERANGE) return -1;
    *out = value;
    return 0;
}

static int parse_size_token(const char *token, size_t *out) {
    long value = 0;
    if (parse_long_token(token, &value) != 0 || value < 0) return -1;
    *out = (size_t)value;
    return 0;
}

static int parse_bool_token(const char *token, int *out) {
    if (!token || !out) return -1;
    if (strcmp(token, "1") == 0 || strcmp(token, "true") == 0 ||
        strcmp(token, "yes") == 0) {
        *out = 1;
        return 0;
    }
    if (strcmp(token, "0") == 0 || strcmp(token, "false") == 0 ||
        strcmp(token, "no") == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static void print_param_value(FILE *out, const isogl_symbol_param *p,
                              size_t index) {
    const isogl_symbol_param_desc *d = p->desc;
    if (!d) return;

    switch (d->kind) {
        case ISOGL_SYM_PARAM_POINTS:
            if (index < p->count) {
                fprintf(out, "(%.12g, %.12g)", p->value.points[index].x,
                        p->value.points[index].y);
            }
            break;
        case ISOGL_SYM_PARAM_TEXTS:
            if (index < p->count) {
                fprintf(out, "\"%s\"", p->value.texts[index]
                                       ? p->value.texts[index] : "");
            }
            break;
        case ISOGL_SYM_PARAM_SEGMENTS:
            if (index < p->count) {
                const isogl_symbol_segment *s = &p->value.segments[index];
                fprintf(out, "(%.12g, %.12g)-(%.12g, %.12g)",
                        s->x0, s->y0, s->x1, s->y1);
            }
            break;
        case ISOGL_SYM_PARAM_NUMBER:
        case ISOGL_SYM_PARAM_SCALE:
            fprintf(out, "%.12g", p->value.number);
            break;
        case ISOGL_SYM_PARAM_INTEGER:
            fprintf(out, "%d", p->value.integer);
            break;
        case ISOGL_SYM_PARAM_ENUM: {
            const char *label =
                isogl_symbol_param_enum_label(d, p->value.integer);
            if (label) {
                fprintf(out, "%s (%d)", label, p->value.integer);
            } else {
                fprintf(out, "%d", p->value.integer);
            }
            break;
        }
        case ISOGL_SYM_PARAM_BOOLEAN:
            fprintf(out, "%s", p->value.boolean ? "true" : "false");
            break;
        case ISOGL_SYM_PARAM_COLOR:
            fprintf(out, "[%.12g, %.12g, %.12g, %.12g]",
                    p->value.color[0], p->value.color[1],
                    p->value.color[2], p->value.color[3]);
            break;
        default:
            break;
    }
}

static void print_param_dump(FILE *out, const isogl_symbol_param *p) {
    const isogl_symbol_param_desc *d = p->desc;
    if (!d) return;

    fprintf(out, "  %s (%s) kind=%s unit=%s count=%zu editable=%d\n",
            d->key, d->label ? d->label : "-", cmd_kind_name(d->kind),
            d->unit ? d->unit : "", p->count, d->editable);
    if (d->help) fprintf(out, "    help: %s\n", d->help);
    if (d->kind == ISOGL_SYM_PARAM_ENUM && d->enum_values) {
        fprintf(out, "    values:");
        for (size_t i = 0; i < d->enum_count; ++i) {
            fprintf(out, " %s=%s(%d)",
                    d->enum_values[i].name ? d->enum_values[i].name : "?",
                    d->enum_values[i].label ? d->enum_values[i].label : "?",
                    d->enum_values[i].value);
        }
        fputc('\n', out);
    }

    switch (d->kind) {
        case ISOGL_SYM_PARAM_POINTS:
        case ISOGL_SYM_PARAM_TEXTS:
        case ISOGL_SYM_PARAM_SEGMENTS:
            for (size_t i = 0; i < p->count; ++i) {
                fprintf(out, "    [%zu] ", i);
                print_param_value(out, p, i);
                fputc('\n', out);
            }
            break;
        default:
            fprintf(out, "    value: ");
            print_param_value(out, p, 0);
            fputc('\n', out);
            break;
    }
}

void isocad_symbol_summary(const CadSymbolRef *ref, char *out, size_t size) {
    if (!out || size == 0) return;
    if (!ref) {
        snprintf(out, size, "no symbol");
        return;
    }

    const char *type_name =
        (ref->type && ref->type->name) ? ref->type->name : "?";
    const size_t param_count =
        ref->params ? ref->params->param_count : (size_t)0;

    int points_index = ref->params ? find_param_index(ref->params, "points")
                                   : -1;
    if (points_index >= 0) {
        size_t points = ref->params->params[points_index].count;
        snprintf(out, size, "S%u %s %zup pts=%zu", ref->id, type_name,
                 param_count, points);
    } else {
        snprintf(out, size, "S%u %s %zup", ref->id, type_name, param_count);
    }
}

static int command_list(CadDocument *doc, IsocadConsole *console) {
    printf("%zu symbol(s)\n", doc->symbol_count);
    for (size_t i = 0; i < doc->symbol_count; ++i) {
        const CadSymbolRef *ref = &doc->symbols[i];
        const char *name =
            (ref->type && ref->type->name) ? ref->type->name : "?";
        const char *label =
            (ref->type && ref->type->label) ? ref->type->label : "";
        printf("S%u %s \"%s\" %zu params\n", ref->id, name, label,
               ref->params ? ref->params->param_count : (size_t)0);
    }

    char line[ISOCAD_CONSOLE_MAX_INPUT + 1];
    snprintf(line, sizeof(line), "%zu symbol(s) (stdout)", doc->symbol_count);
    isocad_console_echo(console, line);
    return 0;
}

static int command_info(CadDocument *doc, IsocadConsole *console,
                        uint32_t symbol) {
    const CadSymbolRef *ref = cad_symbol_find_const(doc, symbol);
    if (!ref) return cmd_fail(console, "no such symbol");

    const char *name =
        (ref->type && ref->type->name) ? ref->type->name : "?";
    const char *label =
        (ref->type && ref->type->label) ? ref->type->label : "";
    const char *help =
        (ref->type && ref->type->description) ? ref->type->description : "";

    printf("S%u %s \"%s\"\n", ref->id, name, label);
    if (help[0]) printf("  %s\n", help);
    if (!ref->params) {
        printf("  (no parameter copy)\n");
        isocad_console_echo(console, "no parameter copy");
        return -1;
    }
    for (size_t i = 0; i < ref->params->param_count; ++i) {
        print_param_dump(stdout, &ref->params->params[i]);
    }

    char line[ISOCAD_CONSOLE_MAX_INPUT + 1];
    isocad_symbol_summary(ref, line, sizeof(line));
    isocad_console_echo(console, line);
    return 0;
}

static int command_points(CadDocument *doc, IsocadConsole *console,
                          uint32_t symbol) {
    const CadSymbolRef *ref = cad_symbol_find_const(doc, symbol);
    if (!ref) return cmd_fail(console, "no such symbol");

    int index = ref->params ? find_param_index(ref->params, "points") : -1;
    if (index < 0 ||
        ref->params->params[index].desc->kind != ISOGL_SYM_PARAM_POINTS) {
        return cmd_fail(console, "no points");
    }

    const isogl_symbol_param *p = &ref->params->params[index];
    const char *label = p->desc->label ? p->desc->label : "points";
    printf("S%u %s: %zu point(s)\n", ref->id, label, p->count);
    for (size_t i = 0; i < p->count; ++i) {
        printf("  [%zu] (%.12g, %.12g)\n", i,
               p->value.points[i].x, p->value.points[i].y);
    }

    char line[ISOCAD_CONSOLE_MAX_INPUT + 1];
    snprintf(line, sizeof(line), "S%u: %zu point(s) (stdout)",
             ref->id, p->count);
    isocad_console_echo(console, line);
    return 0;
}

static int command_remove(CadDocument *doc, IsocadConsole *console,
                          uint32_t symbol) {
    if (cad_remove_symbol(doc, symbol) != 0) {
        return cmd_fail(console, "remove failed");
    }
    char line[ISOCAD_CONSOLE_MAX_INPUT + 1];
    snprintf(line, sizeof(line), "S%u removed", symbol);
    printf("%s\n", line);
    isocad_console_echo(console, line);
    return 0;
}

static int command_save(CadDocument *doc, IsocadConsole *console,
                        const char *path) {
    int rc = cad_save_symbols_v2(path, doc);
    char line[ISOCAD_CONSOLE_MAX_INPUT + 1];
    if (rc != 0) {
        printf("isocad: save failed: %s\n", path);
        snprintf(line, sizeof(line), "save failed");
        isocad_console_echo(console, line);
        return -1;
    }
    printf("saved %zu symbol(s) to %s\n", doc->symbol_count, path);
    snprintf(line, sizeof(line), "saved %zu symbol(s)", doc->symbol_count);
    isocad_console_echo(console, line);
    return 0;
}

static int set_list_value(IsocadConsole *console,
                          isogl_symbol_params *work, int index,
                          const IsocadCommand *cmd) {
    isogl_symbol_param *p = &work->params[index];
    const char *key = p->desc->key;

    switch (p->desc->kind) {
        case ISOGL_SYM_PARAM_POINTS: {
            if (cmd->arg_count < 3) {
                return cmd_fail(console, "usage: points <i> <x> <y>");
            }
            size_t element = 0;
            double x = 0.0, y = 0.0;
            if (parse_size_token(cmd->arg[0], &element) != 0 ||
                parse_double_token(cmd->arg[1], &x) != 0 ||
                parse_double_token(cmd->arg[2], &y) != 0) {
                return cmd_fail(console, "invalid point value");
            }
            if (element >= p->count) {
                return cmd_fail(console, "point index out of range");
            }
            p->value.points[element].x = x;
            p->value.points[element].y = y;
            printf("S%u %s[%zu] = (%.12g, %.12g)\n",
                   (unsigned)cmd->symbol, key, element, x, y);
            break;
        }
        case ISOGL_SYM_PARAM_TEXTS: {
            if (cmd->arg_count < 1) {
                return cmd_fail(console, "usage: texts <i> <text>");
            }
            size_t element = 0;
            if (parse_size_token(cmd->arg[0], &element) != 0) {
                return cmd_fail(console, "invalid text index");
            }
            if (element >= p->count) {
                return cmd_fail(console, "text index out of range");
            }
            const char *text = cmd->rest_after_first;
            if (isogl_symbol_params_set_text(work, (size_t)index, element,
                                             text) != 0) {
                return cmd_fail(console, "text update failed");
            }
            printf("S%u %s[%zu] = \"%s\"\n",
                   (unsigned)cmd->symbol, key, element, text);
            break;
        }
        case ISOGL_SYM_PARAM_SEGMENTS: {
            if (cmd->arg_count < 5) {
                return cmd_fail(console,
                                "usage: segments <i> <x0> <y0> <x1> <y1>");
            }
            size_t element = 0;
            isogl_symbol_segment s;
            if (parse_size_token(cmd->arg[0], &element) != 0 ||
                parse_double_token(cmd->arg[1], &s.x0) != 0 ||
                parse_double_token(cmd->arg[2], &s.y0) != 0 ||
                parse_double_token(cmd->arg[3], &s.x1) != 0 ||
                parse_double_token(cmd->arg[4], &s.y1) != 0) {
                return cmd_fail(console, "invalid segment value");
            }
            if (element >= p->count) {
                return cmd_fail(console, "segment index out of range");
            }
            p->value.segments[element] = s;
            printf("S%u %s[%zu] = (%.12g, %.12g)-(%.12g, %.12g)\n",
                   (unsigned)cmd->symbol, key, element,
                   s.x0, s.y0, s.x1, s.y1);
            break;
        }
        default:
            return cmd_fail(console, "unsupported list kind");
    }
    return 0;
}

static int set_color_value(IsocadConsole *console, isogl_symbol_param *p,
                           const IsocadCommand *cmd) {
    if (cmd->arg_count < 3) {
        return cmd_fail(console, "usage: color <r> <g> <b> [a]");
    }

    double value[4] = {
        (double)p->value.color[0], (double)p->value.color[1],
        (double)p->value.color[2], (double)p->value.color[3]
    };
    size_t count = cmd->arg_count < 4 ? 3 : 4;
    int use_bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        if (parse_double_token(cmd->arg[i], &value[i]) != 0) {
            return cmd_fail(console, "invalid color value");
        }
        if (value[i] > 1.0 && value[i] <= 255.0) use_bytes = 1;
    }
    if (count == 3) value[3] = (double)p->value.color[3];

    if (use_bytes) {
        for (size_t i = 0; i < count; ++i) value[i] /= 255.0;
    }
    for (size_t i = 0; i < count; ++i) {
        if (value[i] < 0.0 || value[i] > 1.0) {
            return cmd_fail(console, "color out of range");
        }
    }

    for (size_t i = 0; i < 4; ++i) p->value.color[i] = (float)value[i];
    printf("S%u color = [%.12g, %.12g, %.12g, %.12g]\n",
           (unsigned)cmd->symbol, value[0], value[1], value[2], value[3]);
    return 0;
}

static int command_set(CadDocument *doc, IsocadConsole *console,
                       const IsocadCommand *cmd) {
    CadSymbolRef *ref = cad_symbol_find(doc, cmd->symbol);
    if (!ref) return cmd_fail(console, "no such symbol");

    /* Edit a fresh registry copy, not the document copy: a rejected update
       then leaves both the symbol and CadSymbolRef.params untouched. */
    isogl_symbol_params *work = NULL;
    if (isogl_symbol_params_get(ref->id, &work) != 0 || !work) {
        return cmd_fail(console, "params unavailable");
    }

    int index = find_param_index(work, cmd->key);
    if (index < 0) {
        isogl_symbol_params_free(work);
        return cmd_fail(console, "unknown key");
    }

    isogl_symbol_param *p = &work->params[index];
    int rc = 0;

    if (!p->desc->editable) {
        rc = cmd_fail(console, "read-only parameter");
    } else if (p->desc->kind == ISOGL_SYM_PARAM_POINTS ||
               p->desc->kind == ISOGL_SYM_PARAM_TEXTS ||
               p->desc->kind == ISOGL_SYM_PARAM_SEGMENTS) {
        rc = set_list_value(console, work, index, cmd);
    } else {
        switch (p->desc->kind) {
            case ISOGL_SYM_PARAM_NUMBER: {
                double value = 0.0;
                if (cmd->arg_count < 1) {
                    rc = cmd_fail(console, "missing number");
                } else if (parse_double_token(cmd->arg[0], &value) != 0) {
                    rc = cmd_fail(console, "invalid number");
                } else {
                    p->value.number = value;
                    printf("S%u %s = %.12g\n", (unsigned)cmd->symbol,
                           p->desc->key, value);
                }
                break;
            }
            case ISOGL_SYM_PARAM_INTEGER: {
                long value = 0;
                if (cmd->arg_count < 1) {
                    rc = cmd_fail(console, "missing integer");
                } else if (parse_long_token(cmd->arg[0], &value) != 0 ||
                           value < INT_MIN || value > INT_MAX) {
                    rc = cmd_fail(console, "invalid integer");
                } else {
                    p->value.integer = (int)value;
                    printf("S%u %s = %ld\n", (unsigned)cmd->symbol,
                           p->desc->key, value);
                }
                break;
            }
            case ISOGL_SYM_PARAM_ENUM: {
                int value = 0;
                if (cmd->arg_count < 1) {
                    rc = cmd_fail(console, "missing enum value");
                    break;
                }
                if (isogl_symbol_param_enum_value(p->desc, cmd->arg[0],
                                                  &value) != 0) {
                    /* Raw integers stay accepted for scripting, but only
                       values present in the descriptor table. */
                    long raw = 0;
                    if (parse_long_token(cmd->arg[0], &raw) != 0 ||
                        raw < INT_MIN || raw > INT_MAX) {
                        rc = cmd_fail(console, "unknown enum value");
                        break;
                    }
                    value = (int)raw;
                }
                const char *label =
                    isogl_symbol_param_enum_label(p->desc, value);
                if (!label) {
                    rc = cmd_fail(console, "unknown enum value");
                    break;
                }
                p->value.integer = value;
                printf("S%u %s = %s (%d)\n", (unsigned)cmd->symbol,
                       p->desc->key, label, value);
                break;
            }
            case ISOGL_SYM_PARAM_BOOLEAN: {
                int value = 0;
                if (cmd->arg_count < 1) {
                    rc = cmd_fail(console, "missing boolean");
                } else if (parse_bool_token(cmd->arg[0], &value) != 0) {
                    rc = cmd_fail(console, "invalid boolean");
                } else {
                    p->value.boolean = value;
                    printf("S%u %s = %s\n", (unsigned)cmd->symbol,
                           p->desc->key, value ? "true" : "false");
                }
                break;
            }
            case ISOGL_SYM_PARAM_COLOR:
                rc = set_color_value(console, p, cmd);
                break;
            default:
                rc = cmd_fail(console, "read-only parameter");
                break;
        }
    }

    if (rc == 0) {
        if (cad_symbol_update_params(doc, ref->id, work) != 0) {
            rc = cmd_fail(console, "update failed (invalid value)");
        } else {
            char line[ISOCAD_CONSOLE_MAX_INPUT + 1];
            snprintf(line, sizeof(line), "S%u %s updated", (unsigned)ref->id,
                     cmd->key);
            isocad_console_echo(console, line);
        }
    }

    isogl_symbol_params_free(work);
    return rc;
}

int isocad_command_execute(CadDocument *doc, IsocadConsole *console,
                           const IsocadCommand *cmd) {
    if (!doc || !console || !cmd) return -1;

    switch (cmd->kind) {
        case ISOCAD_CMD_SYM_LIST:
            return command_list(doc, console);
        case ISOCAD_CMD_SYM_INFO:
            return command_info(doc, console, cmd->symbol);
        case ISOCAD_CMD_SYM_SET:
            return command_set(doc, console, cmd);
        case ISOCAD_CMD_SYM_POINTS:
            return command_points(doc, console, cmd->symbol);
        case ISOCAD_CMD_SYM_REMOVE:
            return command_remove(doc, console, cmd->symbol);
        case ISOCAD_CMD_SAVE:
            return command_save(doc, console, cmd->path);
        case ISOCAD_CMD_NONE:
        default:
            return -1;
    }
}
