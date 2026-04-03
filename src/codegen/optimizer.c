#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "optimizer.h"
#include "../platform.h"

extern int verbose;

static int valid_temp(int temp, int temp_count) {
    return temp >= 0 && temp < temp_count;
}

static int ins_writes_temp(const IRIns* ins) {
    switch (ins->op) {
        case IR_CONST:
        case IR_GLOBAL:
        case IR_LOAD:
        case IR_LOAD_ADDR:
        case IR_DEREF:
        case IR_TEMP:
        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_DIV:
        case IR_MOD:
        case IR_EQ:
        case IR_NE:
        case IR_LT:
        case IR_LE:
        case IR_GT:
        case IR_GE:
        case IR_AND:
        case IR_OR:
        case IR_NOT:
        case IR_NEG:
        case IR_CALL:
        case IR_POP:
            return 1;
        default:
            return 0;
    }
}

static int op_has_side_effect(IROp op) {
    switch (op) {
        case IR_STORE:
        case IR_LABEL:
        case IR_JMP:
        case IR_JZ:
        case IR_JNZ:
        case IR_CALL:
        case IR_RETURN:
        case IR_PARAM:
        case IR_MODULE_CALL:
        case IR_PUSH:
        case IR_POP:
        case IR_STRING:
            return 1;
        default:
            return 0;
    }
}

static void free_ir_ins(IRIns* ins) {
    if (!ins) return;
    if (ins->var_name) platform_free(ins->var_name);
    if (ins->module_name) platform_free(ins->module_name);
    if (ins->string_val) platform_free(ins->string_val);
    if (ins->section_var.section) platform_free(ins->section_var.section);
    if (ins->section_var.var) platform_free(ins->section_var.var);
    platform_free(ins);
}

static int get_const_for_temp(int temp, int temp_count, const int* known, const int* value, int* out) {
    if (!valid_temp(temp, temp_count) || !known[temp]) {
        return 0;
    }
    *out = value[temp];
    return 1;
}

void fold_constants(IRProgram* prog) {
    if (!prog || prog->temp_count <= 0) return;

    int* known = calloc((size_t)prog->temp_count, sizeof(int));
    int* value = calloc((size_t)prog->temp_count, sizeof(int));
    if (!known || !value) {
        free(known);
        free(value);
        return;
    }

    IRIns* ins = prog->head;
    while (ins) {
        int left = 0;
        int right = 0;
        int folded = 0;
        int folded_value = 0;

        switch (ins->op) {
            case IR_ADD:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = left + right;
                    folded = 1;
                }
                break;
            case IR_SUB:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = left - right;
                    folded = 1;
                }
                break;
            case IR_MUL:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = left * right;
                    folded = 1;
                }
                break;
            case IR_DIV:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right) &&
                    right != 0) {
                    folded_value = left / right;
                    folded = 1;
                }
                break;
            case IR_MOD:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right) &&
                    right != 0) {
                    folded_value = left % right;
                    folded = 1;
                }
                break;
            case IR_EQ:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = (left == right);
                    folded = 1;
                }
                break;
            case IR_NE:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = (left != right);
                    folded = 1;
                }
                break;
            case IR_LT:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = (left < right);
                    folded = 1;
                }
                break;
            case IR_LE:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = (left <= right);
                    folded = 1;
                }
                break;
            case IR_GT:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = (left > right);
                    folded = 1;
                }
                break;
            case IR_GE:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = (left >= right);
                    folded = 1;
                }
                break;
            case IR_AND:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = left & right;
                    folded = 1;
                }
                break;
            case IR_OR:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left) &&
                    get_const_for_temp(ins->src2, prog->temp_count, known, value, &right)) {
                    folded_value = left | right;
                    folded = 1;
                }
                break;
            case IR_NOT:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left)) {
                    folded_value = !left;
                    folded = 1;
                }
                break;
            case IR_NEG:
                if (get_const_for_temp(ins->src1, prog->temp_count, known, value, &left)) {
                    folded_value = -left;
                    folded = 1;
                }
                break;
            default:
                break;
        }

        if (folded) {
            ins->op = IR_CONST;
            ins->const_val = folded_value;
            ins->src1 = -1;
            ins->src2 = -1;
        }

        if (valid_temp(ins->dest, prog->temp_count)) {
            if (ins->op == IR_CONST) {
                known[ins->dest] = 1;
                value[ins->dest] = ins->const_val;
            } else if (ins->op == IR_TEMP &&
                       valid_temp(ins->src1, prog->temp_count) &&
                       known[ins->src1]) {
                known[ins->dest] = 1;
                value[ins->dest] = value[ins->src1];
            } else {
                known[ins->dest] = 0;
            }
        }

        ins = ins->next;
    }

    free(known);
    free(value);
}

void eliminate_dead_code(IRProgram* prog) {
    if (!prog || prog->temp_count <= 0) return;

    int* used = calloc((size_t)prog->temp_count, sizeof(int));
    if (!used) return;

    IRIns* ins = prog->head;
    while (ins) {
        if (valid_temp(ins->src1, prog->temp_count)) used[ins->src1] = 1;
        if (valid_temp(ins->src2, prog->temp_count)) used[ins->src2] = 1;
        ins = ins->next;
    }

    IRIns* prev = NULL;
    ins = prog->head;
    while (ins) {
        IRIns* next = ins->next;
        int keep = 1;

        if (ins_writes_temp(ins) &&
            valid_temp(ins->dest, prog->temp_count) &&
            !used[ins->dest] &&
            !op_has_side_effect(ins->op)) {
            keep = 0;
        }

        if (keep) {
            prev = ins;
        } else {
            if (prev) prev->next = next;
            else prog->head = next;
            if (next == NULL) prog->tail = prev;
            free_ir_ins(ins);
        }
        ins = next;
    }

    free(used);
}

void propagate_copies(IRProgram* prog) {
    if (!prog || prog->temp_count <= 0) return;

    int* copy_of = calloc((size_t)prog->temp_count, sizeof(int));
    if (!copy_of) return;
    for (int i = 0; i < prog->temp_count; i++) copy_of[i] = -1;

    IRIns* ins = prog->head;
    while (ins) {
        if (valid_temp(ins->src1, prog->temp_count) && copy_of[ins->src1] >= 0) {
            ins->src1 = copy_of[ins->src1];
        }
        if (valid_temp(ins->src2, prog->temp_count) && copy_of[ins->src2] >= 0) {
            ins->src2 = copy_of[ins->src2];
        }

        if (ins->op == IR_TEMP &&
            valid_temp(ins->dest, prog->temp_count) &&
            valid_temp(ins->src1, prog->temp_count)) {
            int src = ins->src1;
            while (valid_temp(src, prog->temp_count) && copy_of[src] >= 0) {
                src = copy_of[src];
            }
            copy_of[ins->dest] = src;
        } else if (ins_writes_temp(ins) && valid_temp(ins->dest, prog->temp_count)) {
            copy_of[ins->dest] = -1;
        }

        ins = ins->next;
    }

    free(copy_of);
}

void optimize_ir(IRProgram* prog, int level) {
    if (!prog || level <= 0) return;

    if (verbose) platform_eprintf("Optimizing IR at level %d\n", level);

    if (level >= 1) {
        fold_constants(prog);
        eliminate_dead_code(prog);
    }

    if (level >= 2) {
        propagate_copies(prog);
        fold_constants(prog);
        eliminate_dead_code(prog);
    }
}
