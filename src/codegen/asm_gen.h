#ifndef ASM_GEN_H
#define ASM_GEN_H

#include "../ast.h"
#include "ir.h"
#include "regalloc_linear.h"

typedef struct {
    FILE* out;
    int bits;           // 16, 32, 64
    int safe_code;      // 0 = bare metal, 1 = OS mode
    int optimize_level; // 0-2
    int current_label;
} AsmContext;

void asm_gen_init(AsmContext* ctx, const char* filename, int bits, int safe_code, int optimize);
void asm_gen_program(AsmContext* ctx, IRProgram* prog, LinearRegAlloc* ra);
void asm_gen_finish(AsmContext* ctx);

#endif
