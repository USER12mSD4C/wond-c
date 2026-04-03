#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "asm_gen.h"
#include "../platform.h"

extern int verbose;

static const char* regs_64[] = {
    "rax", "rcx", "rdx", "rbx", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static const char* regs_32[] = {
    "eax", "ecx", "edx", "ebx", "esi", "edi",
    "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"
};

static const char* regs_16[] = {
    "ax", "cx", "dx", "bx", "si", "di",
    "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"
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
static int in_function_frame = 0;
static const TargetSpec* active_target = NULL;
static const char* default_arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
static char** func_defs = NULL;
static int func_def_count = 0;
static int func_def_cap = 0;
static char** func_calls = NULL;
static int func_call_count = 0;
static int func_call_cap = 0;

typedef struct {
    const char* key;
    const char* value;
} TplArg;

static void code_append(const char* fmt, ...);

static int name_list_contains(char** arr, int count, const char* name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(arr[i], name) == 0) return 1;
    }
    return 0;
}

static void name_list_add(char*** arr, int* count, int* cap, const char* name) {
    if (!name || !*name) return;
    if (name_list_contains(*arr, *count, name)) return;
    if (*count >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *arr = realloc(*arr, *cap * sizeof(char*));
    }
    (*arr)[*count] = strdup(name);
    (*count)++;
}

static void name_list_free(char*** arr, int* count, int* cap) {
    for (int i = 0; i < *count; i++) {
        free((*arr)[i]);
    }
    free(*arr);
    *arr = NULL;
    *count = 0;
    *cap = 0;
}

static const char* target_value(const char* key, const char* fallback) {
    const char* v;
    if (!active_target || !key) return fallback;
    v = target_spec_get(active_target, key);
    if (v && *v) return v;
    return fallback;
}

static void make_local_label(int label, char* out, size_t out_sz) {
    const char* prefix = target_value("asm.label_prefix", ".L");
    snprintf(out, out_sz, "%s%d", prefix, label);
}

static const char* target_arg_reg(int idx) {
    switch (idx) {
        case 0: return target_value("abi.arg0", default_arg_regs[0]);
        case 1: return target_value("abi.arg1", default_arg_regs[1]);
        case 2: return target_value("abi.arg2", default_arg_regs[2]);
        case 3: return target_value("abi.arg3", default_arg_regs[3]);
        case 4: return target_value("abi.arg4", default_arg_regs[4]);
        case 5: return target_value("abi.arg5", default_arg_regs[5]);
        default: return NULL;
    }
}

static const char* target_ret_reg(void) {
    return target_value("abi.ret", "rax");
}

static const char* tmpl_value(const TplArg* args, int count, const char* key) {
    int i;
    if (!args || !key) return "";
    for (i = 0; i < count; i++) {
        if (args[i].key && strcmp(args[i].key, key) == 0) {
            return args[i].value ? args[i].value : "";
        }
    }
    return "";
}

static void code_append_template(const char* tmpl, const TplArg* args, int arg_count) {
    const char* p;
    char key[64];
    int k;
    if (!tmpl) return;
    p = tmpl;
    while (*p) {
        if (*p == '{') {
            p++;
            k = 0;
            while (*p && *p != '}' && k < (int)sizeof(key) - 1) {
                key[k++] = *p++;
            }
            key[k] = '\0';
            if (*p == '}') p++;
            code_append("%s", tmpl_value(args, arg_count, key));
        } else {
            char c[2];
            c[0] = *p++;
            c[1] = '\0';
            code_append("%s", c);
        }
    }
    if (tmpl[0] && tmpl[strlen(tmpl) - 1] != '\n') {
        code_append("\n");
    }
}

static void emit_target_line(const char* key, const char* fallback, const TplArg* args, int arg_count) {
    const char* tmpl = NULL;
    if (active_target) tmpl = target_spec_get(active_target, key);
    if (!tmpl || !*tmpl) tmpl = fallback;
    code_append_template(tmpl, args, arg_count);
}

static void code_append(const char* fmt, ...) {
    va_list args;
    int needed;
    va_start(args, fmt);
    needed = vsnprintf(NULL, 0, fmt, args) + 1;
    va_end(args);
    
    while (code_len + needed >= code_cap) {
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
        if (strcmp(vars[i].name, name) == 0) {
            if (init_string && !vars[i].init_string) {
                vars[i].init_string = strdup(init_string);
            }
            if (is_global) vars[i].is_global = 1;
            if (init_value != 0) vars[i].init_value = init_value;
            return;
        }
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
    name_list_free(&func_defs, &func_def_count, &func_def_cap);
    name_list_free(&func_calls, &func_call_count, &func_call_cap);
}

static void emit_db_string(FILE* out, const char* s) {
    if (!s) {
        fprintf(out, "0");
        return;
    }
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        fprintf(out, "%u", (unsigned int)*p);
        p++;
        if (*p) fprintf(out, ", ");
    }
}

void asm_gen_set_target(const TargetSpec* spec) {
    active_target = spec;
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
    func_defs = NULL;
    func_def_count = 0;
    func_def_cap = 0;
    func_calls = NULL;
    func_call_count = 0;
    func_call_cap = 0;
    
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
    int spill;
    (void)ctx;
    
    switch (ins->op) {
        case IR_LABEL:
            if (ins->var_name) {
                name_list_add(&func_defs, &func_def_count, &func_def_cap, ins->var_name);
                TplArg a[1];
                a[0].key = "name"; a[0].value = ins->var_name;
                emit_target_line("ir.func.label", "{name}:\n", a, 1);
                in_function_frame = 1;
                emit_target_line("ir.func.prologue.pushbp", "    push rbp\n", NULL, 0);
                emit_target_line("ir.func.prologue.setbp", "    mov rbp, rsp\n", NULL, 0);
                if (ra && ra->spill_count > 0) {
                    char spill_buf[64];
                    TplArg a2[1];
                    int spill_bytes = ra->spill_count * 8;
                    if (spill_bytes % 16 != 0) spill_bytes += 8;
                    snprintf(spill_buf, sizeof(spill_buf), "%d", spill_bytes);
                    a2[0].key = "size"; a2[0].value = spill_buf;
                    emit_target_line("ir.func.prologue.alloc", "    sub rsp, {size}\n", a2, 1);
                }
            } else {
                char label_buf[64];
                TplArg a[1];
                make_local_label(ins->label, label_buf, sizeof(label_buf));
                a[0].key = "label"; a[0].value = label_buf;
                emit_target_line("ir.label", "{label}:\n", a, 1);
            }
            break;
            
        case IR_CONST:
            dest_reg = linear_get_reg(ra, ins->dest);
            if (dest_reg >= 0) {
                char imm_buf[64];
                TplArg a[2];
                snprintf(imm_buf, sizeof(imm_buf), "%d", ins->const_val);
                a[0].key = "dest"; a[0].value = regs_64[dest_reg];
                a[1].key = "imm";  a[1].value = imm_buf;
                emit_target_line("ir.const.reg", "    mov {dest}, {imm}\n", a, 2);
            } else {
                spill = linear_get_spill(ra, ins->dest);
                if (spill >= 0) {
                    char imm_buf[64];
                    char off_buf[64];
                    TplArg a1[2];
                    TplArg a2[2];
                    snprintf(imm_buf, sizeof(imm_buf), "%d", ins->const_val);
                    snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                    a1[0].key = "dest"; a1[0].value = "rax";
                    a1[1].key = "imm";  a1[1].value = imm_buf;
                    emit_target_line("ir.const.spill.tmp", "    mov {dest}, {imm}\n", a1, 2);
                    a2[0].key = "off"; a2[0].value = off_buf;
                    a2[1].key = "src"; a2[1].value = "rax";
                    emit_target_line("ir.spill.store", "    mov [rbp-{off}], {src}\n", a2, 2);
                }
            }
            break;
            
        case IR_GLOBAL:
            if (ins->var_name) {
                add_var(ins->var_name, (ins->dest < 0) ? 1 : 0, 0, NULL);
                if (ins->dest >= 0) {
                    dest_reg = linear_get_reg(ra, ins->dest);
                    if (dest_reg >= 0) {
                        TplArg a[2];
                        a[0].key = "dest"; a[0].value = regs_64[dest_reg];
                        a[1].key = "name"; a[1].value = ins->var_name;
                        emit_target_line("ir.global.load", "    mov {dest}, [rel {name}]\n", a, 2);
                    } else {
                        spill = linear_get_spill(ra, ins->dest);
                        if (spill >= 0) {
                            char off_buf[64];
                            TplArg a1[2];
                            TplArg a2[2];
                            snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                            a1[0].key = "dest"; a1[0].value = "rax";
                            a1[1].key = "name"; a1[1].value = ins->var_name;
                            emit_target_line("ir.global.load", "    mov {dest}, [rel {name}]\n", a1, 2);
                            a2[0].key = "off"; a2[0].value = off_buf;
                            a2[1].key = "src"; a2[1].value = "rax";
                            emit_target_line("ir.spill.store", "    mov [rbp-{off}], {src}\n", a2, 2);
                        }
                    }
                }
            }
            break;
            
        case IR_STORE:
            if (ins->var_name) {
                add_var(ins->var_name, 0, 0, NULL);
                src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    TplArg a[2];
                    a[0].key = "name"; a[0].value = ins->var_name;
                    a[1].key = "src";  a[1].value = regs_64[src_reg];
                    emit_target_line("ir.global.store", "    mov [rel {name}], {src}\n", a, 2);
                } else {
                    spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        char off_buf[64];
                        TplArg a1[2];
                        TplArg a2[2];
                        snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                        a1[0].key = "dest"; a1[0].value = "rax";
                        a1[1].key = "off";  a1[1].value = off_buf;
                        emit_target_line("ir.spill.load", "    mov {dest}, [rbp-{off}]\n", a1, 2);
                        a2[0].key = "name"; a2[0].value = ins->var_name;
                        a2[1].key = "src";  a2[1].value = "rax";
                        emit_target_line("ir.global.store", "    mov [rel {name}], {src}\n", a2, 2);
                    }
                }
            }
            break;
            
        case IR_LOAD:
            if (ins->var_name) {
                add_var(ins->var_name, 0, 0, NULL);
                if (ins->dest >= 0) {
                    dest_reg = linear_get_reg(ra, ins->dest);
                    if (dest_reg >= 0) {
                        TplArg a[2];
                        a[0].key = "dest"; a[0].value = regs_64[dest_reg];
                        a[1].key = "name"; a[1].value = ins->var_name;
                        emit_target_line("ir.load.global", "    mov {dest}, [rel {name}]\n", a, 2);
                    } else {
                        spill = linear_get_spill(ra, ins->dest);
                        if (spill >= 0) {
                            char off_buf[64];
                            TplArg a1[2];
                            TplArg a2[2];
                            snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                            a1[0].key = "dest"; a1[0].value = "rax";
                            a1[1].key = "name"; a1[1].value = ins->var_name;
                            emit_target_line("ir.load.global", "    mov {dest}, [rel {name}]\n", a1, 2);
                            a2[0].key = "off"; a2[0].value = off_buf;
                            a2[1].key = "src"; a2[1].value = "rax";
                            emit_target_line("ir.spill.store", "    mov [rbp-{off}], {src}\n", a2, 2);
                        }
                    }
                }
            }
            break;

        case IR_LOAD_ADDR:
            if (ins->var_name && ins->dest >= 0) {
                add_var(ins->var_name, 0, 0, NULL);
                dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    TplArg a[2];
                    a[0].key = "dest"; a[0].value = regs_64[dest_reg];
                    a[1].key = "name"; a[1].value = ins->var_name;
                    emit_target_line("ir.loadaddr", "    lea {dest}, [rel {name}]\n", a, 2);
                } else {
                    spill = linear_get_spill(ra, ins->dest);
                    if (spill >= 0) {
                        char off_buf[64];
                        TplArg a1[2];
                        TplArg a2[2];
                        snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                        a1[0].key = "dest"; a1[0].value = "rax";
                        a1[1].key = "name"; a1[1].value = ins->var_name;
                        emit_target_line("ir.loadaddr", "    lea {dest}, [rel {name}]\n", a1, 2);
                        a2[0].key = "off"; a2[0].value = off_buf;
                        a2[1].key = "src"; a2[1].value = "rax";
                        emit_target_line("ir.spill.store", "    mov [rbp-{off}], {src}\n", a2, 2);
                    }
                }
            }
            break;
            
        case IR_TEMP:
            src_reg = linear_get_reg(ra, ins->src1);
            dest_reg = linear_get_reg(ra, ins->dest);
            if (src_reg >= 0 && dest_reg >= 0) {
                if (src_reg != dest_reg) {
                    TplArg a[2];
                    a[0].key = "dest"; a[0].value = regs_64[dest_reg];
                    a[1].key = "src";  a[1].value = regs_64[src_reg];
                    emit_target_line("ir.mov", "    mov {dest}, {src}\n", a, 2);
                }
            } else if (src_reg >= 0 && dest_reg < 0) {
                spill = linear_get_spill(ra, ins->dest);
                if (spill >= 0) {
                    char off_buf[64];
                    TplArg a[2];
                    snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                    a[0].key = "off"; a[0].value = off_buf;
                    a[1].key = "src"; a[1].value = regs_64[src_reg];
                    emit_target_line("ir.spill.store", "    mov [rbp-{off}], {src}\n", a, 2);
                }
            } else if (src_reg < 0 && dest_reg >= 0) {
                spill = linear_get_spill(ra, ins->src1);
                if (spill >= 0) {
                    char off_buf[64];
                    TplArg a[2];
                    snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                    a[0].key = "dest"; a[0].value = regs_64[dest_reg];
                    a[1].key = "off";  a[1].value = off_buf;
                    emit_target_line("ir.spill.load", "    mov {dest}, [rbp-{off}]\n", a, 2);
                }
            }
            break;
            
        case IR_ADD:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    TplArg a1[2];
                    a1[0].key = "dest"; a1[0].value = regs_64[dest_reg];
                    a1[1].key = "src";  a1[1].value = regs_64[src_reg];
                    emit_target_line("ir.mov", "    mov {dest}, {src}\n", a1, 2);
                }
                TplArg a2[2];
                a2[0].key = "dest"; a2[0].value = regs_64[dest_reg];
                a2[1].key = "src";  a2[1].value = regs_64[src2_reg];
                emit_target_line("ir.add", "    add {dest}, {src}\n", a2, 2);
            }
            break;
            
        case IR_SUB:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    TplArg a1[2];
                    a1[0].key = "dest"; a1[0].value = regs_64[dest_reg];
                    a1[1].key = "src";  a1[1].value = regs_64[src_reg];
                    emit_target_line("ir.mov", "    mov {dest}, {src}\n", a1, 2);
                }
                TplArg a2[2];
                a2[0].key = "dest"; a2[0].value = regs_64[dest_reg];
                a2[1].key = "src";  a2[1].value = regs_64[src2_reg];
                emit_target_line("ir.sub", "    sub {dest}, {src}\n", a2, 2);
            }
            break;
            
        case IR_MUL:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    TplArg a1[2];
                    a1[0].key = "dest"; a1[0].value = regs_64[dest_reg];
                    a1[1].key = "src";  a1[1].value = regs_64[src_reg];
                    emit_target_line("ir.mov", "    mov {dest}, {src}\n", a1, 2);
                }
                TplArg a2[2];
                a2[0].key = "dest"; a2[0].value = regs_64[dest_reg];
                a2[1].key = "src";  a2[1].value = regs_64[src2_reg];
                emit_target_line("ir.mul", "    imul {dest}, {src}\n", a2, 2);
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
                    TplArg a1[2];
                    a1[0].key = "dest"; a1[0].value = regs_64[dest_reg];
                    a1[1].key = "src";  a1[1].value = regs_64[src_reg];
                    emit_target_line("ir.mov", "    mov {dest}, {src}\n", a1, 2);
                }
                TplArg a2[2];
                a2[0].key = "dest"; a2[0].value = regs_64[dest_reg];
                a2[1].key = "src";  a2[1].value = regs_64[src2_reg];
                emit_target_line("ir.and", "    and {dest}, {src}\n", a2, 2);
            }
            break;
            
        case IR_OR:
            src_reg = linear_get_reg(ra, ins->src1);
            src2_reg = linear_get_reg(ra, ins->src2);
            dest_reg = linear_get_reg(ra, ins->dest);
            
            if (src_reg >= 0 && src2_reg >= 0 && dest_reg >= 0) {
                if (dest_reg != src_reg) {
                    TplArg a1[2];
                    a1[0].key = "dest"; a1[0].value = regs_64[dest_reg];
                    a1[1].key = "src";  a1[1].value = regs_64[src_reg];
                    emit_target_line("ir.mov", "    mov {dest}, {src}\n", a1, 2);
                }
                TplArg a2[2];
                a2[0].key = "dest"; a2[0].value = regs_64[dest_reg];
                a2[1].key = "src";  a2[1].value = regs_64[src2_reg];
                emit_target_line("ir.or", "    or {dest}, {src}\n", a2, 2);
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
            {
                char label_buf[64];
                TplArg a[1];
                make_local_label(ins->label, label_buf, sizeof(label_buf));
                a[0].key = "label"; a[0].value = label_buf;
                emit_target_line("ir.jmp", "    jmp {label}\n", a, 1);
            }
            break;
            
        case IR_JZ:
            src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                char label_buf[64];
                TplArg a1[1];
                TplArg a2[1];
                make_local_label(ins->label, label_buf, sizeof(label_buf));
                a1[0].key = "src"; a1[0].value = regs_64[src_reg];
                a2[0].key = "label"; a2[0].value = label_buf;
                emit_target_line("ir.jz.test", "    test {src}, {src}\n", a1, 1);
                emit_target_line("ir.jz", "    jz {label}\n", a2, 1);
            }
            break;
            
        case IR_JNZ:
            src_reg = linear_get_reg(ra, ins->src1);
            if (src_reg >= 0) {
                char label_buf[64];
                TplArg a1[1];
                TplArg a2[1];
                make_local_label(ins->label, label_buf, sizeof(label_buf));
                a1[0].key = "src"; a1[0].value = regs_64[src_reg];
                a2[0].key = "label"; a2[0].value = label_buf;
                emit_target_line("ir.jnz.test", "    test {src}, {src}\n", a1, 1);
                emit_target_line("ir.jnz", "    jnz {label}\n", a2, 1);
            }
            break;
            
        case IR_CALL:
            name_list_add(&func_calls, &func_call_count, &func_call_cap, ins->var_name);
            emit_target_line("ir.call.pre", "    xor eax, eax\n", NULL, 0);
            {
                TplArg a[1];
                a[0].key = "func"; a[0].value = ins->var_name ? ins->var_name : "";
                emit_target_line("ir.call", "    call {func}\n", a, 1);
            }
            if (ins->dest >= 0) {
                const char* retreg = target_ret_reg();
                dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    if (strcmp(regs_64[dest_reg], retreg) != 0) {
                        TplArg a[2];
                        a[0].key = "dest"; a[0].value = regs_64[dest_reg];
                        a[1].key = "src";  a[1].value = retreg;
                        emit_target_line("ir.mov", "    mov {dest}, {src}\n", a, 2);
                    }
                } else if (dest_reg < 0) {
                    spill = linear_get_spill(ra, ins->dest);
                    if (spill >= 0) {
                        char off_buf[64];
                        TplArg a[2];
                        snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                        a[0].key = "off"; a[0].value = off_buf;
                        a[1].key = "src"; a[1].value = retreg;
                        emit_target_line("ir.call.spillret", "    mov [rbp-{off}], {src}\n", a, 2);
                    }
                }
            }
            break;
            
        case IR_RETURN:
            if (ins->src1 >= 0) {
                const char* retreg = target_ret_reg();
                src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    if (strcmp(regs_64[src_reg], retreg) != 0) {
                        TplArg a[2];
                        a[0].key = "dest"; a[0].value = retreg;
                        a[1].key = "src";  a[1].value = regs_64[src_reg];
                        emit_target_line("ir.return.move", "    mov {dest}, {src}\n", a, 2);
                    }
                } else {
                    spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        char off_buf[64];
                        TplArg a[2];
                        snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                        a[0].key = "dest"; a[0].value = retreg;
                        a[1].key = "off";  a[1].value = off_buf;
                        emit_target_line("ir.return.loadspill", "    mov {dest}, [rbp-{off}]\n", a, 2);
                    }
                }
            }
            if (in_function_frame) {
                emit_target_line("ir.func.epilogue", "    leave\n", NULL, 0);
            }
            emit_target_line("ir.ret", "    ret\n", NULL, 0);
            break;
            
        case IR_PUSH:
            if (ins->src1 == -1) {
                // function prologue is emitted at function label
            } else {
                src_reg = linear_get_reg(ra, ins->src1);
                if (src_reg >= 0) {
                    TplArg a[1];
                    a[0].key = "src"; a[0].value = regs_64[src_reg];
                    emit_target_line("ir.push.reg", "    push {src}\n", a, 1);
                } else {
                    spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        char off_buf[64];
                        TplArg a[1];
                        snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                        a[0].key = "off"; a[0].value = off_buf;
                        emit_target_line("ir.push.spill", "    push qword [rbp-{off}]\n", a, 1);
                    }
                }
            }
            break;
            
        case IR_POP:
            if (ins->src1 == -1) {
                // function epilogue is emitted before each return
            } else if (ins->dest >= 0) {
                dest_reg = linear_get_reg(ra, ins->dest);
                if (dest_reg >= 0) {
                    TplArg a[1];
                    a[0].key = "dest"; a[0].value = regs_64[dest_reg];
                    emit_target_line("ir.pop.reg", "    pop {dest}\n", a, 1);
                } else {
                    spill = linear_get_spill(ra, ins->dest);
                    if (spill >= 0) {
                        char off_buf[64];
                        TplArg a[1];
                        snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                        a[0].key = "off"; a[0].value = off_buf;
                        emit_target_line("ir.pop.spill", "    pop qword [rbp-{off}]\n", a, 1);
                    }
                }
            }
            break;
            
        case IR_PARAM:
            src_reg = linear_get_reg(ra, ins->src1);
            {
                const char* argreg = target_arg_reg(ins->dest);
                if (!argreg) break;
                if (src_reg >= 0) {
                    TplArg a[2];
                    a[0].key = "arg"; a[0].value = argreg;
                    a[1].key = "src"; a[1].value = regs_64[src_reg];
                    emit_target_line("ir.param.reg", "    mov {arg}, {src}\n", a, 2);
                } else {
                    spill = linear_get_spill(ra, ins->src1);
                    if (spill >= 0) {
                        char off_buf[64];
                        TplArg a[2];
                        snprintf(off_buf, sizeof(off_buf), "%d", (spill + 1) * 8);
                        a[0].key = "arg"; a[0].value = argreg;
                        a[1].key = "off"; a[1].value = off_buf;
                        emit_target_line("ir.param.spill", "    mov {arg}, [rbp-{off}]\n", a, 2);
                    }
                }
            }
            break;
            
        case IR_STRING:
            if (ins->var_name && ins->string_val) {
                add_var(ins->var_name, 1, 0, ins->string_val);
            }
            break;
            
        default:
            if (verbose) platform_eprintf("Unknown op: %d\n", ins->op);
            break;
    }
}

static void gen_ins_32(AsmContext* ctx, IRIns* ins, LinearRegAlloc* ra) {
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
    (void)ctx;
    
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

    in_function_frame = 0;
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

    {
        int hdr_count = 0;
        for (int i = 0; i < var_count; i++) {
            if (!vars[i].is_global && !vars[i].init_string) {
                fprintf(ctx->out, "extern %s\n", vars[i].name);
                hdr_count++;
            }
        }
        for (int i = 0; i < var_count; i++) {
            if (vars[i].is_global && !vars[i].init_string) {
                fprintf(ctx->out, "global %s\n", vars[i].name);
                hdr_count++;
            }
        }
        for (int i = 0; i < func_call_count; i++) {
            if (!name_list_contains(func_defs, func_def_count, func_calls[i])) {
                fprintf(ctx->out, "extern %s\n", func_calls[i]);
                hdr_count++;
            }
        }
        for (int i = 0; i < func_def_count; i++) {
            fprintf(ctx->out, "global %s\n", func_defs[i]);
            hdr_count++;
        }
        if (hdr_count > 0) fprintf(ctx->out, "\n");
    }
    
    // Секция .rodata
    fprintf(ctx->out, "section .rodata\n");
    int has_rodata = 0;
    for (int i = 0; i < var_count; i++) {
        if (vars[i].init_string) {
            fprintf(ctx->out, "%s: db ", vars[i].name);
            emit_db_string(ctx->out, vars[i].init_string);
            fprintf(ctx->out, ", 0\n");
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
    
    if (ctx->bits == 64 && !ctx->safe_code &&
        name_list_contains(func_defs, func_def_count, "main")) {
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
