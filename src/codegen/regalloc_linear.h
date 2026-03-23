#ifndef REGALLOC_LINEAR_H
#define REGALLOC_LINEAR_H

#include "ir.h"

typedef struct {
    int* reg_map;       // временная переменная -> физический регистр
    int reg_count;
    int* reg_used;      // занят ли регистр
    int* spill_slots;   // временная переменная -> слот на стеке
    int spill_count;
} LinearRegAlloc;

LinearRegAlloc* linear_allocator_new(int reg_count);
void linear_allocator_free(LinearRegAlloc* ra);
void linear_allocate(LinearRegAlloc* ra, IRProgram* prog);

int linear_get_reg(LinearRegAlloc* ra, int temp);
int linear_get_spill(LinearRegAlloc* ra, int temp);

#endif
