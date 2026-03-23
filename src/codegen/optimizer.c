#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "optimizer.h"
#include "../platform.h"

extern int verbose;

static int is_const(IRIns* ins) {
    return ins->op == IR_CONST;
}

static int get_const_val(IRIns* ins) {
    return ins->const_val;
}

static void replace_use(IRIns* ins, int old_temp, int new_temp) {
    if (ins->src1 == old_temp) ins->src1 = new_temp;
    if (ins->src2 == old_temp) ins->src2 = new_temp;
    if (ins->dest == old_temp) ins->dest = new_temp;
}

void fold_constants(IRProgram* prog) {
    IRIns* ins = prog->head;
    while (ins) {
        if (ins->op == IR_ADD && is_const(ins->src1) && is_const(ins->src2)) {
            ins->const_val = get_const_val(ins->src1) + get_const_val(ins->src2);
            ins->op = IR_CONST;
            ins->src1 = -1;
            ins->src2 = -1;
        }
        else if (ins->op == IR_SUB && is_const(ins->src1) && is_const(ins->src2)) {
            ins->const_val = get_const_val(ins->src1) - get_const_val(ins->src2);
            ins->op = IR_CONST;
        }
        else if (ins->op == IR_MUL && is_const(ins->src1) && is_const(ins->src2)) {
            ins->const_val = get_const_val(ins->src1) * get_const_val(ins->src2);
            ins->op = IR_CONST;
        }
        else if (ins->op == IR_DIV && is_const(ins->src1) && is_const(ins->src2) && get_const_val(ins->src2) != 0) {
            ins->const_val = get_const_val(ins->src1) / get_const_val(ins->src2);
            ins->op = IR_CONST;
        }
        ins = ins->next;
    }
}

void eliminate_dead_code(IRProgram* prog) {
    // Простой маркировочный анализ
    int* used = calloc(prog->temp_count, sizeof(int));
    
    // Отмечаем используемые временные
    IRIns* ins = prog->head;
    while (ins) {
        if (ins->src1 >= 0 && ins->src1 < prog->temp_count) used[ins->src1] = 1;
        if (ins->src2 >= 0 && ins->src2 < prog->temp_count) used[ins->src2] = 1;
        if (ins->dest >= 0 && ins->dest < prog->temp_count && ins->op != IR_CONST) used[ins->dest] = 1;
        ins = ins->next;
    }
    
    // Удаляем неиспользуемые определения
    IRIns* prev = NULL;
    ins = prog->head;
    while (ins) {
        IRIns* next = ins->next;
        int keep = 1;
        
        if (ins->dest >= 0 && ins->dest < prog->temp_count && !used[ins->dest] && ins->op != IR_RETURN && ins->op != IR_CALL) {
            keep = 0;
        }
        
        if (keep) {
            prev = ins;
        } else {
            if (prev) prev->next = next;
            else prog->head = next;
            if (next == NULL) prog->tail = prev;
            
            if (ins->var_name) platform_free(ins->var_name);
            if (ins->module_name) platform_free(ins->module_name);
            if (ins->string_val) platform_free(ins->string_val);
            platform_free(ins);
        }
        ins = next;
    }
    
    free(used);
}

void propagate_copies(IRProgram* prog) {
    // Простая копи-пропагация
    int* copy_of = calloc(prog->temp_count, sizeof(int));
    for (int i = 0; i < prog->temp_count; i++) copy_of[i] = -1;
    
    IRIns* ins = prog->head;
    while (ins) {
        if (ins->op == IR_MOV && ins->src1 >= 0) {
            copy_of[ins->dest] = ins->src1;
        } else {
            if (ins->src1 >= 0 && copy_of[ins->src1] >= 0) ins->src1 = copy_of[ins->src1];
            if (ins->src2 >= 0 && copy_of[ins->src2] >= 0) ins->src2 = copy_of[ins->src2];
            if (ins->dest >= 0 && copy_of[ins->dest] >= 0) ins->dest = copy_of[ins->dest];
        }
        ins = ins->next;
    }
    
    free(copy_of);
}

void optimize_ir(IRProgram* prog, int level) {
    if (level <= 0) return;
    
    if (verbose) platform_eprintf("Optimizing IR at level %d\n", level);
    
    // Уровень 1: базовые оптимизации
    if (level >= 1) {
        fold_constants(prog);
        eliminate_dead_code(prog);
    }
    
    // Уровень 2: более агрессивные
    if (level >= 2) {
        propagate_copies(prog);
        fold_constants(prog);
        eliminate_dead_code(prog);
    }
}
