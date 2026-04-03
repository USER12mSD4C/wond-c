#ifndef PLATFORM_H
#define PLATFORM_H

extern int verbose;
typedef struct TargetSpec TargetSpec;

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
                      const char* fmt, int raw, int safe_code,
                      const TargetSpec* spec);
int platform_assemble_to_object(const char* asm_file, const char* obj_file,
                                const char* fmt, int safe_code,
                                const TargetSpec* spec);
int platform_link_objects(const char** obj_files, int obj_count,
                          const char* out, const char* fmt,
                          int safe_code, const TargetSpec* spec);

#endif
