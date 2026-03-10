#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "ast.h"

typedef struct {
    FILE* out;
    int label_counter;
    int safe_code;        // 0 = bare metal, 1 = OS mode
    int target_raw;
    int target_uefi;
    int opt_level;
    int bit_mode;
    char* adrload;        // текущий adrload адрес
    int current_bits;      // текущая разрядность (16/32/64)
} CodeGen;

void codegen_init(CodeGen* cg, FILE* out, int safe_code, int target_raw);
void codegen_generate(CodeGen* cg, AstNode* ast);
void codegen_free(CodeGen* cg);

#endif
