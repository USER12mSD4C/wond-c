#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "target_spec.h"
#include "platform.h"

static char* ts_strdup(const char* s) {
    return s ? platform_strdup(s) : NULL;
}

static int has_path_sep(const char* s) {
    if (!s) return 0;
    return strchr(s, '/') != NULL || strchr(s, '\\') != NULL;
}

static int ends_with(const char* s, const char* suffix) {
    size_t ls, lf;
    if (!s || !suffix) return 0;
    ls = strlen(s);
    lf = strlen(suffix);
    if (lf > ls) return 0;
    return strcmp(s + ls - lf, suffix) == 0;
}

static char* join_path(const char* a, const char* b) {
    size_t la, lb;
    char* out;
    if (!a || !b) return NULL;
    la = strlen(a);
    lb = strlen(b);
    out = (char*)platform_malloc(la + lb + 2);
    if (!out) return NULL;
    memcpy(out, a, la);
    if (la > 0 && a[la - 1] != '/' && a[la - 1] != '\\') {
        out[la++] = '/';
    }
    memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

static char* expand_escapes(const char* s) {
    size_t n, i, j;
    char* out;
    if (!s) return ts_strdup("");
    n = strlen(s);
    out = (char*)platform_malloc(n + 1);
    if (!out) return NULL;
    for (i = 0, j = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            i++;
            switch (s[i]) {
                case 'n': out[j++] = '\n'; break;
                case 'r': out[j++] = '\r'; break;
                case 't': out[j++] = '\t'; break;
                case '\\': out[j++] = '\\'; break;
                default:
                    out[j++] = s[i];
                    break;
            }
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = '\0';
    return out;
}

static char* trim_ws(char* s) {
    char* end;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static void target_spec_setkv(TargetSpec* spec, const char* key, const char* value) {
    int i;
    if (!spec || !key || !value) return;
    for (i = 0; i < spec->kv_count; i++) {
        if (strcmp(spec->kvs[i].key, key) == 0) {
            platform_free(spec->kvs[i].value);
            spec->kvs[i].value = ts_strdup(value);
            return;
        }
    }
    if (spec->kv_count >= spec->kv_cap) {
        spec->kv_cap = spec->kv_cap ? spec->kv_cap * 2 : 32;
        spec->kvs = (TargetKV*)platform_realloc(spec->kvs, spec->kv_cap * sizeof(TargetKV));
    }
    spec->kvs[spec->kv_count].key = ts_strdup(key);
    spec->kvs[spec->kv_count].value = ts_strdup(value);
    spec->kv_count++;
}

const char* target_spec_get(const TargetSpec* spec, const char* key) {
    int i;
    if (!spec || !key) return NULL;
    for (i = 0; i < spec->kv_count; i++) {
        if (strcmp(spec->kvs[i].key, key) == 0) {
            return spec->kvs[i].value;
        }
    }
    return NULL;
}

static char* find_target_path(const char* name_or_path) {
    static const char* dirs_static[] = {
        "./targets",
        ".",
        "/usr/local/lib/wandc/targets",
        "/usr/lib/wandc/targets"
    };
    char* p;
    char* cand;
    char* cand2;
    const char* home;
    char* home_targets = NULL;
    int i;

    if (!name_or_path || !*name_or_path) return NULL;

    if (has_path_sep(name_or_path) || ends_with(name_or_path, ".wtarget")) {
        if (platform_file_exists(name_or_path)) {
            return ts_strdup(name_or_path);
        }
        return NULL;
    }

    home = platform_get_home();
    if (home) {
        home_targets = (char*)platform_malloc(strlen(home) + 32);
        sprintf(home_targets, "%s/.wandc/targets", home);
    }

    for (i = 0; i < (int)(sizeof(dirs_static) / sizeof(dirs_static[0])); i++) {
        p = join_path(dirs_static[i], name_or_path);
        if (platform_file_exists(p)) {
            return p;
        }
        cand = (char*)platform_malloc(strlen(name_or_path) + 9);
        sprintf(cand, "%s.wtarget", name_or_path);
        cand2 = join_path(dirs_static[i], cand);
        platform_free(cand);
        platform_free(p);
        if (platform_file_exists(cand2)) {
            return cand2;
        }
        platform_free(cand2);
    }

    if (home_targets) {
        p = join_path(home_targets, name_or_path);
        if (platform_file_exists(p)) {
            platform_free(home_targets);
            return p;
        }
        cand = (char*)platform_malloc(strlen(name_or_path) + 9);
        sprintf(cand, "%s.wtarget", name_or_path);
        cand2 = join_path(home_targets, cand);
        platform_free(cand);
        platform_free(p);
        if (platform_file_exists(cand2)) {
            platform_free(home_targets);
            return cand2;
        }
        platform_free(cand2);
        platform_free(home_targets);
    }

    return NULL;
}

TargetSpec* target_spec_load(const char* name_or_path) {
    TargetSpec* spec;
    char* path;
    FILE* f;
    char line_buf[4096];

    path = find_target_path(name_or_path);
    if (!path) return NULL;

    f = fopen(path, "rb");
    if (!f) {
        platform_free(path);
        return NULL;
    }

    spec = (TargetSpec*)platform_malloc(sizeof(TargetSpec));
    memset(spec, 0, sizeof(TargetSpec));
    spec->bits = 64;
    spec->format = ts_strdup("elf");
    spec->nasm_format = ts_strdup("elf64");
    spec->source_path = path;

    while (fgets(line_buf, sizeof(line_buf), f)) {
        char* line = trim_ws(line_buf);
        char* eq;
        char* key;
        char* val;
        char* decoded;

        if (*line == '\0' || *line == '#' || *line == ';') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        key = trim_ws(line);
        val = trim_ws(eq + 1);
        decoded = expand_escapes(val);
        target_spec_setkv(spec, key, decoded ? decoded : "");

        if (strcmp(key, "name") == 0) {
            if (spec->name) platform_free(spec->name);
            spec->name = ts_strdup(decoded);
        } else if (strcmp(key, "bits") == 0) {
            spec->bits = atoi(decoded);
            if (spec->bits != 16 && spec->bits != 32 && spec->bits != 64) spec->bits = 64;
        } else if (strcmp(key, "format") == 0) {
            if (spec->format) platform_free(spec->format);
            spec->format = ts_strdup(decoded);
        } else if (strcmp(key, "nasm_format") == 0) {
            if (spec->nasm_format) platform_free(spec->nasm_format);
            spec->nasm_format = ts_strdup(decoded);
        } else if (strcmp(key, "assembler") == 0) {
            if (spec->assembler_cmd) platform_free(spec->assembler_cmd);
            spec->assembler_cmd = ts_strdup(decoded);
        } else if (strcmp(key, "linker") == 0) {
            if (spec->linker_cmd) platform_free(spec->linker_cmd);
            spec->linker_cmd = ts_strdup(decoded);
        }
        if (decoded) platform_free(decoded);
    }

    fclose(f);

    if (!spec->name) {
        const char* slash = strrchr(path, '/');
        const char* bslash = strrchr(path, '\\');
        const char* base = slash ? slash + 1 : path;
        if (bslash && bslash + 1 > base) base = bslash + 1;
        spec->name = ts_strdup(base);
    }

    return spec;
}

TargetSpec* target_spec_load_default(void) {
    return target_spec_load("default");
}

void target_spec_free(TargetSpec* spec) {
    int i;
    if (!spec) return;
    if (spec->name) platform_free(spec->name);
    if (spec->format) platform_free(spec->format);
    if (spec->nasm_format) platform_free(spec->nasm_format);
    if (spec->assembler_cmd) platform_free(spec->assembler_cmd);
    if (spec->linker_cmd) platform_free(spec->linker_cmd);
    if (spec->source_path) platform_free(spec->source_path);
    for (i = 0; i < spec->kv_count; i++) {
        if (spec->kvs[i].key) platform_free(spec->kvs[i].key);
        if (spec->kvs[i].value) platform_free(spec->kvs[i].value);
    }
    if (spec->kvs) platform_free(spec->kvs);
    platform_free(spec);
}
