#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "target_x86_64.h"
#include "../platform.h"

const char* x86_64_regs[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

typedef struct {
    char* name;
    int has_init;
    char* init_val;
} VarDecl;

static VarDecl* vars = NULL;
static int var_count = 0;
static int var_cap = 0;

typedef struct CodeBuf {
    char* data;
    int len;
    int cap;
} CodeBuf;

static CodeBuf code_buf;

static void code_buf_init(void) {
    code_buf.cap = 65536;
    code_buf.data = platform_malloc(code_buf.cap);
    code_buf.len = 0;
    code_buf.data[0] = '\0';
}

static void code_buf_append(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args) + 1;
    va_end(args);
    
    if (code_buf.len + needed >= code_buf.cap) {
        code_buf.cap = code_buf.cap * 2;
        code_buf.data = platform_realloc(code_buf.data, code_buf.cap);
    }
    
    va_start(args, fmt);
    vsprintf(code_buf.data + code_buf.len, fmt, args);
    code_buf.len += needed - 1;
    va_end(args);
}

static void code_buf_free(void) {
    if (code_buf.data) platform_free(code_buf.data);
    code_buf.data = NULL;
    code_buf.len = 0;
    code_buf.cap = 0;
}

static void add_var(const char* name, int has_init, const char* init_val) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].name, name) == 0) {
            if (has_init && !vars[i].has_init) {
                vars[i].has_init = 1;
                if (init_val) vars[i].init_val = platform_strdup(init_val);
            }
            return;
        }
    }
    
    if (var_count >= var_cap) {
        var_cap = var_cap ? var_cap * 2 : 64;
        vars = platform_realloc(vars, var_cap * sizeof(VarDecl));
    }
    
    vars[var_count].name = platform_strdup(name);
    vars[var_count].has_init = has_init;
    vars[var_count].init_val = has_init && init_val ? platform_strdup(init_val) : NULL;
    var_count++;
}

static void x86_64_init(void) {
    var_count = 0;
    code_buf_init();
}

static void x86_64_prologue(void) {
    code_buf_append("    push rbp\n");
    code_buf_append("    mov rbp, rsp\n");
    code_buf_append("    sub rsp, 32\n");
}

static void x86_64_epilogue(void) {
    code_buf_append("    mov rsp, rbp\n");
    code_buf_append("    pop rbp\n");
}

static void x86_64_gen_ins(IRIns* ins, LinearRegAlloc* ra, FILE* out) {
    (void)out;
    
    switch (ins->op) {
        case IR_STRING: {
            add_var(ins->var_name, 1, ins->string_val);
            break;
        }
        
        case IR_GLOBAL: {
            add_var(ins->var_name, 0, NULL);
            
            if (ins->dest == -1) {
                break;
            }
            
            int reg = linear_get_reg(ra, ins->dest);
            if (reg >= 0) {
                code_buf_append("    mov %s, [rel %s]\n", x86_64_regs[reg], ins->var_name);
            } else {
                int spill = linear_get_spill(ra, ins->dest);
                if (spill >= 0) {
                    code_buf_append("    mov rax, [rel %s]\n", ins->var_name);
                    code_buf_append("    mov [rbp-%d], rax\n", (spill+1)*8);
                }
            }
            break;
        }
        
        case IR_CONST: {
            int reg = linear_get_reg(ra, ins->dest);
            if (reg >= 0) {
                code_buf_append("    mov %s, %d\n", x86_64_regs[reg], ins->const_val);
            } else {
                int spill = linear_get_spill(ra, ins->dest);
                if (spill >= 0) {
                    code_buf_append("    mov rax, %d\n", ins->const_val);
                    code_buf_append("    mov [rbp-%d], rax\n", (spill+1)*8);
                }
            }
            break;
        }
        
        case IR_STORE: {
            if (ins->var_name) {
                add_var(ins->var_name, 1, NULL);
                int src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    code_buf_append("    mov [rel %s], %s\n", ins->var_name, x86_64_regs[src_reg]);
                } else {
                    int spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        code_buf_append("    mov rax, [rbp-%d]\n", (spill+1)*8);
                        code_buf_append("    mov [rel %s], rax\n", ins->var_name);
                    }
                }
            }
            break;
        }
        
        case IR_ADD: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src1_reg) {
                    code_buf_append("    mov %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src1_reg]);
                }
                code_buf_append("    add %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src2_reg]);
            }
            break;
        }
        
        case IR_SUB: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src1_reg) {
                    code_buf_append("    mov %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src1_reg]);
                }
                code_buf_append("    sub %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src2_reg]);
            }
            break;
        }
        
        case IR_MUL: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src1_reg) {
                    code_buf_append("    mov %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src1_reg]);
                }
                code_buf_append("    imul %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src2_reg]);
            }
            break;
        }
        
        case IR_DIV: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            
            if (src1_reg >= 0 && src2_reg >= 0) {
                code_buf_append("    mov rax, %s\n", x86_64_regs[src1_reg]);
                code_buf_append("    xor rdx, rdx\n");
                code_buf_append("    div %s\n", x86_64_regs[src2_reg]);
                
                int dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    code_buf_append("    mov %s, rax\n", x86_64_regs[dest_reg]);
                }
            }
            break;
        }
        
        case IR_MOD: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            
            if (src1_reg >= 0 && src2_reg >= 0) {
                code_buf_append("    mov rax, %s\n", x86_64_regs[src1_reg]);
                code_buf_append("    xor rdx, rdx\n");
                code_buf_append("    div %s\n", x86_64_regs[src2_reg]);
                
                int dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    code_buf_append("    mov %s, rdx\n", x86_64_regs[dest_reg]);
                }
            }
            break;
        }
        
        case IR_EQ: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_buf_append("    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                code_buf_append("    sete al\n");
                code_buf_append("    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_NE: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_buf_append("    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                code_buf_append("    setne al\n");
                code_buf_append("    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_LT: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_buf_append("    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                code_buf_append("    setl al\n");
                code_buf_append("    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_LE: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_buf_append("    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                code_buf_append("    setle al\n");
                code_buf_append("    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_GT: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_buf_append("    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                code_buf_append("    setg al\n");
                code_buf_append("    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_GE: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_buf_append("    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                code_buf_append("    setge al\n");
                code_buf_append("    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_AND: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_buf_append("    test %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src1_reg]);
                code_buf_append("    setnz al\n");
                code_buf_append("    test %s, %s\n", x86_64_regs[src2_reg], x86_64_regs[src2_reg]);
                code_buf_append("    setnz bl\n");
                code_buf_append("    and al, bl\n");
                code_buf_append("    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_OR: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                code_buf_append("    test %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src1_reg]);
                code_buf_append("    setnz al\n");
                code_buf_append("    test %s, %s\n", x86_64_regs[src2_reg], x86_64_regs[src2_reg]);
                code_buf_append("    setnz bl\n");
                code_buf_append("    or al, bl\n");
                code_buf_append("    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_NOT: {
            int src_reg = linear_get_reg(ra, ins->src1);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && dest_reg >= 0) {
                code_buf_append("    test %s, %s\n", x86_64_regs[src_reg], x86_64_regs[src_reg]);
                code_buf_append("    sete al\n");
                code_buf_append("    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_NEG: {
            int src_reg = linear_get_reg(ra, ins->src1);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    code_buf_append("    mov %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src_reg]);
                }
                code_buf_append("    neg %s\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_LABEL: {
            if (ins->var_name) {
                code_buf_append("%s:\n", ins->var_name);
            } else {
                code_buf_append(".L%d:\n", ins->label);
            }
            break;
        }
        
        case IR_JMP: {
            code_buf_append("    jmp .L%d\n", ins->label);
            break;
        }
        
        case IR_JZ: {
            int src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                code_buf_append("    test %s, %s\n", x86_64_regs[src_reg], x86_64_regs[src_reg]);
                code_buf_append("    jz .L%d\n", ins->label);
            }
            break;
        }
        
        case IR_JNZ: {
            int src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                code_buf_append("    test %s, %s\n", x86_64_regs[src_reg], x86_64_regs[src_reg]);
                code_buf_append("    jnz .L%d\n", ins->label);
            }
            break;
        }
        
        case IR_CALL: {
            code_buf_append("    call %s\n", ins->var_name);
            break;
        }
        
        case IR_RETURN: {
            if (ins->src1 >= 0) {
                int src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    code_buf_append("    mov rax, %s\n", x86_64_regs[src_reg]);
                } else {
                    int spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        code_buf_append("    mov rax, [rbp-%d]\n", (spill+1)*8);
                    }
                }
            }
            code_buf_append("    ret\n");
            break;
        }
        
        case IR_PARAM: {
            int src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                switch (ins->dest) {
                    case 0: code_buf_append("    mov rdi, %s\n", x86_64_regs[src_reg]); break;
                    case 1: code_buf_append("    mov rsi, %s\n", x86_64_regs[src_reg]); break;
                    case 2: code_buf_append("    mov rdx, %s\n", x86_64_regs[src_reg]); break;
                    case 3: code_buf_append("    mov rcx, %s\n", x86_64_regs[src_reg]); break;
                    case 4: code_buf_append("    mov r8, %s\n", x86_64_regs[src_reg]); break;
                    case 5: code_buf_append("    mov r9, %s\n", x86_64_regs[src_reg]); break;
                    default: break;
                }
            }
            break;
        }
        
        case IR_MODULE_CALL: {
            code_buf_append("    call module_entry\n");
            break;
        }
        
        case IR_PUSH: {
            if (ins->src1 == -1) {
                code_buf_append("    push rbp\n");
            } else {
                int src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    code_buf_append("    push %s\n", x86_64_regs[src_reg]);
                }
            }
            break;
        }
        
        case IR_POP: {
            if (ins->src1 == -1) {
                code_buf_append("    pop rbp\n");
            } else {
                int dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    code_buf_append("    pop %s\n", x86_64_regs[dest_reg]);
                }
            }
            break;
        }
        
        default:
            break;
    }
}

static void x86_64_gen_label(int label, FILE* out) {
    (void)label;
    (void)out;
}

static void x86_64_finish(FILE* out) {
    fprintf(out, "bits 64\n");
    fprintf(out, "default rel\n\n");
    
    fprintf(out, "section .rodata\n");
    int has_rodata = 0;
    for (int i = 0; i < var_count; i++) {
        if (strncmp(vars[i].name, "str_", 4) == 0 && vars[i].has_init && vars[i].init_val) {
            fprintf(out, "%s: db %s, 0\n", vars[i].name, vars[i].init_val);
            has_rodata = 1;
        }
    }
    if (!has_rodata) fprintf(out, "; no rodata\n");
    fprintf(out, "\n");
    
    fprintf(out, "section .data\n");
    int has_data = 0;
    for (int i = 0; i < var_count; i++) {
        if (strncmp(vars[i].name, "str_", 4) != 0 && vars[i].has_init) {
            fprintf(out, "%s: dq 0\n", vars[i].name);
            has_data = 1;
        }
    }
    if (!has_data) fprintf(out, "; no data\n");
    fprintf(out, "\n");
    
    fprintf(out, "section .bss\n");
    int has_bss = 0;
    for (int i = 0; i < var_count; i++) {
        if (strncmp(vars[i].name, "str_", 4) != 0 && !vars[i].has_init) {
            fprintf(out, "%s: resq 1\n", vars[i].name);
            has_bss = 1;
        }
    }
    if (!has_bss) fprintf(out, "; no bss\n");
    fprintf(out, "\n");
    
    fprintf(out, "section .text\n");
    fprintf(out, "%s", code_buf.data);
    
    code_buf_free();
    for (int i = 0; i < var_count; i++) {
        platform_free(vars[i].name);
        if (vars[i].init_val) platform_free(vars[i].init_val);
    }
    platform_free(vars);
    vars = NULL;
    var_count = var_cap = 0;
}

static void x86_64_gen_global(const char* name, int size, const char* init, FILE* out) {
    (void)name;
    (void)size;
    (void)init;
    (void)out;
}

static void x86_64_gen_section_global(const char* section, const char* name, int size, const char* init, FILE* out) {
    (void)section;
    (void)name;
    (void)size;
    (void)init;
    (void)out;
}

static void x86_64_gen_string(const char* str, int label, FILE* out) {
    (void)str;
    (void)label;
    (void)out;
}

Target target_x86_64 = {
    .name = "x86_64",
    .reg_count = 16,
    .reg_names = x86_64_regs,
    .init = x86_64_init,
    .prologue = x86_64_prologue,
    .epilogue = x86_64_epilogue,
    .gen_ins = x86_64_gen_ins,
    .gen_label = x86_64_gen_label,
    .finish = x86_64_finish,
    .gen_global = x86_64_gen_global,
    .gen_section_global = x86_64_gen_section_global,
    .gen_string = x86_64_gen_string
};
