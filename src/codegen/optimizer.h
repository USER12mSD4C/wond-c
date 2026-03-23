#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "ir.h"

void optimize_ir(IRProgram* prog, int level);
void eliminate_dead_code(IRProgram* prog);
void fold_constants(IRProgram* prog);
void propagate_copies(IRProgram* prog);

#endif
