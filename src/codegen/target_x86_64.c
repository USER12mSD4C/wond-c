#include <stdio.h>
#include <string.h>
#include "target_x86_64.h"
#include "../platform.h"

const char* x86_64_regs[] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static FILE* current_out = NULL;
static int has_data = 0;
static int has_bss = 0;
static int has_rodata = 0;

static void x86_64_init(void) {
    has_data = 0;
    has_bss = 0;
    has_rodata = 0;
}

static void x86_64_prologue(void) {
    fprintf(current_out, "    push rbp\n");
    fprintf(current_out, "    mov rbp, rsp\n");
    fprintf(current_out, "    sub rsp, 32\n");
}

static void x86_64_epilogue(void) {
    fprintf(current_out, "    mov rsp, rbp\n");
    fprintf(current_out, "    pop rbp\n");
}

static void x86_64_gen_ins(IRIns* ins, LinearRegAlloc* ra, FILE* out) {
    current_out = out;
    
    switch (ins->op) {
        case IR_STRING: {
            if (!has_rodata) {
                fprintf(out, "section .rodata\n");
                has_rodata = 1;
            }
            fprintf(out, "%s: db ", ins->var_name);
            int len = strlen(ins->string_val);
            for (int i = 0; i < len; i++) {
                if (i > 0) fprintf(out, ", ");
                fprintf(out, "%d", (unsigned char)ins->string_val[i]);
            }
            fprintf(out, ", 0\n");
            break;
        }
        
        case IR_GLOBAL: {
            // пропускаем объявления, они будут в секциях
            break;
        }
        
        case IR_CONST: {
            int reg = linear_get_reg(ra, ins->dest);
            if (reg >= 0) {
                fprintf(out, "    mov %s, %d\n", x86_64_regs[reg], ins->const_val);
            } else {
                int spill = linear_get_spill(ra, ins->dest);
                if (spill >= 0) {
                    fprintf(out, "    mov rax, %d\n", ins->const_val);
                    fprintf(out, "    mov [rbp-%d], rax\n", (spill+1)*8);
                }
            }
            break;
        }
        
        case IR_LOAD: {
            int src_reg = linear_get_reg(ra, ins->src1);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    mov %s, [%s]\n", x86_64_regs[dest_reg], x86_64_regs[src_reg]);
            } else if (src_reg >= 0 && dest_reg < 0) {
                int spill = linear_get_spill(ra, ins->dest);
                if (spill >= 0) {
                    fprintf(out, "    mov rax, [%s]\n", x86_64_regs[src_reg]);
                    fprintf(out, "    mov [rbp-%d], rax\n", (spill+1)*8);
                }
            }
            break;
        }
        
        case IR_STORE: {
            int src_reg = linear_get_reg(ra, ins->src1);
            if (ins->var_name) {
                if (!has_data && !has_bss) {
                    fprintf(out, "section .data\n");
                    has_data = 1;
                }
                fprintf(out, "%s: dq 0\n", ins->var_name);
                if (src_reg >= 0) {
                    fprintf(out, "    mov [rel %s], %s\n", ins->var_name, x86_64_regs[src_reg]);
                } else {
                    int spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        fprintf(out, "    mov rax, [rbp-%d]\n", (spill+1)*8);
                        fprintf(out, "    mov [rel %s], rax\n", ins->var_name);
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
                    fprintf(out, "    mov %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src1_reg]);
                }
                fprintf(out, "    add %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src2_reg]);
            }
            break;
        }
        
        case IR_SUB: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src1_reg) {
                    fprintf(out, "    mov %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src1_reg]);
                }
                fprintf(out, "    sub %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src2_reg]);
            }
            break;
        }
        
        case IR_MUL: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src1_reg) {
                    fprintf(out, "    mov %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src1_reg]);
                }
                fprintf(out, "    imul %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src2_reg]);
            }
            break;
        }
        
        case IR_DIV: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            
            if (src1_reg >= 0 && src2_reg >= 0) {
                fprintf(out, "    mov rax, %s\n", x86_64_regs[src1_reg]);
                fprintf(out, "    xor rdx, rdx\n");
                fprintf(out, "    div %s\n", x86_64_regs[src2_reg]);
                
                int dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    fprintf(out, "    mov %s, rax\n", x86_64_regs[dest_reg]);
                }
            }
            break;
        }
        
        case IR_MOD: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            
            if (src1_reg >= 0 && src2_reg >= 0) {
                fprintf(out, "    mov rax, %s\n", x86_64_regs[src1_reg]);
                fprintf(out, "    xor rdx, rdx\n");
                fprintf(out, "    div %s\n", x86_64_regs[src2_reg]);
                
                int dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    fprintf(out, "    mov %s, rdx\n", x86_64_regs[dest_reg]);
                }
            }
            break;
        }
        
        case IR_EQ: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                fprintf(out, "    sete al\n");
                fprintf(out, "    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_NE: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                fprintf(out, "    setne al\n");
                fprintf(out, "    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_LT: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                fprintf(out, "    setl al\n");
                fprintf(out, "    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_LE: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                fprintf(out, "    setle al\n");
                fprintf(out, "    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_GT: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                fprintf(out, "    setg al\n");
                fprintf(out, "    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_GE: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    cmp %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src2_reg]);
                fprintf(out, "    setge al\n");
                fprintf(out, "    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_AND: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    test %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src1_reg]);
                fprintf(out, "    setnz al\n");
                fprintf(out, "    test %s, %s\n", x86_64_regs[src2_reg], x86_64_regs[src2_reg]);
                fprintf(out, "    setnz bl\n");
                fprintf(out, "    and al, bl\n");
                fprintf(out, "    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_OR: {
            int src1_reg = linear_get_reg(ra, ins->src1);
            int src2_reg = linear_get_reg(ra, ins->src2);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src1_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    test %s, %s\n", x86_64_regs[src1_reg], x86_64_regs[src1_reg]);
                fprintf(out, "    setnz al\n");
                fprintf(out, "    test %s, %s\n", x86_64_regs[src2_reg], x86_64_regs[src2_reg]);
                fprintf(out, "    setnz bl\n");
                fprintf(out, "    or al, bl\n");
                fprintf(out, "    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_NOT: {
            int src_reg = linear_get_reg(ra, ins->src1);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && dest_reg >= 0) {
                fprintf(out, "    test %s, %s\n", x86_64_regs[src_reg], x86_64_regs[src_reg]);
                fprintf(out, "    sete al\n");
                fprintf(out, "    movzx %s, al\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_NEG: {
            int src_reg = linear_get_reg(ra, ins->src1);
            int dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    fprintf(out, "    mov %s, %s\n", x86_64_regs[dest_reg], x86_64_regs[src_reg]);
                }
                fprintf(out, "    neg %s\n", x86_64_regs[dest_reg]);
            }
            break;
        }
        
        case IR_LABEL: {
            if (ins->var_name) {
                fprintf(out, "%s:\n", ins->var_name);
            } else {
                fprintf(out, ".L%d:\n", ins->label);
            }
            break;
        }
        
        case IR_JMP: {
            fprintf(out, "    jmp .L%d\n", ins->label);
            break;
        }
        
        case IR_JZ: {
            int src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                fprintf(out, "    test %s, %s\n", x86_64_regs[src_reg], x86_64_regs[src_reg]);
                fprintf(out, "    jz .L%d\n", ins->label);
            }
            break;
        }
        
        case IR_JNZ: {
            int src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                fprintf(out, "    test %s, %s\n", x86_64_regs[src_reg], x86_64_regs[src_reg]);
                fprintf(out, "    jnz .L%d\n", ins->label);
            }
            break;
        }
        
        case IR_CALL: {
            fprintf(out, "    call %s\n", ins->var_name);
            break;
        }
        
        case IR_RETURN: {
            if (ins->src1 >= 0) {
                int src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    fprintf(out, "    mov rax, %s\n", x86_64_regs[src_reg]);
                } else {
                    int spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        fprintf(out, "    mov rax, [rbp-%d]\n", (spill+1)*8);
                    }
                }
            }
            fprintf(out, "    ret\n");
            break;
        }
        
        case IR_PARAM: {
            int src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                switch (ins->dest) {
                    case 0: fprintf(out, "    mov rdi, %s\n", x86_64_regs[src_reg]); break;
                    case 1: fprintf(out, "    mov rsi, %s\n", x86_64_regs[src_reg]); break;
                    case 2: fprintf(out, "    mov rdx, %s\n", x86_64_regs[src_reg]); break;
                    case 3: fprintf(out, "    mov rcx, %s\n", x86_64_regs[src_reg]); break;
                    case 4: fprintf(out, "    mov r8, %s\n", x86_64_regs[src_reg]); break;
                    case 5: fprintf(out, "    mov r9, %s\n", x86_64_regs[src_reg]); break;
                    default: break;
                }
            }
            break;
        }
        
        case IR_MODULE_CALL: {
            fprintf(out, "    call module_entry\n");
            break;
        }
        
        case IR_PUSH: {
            if (ins->src1 == -1) {
                fprintf(out, "    push rbp\n");
            } else {
                int src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    fprintf(out, "    push %s\n", x86_64_regs[src_reg]);
                }
            }
            break;
        }
        
        case IR_POP: {
            if (ins->src1 == -1) {
                fprintf(out, "    pop rbp\n");
            } else {
                int dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    fprintf(out, "    pop %s\n", x86_64_regs[dest_reg]);
                }
            }
            break;
        }
        
        default:
            break;
    }
}

static void x86_64_gen_label(int label, FILE* out) {
    fprintf(out, ".L%d:\n", label);
}

static void x86_64_finish(FILE* out) {
    (void)out;
}

static void x86_64_gen_global(const char* name, int size, const char* init, FILE* out) {
    (void)size;
    (void)out;
    (void)name;
    (void)init;
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
