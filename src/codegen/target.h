#ifndef TARGET_H
#define TARGET_H

#include "ir.h"
#include "regalloc_linear.h"

typedef struct {
    const char* name;
    int reg_count;
    const char** reg_names;
    
    void (*init)(void);
    void (*prologue)(void);
    void (*epilogue)(void);
    void (*gen_ins)(IRIns* ins, LinearRegAlloc* ra, FILE* out);
    void (*gen_label)(int label, FILE* out);
    void (*finish)(FILE* out);
    
    void (*gen_global)(const char* name, int size, const char* init, FILE* out);
    void (*gen_section_global)(const char* section, const char* name, int size, const char* init, FILE* out);
    void (*gen_string)(const char* str, int label, FILE* out);
} Target;

extern Target target_json;

#endif
