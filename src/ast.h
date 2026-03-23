#ifndef AST_H
#define AST_H

#include <stdint.h>

typedef enum {
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_STRING,
    TYPE_VOID
} VarType;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_AND, OP_OR, OP_NOT
} OperatorType;

typedef enum {
    NODE_PROGRAM,
    NODE_IMPORT,         // #import
    NODE_DIRECTIVE,      // adrload, bits
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
    NODE_OUTB
} NodeType;

typedef struct AstNode {
    NodeType type;
    int line;
    int column;
    int attr_baint;      // для функций-обработчиков прерываний
    int attr_bclear;     // для функций без пролога/эпилога
    
    union {
        // Импорт
        struct {
            char* module;
            char* func;
            char* alias;
        } import;
        
        // Директивы
        struct {
            char* name;
            char* value;
        } directive;
        
        // Число
        struct {
            char* value;
        } number;
        
        // Строка
        struct {
            char* value;
        } string;
        
        // Идентификатор
        struct {
            char* name;
        } identifier;
        
        // Ссылка на секцию (section:member)
        struct {
            char* section;
            char* member;
        } section_ref;
        
        // Объявление переменной
        struct {
            VarType var_type;
            int is_locate;
            char* name;
            struct AstNode* value;
        } variable;
        
        // Присваивание
        struct {
            char* name;
            struct AstNode* value;
        } assign;
        
        // Бинарная операция
        struct {
            OperatorType op;
            struct AstNode* left;
            struct AstNode* right;
        } binary;
        
        // Унарная операция
        struct {
            OperatorType op;
            struct AstNode* expr;
        } unary;
        
        // Вызов функции
        struct {
            char* name;
            struct AstNode** args;
            int arg_count;
        } call;
        
        // If
        struct {
            struct AstNode* condition;
            struct AstNode* then_branch;
            struct AstNode* else_branch;
        } if_stmt;
        
        // While
        struct {
            struct AstNode* condition;
            struct AstNode* body;
        } while_loop;
        
        // For
        struct {
            struct AstNode* init;
            struct AstNode* condition;
            struct AstNode* body;
            struct AstNode* post;
        } for_loop;
        
        // Функция
        struct {
            char* name;
            struct AstNode** params;
            int param_count;
            struct AstNode* body;
        } function;
        
        // Блок
        struct {
            struct AstNode** statements;
            int count;
        } block;
        
        // Return
        struct {
            struct AstNode** values;
            int count;
        } return_stmt;
        
        // Секция (sect.name)
        struct {
            char* name;
            struct AstNode** variables;
            int var_count;
        } section;
        
        // Ассемблерный блок
        struct {
            char* instructions;
        } asm_block;
        
        // jmpto
        struct {
            char* filename;
            struct AstNode** vars;
            int var_count;
            struct AstNode* block;
        } jmpto;
        
        // input
        struct {
            struct AstNode* prompt;
            struct AstNode* target;
        } input;
        
        // mloc (обычное выделение)
        struct {
            struct AstNode* owner;
            struct AstNode* size;
            struct AstNode* align;
        } mloc;
        
        // bmloc (выделение по физическому адресу)
        struct {
            struct AstNode* address;
            struct AstNode* size;
        } bmloc;
        
        // mfree
        struct {
            struct AstNode* target;
        } mfree;
        
        // inb/outb
        struct {
            struct AstNode* port;
        } inb;
        
        struct {
            struct AstNode* port;
            struct AstNode* value;
        } outb;
        
        // e820f - нет данных
        
        // Программа (корневой узел)
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

#endif
