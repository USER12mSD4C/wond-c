#ifndef TARGET_SPEC_H
#define TARGET_SPEC_H

typedef struct {
    char* key;
    char* value;
} TargetKV;

typedef struct TargetSpec {
    char* name;
    int bits;
    char* format;
    char* nasm_format;
    char* assembler_cmd;
    char* linker_cmd;
    TargetKV* kvs;
    int kv_count;
    int kv_cap;
    char* source_path;
} TargetSpec;

TargetSpec* target_spec_load(const char* name_or_path);
TargetSpec* target_spec_load_default(void);
void target_spec_free(TargetSpec* spec);
const char* target_spec_get(const TargetSpec* spec, const char* key);

#endif
