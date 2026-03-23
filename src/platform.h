#ifndef PLATFORM_H
#define PLATFORM_H

void platform_init(void);
void platform_cleanup(void);

void* platform_malloc(size_t sz);
void* platform_realloc(void* p, size_t sz);
void platform_free(void* p);
char* platform_strdup(const char* s);

char* platform_read_file(const char* path);
int platform_file_exists(const char* path);
int platform_remove(const char* path);

void platform_eprintf(const char* fmt, ...);
void platform_printf(const char* fmt, ...);

const char* platform_get_home(void);

int platform_assemble(const char* asm_file, const char* out,
                      const char* fmt, int raw);

#endif
