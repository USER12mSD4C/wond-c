#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "platform.h"
#include "target_spec.h"

// verbose объявлен как extern в platform.h, не определяем его здесь

void platform_init(void) {}
void platform_cleanup(void) {}

void* platform_malloc(size_t sz) { return malloc(sz); }
void* platform_realloc(void* p, size_t sz) { return realloc(p, sz); }
void platform_free(void* p) { free(p); }

char* platform_strdup(const char* s) {
    if (!s) return NULL;
    size_t l = strlen(s);
    char* d = malloc(l + 1);
    if (d) memcpy(d, s, l + 1);
    return d;
}

char* platform_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    return buf;
}

int platform_file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int platform_remove(const char* path) {
    return remove(path);
}

void platform_eprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void platform_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

const char* platform_get_home(void) {
    return getenv("HOME");
}

static char* replace_all(const char* in, const char* key, const char* value) {
    const char* p;
    size_t in_len, key_len, val_len, count, out_len;
    char* out;
    char* w;

    if (!in || !key || !value) return NULL;
    in_len = strlen(in);
    key_len = strlen(key);
    val_len = strlen(value);
    if (key_len == 0) return platform_strdup(in);

    count = 0;
    p = in;
    while ((p = strstr(p, key)) != NULL) {
        count++;
        p += key_len;
    }

    out_len = in_len + count * (val_len - key_len);
    out = (char*)malloc(out_len + 1);
    if (!out) return NULL;

    p = in;
    w = out;
    while (*p) {
        if (strncmp(p, key, key_len) == 0) {
            memcpy(w, value, val_len);
            w += val_len;
            p += key_len;
        } else {
            *w++ = *p++;
        }
    }
    *w = '\0';
    return out;
}

static char* expand_cmd_template(const char* tmpl, const char* asm_file, const char* out,
                                 const char* obj, const char* objs, const char* fmt,
                                 const char* nasm_format, int safe_code) {
    char* s1;
    char* s2;
    char* s3;
    char* s4;
    char* s5;
    char* s6;
    char* s7;
    char safe_buf[16];
    if (!tmpl) return NULL;
    s1 = replace_all(tmpl, "{asm}", asm_file ? asm_file : "");
    s2 = replace_all(s1 ? s1 : tmpl, "{out}", out ? out : "");
    if (s1) free(s1);
    s3 = replace_all(s2 ? s2 : tmpl, "{obj}", obj ? obj : "");
    if (s2) free(s2);
    s4 = replace_all(s3 ? s3 : tmpl, "{fmt}", fmt ? fmt : "");
    if (s3) free(s3);
    s5 = replace_all(s4 ? s4 : tmpl, "{nasm_format}", nasm_format ? nasm_format : "elf64");
    if (s4) free(s4);
    snprintf(safe_buf, sizeof(safe_buf), "%d", safe_code ? 1 : 0);
    s6 = replace_all(s5 ? s5 : tmpl, "{safe}", safe_buf);
    if (s5) free(s5);
    s7 = replace_all(s6 ? s6 : tmpl, "{objs}", objs ? objs : (obj ? obj : ""));
    if (s6) free(s6);
    return s7;
}

static char* build_obj_list(const char** obj_files, int obj_count) {
    size_t total = 1;
    char* out;
    char* p;
    if (!obj_files || obj_count <= 0) return NULL;
    for (int i = 0; i < obj_count; i++) {
        if (obj_files[i]) total += strlen(obj_files[i]) + 1;
    }
    out = (char*)malloc(total);
    if (!out) return NULL;
    p = out;
    *p = '\0';
    for (int i = 0; i < obj_count; i++) {
        if (!obj_files[i]) continue;
        if (p != out) *p++ = ' ';
        strcpy(p, obj_files[i]);
        p += strlen(obj_files[i]);
    }
    *p = '\0';
    return out;
}

int platform_assemble_to_object(const char* asm_file, const char* obj_file,
                                const char* fmt, int safe_code,
                                const TargetSpec* spec) {
    char cmd[1024];
    const char* nasm_fmt;
    char* asm_cmd;
    int ret;
    (void)safe_code;

    nasm_fmt = (spec && spec->nasm_format && *spec->nasm_format)
        ? spec->nasm_format : "elf64";

    if (spec && spec->assembler_cmd && *spec->assembler_cmd) {
        asm_cmd = expand_cmd_template(spec->assembler_cmd, asm_file, obj_file, obj_file, obj_file,
                                      fmt, nasm_fmt, safe_code);
        if (!asm_cmd) return 1;
        ret = system(asm_cmd);
        free(asm_cmd);
        return ret;
    }

    snprintf(cmd, sizeof(cmd), "nasm -f %s %s -o %s", nasm_fmt, asm_file, obj_file);
    return system(cmd);
}

int platform_link_objects(const char** obj_files, int obj_count,
                          const char* out, const char* fmt,
                          int safe_code, const TargetSpec* spec) {
    char cmd[4096];
    char* obj_list;
    int ret;

    obj_list = build_obj_list(obj_files, obj_count);
    if (!obj_list) return 1;

    if (spec && spec->linker_cmd && *spec->linker_cmd) {
        char* link_cmd = expand_cmd_template(spec->linker_cmd, "", out,
                                             obj_list,
                                             obj_list, fmt,
                                             spec->nasm_format ? spec->nasm_format : "elf64",
                                             safe_code);
        if (!link_cmd) {
            free(obj_list);
            return 1;
        }
        ret = system(link_cmd);
        free(link_cmd);
        free(obj_list);
        return ret;
    }

    if (strcmp(fmt, "uefi") == 0) {
        snprintf(cmd, sizeof(cmd),
                 "ld -m elf_x86_64 %s -o %s --subsystem=efi_application",
                 obj_list, out);
    } else if (safe_code) {
        snprintf(cmd, sizeof(cmd), "gcc -no-pie %s -o %s", obj_list, out);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "ld %s -o %s -lc -dynamic-linker /lib64/ld-linux-x86-64.so.2",
                 obj_list, out);
    }

    ret = system(cmd);
    free(obj_list);
    return ret;
}

int platform_assemble(const char* asm_file, const char* out,
                      const char* fmt, int raw, int safe_code,
                      const TargetSpec* spec) {
    char cmd[1024];
    int ret;

    if (spec && spec->assembler_cmd && *spec->assembler_cmd) {
        char obj[512];
        char* asm_cmd = NULL;
        char* link_cmd = NULL;
        int ret_asm;
        snprintf(obj, sizeof(obj), "%s.o", out);

        asm_cmd = expand_cmd_template(spec->assembler_cmd, asm_file, out, obj, obj,
                                      fmt, spec->nasm_format ? spec->nasm_format : "elf64",
                                      safe_code);
        if (!asm_cmd) return 1;
        ret_asm = system(asm_cmd);
        free(asm_cmd);
        if (ret_asm != 0) return ret_asm;

        if (spec->linker_cmd && *spec->linker_cmd) {
            link_cmd = expand_cmd_template(spec->linker_cmd, asm_file, out, obj, obj,
                                           fmt, spec->nasm_format ? spec->nasm_format : "elf64",
                                           safe_code);
            if (!link_cmd) return 1;
            ret = system(link_cmd);
            free(link_cmd);
            remove(obj);
            return ret;
        }
        return 0;
    }

    if (raw) {
        snprintf(cmd, sizeof(cmd), "nasm -f bin %s -o %s", asm_file, out);
    } else if (strcmp(fmt, "uefi") == 0) {
        snprintf(cmd, sizeof(cmd),
                 "nasm -f win64 %s -o %s.obj && "
                 "ld -m elf_x86_64 %s.obj -o %s --subsystem=efi_application",
                 asm_file, out, out, out);
    } else if (safe_code) {
        snprintf(cmd, sizeof(cmd),
                 "nasm -f elf64 %s -o %s.o && gcc -no-pie %s.o -o %s",
                 asm_file, out, out, out);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "nasm -f elf64 %s -o %s.o && ld %s.o -o %s -lc -dynamic-linker /lib64/ld-linux-x86-64.so.2",
                 asm_file, out, out, out);
    }

    ret = system(cmd);

    if (!raw && strcmp(fmt, "uefi") != 0) {
        char obj[512];
        snprintf(obj, sizeof(obj), "%s.o", out);
        remove(obj);
    } else if (strcmp(fmt, "uefi") == 0) {
        char obj[512];
        snprintf(obj, sizeof(obj), "%s.obj", out);
        remove(obj);
    }
    
    return ret;
}
