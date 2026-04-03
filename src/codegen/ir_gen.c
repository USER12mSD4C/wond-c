#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ir_gen.h"
#include "../platform.h"

extern int verbose;

typedef struct {
    IRProgram* prog;
    int safe_code;
    int alloc_type;
    int* temps;
    int temp_count;
    int temp_cap;
    char** globals;
    int global_count;
    int global_cap;
    char** global_defs;
    int global_def_count;
    int global_def_cap;
    char** strings;
    int string_count;
    int string_cap;
} GenContext;

static void add_global(GenContext* ctx, const char* name) {
    for (int i = 0; i < ctx->global_count; i++) {
        if (strcmp(ctx->globals[i], name) == 0) return;
    }
    
    if (ctx->global_count >= ctx->global_cap) {
        ctx->global_cap = ctx->global_cap ? ctx->global_cap * 2 : 64;
        ctx->globals = platform_realloc(ctx->globals, ctx->global_cap * sizeof(char*));
    }
    ctx->globals[ctx->global_count++] = platform_strdup(name);
}

static void add_global_def(GenContext* ctx, const char* name) {
    for (int i = 0; i < ctx->global_def_count; i++) {
        if (strcmp(ctx->global_defs[i], name) == 0) return;
    }
    if (ctx->global_def_count >= ctx->global_def_cap) {
        ctx->global_def_cap = ctx->global_def_cap ? ctx->global_def_cap * 2 : 64;
        ctx->global_defs = platform_realloc(ctx->global_defs, ctx->global_def_cap * sizeof(char*));
    }
    ctx->global_defs[ctx->global_def_count++] = platform_strdup(name);
    add_global(ctx, name);
}

static int add_string(GenContext* ctx, const char* str) {
    for (int i = 0; i < ctx->string_count; i++) {
        if (strcmp(ctx->strings[i], str) == 0) return i;
    }
    
    if (ctx->string_count >= ctx->string_cap) {
        ctx->string_cap = ctx->string_cap ? ctx->string_cap * 2 : 64;
        ctx->strings = platform_realloc(ctx->strings, ctx->string_cap * sizeof(char*));
    }
    ctx->strings[ctx->string_count] = platform_strdup(str);
    return ctx->string_count++;
}

static int new_temp(GenContext* ctx) {
    if (ctx->temp_count >= ctx->temp_cap) {
        ctx->temp_cap = ctx->temp_cap ? ctx->temp_cap * 2 : 64;
        ctx->temps = platform_realloc(ctx->temps, ctx->temp_cap * sizeof(int));
    }
    int idx = ctx->prog->temp_count++;
    ctx->temps[ctx->temp_count++] = idx;
    return idx;
}

static void module_entry_name(const char* module_name, char* out, size_t out_sz) {
    size_t pos;
    if (!module_name || !*module_name) {
        snprintf(out, out_sz, "__jmpto_module_main");
        return;
    }
    pos = 0;
    pos += snprintf(out + pos, out_sz - pos, "__jmpto_");
    for (const char* p = module_name; *p && pos + 6 < out_sz; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) out[pos++] = (char)c;
        else out[pos++] = '_';
    }
    if (pos + 6 >= out_sz) pos = out_sz - 6;
    memcpy(out + pos, "_main", 6);
}

static void gen_expr(GenContext* ctx, AstNode* node, int dest) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_NUMBER: {
            IRIns* ins = ir_ins_new(IR_CONST);
            ins->const_val = atoi(node->data.number.value);
            ins->dest = dest;
            ir_emit(ctx->prog, ins);
            break;
        }
        
        case NODE_STRING: {
            int str_idx = add_string(ctx, node->data.string.value);
            char str_name[256];
            snprintf(str_name, sizeof(str_name), "str_%d", str_idx);
            add_global(ctx, str_name);
            IRIns* ins = ir_ins_new(IR_LOAD_ADDR);
            ins->var_name = platform_strdup(str_name);
            ins->dest = dest;
            ir_emit(ctx->prog, ins);
            break;
        }
        
        case NODE_IDENTIFIER: {
            char var_name[256];
            snprintf(var_name, sizeof(var_name), "var_%s", node->data.identifier.name);
            add_global(ctx, var_name);
            IRIns* ins = ir_ins_new(IR_GLOBAL);
            ins->var_name = platform_strdup(var_name);
            ins->dest = dest;
            ir_emit(ctx->prog, ins);
            break;
        }
        
        case NODE_SECTION_REF: {
            char var_name[256];
            snprintf(var_name, sizeof(var_name), "sect_%s_%s", 
                     node->data.section_ref.section, node->data.section_ref.member);
            add_global(ctx, var_name);
            IRIns* ins = ir_ins_new(IR_GLOBAL);
            ins->var_name = platform_strdup(var_name);
            ins->dest = dest;
            ir_emit(ctx->prog, ins);
            break;
        }
        
        case NODE_BINARY_OP: {
            int left_temp = new_temp(ctx);
            int right_temp = new_temp(ctx);
            
            gen_expr(ctx, node->data.binary.left, left_temp);
            gen_expr(ctx, node->data.binary.right, right_temp);
            
            IROp op;
            switch (node->data.binary.op) {
                case OP_ADD: op = IR_ADD; break;
                case OP_SUB: op = IR_SUB; break;
                case OP_MUL: op = IR_MUL; break;
                case OP_DIV: op = IR_DIV; break;
                case OP_MOD: op = IR_MOD; break;
                case OP_EQ: op = IR_EQ; break;
                case OP_NE: op = IR_NE; break;
                case OP_LT: op = IR_LT; break;
                case OP_LE: op = IR_LE; break;
                case OP_GT: op = IR_GT; break;
                case OP_GE: op = IR_GE; break;
                case OP_AND: op = IR_AND; break;
                case OP_OR: op = IR_OR; break;
                default: op = IR_ADD; break;
            }
            
            IRIns* ins = ir_ins_new(op);
            ins->src1 = left_temp;
            ins->src2 = right_temp;
            ins->dest = dest;
            ir_emit(ctx->prog, ins);
            break;
        }
        
        case NODE_UNARY_OP: {
            int src_temp = new_temp(ctx);
            gen_expr(ctx, node->data.unary.expr, src_temp);
            
            IROp op;
            if (node->data.unary.op == OP_NOT) {
                op = IR_NOT;
            } else if (node->data.unary.op == OP_SUB) {
                op = IR_NEG;
            } else {
                op = IR_NOT;
            }
            
            IRIns* ins = ir_ins_new(op);
            ins->src1 = src_temp;
            ins->dest = dest;
            ir_emit(ctx->prog, ins);
            break;
        }
        
        case NODE_CALL: {
            int* args = NULL;
            int arg_count = node->data.call.arg_count;
            
            if (arg_count > 0) {
                args = platform_malloc(arg_count * sizeof(int));
                for (int i = 0; i < arg_count; i++) {
                    int temp = new_temp(ctx);
                    gen_expr(ctx, node->data.call.args[i], temp);
                    args[i] = temp;
                }
            }
            
            for (int i = arg_count - 1; i >= 0; i--) {
                IRIns* param = ir_ins_new(IR_PARAM);
                param->src1 = args[i];
                param->dest = i;
                ir_emit(ctx->prog, param);
            }
            
            IRIns* ins = ir_ins_new(IR_CALL);
            ins->var_name = platform_strdup(node->data.call.name);
            ins->param_count = arg_count;
            ins->dest = dest;
            ir_emit(ctx->prog, ins);
            
            if (args) platform_free(args);
            break;
        }
        
        default:
            break;
    }
}

static void gen_stmt(GenContext* ctx, AstNode* node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_VARIABLE: {
            char var_name[256];
            snprintf(var_name, sizeof(var_name), "var_%s", node->data.variable.name);
            add_global_def(ctx, var_name);
            
            if (node->data.variable.value) {
                int temp = new_temp(ctx);
                gen_expr(ctx, node->data.variable.value, temp);
                
                IRIns* ins = ir_ins_new(IR_STORE);
                ins->var_name = platform_strdup(var_name);
                ins->src1 = temp;
                ir_emit(ctx->prog, ins);
            }
            break;
        }
        
        case NODE_ASSIGN: {
            char var_name[256];
            if (strchr(node->data.assign.name, ':')) {
                char sec[64], mem[64];
                sscanf(node->data.assign.name, "%[^:]:%s", sec, mem);
                snprintf(var_name, sizeof(var_name), "sect_%s_%s", sec, mem);
            } else {
                snprintf(var_name, sizeof(var_name), "var_%s", node->data.assign.name);
            }
            add_global(ctx, var_name);
            
            int temp = new_temp(ctx);
            gen_expr(ctx, node->data.assign.value, temp);
            
            IRIns* ins = ir_ins_new(IR_STORE);
            ins->var_name = platform_strdup(var_name);
            ins->src1 = temp;
            ir_emit(ctx->prog, ins);
            break;
        }
        
        case NODE_IF: {
            int cond_temp = new_temp(ctx);
            gen_expr(ctx, node->data.if_stmt.condition, cond_temp);
            
            int else_label = ir_new_label(ctx->prog);
            int end_label = ir_new_label(ctx->prog);
            
            IRIns* jz = ir_ins_new(IR_JZ);
            jz->src1 = cond_temp;
            jz->label = else_label;
            ir_emit(ctx->prog, jz);
            
            if (node->data.if_stmt.then_branch->type == NODE_BLOCK) {
                for (int i = 0; i < node->data.if_stmt.then_branch->data.block.count; i++) {
                    gen_stmt(ctx, node->data.if_stmt.then_branch->data.block.statements[i]);
                }
            } else {
                gen_stmt(ctx, node->data.if_stmt.then_branch);
            }
            
            IRIns* jmp = ir_ins_new(IR_JMP);
            jmp->label = end_label;
            ir_emit(ctx->prog, jmp);
            
            IRIns* label_else = ir_ins_new(IR_LABEL);
            label_else->label = else_label;
            ir_emit(ctx->prog, label_else);
            
            if (node->data.if_stmt.else_branch) {
                if (node->data.if_stmt.else_branch->type == NODE_BLOCK) {
                    for (int i = 0; i < node->data.if_stmt.else_branch->data.block.count; i++) {
                        gen_stmt(ctx, node->data.if_stmt.else_branch->data.block.statements[i]);
                    }
                } else {
                    gen_stmt(ctx, node->data.if_stmt.else_branch);
                }
            }
            
            IRIns* label_end = ir_ins_new(IR_LABEL);
            label_end->label = end_label;
            ir_emit(ctx->prog, label_end);
            break;
        }
        
        case NODE_WHILE: {
            int loop_label = ir_new_label(ctx->prog);
            int end_label = ir_new_label(ctx->prog);
            
            IRIns* label_loop = ir_ins_new(IR_LABEL);
            label_loop->label = loop_label;
            ir_emit(ctx->prog, label_loop);
            
            int cond_temp = new_temp(ctx);
            gen_expr(ctx, node->data.while_loop.condition, cond_temp);
            
            IRIns* jz = ir_ins_new(IR_JZ);
            jz->src1 = cond_temp;
            jz->label = end_label;
            ir_emit(ctx->prog, jz);
            
            if (node->data.while_loop.body->type == NODE_BLOCK) {
                for (int i = 0; i < node->data.while_loop.body->data.block.count; i++) {
                    gen_stmt(ctx, node->data.while_loop.body->data.block.statements[i]);
                }
            } else {
                gen_stmt(ctx, node->data.while_loop.body);
            }
            
            IRIns* jmp = ir_ins_new(IR_JMP);
            jmp->label = loop_label;
            ir_emit(ctx->prog, jmp);
            
            IRIns* label_end = ir_ins_new(IR_LABEL);
            label_end->label = end_label;
            ir_emit(ctx->prog, label_end);
            break;
        }
        
        case NODE_FOR: {
            if (node->data.for_loop.init) {
                gen_stmt(ctx, node->data.for_loop.init);
            }
            
            int loop_label = ir_new_label(ctx->prog);
            int end_label = ir_new_label(ctx->prog);
            
            IRIns* label_loop = ir_ins_new(IR_LABEL);
            label_loop->label = loop_label;
            ir_emit(ctx->prog, label_loop);
            
            if (node->data.for_loop.condition) {
                int cond_temp = new_temp(ctx);
                gen_expr(ctx, node->data.for_loop.condition, cond_temp);
                
                IRIns* jz = ir_ins_new(IR_JZ);
                jz->src1 = cond_temp;
                jz->label = end_label;
                ir_emit(ctx->prog, jz);
            }
            
            if (node->data.for_loop.body->type == NODE_BLOCK) {
                for (int i = 0; i < node->data.for_loop.body->data.block.count; i++) {
                    gen_stmt(ctx, node->data.for_loop.body->data.block.statements[i]);
                }
            } else {
                gen_stmt(ctx, node->data.for_loop.body);
            }
            
            if (node->data.for_loop.post) {
                int post_temp = new_temp(ctx);
                gen_expr(ctx, node->data.for_loop.post, post_temp);
            }
            
            IRIns* jmp = ir_ins_new(IR_JMP);
            jmp->label = loop_label;
            ir_emit(ctx->prog, jmp);
            
            IRIns* label_end = ir_ins_new(IR_LABEL);
            label_end->label = end_label;
            ir_emit(ctx->prog, label_end);
            break;
        }
        
        case NODE_RETURN: {
            if (node->data.return_stmt.count > 0) {
                int ret_temp = new_temp(ctx);
                gen_expr(ctx, node->data.return_stmt.values[0], ret_temp);
                
                IRIns* ins = ir_ins_new(IR_RETURN);
                ins->src1 = ret_temp;
                ir_emit(ctx->prog, ins);
            } else {
                IRIns* ins = ir_ins_new(IR_RETURN);
                ir_emit(ctx->prog, ins);
            }
            break;
        }
        
        case NODE_JMPTO: {
            int* args = NULL;
            int arg_count = node->data.jmpto.var_count;
            char entry_name[256];
            
            if (arg_count > 0) {
                args = platform_malloc(arg_count * sizeof(int));
                for (int i = 0; i < arg_count; i++) {
                    int temp = new_temp(ctx);
                    gen_expr(ctx, node->data.jmpto.vars[i], temp);
                    args[i] = temp;
                }
            }
            
            for (int i = arg_count - 1; i >= 0; i--) {
                IRIns* param = ir_ins_new(IR_PARAM);
                param->src1 = args[i];
                param->dest = i;
                ir_emit(ctx->prog, param);
            }
            
            module_entry_name(node->data.jmpto.filename, entry_name, sizeof(entry_name));
            IRIns* ins = ir_ins_new(IR_CALL);
            ins->var_name = platform_strdup(entry_name);
            ins->param_count = arg_count;
            ins->dest = -1;
            ir_emit(ctx->prog, ins);
            
            if (args) platform_free(args);
            break;
        }
        
        case NODE_BLOCK: {
            for (int i = 0; i < node->data.block.count; i++) {
                gen_stmt(ctx, node->data.block.statements[i]);
            }
            break;
        }

        case NODE_CALL: {
            int tmp = new_temp(ctx);
            gen_expr(ctx, node, tmp);
            break;
        }
        
        default:
            break;
    }
}

static void gen_function(GenContext* ctx, AstNode* func) {
    if (func->type != NODE_FUNCTION) return;
    
    IRIns* label = ir_ins_new(IR_LABEL);
    label->label = ir_new_label(ctx->prog);
    label->var_name = platform_strdup(func->data.function.name);
    ir_emit(ctx->prog, label);
    
    if (!func->attr_bclear) {
        IRIns* push = ir_ins_new(IR_PUSH);
        push->src1 = -1;
        ir_emit(ctx->prog, push);
    }
    
    for (int i = 0; i < func->data.function.param_count; i++) {
        AstNode* param = func->data.function.params[i];
        if (param->type == NODE_VARIABLE) {
            char var_name[256];
            snprintf(var_name, sizeof(var_name), "var_%s", param->data.variable.name);
            add_global_def(ctx, var_name);
            
            IRIns* param_ins = ir_ins_new(IR_PARAM);
            param_ins->src1 = i;
            param_ins->dest = new_temp(ctx);
            ir_emit(ctx->prog, param_ins);
            
            IRIns* store = ir_ins_new(IR_STORE);
            store->var_name = platform_strdup(var_name);
            store->src1 = param_ins->dest;
            ir_emit(ctx->prog, store);
        }
    }
    
    if (func->data.function.body->type == NODE_BLOCK) {
        for (int i = 0; i < func->data.function.body->data.block.count; i++) {
            gen_stmt(ctx, func->data.function.body->data.block.statements[i]);
        }
    } else {
        gen_stmt(ctx, func->data.function.body);
    }
    
    if (!func->attr_bclear) {
        IRIns* pop = ir_ins_new(IR_POP);
        pop->src1 = -1;
        ir_emit(ctx->prog, pop);
    }
    
    IRIns* ret = ir_ins_new(IR_RETURN);
    ir_emit(ctx->prog, ret);
}

IRProgram* ir_generate(AstNode* ast, int safe_code, int alloc_type) {
    if (!ast || ast->type != NODE_PROGRAM) return NULL;
    
    GenContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.prog = ir_program_new();
    ctx.safe_code = safe_code;
    ctx.alloc_type = alloc_type;
    
    // Сначала собираем все глобальные переменные
    for (int i = 0; i < ast->data.program.count; i++) {
        AstNode* item = ast->data.program.items[i];
        if (item->type == NODE_VARIABLE) {
            char var_name[256];
            snprintf(var_name, sizeof(var_name), "var_%s", item->data.variable.name);
            add_global_def(&ctx, var_name);
        } else if (item->type == NODE_SECTION) {
            for (int j = 0; j < item->data.section.var_count; j++) {
                AstNode* var = item->data.section.variables[j];
                if (var->type == NODE_VARIABLE) {
                    char var_name[256];
                    snprintf(var_name, sizeof(var_name), "sect_%s_%s", 
                             item->data.section.name, var->data.variable.name);
                    add_global_def(&ctx, var_name);
                }
            }
        }
    }
    
    // Генерируем код для переменных
    for (int i = 0; i < ast->data.program.count; i++) {
        AstNode* item = ast->data.program.items[i];
        if (item->type == NODE_VARIABLE) {
            gen_stmt(&ctx, item);
        } else if (item->type == NODE_SECTION) {
            for (int j = 0; j < item->data.section.var_count; j++) {
                gen_stmt(&ctx, item->data.section.variables[j]);
            }
        }
    }
    
    // Генерируем функции
    for (int i = 0; i < ast->data.program.count; i++) {
        AstNode* item = ast->data.program.items[i];
        if (item->type == NODE_FUNCTION) {
            gen_function(&ctx, item);
        }
    }
    
    if (verbose) {
        fprintf(stderr, "DEBUG ir_generate: global_count=%d\n", ctx.global_count);
        for (int i = 0; i < ctx.global_count; i++) {
            fprintf(stderr, "  global[%d]: %s\n", i, ctx.globals[i]);
        }
    }
    
    // Добавляем IR_GLOBAL для всех переменных
    for (int i = 0; i < ctx.global_def_count; i++) {
        char* name = ctx.global_defs[i];
        IRIns* decl = ir_ins_new(IR_GLOBAL);
        decl->var_name = platform_strdup(name);
        decl->dest = -1;
        ir_emit(ctx.prog, decl);
    }
    
    // Добавляем строки
    for (int i = 0; i < ctx.string_count; i++) {
        char str_name[256];
        snprintf(str_name, sizeof(str_name), "str_%d", i);
        IRIns* str_ins = ir_ins_new(IR_STRING);
        str_ins->var_name = platform_strdup(str_name);
        str_ins->string_val = platform_strdup(ctx.strings[i]);
        ir_emit(ctx.prog, str_ins);
    }
    
    // НЕ ОСВОБОЖДАЕМ НИЧЕГО, кроме temps
    if (ctx.temps) {
        platform_free(ctx.temps);
    }
    for (int i = 0; i < ctx.global_count; i++) {
        platform_free(ctx.globals[i]);
    }
    if (ctx.globals) platform_free(ctx.globals);
    for (int i = 0; i < ctx.global_def_count; i++) {
        platform_free(ctx.global_defs[i]);
    }
    if (ctx.global_defs) platform_free(ctx.global_defs);
    for (int i = 0; i < ctx.string_count; i++) {
        platform_free(ctx.strings[i]);
    }
    if (ctx.strings) platform_free(ctx.strings);
    
    return ctx.prog;
}
