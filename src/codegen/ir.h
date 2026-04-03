#ifndef IR_H
#define IR_H

#include <stdint.h>

typedef enum {
    IR_TEMP,
    IR_GLOBAL,
    IR_SECTION,
    IR_CONST,
    IR_STRING,
    
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_EQ, IR_NE, IR_LT, IR_LE, IR_GT, IR_GE,
    IR_AND, IR_OR, IR_NOT,
    IR_NEG,
    
    IR_LOAD,
    IR_STORE,
    IR_LOAD_ADDR,
    IR_DEREF,
    
    IR_LABEL,
    IR_JMP,
    IR_JZ,
    IR_JNZ,
    
    IR_CALL,
    IR_RETURN,
    IR_PARAM,
    
    IR_MODULE_CALL,
    
    IR_PUSH,
    IR_POP,
} IROp;

typedef struct IRIns {
    IROp op;
    int dest;
    int src1, src2;

    int const_val;
    char* var_name;
    char* module_name;
    char* string_val;
    struct {
        char* section;
        char* var;
    } section_var;
    
    int label;
    int param_count;
    
    struct IRIns* next;
} IRIns;

typedef struct {
    IRIns* head;
    IRIns* tail;
    int temp_count;
    int label_count;
} IRProgram;

IRProgram* ir_program_new(void);
void ir_program_free(IRProgram* prog);
void ir_emit(IRProgram* prog, IRIns* ins);
IRIns* ir_ins_new(IROp op);
int ir_new_label(IRProgram* prog);

IRIns* ir_ins_const(int val);
IRIns* ir_ins_load_global(const char* name);
IRIns* ir_ins_load_section(const char* section, const char* var);
IRIns* ir_ins_store_global(const char* name, int src);
IRIns* ir_ins_store_section(const char* section, const char* var, int src);
IRIns* ir_ins_binary(IROp op, int left, int right);
IRIns* ir_ins_call(const char* name, int* args, int arg_count);
IRIns* ir_ins_module_call(const char* module, int* args, int arg_count);

#endif
