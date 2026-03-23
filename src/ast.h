#ifndef AST_H
#define AST_H

#include <stdint.h>

typedef enum {
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_STRING, TYPE_PTR,
    TYPE_VOID
} VarType;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_AND, OP_OR, OP_NOT
} OperatorType;

typedef enum {
    NODE_PROGRAM,
    NODE_IMPORT,
    NODE_DIRECTIVE,
    NODE_NUMBER,
    NODE_STRING,
    NODE_IDENTIFIER,
    NODE_SECTION_REF,
    NODE_VARIABLE,
    NODE_ASSIGN,
    NODE_BINARY_OP,
    NODE_UNARY_OP,
    NODE_CALL,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_FUNCTION,
    NODE_BLOCK,
    NODE_RETURN,
    NODE_SECTION,
    NODE_ASM_BLOCK,
    NODE_JMPTO,
    NODE_INPUT,
    NODE_MLOC,
    NODE_MFREE,
    NODE_BMLOC,
    NODE_E820F,
    NODE_INB,
    NODE_OUTB,
    NODE_STRUCT,
    NODE_ENUM
} NodeType;

typedef struct FieldInfo {
    char* name;
    VarType type;
    int version_added;
    int version_removed;
    int offset;
    int is_pointer;
    int array_size;
} FieldInfo;

typedef struct EnumValue {
    char* name;
    uint64_t value;
    int version_added;
    int version_removed;
} EnumValue;

typedef struct AstNode {
    NodeType type;
    int line;
    int column;
    int attr_baint;
    int attr_bclear;
    
    union {
        struct {
            char* module;
            char* func;
            char* alias;
        } import;
        
        struct {
            char* name;
            char* value;
        } directive;
        
        struct {
            char* value;
        } number;
        
        struct {
            char* value;
        } string;
        
        struct {
            char* name;
        } identifier;
        
        struct {
            char* section;
            char* member;
        } section_ref;
        
        struct {
            VarType var_type;
            int is_locate;
            char* name;
            struct AstNode* value;
        } variable;
        
        struct {
            char* name;
            struct AstNode* value;
        } assign;
        
        struct {
            OperatorType op;
            struct AstNode* left;
            struct AstNode* right;
        } binary;
        
        struct {
            OperatorType op;
            struct AstNode* expr;
        } unary;
        
        struct {
            char* name;
            struct AstNode** args;
            int arg_count;
        } call;
        
        struct {
            struct AstNode* condition;
            struct AstNode* then_branch;
            struct AstNode* else_branch;
        } if_stmt;
        
        struct {
            struct AstNode* condition;
            struct AstNode* body;
        } while_loop;
        
        struct {
            struct AstNode* init;
            struct AstNode* condition;
            struct AstNode* body;
            struct AstNode* post;
        } for_loop;
        
        struct {
            char* name;
            struct AstNode** params;
            int param_count;
            struct AstNode* body;
        } function;
        
        struct {
            struct AstNode** statements;
            int count;
        } block;
        
        struct {
            struct AstNode** values;
            int count;
        } return_stmt;
        
        struct {
            char* name;
            struct AstNode** variables;
            int var_count;
        } section;
        
        struct {
            char* instructions;
        } asm_block;
        
        struct {
            char* filename;
            struct AstNode** vars;
            int var_count;
            struct AstNode* block;
        } jmpto;
        
        struct {
            struct AstNode* prompt;
            struct AstNode* target;
        } input;
        
        struct {
            struct AstNode* owner;
            struct AstNode* size;
            struct AstNode* align;
        } mloc;
        
        struct {
            struct AstNode* address;
            struct AstNode* size;
        } bmloc;
        
        struct {
            struct AstNode* target;
        } mfree;
        
        struct {
            struct AstNode* port;
        } inb;
        
        struct {
            struct AstNode* port;
            struct AstNode* value;
        } outb;
        
        struct {
            char* name;
            int version;
            FieldInfo** fields;
            int field_count;
            int is_reflect;
            int size;
        } struct_def;
        
        struct {
            char* name;
            int version;
            int size;
            EnumValue** values;
            int value_count;
        } enum_def;
        
        struct {
            struct AstNode** items;
            int count;
        } program;
        
    } data;
} AstNode;

void ast_free(AstNode* node);

AstNode* ast_create_import(char* module, char* func, char* alias, int line, int column);
AstNode* ast_create_directive(char* name, char* value, int line, int column);
AstNode* ast_create_number(char* value, int line, int column);
AstNode* ast_create_string(char* value, int line, int column);
AstNode* ast_create_identifier(char* name, int line, int column);
AstNode* ast_create_section_ref(char* section, char* member, int line, int column);
AstNode* ast_create_variable(VarType type, int is_locate, char* name, AstNode* value, int line, int column);
AstNode* ast_create_assign(char* name, AstNode* value, int line, int column);
AstNode* ast_create_binary_op(OperatorType op, AstNode* left, AstNode* right, int line, int column);
AstNode* ast_create_unary_op(OperatorType op, AstNode* expr, int line, int column);
AstNode* ast_create_call(char* name, AstNode** args, int arg_count, int line, int column);
AstNode* ast_create_if(AstNode* cond, AstNode* then_branch, AstNode* else_branch, int line, int column);
AstNode* ast_create_while(AstNode* cond, AstNode* body, int line, int column);
AstNode* ast_create_for(AstNode* init, AstNode* cond, AstNode* body, AstNode* post, int line, int column);
AstNode* ast_create_function(char* name, AstNode** params, int param_count, AstNode* body, int line, int column);
AstNode* ast_create_block(AstNode** statements, int count, int line, int column);
AstNode* ast_create_return(AstNode** values, int count, int line, int column);
AstNode* ast_create_section(char* name, AstNode** vars, int var_count, int line, int column);
AstNode* ast_create_asm_block(char* instructions, int line, int column);
AstNode* ast_create_jmpto(char* filename, AstNode** vars, int var_count, AstNode* block, int line, int column);
AstNode* ast_create_input(AstNode* prompt, AstNode* target, int line, int column);
AstNode* ast_create_mloc(AstNode* owner, AstNode* size, AstNode* align, int line, int column);
AstNode* ast_create_bmloc(AstNode* addr, AstNode* size, int line, int column);
AstNode* ast_create_mfree(AstNode* target, int line, int column);
AstNode* ast_create_inb(AstNode* port, int line, int column);
AstNode* ast_create_outb(AstNode* port, AstNode* value, int line, int column);
AstNode* ast_create_e820f(int line, int column);
AstNode* ast_create_program(AstNode** items, int count, int line, int column);
AstNode* ast_create_struct(char* name, int version, int is_reflect, int line, int column);
void ast_struct_add_field(AstNode* struct_node, char* name, VarType type, int version_added, int version_removed, int is_pointer, int array_size);
AstNode* ast_create_enum(char* name, int version, int line, int column);
void ast_enum_add_value(AstNode* enum_node, char* name, uint64_t value, int version_added, int version_removed);

#endif
