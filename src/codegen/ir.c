#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "../platform.h"

IRProgram* ir_program_new(void) {
    IRProgram* prog = platform_malloc(sizeof(IRProgram));
    prog->head = NULL;
    prog->tail = NULL;
    prog->temp_count = 0;
    prog->label_count = 0;
    return prog;
}

void ir_program_free(IRProgram* prog) {
    if (!prog) return;
    
    IRIns* ins = prog->head;
    while (ins) {
        IRIns* next = ins->next;
        // Освобождаем только если указатели не NULL
        if (ins->var_name) {
            platform_free(ins->var_name);
            ins->var_name = NULL;
        }
        if (ins->module_name) {
            platform_free(ins->module_name);
            ins->module_name = NULL;
        }
        if (ins->string_val) {
            platform_free(ins->string_val);
            ins->string_val = NULL;
        }
        if (ins->section_var.section) {
            platform_free(ins->section_var.section);
            ins->section_var.section = NULL;
        }
        if (ins->section_var.var) {
            platform_free(ins->section_var.var);
            ins->section_var.var = NULL;
        }
        platform_free(ins);
        ins = next;
    }
    
    platform_free(prog);
}

void ir_emit(IRProgram* prog, IRIns* ins) {
    ins->next = NULL;
    
    if (!prog->head) {
        prog->head = ins;
        prog->tail = ins;
    } else {
        prog->tail->next = ins;
        prog->tail = ins;
    }
}

IRIns* ir_ins_new(IROp op) {
    IRIns* ins = platform_malloc(sizeof(IRIns));
    memset(ins, 0, sizeof(IRIns));
    ins->op = op;
    ins->dest = -1;
    ins->src1 = -1;
    ins->src2 = -1;
    ins->label = -1;
    ins->param_count = 0;
    ins->next = NULL;
    return ins;
}

int ir_new_label(IRProgram* prog) {
    return prog->label_count++;
}

IRIns* ir_ins_const(int val) {
    IRIns* ins = ir_ins_new(IR_CONST);
    ins->const_val = val;
    return ins;
}

IRIns* ir_ins_load_global(const char* name) {
    IRIns* ins = ir_ins_new(IR_GLOBAL);
    if (name) ins->var_name = platform_strdup(name);
    return ins;
}

IRIns* ir_ins_load_section(const char* section, const char* var) {
    IRIns* ins = ir_ins_new(IR_SECTION);
    if (section) ins->section_var.section = platform_strdup(section);
    if (var) ins->section_var.var = platform_strdup(var);
    return ins;
}

IRIns* ir_ins_store_global(const char* name, int src) {
    IRIns* ins = ir_ins_new(IR_STORE);
    if (name) ins->var_name = platform_strdup(name);
    ins->src1 = src;
    return ins;
}

IRIns* ir_ins_store_section(const char* section, const char* var, int src) {
    IRIns* ins = ir_ins_new(IR_STORE);
    if (section) ins->section_var.section = platform_strdup(section);
    if (var) ins->section_var.var = platform_strdup(var);
    ins->src1 = src;
    return ins;
}

IRIns* ir_ins_binary(IROp op, int left, int right) {
    IRIns* ins = ir_ins_new(op);
    ins->src1 = left;
    ins->src2 = right;
    return ins;
}

IRIns* ir_ins_call(const char* name, int* args, int arg_count) {
    (void)args;
    IRIns* ins = ir_ins_new(IR_CALL);
    if (name) ins->var_name = platform_strdup(name);
    ins->param_count = arg_count;
    return ins;
}

IRIns* ir_ins_module_call(const char* module, int* args, int arg_count) {
    (void)args;
    IRIns* ins = ir_ins_new(IR_MODULE_CALL);
    if (module) ins->module_name = platform_strdup(module);
    ins->param_count = arg_count;
    return ins;
}
