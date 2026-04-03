#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "preproc.h"
#include "platform.h"

typedef struct Macro {
    char* name;
    char* val;
    struct Macro* next;
} Macro;

static Macro* macros = NULL;
static int in_if = 0;
static int skip_mode = 0;

static void clear_macros(void) {
    Macro* m = macros;
    while (m) {
        Macro* next = m->next;
        free(m->name);
        free(m->val);
        free(m);
        m = next;
    }
    macros = NULL;
}

static void add_macro(const char* name, const char* val) {
    Macro* m = macros;
    while (m) {
        if (strcmp(m->name, name) == 0) {
            free(m->val);
            m->val = val ? strdup(val) : strdup("");
            return;
        }
        m = m->next;
    }
    m = malloc(sizeof(Macro));
    m->name = strdup(name);
    m->val = val ? strdup(val) : strdup("");
    m->next = macros;
    macros = m;
}

static void undef_macro(const char* name) {
    Macro* prev = NULL;
    Macro* m = macros;
    while (m) {
        if (strcmp(m->name, name) == 0) {
            if (prev) prev->next = m->next;
            else macros = m->next;
            free(m->name);
            free(m->val);
            free(m);
            return;
        }
        prev = m;
        m = m->next;
    }
}

static Macro* find_macro(const char* name) {
    Macro* m = macros;
    while (m) {
        if (strcmp(m->name, name) == 0) return m;
        m = m->next;
    }
    return NULL;
}

static int is_id_char(char c) {
    return isalnum(c) || c == '_';
}

static char* expand_macros(const char* line) {
    char* res = malloc(strlen(line) + 1024);
    char* r = res;
    const char* p = line;
    
    while (*p) {
        if (is_id_char(*p)) {
            const char* start = p;
            while (is_id_char(*p)) p++;
            int len = p - start;
            char* name = malloc(len + 1);
            memcpy(name, start, len);
            name[len] = 0;
            
            Macro* m = find_macro(name);
            if (m) {
                strcpy(r, m->val);
                r += strlen(m->val);
            } else {
                memcpy(r, start, len);
                r += len;
            }
            free(name);
        } else {
            *r++ = *p++;
        }
    }
    *r = 0;
    return res;
}

PreprocOutput* preprocess(const char* src, const char* fname, void* paths) {
    (void)fname;
    (void)paths;

    clear_macros();
    in_if = 0;
    skip_mode = 0;

    PreprocOutput* out = malloc(sizeof(PreprocOutput));
    out->out = malloc(strlen(src) + 1);
    out->out[0] = 0;
    out->len = 0;

    char* src_copy = strdup(src);
    char* line = src_copy;
    char* next;

    while (line) {
        next = strchr(line, '\n');
        if (next) *next = 0;

        if (*line == '\0' && next == NULL) {
            break;
        }

        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        if (*trimmed == '#' && !skip_mode) {
            trimmed++;
            while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

            if (strncmp(trimmed, "define", 6) == 0 && !is_id_char(trimmed[6])) {
                trimmed += 6;
                while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

                const char* name_start = trimmed;
                while (is_id_char(*trimmed)) trimmed++;
                int name_len = trimmed - name_start;
                char* name = malloc(name_len + 1);
                memcpy(name, name_start, name_len);
                name[name_len] = 0;

                while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
                add_macro(name, trimmed);
                free(name);
            }
            else if (strncmp(trimmed, "undef", 5) == 0 && !is_id_char(trimmed[5])) {
                trimmed += 5;
                while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
                const char* name_start = trimmed;
                while (is_id_char(*trimmed)) trimmed++;
                int name_len = trimmed - name_start;
                char* name = malloc(name_len + 1);
                memcpy(name, name_start, name_len);
                name[name_len] = 0;
                undef_macro(name);
                free(name);
            }
            else if (strncmp(trimmed, "ifdef", 5) == 0 && !is_id_char(trimmed[5])) {
                trimmed += 5;
                while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
                const char* name_start = trimmed;
                while (is_id_char(*trimmed)) trimmed++;
                int name_len = trimmed - name_start;
                char* name = malloc(name_len + 1);
                memcpy(name, name_start, name_len);
                name[name_len] = 0;

                int defined = (find_macro(name) != NULL);
                if (!defined) skip_mode = 1;
                in_if++;
                free(name);
            }
            else if (strncmp(trimmed, "ifndef", 6) == 0 && !is_id_char(trimmed[6])) {
                trimmed += 6;
                while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
                const char* name_start = trimmed;
                while (is_id_char(*trimmed)) trimmed++;
                int name_len = trimmed - name_start;
                char* name = malloc(name_len + 1);
                memcpy(name, name_start, name_len);
                name[name_len] = 0;

                int defined = (find_macro(name) != NULL);
                if (defined) skip_mode = 1;
                in_if++;
                free(name);
            }
            else if (strncmp(trimmed, "else", 4) == 0 && !is_id_char(trimmed[4])) {
                if (in_if) skip_mode = !skip_mode;
            }
            else if (strncmp(trimmed, "endif", 5) == 0 && !is_id_char(trimmed[5])) {
                if (in_if) {
                    in_if--;
                    if (in_if == 0) skip_mode = 0;
                }
            }
        }
        else if (!skip_mode) {
            char* expanded = expand_macros(line);
            int l = strlen(expanded);
            out->out = realloc(out->out, out->len + l + 2);
            strcpy(out->out + out->len, expanded);
            out->len += l;
            strcat(out->out + out->len, "\n");
            out->len++;
            free(expanded);
        }

        if (!next) {
            break;
        }
        line = next + 1;
    }

    free(src_copy);
    clear_macros();
    in_if = 0;
    skip_mode = 0;
    return out;
}

void preproc_free(PreprocOutput* pp) {
    if (pp) {
        if (pp->out) free(pp->out);
        free(pp);
    }
}
