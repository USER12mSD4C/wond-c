#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "asm_gen.h"
#include "../platform.h"

extern int verbose;

static const char* regs_64[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static const char* regs_32[] = {
    "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"
};

static const char* regs_16[] = {
    "ax", "cx", "dx", "bx", "sp", "bp", "si", "di"
};

typedef struct {
    char* name;
    int offset;
    int size;
    int is_global;
    int init_value;
    char* init_string;
} VarInfo;

static VarInfo* vars = NULL;
static int var_count = 0;
static int var_cap = 0;
static char* code_buffer = NULL;
static int code_len = 0;
static int code_cap = 0;

static void code_append(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args) + 1;
    va_end(args);
    
    if (code_len + needed >= code_cap) {
        code_cap = code_cap ? code_cap * 2 : 65536;
        code_buffer = realloc(code_buffer, code_cap);
    }
    
    va_start(args, fmt);
    vsprintf(code_buffer + code_len, fmt, args);
    code_len += needed - 1;
    va_end(args);
}

static void add_var(const char* name, int is_global, int init_value, const char* init_string) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) return;
    }
    
    if (var_count >= var_cap) {
        var_cap = var_cap ? var_cap * 2 : 64;
        vars = realloc(vars, var_cap * sizeof(VarInfo));
    }
    
    vars[var_count].name = strdup(name);
    vars[var_count].offset = 0;
    vars[var_count].size = 8;
    vars[var_count].is_global = is_global;
    vars[var_count].init_value = init_value;
    vars[var_count].init_string = init_string ? strdup(init_string) : NULL;
    var_count++;
}

static void free_vars(void) {
    for (int i = 0; i < var_count; i++) {
        free(vars[i].name);
        if (vars[i].init_string) free(vars[i].init_string);
    }
    free(vars);
    vars = NULL;
    var_count = 0;
    var_cap = 0;
    if (code_buffer) {
        free(code_buffer);
        code_buffer = NULL;
    }
    code_len = 0;
    code_cap = 0;
}

static const char* get_reg(AsmContext* ctx, int reg_idx) {
    if (ctx->bits == 64) return regs_64[reg_idx];
    if (ctx->bits == 32) return regs_32[reg_idx];
    return regs_16[reg_idx];
}

static void emit(AsmContext* ctx, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->out, fmt, args);
    va_end(args);
    fprintf(ctx->out, "\n");
}

static void emit_ins(AsmContext* ctx, const char* ins, const char* args) {
    fprintf(ctx->out, "    %-8s %s\n", ins, args);
}

void asm_gen_init(AsmContext* ctx, const char* filename, int bits, int safe_code, int optimize) {
    ctx->out = fopen(filename, "w");
    ctx->bits = bits;
    ctx->safe_code = safe_code;
    ctx->optimize_level = optimize;
    ctx->current_label = 0;
    
    if (!ctx->out) {
        platform_eprintf("Cannot create %s\n", filename);
        return;
    }
    
    code_buffer = NULL;
    code_len = 0;
    code_cap = 0;
    vars = NULL;
    var_count = 0;
    var_cap = 0;
    
    if (bits == 64) {
        fprintf(ctx->out, "bits 64\n");
        fprintf(ctx->out, "default rel\n\n");
    } else if (bits == 32) {
        fprintf(ctx->out, "bits 32\n\n");
    } else {
        fprintf(ctx->out, "bits 16\n\n");
    }
}

static void gen_ins_64(AsmContext* ctx, IRIns* ins, LinearRegAlloc* ra) {
    int src_reg, src2_reg, dest_reg;
    int spill, spill2;
    
    switch (ins->op) {
        case IR_LABEL:
            if (ins->var_name) {
                code_append("%s:\n", ins->var_name);
            } else {
                code_append(".L%d:\n", ins->label);
            }
            break;
            
        case IR_CONST:
            dest_reg = linear_get_reg(ra, ins->dest);
            if (dest_reg >= 0) {
                code_append("    mov %s, %d\n", regs_64[dest_reg], ins->const_val);
            } else {
                spill = linear_get_spill(ra, ins->dest);
                if (spill >= 0) {
                    code_append("    mov rax, %d\n", ins->const_val);
                    code_append("    mov [rbp-%d], rax\n", (spill + 1) * 8);
                }
            }
            break;
            
        case IR_GLOBAL:
            if (ins->var_name) {
                add_var(ins->var_name, 1, 0, NULL);
                if (ins->dest >= 0) {
                    dest_reg = linear_get_reg(ra, ins->dest);
                    if (dest_reg >= 0) {
                        code_append("    mov %s, [rel %s]\n", regs_64[dest_reg], ins->var_name);
                    } else {
                        spill = linear_get_spill(ra, ins->dest);
                        if (spill >= 0) {
                            code_append("    mov rax, [rel %s]\n", ins->var_name);
                            code_append("    mov [rbp-%d], rax\n", (spill + 1) * 8);
                        }
                    }
                }
            }
            break;
            
        case IR_STORE:
            if (ins->var_name) {
                add_var(ins->var_name, 1, 0, NULL);
                src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    code_append("    mov [rel %s], %s\n", ins->var_name, regs_64[src_reg]);
                } else {
                    spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        code_append("    mov rax, [rbp-%d]\n", (spill + 1) * 8);
                        code_append("    mov [rel %s], rax\n", ins->var_name);
                    }
                }
            }
            break;
            
        case IR_LOAD:
            if (ins->var_name) {
                if (ins->dest >= 0) {
                    dest_reg = linear_get_reg(ra, ins->dest);
                    if (dest_reg >= 0) {
                        code_append("    mov %s, [rel %s]\n", regs_64[dest_reg], ins->var_name);
                    } else {
                        spill = linear_get_spill(ra, ins->dest);
                        if (spill >= 0) {
                            code_append("    mov rax, [rel %s]\n", ins->var_name);
                            code_append("    mov [rbp-%d], rax\n", (spill + 1) * 8);
                        }
                    }
                }
            }
            break;
            
        case IR_MOV:
            src_reg = linear_get_reg(ra, ins->src1);
            dest_reg = linear_get_reg(ra, ins->dest);
            if (src_reg >= 0 && dest_reg >= 0) {
                if (src_reg != dest_reg) {
                    code_append("    mov %s, %s\n", regs_64[dest_reg], regs_64[src_reg]);
                }
            } else if (src_reg >= 0 && dest_reg < 0) {
                spill = linear_get_spill(ra, ins->dest);
                if (spill >= 0) {
                    code_append("    mov [rbp-%d], %s\n", (spill + 1) * 8, regs_64[src_reg]);
                }
            } else if (src_reg < 0 && dest_reg >= 0) {
                spill = linear_get_spill(ra, ins->src1);
                if (spill >= 0) {
                    code_append("    mov %s, [rbp-%d]\n", regs_64[dest_reg], (spill + 1) * 8);
                }
            }
            break;
            
        case IR_ADD:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    code_append("    mov %s, %s\n", regs_64[dest_reg], regs_64[src_reg]);
                }
                code_append("    add %s, %s\n", regs_64[dest_reg], regs_64[src2_reg]);
            }
            break;
            
        case IR_SUB:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    code_append("    mov %s, %s\n", regs_64[dest_reg], regs_64[src_reg]);
                }
                code_append("    sub %s, %s\n", regs_64[dest_reg], regs_64[src2_reg]);
            }
            break;
            
        case IR_MUL:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    code_append("    mov %s, %s\n", regs_64[dest_reg], regs_64[src_reg]);
                }
                code_append("    imul %s, %s\n", regs_64[dest_reg], regs_64[src2_reg]);
            }
            break;
            
        case IR_DIV:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            
            if (src_reg >= 0 && src2_reg >= 0) {
                code_append("    mov rax, %s\n", regs_64[src_reg]);
                code_append("    xor rdx, rdx\n");
                code_append("    div %s\n", regs_64[src2_reg]);
                
                dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0 && dest_reg != 0) {
                    code_append("    mov %s, rax\n", regs_64[dest_reg]);
                }
            }
            break;
            
        case IR_MOD:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            
            if (src_reg >= 0 && src2_reg >= 0) {
                code_append("    mov rax, %s\n", regs_64[src_reg]);
                code_append("    xor rdx, rdx\n");
                code_append("    div %s\n", regs_64[src2_reg]);
                
                dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    code_append("    mov %s, rdx\n", regs_64[dest_reg]);
                }
            }
            break;
            
        case IR_EQ:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_append("    cmp %s, %s\n", regs_64[src_reg], regs_64[src2_reg]);
                code_append("    sete al\n");
                code_append("    movzx %s, al\n", regs_64[dest_reg]);
            }
            break;
            
        case IR_NE:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_append("    cmp %s, %s\n", regs_64[src_reg], regs_64[src2_reg]);
                code_append("    setne al\n");
                code_append("    movzx %s, al\n", regs_64[dest_reg]);
            }
            break;
            
        case IR_LT:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_append("    cmp %s, %s\n", regs_64[src_reg], regs_64[src2_reg]);
                code_append("    setl al\n");
                code_append("    movzx %s, al\n", regs_64[dest_reg]);
            }
            break;
            
        case IR_LE:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_append("    cmp %s, %s\n", regs_64[src_reg], regs_64[src2_reg]);
                code_append("    setle al\n");
                code_append("    movzx %s, al\n", regs_64[dest_reg]);
            }
            break;
            
        case IR_GT:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_append("    cmp %s, %s\n", regs_64[src_reg], regs_64[src2_reg]);
                code_append("    setg al\n");
                code_append("    movzx %s, al\n", regs_64[dest_reg]);
            }
            break;
            
        case IR_GE:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_append("    cmp %s, %s\n", regs_64[src_reg], regs_64[src2_reg]);
                code_append("    setge al\n");
                code_append("    movzx %s, al\n", regs_64[dest_reg]);
            }
            break;
            
        case IR_AND:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    code_append("    mov %s, %s\n", regs_64[dest_reg], regs_64[src_reg]);
                }
                code_append("    and %s, %s\n", regs_64[dest_reg], regs_64[src2_reg]);
            }
            break;
            
        case IR_OR:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    code_append("    mov %s, %s\n", regs_64[dest_reg], regs_64[src_reg]);
                }
                code_append("    or %s, %s\n", regs_64[dest_reg], regs_64[src2_reg]);
            }
            break;
            
        case IR_NOT:
            src_reg = linear_get_reg(ra, ins->src1);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && dest_reg >= 0) {
                code_append("    test %s, %s\n", regs_64[src_reg], regs_64[src_reg]);
                code_append("    sete al\n");
                code_append("    movzx %s, al\n", regs_64[dest_reg]);
            }
            break;
            
        case IR_NEG:
            src_reg = linear_get_reg(ra, ins->src1);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    code_append("    mov %s, %s\n", regs_64[dest_reg], regs_64[src_reg]);
                }
                code_append("    neg %s\n", regs_64[dest_reg]);
            }
            break;
            
        case IR_JMP:
            code_append("    jmp .L%d\n", ins->label);
            break;
            
        case IR_JZ:
            src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                code_append("    test %s, %s\n", regs_64[src_reg], regs_64[src_reg]);
                code_append("    jz .L%d\n", ins->label);
            }
            break;
            
        case IR_JNZ:
            src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                code_append("    test %s, %s\n", regs_64[src_reg], regs_64[src_reg]);
                code_append("    jnz .L%d\n", ins->label);
            }
            break;
            
        case IR_CALL:
            code_append("    call %s\n", ins->var_name);
            if (ins->dest >= 0) {
                dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0 && dest_reg != 0) {
                    code_append("    mov %s, rax\n", regs_64[dest_reg]);
                } else if (dest_reg < 0) {
                    spill = linear_get_spill(ra, ins->dest);
                    if (spill >= 0) {
                        code_append("    mov [rbp-%d], rax\n", (spill + 1) * 8);
                    }
                }
            }
            break;
            
        case IR_RETURN:
            if (ins->src1 >= 0) {
                src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    if (src_reg != 0) {
                        code_append("    mov rax, %s\n", regs_64[src_reg]);
                    }
                } else {
                    spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        code_append("    mov rax, [rbp-%d]\n", (spill + 1) * 8);
                    }
                }
            }
            code_append("    ret\n");
            break;
            
        case IR_PUSH:
            if (ins->src1 == -1) {
                code_append("    push rbp\n");
            } else {
                src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    code_append("    push %s\n", regs_64[src_reg]);
                } else {
                    spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        code_append("    push qword [rbp-%d]\n", (spill + 1) * 8);
                    }
                }
            }
            break;
            
        case IR_POP:
            if (ins->src1 == -1) {
                code_append("    pop rbp\n");
            } else if (ins->dest >= 0) {
                dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    code_append("    pop %s\n", regs_64[dest_reg]);
                } else {
                    spill = linear_get_spill(ra, ins->dest);
                    if (spill >= 0) {
                        code_append("    pop qword [rbp-%d]\n", (spill + 1) * 8);
                    }
                }
            }
            break;
            
        case IR_PARAM:
            src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                switch (ins->dest) {
                    case 0: code_append("    mov rdi, %s\n", regs_64[src_reg]); break;
                    case 1: code_append("    mov rsi, %s\n", regs_64[src_reg]); break;
                    case 2: code_append("    mov rdx, %s\n", regs_64[src_reg]); break;
                    case 3: code_append("    mov rcx, %s\n", regs_64[src_reg]); break;
                    case 4: code_append("    mov r8, %s\n", regs_64[src_reg]); break;
                    case 5: code_append("    mov r9, %s\n", regs_64[src_reg]); break;
                    default: break;
                }
            } else {
                spill = linear_get_spill(ra, ins->src1);
                if (spill >= 0) {
                    switch (ins->dest) {
                        case 0: code_append("    mov rdi, [rbp-%d]\n", (spill + 1) * 8); break;
                        case 1: code_append("    mov rsi, [rbp-%d]\n", (spill + 1) * 8); break;
                        case 2: code_append("    mov rdx, [rbp-%d]\n", (spill + 1) * 8); break;
                        case 3: code_append("    mov rcx, [rbp-%d]\n", (spill + 1) * 8); break;
                        case 4: code_append("    mov r8, [rbp-%d]\n", (spill + 1) * 8); break;
                        case 5: code_append("    mov r9, [rbp-%d]\n", (spill + 1) * 8); break;
                        default: break;
                    }
                }
            }
            break;
            
        case IR_STRING:
            if (ins->var_name && ins->string_val) {
                add_var(ins->var_name, 0, 0, ins->string_val);
            }
            break;
            
        default:
            if (verbose) platform_eprintf("Unknown op: %d\n", ins->op);
            break;
    }
}

static void gen_ins_32(AsmContext* ctx, IRIns* ins, LinearRegAlloc* ra) {
    int src_reg, src2_reg, dest_reg;
    
    switch (ins->op) {
        case IR_LABEL:
            if (ins->var_name) {
                code_append("%s:\n", ins->var_name);
            } else {
                code_append(".L%d:\n", ins->label);
            }
            break;
            
        case IR_CONST:
            dest_reg = linear_get_reg(ra, ins->dest);
            if (dest_reg >= 0) {
                code_append("    mov %s, %d\n", regs_32[dest_reg], ins->const_val);
            }
            break;
            
        case IR_RETURN:
            if (ins->src1 >= 0) {
                src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0 && src_reg != 0) {
                    code_append("    mov eax, %s\n", regs_32[src_reg]);
                }
            }
            code_append("    ret\n");
            break;
            
        default:
            gen_ins_64(ctx, ins, ra);
            break;
    }
}

static void gen_ins_16(AsmContext* ctx, IRIns* ins, LinearRegAlloc* ra) {
    int src_reg, dest_reg;
    
    switch (ins->op) {
        case IR_LABEL:
            if (ins->var_name) {
                code_append("%s:\n", ins->var_name);
            } else {
                code_append(".L%d:\n", ins->label);
            }
            break;
            
        case IR_CONST:
            dest_reg = linear_get_reg(ra, ins->dest);
            if (dest_reg >= 0) {
                code_append("    mov %s, %d\n", regs_16[dest_reg], ins->const_val);
            }
            break;
            
        case IR_RETURN:
            if (ins->src1 >= 0) {
                src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0 && src_reg != 0) {
                    code_append("    mov ax, %s\n", regs_16[src_reg]);
                }
            }
            code_append("    ret\n");
            break;
            
        default:
            break;
    }
}

static void optimize_peephole(void) {
    if (!code_buffer || code_len < 10) return;
    
    char* new_buf = malloc(code_len + 1);
    int new_len = 0;
    char* line = code_buffer;
    char* next_line;
    
    while ((next_line = strchr(line, '\n')) != NULL) {
        *next_line = '\0';
        
        // Удаляем mov reg, reg
        if (strstr(line, "mov ") && strstr(line, ", ") && 
            strncmp(strchr(line, ',') + 2, line + 4, 3) == 0) {
            line = next_line + 1;
            continue;
        }
        
        // Удаляем add/sub 0
        if ((strstr(line, "add ") || strstr(line, "sub ")) && strstr(line, ", 0")) {
            line = next_line + 1;
            continue;
        }
        
        strcpy(new_buf + new_len, line);
        new_len += strlen(line);
        new_buf[new_len++] = '\n';
        line = next_line + 1;
    }
    
    new_buf[new_len] = '\0';
    free(code_buffer);
    code_buffer = new_buf;
    code_len = new_len;
}

void asm_gen_program(AsmContext* ctx, IRProgram* prog, LinearRegAlloc* ra) {
    if (!ctx->out || !prog) return;
    
    IRIns* ins = prog->head;
    while (ins) {
        if (ctx->bits == 64) {
            gen_ins_64(ctx, ins, ra);
        } else if (ctx->bits == 32) {
            gen_ins_32(ctx, ins, ra);
        } else {
            gen_ins_16(ctx, ins, ra);
        }
        ins = ins->next;
    }
    
    if (ctx->optimize_level >= 1) {
        optimize_peephole();
    }
}

void asm_gen_finish(AsmContext* ctx) {
    if (!ctx->out) return;
    
    // Перемещаемся в начало файла для перезаписи
    rewind(ctx->out);
    
    // Заголовок
    if (ctx->bits == 64) {
        fprintf(ctx->out, "bits 64\n");
        fprintf(ctx->out, "default rel\n\n");
    } else if (ctx->bits == 32) {
        fprintf(ctx->out, "bits 32\n\n");
    } else {
        fprintf(ctx->out, "bits 16\n\n");
    }
    
    // Секция .rodata
    fprintf(ctx->out, "section .rodata\n");
    int has_rodata = 0;
    for (int i = 0; i < var_count; i++) {
        if (vars[i].init_string) {
            fprintf(ctx->out, "%s: db %s, 0\n", vars[i].name, vars[i].init_string);
            has_rodata = 1;
        }
    }
    if (!has_rodata) fprintf(ctx->out, "; no rodata\n");
    fprintf(ctx->out, "\n");
    
    // Секция .data
    fprintf(ctx->out, "section .data\n");
    int has_data = 0;
    for (int i = 0; i < var_count; i++) {
        if (!vars[i].init_string && vars[i].init_value != 0) {
            fprintf(ctx->out, "%s: dq %d\n", vars[i].name, vars[i].init_value);
            has_data = 1;
        }
    }
    if (!has_data) fprintf(ctx->out, "; no data\n");
    fprintf(ctx->out, "\n");
    
    // Секция .bss
    fprintf(ctx->out, "section .bss\n");
    int has_bss = 0;
    for (int i = 0; i < var_count; i++) {
        if (!vars[i].init_string && vars[i].init_value == 0 && vars[i].is_global) {
            fprintf(ctx->out, "%s: resq 1\n", vars[i].name);
            has_bss = 1;
        }
    }
    if (!has_bss) fprintf(ctx->out, "; no bss\n");
    fprintf(ctx->out, "\n");
    
    // Секция .text
    fprintf(ctx->out, "section .text\n");
    
    if (ctx->safe_code && ctx->bits == 64) {
        fprintf(ctx->out, "    global main\n");
        fprintf(ctx->out, "    extern printf\n");
        fprintf(ctx->out, "    extern scanf\n");
        fprintf(ctx->out, "    extern malloc\n");
        fprintf(ctx->out, "    extern free\n\n");
    } else if (ctx->bits == 64) {
        fprintf(ctx->out, "    global _start\n");
        fprintf(ctx->out, "    extern main\n\n");
        fprintf(ctx->out, "_start:\n");
        fprintf(ctx->out, "    call main\n");
        fprintf(ctx->out, "    mov rdi, rax\n");
        fprintf(ctx->out, "    mov rax, 60\n");
        fprintf(ctx->out, "    syscall\n\n");
    }
    
    // Сгенерированный код
    if (code_buffer) {
        fprintf(ctx->out, "%s", code_buffer);
    }
    
    fclose(ctx->out);
    free_vars();
}
