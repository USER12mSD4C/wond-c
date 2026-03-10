#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"

// Структура для строковых констант
typedef struct {
    char* value;
    int label;
} StringEntry;

static StringEntry* string_table = NULL;
static int string_count = 0;
static int string_capacity = 0;

// Структура для переменных
typedef struct {
    char* name;
    VarType type;
    int is_locate;
    int is_initialized;
    char* init_value;
    int is_global;
    int size;
    char* asm_type;
    char* asm_res;
} VarInfo;

static VarInfo* var_table = NULL;
static int var_count = 0;
static int var_capacity = 0;

// Вспомогательные функции
static int get_type_size(VarType type) {
    switch (type) {
        case TYPE_U8: case TYPE_I8: return 1;
        case TYPE_U16: case TYPE_I16: return 2;
        case TYPE_U32: case TYPE_I32: return 4;
        case TYPE_U64: case TYPE_I64: return 8;
        case TYPE_STRING: return 8;
        default: return 8;
    }
}

static const char* get_asm_type(VarType type) {
    switch (type) {
        case TYPE_U8: case TYPE_I8: return "db";
        case TYPE_U16: case TYPE_I16: return "dw";
        case TYPE_U32: case TYPE_I32: return "dd";
        case TYPE_U64: case TYPE_I64: return "dq";
        case TYPE_STRING: return "dq";
        default: return "dq";
    }
}

static const char* get_asm_res(VarType type) {
    switch (type) {
        case TYPE_U8: case TYPE_I8: return "resb";
        case TYPE_U16: case TYPE_I16: return "resw";
        case TYPE_U32: case TYPE_I32: return "resd";
        case TYPE_U64: case TYPE_I64: return "resq";
        case TYPE_STRING: return "resq";
        default: return "resq";
    }
}

static char* codegen_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* d = malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

static int get_string_label(const char* value) {
    for (int i = 0; i < string_count; i++) {
        if (strcmp(string_table[i].value, value) == 0) {
            return string_table[i].label;
        }
    }
    
    int label = string_count;
    if (string_count >= string_capacity) {
        string_capacity = string_capacity ? string_capacity * 2 : 16;
        string_table = realloc(string_table, string_capacity * sizeof(StringEntry));
        if (!string_table) {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }
    }
    
    string_table[string_count].value = codegen_strdup(value);
    string_table[string_count].label = label;
    string_count++;
    return label;
}

static void free_string_table() {
    for (int i = 0; i < string_count; i++) {
        free(string_table[i].value);
    }
    free(string_table);
    string_table = NULL;
    string_count = 0;
    string_capacity = 0;
}

static void reset_var_table() {
    for (int i = 0; i < var_count; i++) {
        free(var_table[i].name);
        free(var_table[i].init_value);
    }
    free(var_table);
    var_table = NULL;
    var_count = 0;
    var_capacity = 0;
}

static void add_var(const char* name, VarType type, int is_locate, const char* init_value, int is_global) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(var_table[i].name, name) == 0) {
            free(var_table[i].init_value);
            var_table[i].init_value = codegen_strdup(init_value ? init_value : "0");
            var_table[i].is_initialized = (init_value != NULL);
            return;
        }
    }
    
    if (var_count >= var_capacity) {
        var_capacity = var_capacity ? var_capacity * 2 : 64;
        var_table = realloc(var_table, var_capacity * sizeof(VarInfo));
        if (!var_table) {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }
    }
    
    var_table[var_count].name = codegen_strdup(name);
    var_table[var_count].type = type;
    var_table[var_count].is_locate = is_locate;
    var_table[var_count].is_initialized = (init_value != NULL);
    var_table[var_count].init_value = codegen_strdup(init_value ? init_value : "0");
    var_table[var_count].is_global = is_global;
    var_table[var_count].size = get_type_size(type);
    var_table[var_count].asm_type = (char*)get_asm_type(type);
    var_table[var_count].asm_res = (char*)get_asm_res(type);
    var_count++;
}

static void collect_vars(AstNode* node, int is_global) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_VARIABLE: {
            char full_name[256];
            if (node->data.variable.name) {
                snprintf(full_name, sizeof(full_name), "var_%s", node->data.variable.name);
                const char* init = NULL;
                if (node->data.variable.value && node->data.variable.value->type == NODE_NUMBER) {
                    init = node->data.variable.value->data.number.value;
                }
                add_var(full_name, node->data.variable.var_type, 
                       node->data.variable.is_locate, init, is_global);
            }
            break;
        }
        
        case NODE_SECTION: {
            for (int i = 0; i < node->data.section.var_count; i++) {
                AstNode* var = node->data.section.variables[i];
                if (var->type == NODE_VARIABLE) {
                    char full_name[256];
                    snprintf(full_name, sizeof(full_name), "sect_%s_%s",
                             node->data.section.name, var->data.variable.name);
                    
                    const char* init = NULL;
                    if (var->data.variable.value && var->data.variable.value->type == NODE_NUMBER) {
                        init = var->data.variable.value->data.number.value;
                    }
                    add_var(full_name, var->data.variable.var_type,
                           var->data.variable.is_locate, init, 1);
                }
            }
            break;
        }
        
        case NODE_FUNCTION:
            for (int i = 0; i < node->data.function.param_count; i++) {
                collect_vars(node->data.function.params[i], 0);
            }
            collect_vars(node->data.function.body, 0);
            break;
            
        case NODE_BLOCK:
            for (int i = 0; i < node->data.block.count; i++) {
                collect_vars(node->data.block.statements[i], is_global);
            }
            break;
            
        case NODE_IF:
            collect_vars(node->data.if_stmt.condition, is_global);
            collect_vars(node->data.if_stmt.then_branch, is_global);
            collect_vars(node->data.if_stmt.else_branch, is_global);
            break;
            
        case NODE_WHILE:
            collect_vars(node->data.while_loop.condition, is_global);
            collect_vars(node->data.while_loop.body, is_global);
            break;
            
        case NODE_FOR:
            if (node->data.for_loop.init) collect_vars(node->data.for_loop.init, is_global);
            if (node->data.for_loop.condition) collect_vars(node->data.for_loop.condition, is_global);
            collect_vars(node->data.for_loop.body, is_global);
            if (node->data.for_loop.post) collect_vars(node->data.for_loop.post, is_global);
            break;
            
        case NODE_RETURN:
            for (int i = 0; i < node->data.return_stmt.count; i++) {
                collect_vars(node->data.return_stmt.values[i], is_global);
            }
            break;
            
        case NODE_JMPTO:
            for (int i = 0; i < node->data.jmpto.var_count; i++) {
                collect_vars(node->data.jmpto.vars[i], is_global);
            }
            if (node->data.jmpto.block) collect_vars(node->data.jmpto.block, is_global);
            break;
            
        case NODE_INPUT:
            collect_vars(node->data.input.target, is_global);
            break;
            
        case NODE_MLOC:
        case NODE_BMLOC:
        case NODE_MFREE:
        case NODE_INB:
        case NODE_OUTB:
        case NODE_E820F:
            break;
            
        case NODE_CALL:
            for (int i = 0; i < node->data.call.arg_count; i++) {
                collect_vars(node->data.call.args[i], is_global);
            }
            break;
            
        case NODE_BINARY_OP:
            collect_vars(node->data.binary.left, is_global);
            collect_vars(node->data.binary.right, is_global);
            break;
            
        case NODE_UNARY_OP:
            collect_vars(node->data.unary.expr, is_global);
            break;
            
        case NODE_PROGRAM:
            for (int i = 0; i < node->data.program.count; i++) {
                collect_vars(node->data.program.items[i], 1);
            }
            break;
            
        default:
            break;
    }
}

static void collect_strings(AstNode* node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_STRING:
            get_string_label(node->data.string.value);
            break;
            
        case NODE_CALL:
            if (strcmp(node->data.call.name, "printf") == 0 && node->data.call.arg_count > 0) {
                if (node->data.call.args[0]->type == NODE_STRING) {
                    get_string_label(node->data.call.args[0]->data.string.value);
                }
            }
            for (int i = 1; i < node->data.call.arg_count; i++) {
                collect_strings(node->data.call.args[i]);
            }
            break;
            
        case NODE_INPUT:
            if (node->data.input.prompt) collect_strings(node->data.input.prompt);
            break;
            
        default:
            if (node->type == NODE_PROGRAM) {
                for (int i = 0; i < node->data.program.count; i++) {
                    collect_strings(node->data.program.items[i]);
                }
            } else if (node->type == NODE_FUNCTION) {
                collect_strings(node->data.function.body);
            } else if (node->type == NODE_BLOCK) {
                for (int i = 0; i < node->data.block.count; i++) {
                    collect_strings(node->data.block.statements[i]);
                }
            }
            break;
    }
}

void codegen_init(CodeGen* cg, FILE* out, int safe_code, int target_raw) {
    cg->out = out;
    cg->label_counter = 0;
    cg->safe_code = safe_code;
    cg->target_raw = target_raw;
    cg->target_uefi = 0;
    cg->opt_level = 0;
    cg->bit_mode = 64;
    cg->adrload = NULL;
    cg->current_bits = 16;
    
    free_string_table();
    reset_var_table();
}

static int codegen_new_label(CodeGen* cg) {
    return cg->label_counter++;
}

static void codegen_emit_directives(CodeGen* cg, AstNode* ast) {
    for (int i = 0; i < ast->data.program.count; i++) {
        AstNode* node = ast->data.program.items[i];
        if (node->type == NODE_DIRECTIVE) {
            if (strcmp(node->data.directive.name, "adrload") == 0) {
                fprintf(cg->out, "org %s\n", node->data.directive.value);
                cg->adrload = node->data.directive.value;
            } else if (strcmp(node->data.directive.name, "bits") == 0) {
                fprintf(cg->out, "bits %s\n", node->data.directive.value);
                cg->current_bits = atoi(node->data.directive.value);
            }
        }
    }
}

static void codegen_emit_rodata(CodeGen* cg) {
    if (string_count == 0) return;
    
    fprintf(cg->out, "section .rodata\n");
    for (int i = 0; i < string_count; i++) {
        fprintf(cg->out, "str_%d: db ", string_table[i].label);
        int len = strlen(string_table[i].value);
        for (int j = 0; j < len; j++) {
            if (j > 0) fprintf(cg->out, ", ");
            fprintf(cg->out, "%d", (unsigned char)string_table[i].value[j]);
        }
        fprintf(cg->out, "\n");
    }
    fprintf(cg->out, "\n");
}

static void codegen_emit_data(CodeGen* cg) {
    fprintf(cg->out, "section .data\n");
    
    int data_count = 0;
    for (int i = 0; i < var_count; i++) {
        if (var_table[i].is_initialized && strcmp(var_table[i].init_value, "0") != 0) {
            if (var_table[i].type == TYPE_STRING) {
                int label = get_string_label(var_table[i].init_value);
                fprintf(cg->out, "%s: dq str_%d\n", var_table[i].name, label);
            } else {
                fprintf(cg->out, "%s: %s %s\n", var_table[i].name, 
                        var_table[i].asm_type, var_table[i].init_value);
            }
            data_count++;
        }
    }
    
    if (data_count == 0) {
        fprintf(cg->out, "    ; no initialized data\n");
    }
    fprintf(cg->out, "\n");
}

static void codegen_emit_bss(CodeGen* cg) {
    fprintf(cg->out, "section .bss\n");
    
    for (int i = 0; i < var_count; i++) {
        if (!var_table[i].is_initialized || strcmp(var_table[i].init_value, "0") == 0) {
            fprintf(cg->out, "%s: %s 1\n", var_table[i].name, var_table[i].asm_res);
        }
    }
    
    if (cg->safe_code == 1) {
        for (int i = 0; i < 8; i++) {
            fprintf(cg->out, "input_buf_%d: resb 256\n", i);
        }
        fprintf(cg->out, "num_buf: resb 32\n");
    }
    fprintf(cg->out, "\n");
}

static void codegen_emit_expr(CodeGen* cg, AstNode* node) {
    if (!node) {
        fprintf(cg->out, "    xor eax, eax\n");
        return;
    }
    
    switch (node->type) {
        case NODE_NUMBER:
            fprintf(cg->out, "    mov rax, %s\n", node->data.number.value);
            break;
            
        case NODE_STRING:
            fprintf(cg->out, "    lea rax, [rel str_%d]\n", 
                    get_string_label(node->data.string.value));
            break;
            
        case NODE_IDENTIFIER: {
            char var_name[256];
            snprintf(var_name, sizeof(var_name), "var_%s", node->data.identifier.name);
            
            // Проверяем, locate ли это
            int is_locate = 0;
            VarType type = TYPE_U64;
            for (int i = 0; i < var_count; i++) {
                if (strcmp(var_table[i].name, var_name) == 0) {
                    is_locate = var_table[i].is_locate;
                    type = var_table[i].type;
                    break;
                }
            }
            
            if (is_locate) {
                // Чтение по указателю
                int size = get_type_size(type);
                fprintf(cg->out, "    mov rax, [rel %s]\n", var_name);
                if (size == 1) fprintf(cg->out, "    movzx rax, byte [rax]\n");
                else if (size == 2) fprintf(cg->out, "    movzx rax, word [rax]\n");
                else if (size == 4) fprintf(cg->out, "    mov eax, [rax]\n");
                else fprintf(cg->out, "    mov rax, [rax]\n");
            } else {
                // Чтение обычной переменной
                int size = get_type_size(type);
                if (size == 1) fprintf(cg->out, "    movzx rax, byte [rel %s]\n", var_name);
                else if (size == 2) fprintf(cg->out, "    movzx rax, word [rel %s]\n", var_name);
                else if (size == 4) fprintf(cg->out, "    mov eax, [rel %s]\n", var_name);
                else fprintf(cg->out, "    mov rax, [rel %s]\n", var_name);
            }
            break;
        }
        
        case NODE_SECTION_REF: {
            char var_name[256];
            snprintf(var_name, sizeof(var_name), "sect_%s_%s",
                     node->data.section_ref.section,
                     node->data.section_ref.member);
            
            // Проверяем, locate ли это
            int is_locate = 0;
            VarType type = TYPE_U64;
            for (int i = 0; i < var_count; i++) {
                if (strcmp(var_table[i].name, var_name) == 0) {
                    is_locate = var_table[i].is_locate;
                    type = var_table[i].type;
                    break;
                }
            }
            
            if (is_locate) {
                // Чтение по указателю
                int size = get_type_size(type);
                fprintf(cg->out, "    mov rax, [rel %s]\n", var_name);
                if (size == 1) fprintf(cg->out, "    movzx rax, byte [rax]\n");
                else if (size == 2) fprintf(cg->out, "    movzx rax, word [rax]\n");
                else if (size == 4) fprintf(cg->out, "    mov eax, [rax]\n");
                else fprintf(cg->out, "    mov rax, [rax]\n");
            } else {
                // Чтение обычной переменной
                int size = get_type_size(type);
                if (size == 1) fprintf(cg->out, "    movzx rax, byte [rel %s]\n", var_name);
                else if (size == 2) fprintf(cg->out, "    movzx rax, word [rel %s]\n", var_name);
                else if (size == 4) fprintf(cg->out, "    mov eax, [rel %s]\n", var_name);
                else fprintf(cg->out, "    mov rax, [rel %s]\n", var_name);
            }
            break;
        }
        
        case NODE_BINARY_OP: {
            codegen_emit_expr(cg, node->data.binary.left);
            fprintf(cg->out, "    push rax\n");
            codegen_emit_expr(cg, node->data.binary.right);
            fprintf(cg->out, "    mov rbx, rax\n");
            fprintf(cg->out, "    pop rax\n");
            
            switch (node->data.binary.op) {
                case OP_ADD: fprintf(cg->out, "    add rax, rbx\n"); break;
                case OP_SUB: fprintf(cg->out, "    sub rax, rbx\n"); break;
                case OP_MUL: fprintf(cg->out, "    imul rax, rbx\n"); break;
                case OP_DIV:
                    fprintf(cg->out, "    xor rdx, rdx\n");
                    fprintf(cg->out, "    div rbx\n");
                    break;
                case OP_MOD:
                    fprintf(cg->out, "    xor rdx, rdx\n");
                    fprintf(cg->out, "    div rbx\n");
                    fprintf(cg->out, "    mov rax, rdx\n");
                    break;
                case OP_EQ:
                    fprintf(cg->out, "    cmp rax, rbx\n");
                    fprintf(cg->out, "    sete al\n");
                    fprintf(cg->out, "    movzx rax, al\n");
                    break;
                case OP_NE:
                    fprintf(cg->out, "    cmp rax, rbx\n");
                    fprintf(cg->out, "    setne al\n");
                    fprintf(cg->out, "    movzx rax, al\n");
                    break;
                case OP_LT:
                    fprintf(cg->out, "    cmp rax, rbx\n");
                    fprintf(cg->out, "    setl al\n");
                    fprintf(cg->out, "    movzx rax, al\n");
                    break;
                case OP_LE:
                    fprintf(cg->out, "    cmp rax, rbx\n");
                    fprintf(cg->out, "    setle al\n");
                    fprintf(cg->out, "    movzx rax, al\n");
                    break;
                case OP_GT:
                    fprintf(cg->out, "    cmp rax, rbx\n");
                    fprintf(cg->out, "    setg al\n");
                    fprintf(cg->out, "    movzx rax, al\n");
                    break;
                case OP_GE:
                    fprintf(cg->out, "    cmp rax, rbx\n");
                    fprintf(cg->out, "    setge al\n");
                    fprintf(cg->out, "    movzx rax, al\n");
                    break;
                case OP_AND:
                    fprintf(cg->out, "    test rax, rax\n");
                    fprintf(cg->out, "    setnz al\n");
                    fprintf(cg->out, "    test rbx, rbx\n");
                    fprintf(cg->out, "    setnz bl\n");
                    fprintf(cg->out, "    and al, bl\n");
                    fprintf(cg->out, "    movzx rax, al\n");
                    break;
                case OP_OR:
                    fprintf(cg->out, "    test rax, rax\n");
                    fprintf(cg->out, "    setnz al\n");
                    fprintf(cg->out, "    test rbx, rbx\n");
                    fprintf(cg->out, "    setnz bl\n");
                    fprintf(cg->out, "    or al, bl\n");
                    fprintf(cg->out, "    movzx rax, al\n");
                    break;
                default:
                    break;
            }
            break;
        }
        
        case NODE_UNARY_OP:
            codegen_emit_expr(cg, node->data.unary.expr);
            if (node->data.unary.op == OP_NOT) {
                fprintf(cg->out, "    test rax, rax\n");
                fprintf(cg->out, "    sete al\n");
                fprintf(cg->out, "    movzx rax, al\n");
            } else if (node->data.unary.op == OP_SUB) {
                fprintf(cg->out, "    neg rax\n");
            }
            break;
            
        case NODE_CALL:
            if (strcmp(node->data.call.name, "inb") == 0) {
                codegen_emit_expr(cg, node->data.call.args[0]);
                fprintf(cg->out, "    mov rdx, rax\n");
                fprintf(cg->out, "    in al, dx\n");
                fprintf(cg->out, "    movzx rax, al\n");
            } else if (strcmp(node->data.call.name, "inw") == 0) {
                codegen_emit_expr(cg, node->data.call.args[0]);
                fprintf(cg->out, "    mov rdx, rax\n");
                fprintf(cg->out, "    in ax, dx\n");
                fprintf(cg->out, "    movzx rax, ax\n");
            } else if (strcmp(node->data.call.name, "inl") == 0) {
                codegen_emit_expr(cg, node->data.call.args[0]);
                fprintf(cg->out, "    mov rdx, rax\n");
                fprintf(cg->out, "    in eax, dx\n");
            } else if (strcmp(node->data.call.name, "outb") == 0) {
                codegen_emit_expr(cg, node->data.call.args[0]);
                fprintf(cg->out, "    push rax\n");
                codegen_emit_expr(cg, node->data.call.args[1]);
                fprintf(cg->out, "    pop rdx\n");
                fprintf(cg->out, "    out dx, al\n");
                fprintf(cg->out, "    xor rax, rax\n");
            } else if (strcmp(node->data.call.name, "outw") == 0) {
                codegen_emit_expr(cg, node->data.call.args[0]);
                fprintf(cg->out, "    push rax\n");
                codegen_emit_expr(cg, node->data.call.args[1]);
                fprintf(cg->out, "    pop rdx\n");
                fprintf(cg->out, "    out dx, ax\n");
                fprintf(cg->out, "    xor rax, rax\n");
            } else if (strcmp(node->data.call.name, "outl") == 0) {
                codegen_emit_expr(cg, node->data.call.args[0]);
                fprintf(cg->out, "    push rax\n");
                codegen_emit_expr(cg, node->data.call.args[1]);
                fprintf(cg->out, "    pop rdx\n");
                fprintf(cg->out, "    out dx, eax\n");
                fprintf(cg->out, "    xor rax, rax\n");
            } else if (strcmp(node->data.call.name, "mloc") == 0) {
                if (node->data.call.arg_count > 0) {
                    codegen_emit_expr(cg, node->data.call.args[0]);
                } else {
                    fprintf(cg->out, "    xor rax, rax\n");
                }
                fprintf(cg->out, "    mov rdi, rax\n");
                fprintf(cg->out, "    call mloc_impl\n");
            } else if (strcmp(node->data.call.name, "bmloc") == 0) {
                codegen_emit_expr(cg, node->data.call.args[0]);
                fprintf(cg->out, "    push rax\n");
                codegen_emit_expr(cg, node->data.call.args[1]);
                fprintf(cg->out, "    pop rdi\n");
                fprintf(cg->out, "    mov rsi, rax\n");
                fprintf(cg->out, "    call bmloc_impl\n");
            } else if (strcmp(node->data.call.name, "e820f") == 0) {
                fprintf(cg->out, "    call e820f_impl\n");
            } else if (strcmp(node->data.call.name, "printf") == 0) {
                fprintf(cg->out, "    xor rax, rax\n");
            } else {
                // Обычный вызов функции
                for (int i = 0; i < node->data.call.arg_count; i++) {
                    codegen_emit_expr(cg, node->data.call.args[i]);
                    fprintf(cg->out, "    push rax\n");
                }
                for (int i = node->data.call.arg_count - 1; i >= 0; i--) {
                    fprintf(cg->out, "    pop rax\n");
                    if (i == 0) fprintf(cg->out, "    mov rdi, rax\n");
                    else if (i == 1) fprintf(cg->out, "    mov rsi, rax\n");
                    else if (i == 2) fprintf(cg->out, "    mov rdx, rax\n");
                    else if (i == 3) fprintf(cg->out, "    mov rcx, rax\n");
                    else if (i == 4) fprintf(cg->out, "    mov r8, rax\n");
                    else if (i == 5) fprintf(cg->out, "    mov r9, rax\n");
                }
                fprintf(cg->out, "    call %s\n", node->data.call.name);
                if (node->data.call.arg_count > 6) {
                    fprintf(cg->out, "    add rsp, %d\n", (node->data.call.arg_count - 6) * 8);
                }
            }
            break;
            
        default:
            fprintf(cg->out, "    xor rax, rax\n");
            break;
    }
}

static void codegen_emit_stmt(CodeGen* cg, AstNode* node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_VARIABLE:
            if (node->data.variable.value) {
                char var_name[256];
                snprintf(var_name, sizeof(var_name), "var_%s", node->data.variable.name);
                codegen_emit_expr(cg, node->data.variable.value);
                
                if (node->data.variable.is_locate) {
                    // Для locate сохраняем адрес
                    fprintf(cg->out, "    mov [rel %s], rax\n", var_name);
                } else {
                    // Для обычных переменных сохраняем значение
                    int size = get_type_size(node->data.variable.var_type);
                    if (size == 1) fprintf(cg->out, "    mov [rel %s], al\n", var_name);
                    else if (size == 2) fprintf(cg->out, "    mov [rel %s], ax\n", var_name);
                    else if (size == 4) fprintf(cg->out, "    mov [rel %s], eax\n", var_name);
                    else fprintf(cg->out, "    mov [rel %s], rax\n", var_name);
                }
            }
            break;
            
        case NODE_ASSIGN: {
            char var_name[256];
            if (strchr(node->data.assign.name, ':')) {
                char section[64], member[64];
                sscanf(node->data.assign.name, "%[^:]:%s", section, member);
                snprintf(var_name, sizeof(var_name), "sect_%s_%s", section, member);
            } else {
                snprintf(var_name, sizeof(var_name), "var_%s", node->data.assign.name);
            }
            
            codegen_emit_expr(cg, node->data.assign.value);
            
            // Проверяем тип и locate
            int is_locate = 0;
            VarType type = TYPE_U64;
            for (int i = 0; i < var_count; i++) {
                if (strcmp(var_table[i].name, var_name) == 0) {
                    is_locate = var_table[i].is_locate;
                    type = var_table[i].type;
                    break;
                }
            }
            
            if (is_locate) {
                // Запись по указателю
                fprintf(cg->out, "    push rax\n");
                fprintf(cg->out, "    mov rax, [rel %s]\n", var_name);
                fprintf(cg->out, "    pop rbx\n");
                int size = get_type_size(type);
                if (size == 1) fprintf(cg->out, "    mov [rax], bl\n");
                else if (size == 2) fprintf(cg->out, "    mov [rax], bx\n");
                else if (size == 4) fprintf(cg->out, "    mov [rax], ebx\n");
                else fprintf(cg->out, "    mov [rax], rbx\n");
            } else {
                int size = get_type_size(type);
                if (size == 1) fprintf(cg->out, "    mov [rel %s], al\n", var_name);
                else if (size == 2) fprintf(cg->out, "    mov [rel %s], ax\n", var_name);
                else if (size == 4) fprintf(cg->out, "    mov [rel %s], eax\n", var_name);
                else fprintf(cg->out, "    mov [rel %s], rax\n", var_name);
            }
            break;
        }
        
        case NODE_IF: {
            int else_label = codegen_new_label(cg);
            int end_label = codegen_new_label(cg);
            
            codegen_emit_expr(cg, node->data.if_stmt.condition);
            fprintf(cg->out, "    test rax, rax\n");
            fprintf(cg->out, "    jz .L%d\n", else_label);
            
            if (node->data.if_stmt.then_branch->type == NODE_BLOCK) {
                for (int i = 0; i < node->data.if_stmt.then_branch->data.block.count; i++) {
                    codegen_emit_stmt(cg, node->data.if_stmt.then_branch->data.block.statements[i]);
                }
            } else {
                codegen_emit_stmt(cg, node->data.if_stmt.then_branch);
            }
            fprintf(cg->out, "    jmp .L%d\n", end_label);
            
            fprintf(cg->out, ".L%d:\n", else_label);
            if (node->data.if_stmt.else_branch) {
                if (node->data.if_stmt.else_branch->type == NODE_BLOCK) {
                    for (int i = 0; i < node->data.if_stmt.else_branch->data.block.count; i++) {
                        codegen_emit_stmt(cg, node->data.if_stmt.else_branch->data.block.statements[i]);
                    }
                } else {
                    codegen_emit_stmt(cg, node->data.if_stmt.else_branch);
                }
            }
            
            fprintf(cg->out, ".L%d:\n", end_label);
            break;
        }
        
        case NODE_WHILE: {
            int loop_label = codegen_new_label(cg);
            int end_label = codegen_new_label(cg);
            
            fprintf(cg->out, ".L%d:\n", loop_label);
            codegen_emit_expr(cg, node->data.while_loop.condition);
            fprintf(cg->out, "    test rax, rax\n");
            fprintf(cg->out, "    jz .L%d\n", end_label);
            
            if (node->data.while_loop.body->type == NODE_BLOCK) {
                for (int i = 0; i < node->data.while_loop.body->data.block.count; i++) {
                    codegen_emit_stmt(cg, node->data.while_loop.body->data.block.statements[i]);
                }
            } else {
                codegen_emit_stmt(cg, node->data.while_loop.body);
            }
            
            fprintf(cg->out, "    jmp .L%d\n", loop_label);
            fprintf(cg->out, ".L%d:\n", end_label);
            break;
        }
        
        case NODE_FOR: {
            if (node->data.for_loop.init) {
                codegen_emit_stmt(cg, node->data.for_loop.init);
            }
            
            int loop_label = codegen_new_label(cg);
            int end_label = codegen_new_label(cg);
            
            fprintf(cg->out, ".L%d:\n", loop_label);
            if (node->data.for_loop.condition) {
                codegen_emit_expr(cg, node->data.for_loop.condition);
                fprintf(cg->out, "    test rax, rax\n");
                fprintf(cg->out, "    jz .L%d\n", end_label);
            }
            
            if (node->data.for_loop.body->type == NODE_BLOCK) {
                for (int i = 0; i < node->data.for_loop.body->data.block.count; i++) {
                    codegen_emit_stmt(cg, node->data.for_loop.body->data.block.statements[i]);
                }
            } else {
                codegen_emit_stmt(cg, node->data.for_loop.body);
            }
            
            if (node->data.for_loop.post) {
                codegen_emit_expr(cg, node->data.for_loop.post);
            }
            
            fprintf(cg->out, "    jmp .L%d\n", loop_label);
            fprintf(cg->out, ".L%d:\n", end_label);
            break;
        }
        
        case NODE_RETURN:
            for (int i = 0; i < node->data.return_stmt.count; i++) {
                codegen_emit_expr(cg, node->data.return_stmt.values[i]);
                if (i < node->data.return_stmt.count - 1) {
                    fprintf(cg->out, "    push rax\n");
                }
            }
            fprintf(cg->out, "    ret\n");
            break;
            
        case NODE_JMPTO: {
            char* module_name = node->data.jmpto.filename;
            
            // Сохраняем все регистры
            for (int r = 0; r < 16; r++) {
                fprintf(cg->out, "    push r%d\n", r);
            }
            
            // Передаём параметры
            for (int i = 0; i < node->data.jmpto.var_count; i++) {
                codegen_emit_expr(cg, node->data.jmpto.vars[i]);
                fprintf(cg->out, "    push rax\n");
            }
            
            // Загружаем параметры в регистры (первые 6)
            for (int i = node->data.jmpto.var_count - 1; i >= 0; i--) {
                fprintf(cg->out, "    pop rax\n");
                if (i == 0) fprintf(cg->out, "    mov rdi, rax\n");
                else if (i == 1) fprintf(cg->out, "    mov rsi, rax\n");
                else if (i == 2) fprintf(cg->out, "    mov rdx, rax\n");
                else if (i == 3) fprintf(cg->out, "    mov rcx, rax\n");
                else if (i == 4) fprintf(cg->out, "    mov r8, rax\n");
                else if (i == 5) fprintf(cg->out, "    mov r9, rax\n");
            }
            
            fprintf(cg->out, "    call %s_entry\n", module_name);
            
            // Восстанавливаем регистры
            for (int r = 15; r >= 0; r--) {
                fprintf(cg->out, "    pop r%d\n", r);
            }
            break;
        }
        
        case NODE_INPUT:
            if (cg->safe_code == 1) {
                static int input_slot = 0;
                int slot = input_slot++ % 8;
                
                if (node->data.input.prompt) {
                    int label = get_string_label(node->data.input.prompt->data.string.value);
                    fprintf(cg->out, "    mov rax, 1\n");
                    fprintf(cg->out, "    mov rdi, 1\n");
                    fprintf(cg->out, "    lea rsi, [rel str_%d]\n", label);
                    fprintf(cg->out, "    call string_length\n");
                    fprintf(cg->out, "    mov rdx, rax\n");
                    fprintf(cg->out, "    syscall\n");
                }
                
                fprintf(cg->out, "    mov rax, 0\n");
                fprintf(cg->out, "    mov rdi, 0\n");
                fprintf(cg->out, "    lea rsi, [rel input_buf_%d]\n", slot);
                fprintf(cg->out, "    mov rdx, 255\n");
                fprintf(cg->out, "    syscall\n");
                
                if (node->data.input.target->type == NODE_IDENTIFIER) {
                    char var_name[256];
                    snprintf(var_name, sizeof(var_name), "var_%s", 
                             node->data.input.target->data.identifier.name);
                    fprintf(cg->out, "    lea rax, [rel input_buf_%d]\n", slot);
                    fprintf(cg->out, "    mov [rel %s], rax\n", var_name);
                }
            }
            break;
            
        case NODE_ASM_BLOCK:
            fprintf(cg->out, "%s\n", node->data.asm_block.instructions);
            break;
            
        case NODE_BLOCK:
            for (int i = 0; i < node->data.block.count; i++) {
                codegen_emit_stmt(cg, node->data.block.statements[i]);
            }
            break;
            
        case NODE_MLOC:
        case NODE_BMLOC:
        case NODE_MFREE:
        case NODE_E820F:
        case NODE_INB:
        case NODE_OUTB:
            codegen_emit_expr(cg, node);
            break;
            
        default:
            break;
    }
}

static void codegen_emit_helpers(CodeGen* cg) {
    fprintf(cg->out, "\n; Helper functions\n");
    
    // string_length - возвращает длину строки в rsi
    fprintf(cg->out, "string_length:\n");
    fprintf(cg->out, "    push rcx\n");
    fprintf(cg->out, "    push rdi\n");
    fprintf(cg->out, "    mov rdi, rsi\n");
    fprintf(cg->out, "    mov rcx, -1\n");
    fprintf(cg->out, "    xor al, al\n");
    fprintf(cg->out, "    repne scasb\n");
    fprintf(cg->out, "    not rcx\n");
    fprintf(cg->out, "    dec rcx\n");
    fprintf(cg->out, "    mov rax, rcx\n");
    fprintf(cg->out, "    pop rdi\n");
    fprintf(cg->out, "    pop rcx\n");
    fprintf(cg->out, "    ret\n\n");
    
    // int_to_string - преобразует rax в строку в num_buf
    fprintf(cg->out, "int_to_string:\n");
    fprintf(cg->out, "    lea rsi, [rel num_buf + 31]\n");
    fprintf(cg->out, "    mov byte [rsi], 0\n");
    fprintf(cg->out, "    test rax, rax\n");
    fprintf(cg->out, "    jz .zero\n");
    fprintf(cg->out, "    mov rcx, 10\n");
    fprintf(cg->out, ".loop:\n");
    fprintf(cg->out, "    xor rdx, rdx\n");
    fprintf(cg->out, "    div rcx\n");
    fprintf(cg->out, "    add dl, '0'\n");
    fprintf(cg->out, "    dec rsi\n");
    fprintf(cg->out, "    mov [rsi], dl\n");
    fprintf(cg->out, "    test rax, rax\n");
    fprintf(cg->out, "    jnz .loop\n");
    fprintf(cg->out, "    mov rax, rsi\n");
    fprintf(cg->out, "    ret\n");
    fprintf(cg->out, ".zero:\n");
    fprintf(cg->out, "    dec rsi\n");
    fprintf(cg->out, "    mov byte [rsi], '0'\n");
    fprintf(cg->out, "    mov rax, rsi\n");
    fprintf(cg->out, "    ret\n\n");
    
    // mloc_impl - заглушка
    fprintf(cg->out, "mloc_impl:\n");
    fprintf(cg->out, "    mov rax, rdi\n");
    fprintf(cg->out, "    ret\n\n");
    
    // bmloc_impl - заглушка
    fprintf(cg->out, "bmloc_impl:\n");
    fprintf(cg->out, "    mov rax, rdi\n");
    fprintf(cg->out, "    ret\n\n");
    
    // e820f_impl - заглушка
    fprintf(cg->out, "e820f_impl:\n");
    fprintf(cg->out, "    ret\n\n");
}

void codegen_generate(CodeGen* cg, AstNode* ast) {
    if (!ast || ast->type != NODE_PROGRAM) return;
    
    reset_var_table();
    collect_vars(ast, 1);
    collect_strings(ast);
    
    codegen_emit_directives(cg, ast);
    codegen_emit_data(cg);
    codegen_emit_rodata(cg);
    codegen_emit_bss(cg);
    
    fprintf(cg->out, "section .text\n");
    
    if (cg->safe_code == 1) {
        fprintf(cg->out, "    global _start\n");
    }
    
    for (int i = 0; i < var_count; i++) {
        if (var_table[i].is_global) {
            fprintf(cg->out, "    global %s\n", var_table[i].name);
        }
    }
    fprintf(cg->out, "    default rel\n\n");
    
    codegen_emit_helpers(cg);
    
    if (cg->safe_code == 1) {
        fprintf(cg->out, "_start:\n");
        fprintf(cg->out, "    call main\n");
        fprintf(cg->out, "    mov rdi, rax\n");
        fprintf(cg->out, "    mov rax, 60\n");
        fprintf(cg->out, "    syscall\n");
    }
    
    // Генерируем глобальный код (не функции)
    for (int i = 0; i < ast->data.program.count; i++) {
        AstNode* item = ast->data.program.items[i];
        if (item->type == NODE_FUNCTION || item->type == NODE_DIRECTIVE) {
            continue;
        }
        codegen_emit_stmt(cg, item);
    }
    
    // Генерируем функции
    for (int i = 0; i < ast->data.program.count; i++) {
        AstNode* item = ast->data.program.items[i];
        if (item->type == NODE_FUNCTION) {
            fprintf(cg->out, "\n%s:\n", item->data.function.name);
            
            if (!item->attr_bclear) {
                fprintf(cg->out, "    push rbp\n");
                fprintf(cg->out, "    mov rbp, rsp\n");
                fprintf(cg->out, "    sub rsp, 32\n");
            }
            
            // Загрузка параметров
            for (int j = 0; j < item->data.function.param_count; j++) {
                AstNode* param = item->data.function.params[j];
                if (param->type == NODE_VARIABLE) {
                    char var_name[256];
                    snprintf(var_name, sizeof(var_name), "var_%s", param->data.variable.name);
                    if (j == 0) fprintf(cg->out, "    mov [rel %s], rdi\n", var_name);
                    else if (j == 1) fprintf(cg->out, "    mov [rel %s], rsi\n", var_name);
                    else if (j == 2) fprintf(cg->out, "    mov [rel %s], rdx\n", var_name);
                    else if (j == 3) fprintf(cg->out, "    mov [rel %s], rcx\n", var_name);
                    else if (j == 4) fprintf(cg->out, "    mov [rel %s], r8\n", var_name);
                    else if (j == 5) fprintf(cg->out, "    mov [rel %s], r9\n", var_name);
                }
            }
            
            // Тело функции
            if (item->data.function.body->type == NODE_BLOCK) {
                for (int j = 0; j < item->data.function.body->data.block.count; j++) {
                    codegen_emit_stmt(cg, item->data.function.body->data.block.statements[j]);
                }
            } else {
                codegen_emit_stmt(cg, item->data.function.body);
            }
            
            // Эпилог
            if (!item->attr_bclear) {
                fprintf(cg->out, "    mov rsp, rbp\n");
                fprintf(cg->out, "    pop rbp\n");
            }
            fprintf(cg->out, "    ret\n");
        }
    }
    
    free_string_table();
    reset_var_table();
}

void codegen_free(CodeGen* cg) {
    (void)cg;
}
