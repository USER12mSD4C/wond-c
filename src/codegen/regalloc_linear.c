#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "regalloc_linear.h"
#include "../platform.h"

typedef struct {
    int temp;
    int start;
    int end;
} LiveInterval;

static int compare_intervals(const void* a, const void* b) {
    const LiveInterval* ia = (const LiveInterval*)a;
    const LiveInterval* ib = (const LiveInterval*)b;
    return ia->start - ib->start;
}

static void compute_live_intervals(IRProgram* prog, LiveInterval** intervals, int* count) {
    // проход по всем инструкциям, собираем uses и defs
    int* first_use = platform_malloc(prog->temp_count * sizeof(int));
    int* last_use = platform_malloc(prog->temp_count * sizeof(int));
    
    for (int i = 0; i < prog->temp_count; i++) {
        first_use[i] = -1;
        last_use[i] = -1;
    }
    
    int pos = 0;
    IRIns* ins = prog->head;
    while (ins) {
        // проверяем dest
        if (ins->dest >= 0 && ins->dest < prog->temp_count) {
            if (first_use[ins->dest] == -1) first_use[ins->dest] = pos;
            last_use[ins->dest] = pos;
        }
        // проверяем src1
        if (ins->src1 >= 0 && ins->src1 < prog->temp_count) {
            if (first_use[ins->src1] == -1) first_use[ins->src1] = pos;
            last_use[ins->src1] = pos;
        }
        // проверяем src2
        if (ins->src2 >= 0 && ins->src2 < prog->temp_count) {
            if (first_use[ins->src2] == -1) first_use[ins->src2] = pos;
            last_use[ins->src2] = pos;
        }
        
        pos++;
        ins = ins->next;
    }
    
    // собираем интервалы
    *count = 0;
    for (int i = 0; i < prog->temp_count; i++) {
        if (first_use[i] != -1) {
            (*count)++;
        }
    }
    
    *intervals = platform_malloc((*count) * sizeof(LiveInterval));
    int idx = 0;
    for (int i = 0; i < prog->temp_count; i++) {
        if (first_use[i] != -1) {
            (*intervals)[idx].temp = i;
            (*intervals)[idx].start = first_use[i];
            (*intervals)[idx].end = last_use[i];
            idx++;
        }
    }
    
    platform_free(first_use);
    platform_free(last_use);
    
    qsort(*intervals, *count, sizeof(LiveInterval), compare_intervals);
}

LinearRegAlloc* linear_allocator_new(int reg_count) {
    LinearRegAlloc* ra = platform_malloc(sizeof(LinearRegAlloc));
    ra->reg_count = reg_count;
    ra->reg_map = platform_malloc(4096 * sizeof(int)); // максимум 4096 временных
    ra->reg_used = platform_malloc(reg_count * sizeof(int));
    ra->spill_slots = platform_malloc(4096 * sizeof(int));
    ra->spill_count = 0;
    
    for (int i = 0; i < 4096; i++) {
        ra->reg_map[i] = -1;
        ra->spill_slots[i] = -1;
    }
    for (int i = 0; i < reg_count; i++) {
        ra->reg_used[i] = 0;
    }
    
    return ra;
}

void linear_allocator_free(LinearRegAlloc* ra) {
    if (ra) {
        if (ra->reg_map) platform_free(ra->reg_map);
        if (ra->reg_used) platform_free(ra->reg_used);
        if (ra->spill_slots) platform_free(ra->spill_slots);
        platform_free(ra);
    }
}

void linear_allocate(LinearRegAlloc* ra, IRProgram* prog) {
    LiveInterval* intervals;
    int interval_count;
    compute_live_intervals(prog, &intervals, &interval_count);
    
    int* active = platform_malloc(ra->reg_count * sizeof(int));
    int active_count = 0;
    
    for (int i = 0; i < interval_count; i++) {
        LiveInterval* curr = &intervals[i];
        
        // удаляем завершившиеся интервалы
        for (int j = 0; j < active_count; j++) {
            if (intervals[active[j]].end < curr->start) {
                // освобождаем регистр
                int reg = ra->reg_map[intervals[active[j]].temp];
                if (reg >= 0) {
                    ra->reg_used[reg] = 0;
                }
                // сдвигаем
                for (int k = j; k < active_count - 1; k++) {
                    active[k] = active[k+1];
                }
                active_count--;
                j--;
            }
        }
        
        // ищем свободный регистр
        int reg = -1;
        for (int j = 0; j < ra->reg_count; j++) {
            if (!ra->reg_used[j]) {
                reg = j;
                break;
            }
        }
        
        if (reg >= 0) {
            // назначаем регистр
            ra->reg_map[curr->temp] = reg;
            ra->reg_used[reg] = 1;
            active[active_count++] = i;
        } else {
            // spilling - вытесняем самый дальний интервал
            int spill_idx = -1;
            int max_end = -1;
            for (int j = 0; j < active_count; j++) {
                if (intervals[active[j]].end > max_end) {
                    max_end = intervals[active[j]].end;
                    spill_idx = active[j];
                }
            }
            
            if (spill_idx >= 0 && intervals[spill_idx].end > curr->end) {
                // вытесняем существующий
                int spill_temp = intervals[spill_idx].temp;
                int spill_reg = ra->reg_map[spill_temp];
                
                // сохраняем на стек
                ra->spill_slots[spill_temp] = ra->spill_count++;
                ra->reg_map[spill_temp] = -1;
                
                // назначаем регистр текущему
                ra->reg_map[curr->temp] = spill_reg;
                ra->reg_used[spill_reg] = 1;
                
                // заменяем в active
                for (int j = 0; j < active_count; j++) {
                    if (active[j] == spill_idx) {
                        active[j] = i;
                        break;
                    }
                }
            } else {
                // спиливаем текущий
                ra->spill_slots[curr->temp] = ra->spill_count++;
            }
        }
    }
    
    platform_free(intervals);
    platform_free(active);
}

int linear_get_reg(LinearRegAlloc* ra, int temp) {
    if (temp < 0 || temp >= 4096) return -1;
    return ra->reg_map[temp];
}

int linear_get_spill(LinearRegAlloc* ra, int temp) {
    if (temp < 0 || temp >= 4096) return -1;
    return ra->spill_slots[temp];
}
