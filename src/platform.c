#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "platform.h"

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

int platform_assemble(const char* asm_file, const char* out,
                      const char* fmt, int raw) {
    char cmd[1024];
    
    if (raw) {
        snprintf(cmd, sizeof(cmd), "nasm -f bin %s -o %s", asm_file, out);
    } else if (strcmp(fmt, "uefi") == 0) {
        snprintf(cmd, sizeof(cmd),
                 "nasm -f win64 %s -o %s.obj && "
                 "ld -m elf_x86_64 %s.obj -o %s --subsystem=efi_application",
                 asm_file, out, out, out);
    } else {
        snprintf(cmd, sizeof(cmd), "nasm -f elf64 %s -o %s.o && ld %s.o -o %s",
                 asm_file, out, out, out);
    }
    
    int ret = system(cmd);
    
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
