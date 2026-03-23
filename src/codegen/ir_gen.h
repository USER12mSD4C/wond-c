#ifndef IR_GEN_H
#define IR_GEN_H

#include "../ast.h"
#include "ir.h"

IRProgram* ir_generate(AstNode* ast, int safe_code, int alloc_type);

#endif
