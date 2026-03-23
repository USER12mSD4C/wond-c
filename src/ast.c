#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ast.h"

void ast_free(AstNode* node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_IMPORT:
            free(node->data.import.module);
            if (node->data.import.func) free(node->data.import.func);
            if (node->data.import.alias) free(node->data.import.alias);
            break;
            
        case NODE_DIRECTIVE:
            free(node->data.directive.name);
            free(node->data.directive.value);
            break;
            
        case NODE_NUMBER:
            free(node->data.number.value);
            break;
            
        case NODE_STRING:
            free(node->data.string.value);
            break;
            
        case NODE_IDENTIFIER:
            free(node->data.identifier.name);
            break;
            
        case NODE_SECTION_REF:
            free(node->data.section_ref.section);
            free(node->data.section_ref.member);
            break;
            
        case NODE_VARIABLE:
            free(node->data.variable.name);
            ast_free(node->data.variable.value);
            break;
            
        case NODE_ASSIGN:
            free(node->data.assign.name);
            ast_free(node->data.assign.value);
            break;
            
        case NODE_BINARY_OP:
            ast_free(node->data.binary.left);
            ast_free(node->data.binary.right);
            break;
            
        case NODE_UNARY_OP:
            ast_free(node->data.unary.expr);
            break;
            
        case NODE_CALL:
            free(node->data.call.name);
            for (int i = 0; i < node->data.call.arg_count; i++) {
                ast_free(node->data.call.args[i]);
            }
            free(node->data.call.args);
            break;
            
        case NODE_IF:
            ast_free(node->data.if_stmt.condition);
            ast_free(node->data.if_stmt.then_branch);
            ast_free(node->data.if_stmt.else_branch);
            break;
            
        case NODE_WHILE:
            ast_free(node->data.while_loop.condition);
            ast_free(node->data.while_loop.body);
            break;
            
        case NODE_FOR:
            ast_free(node->data.for_loop.init);
            ast_free(node->data.for_loop.condition);
            ast_free(node->data.for_loop.body);
            ast_free(node->data.for_loop.post);
            break;
            
        case NODE_FUNCTION:
            free(node->data.function.name);
            for (int i = 0; i < node->data.function.param_count; i++) {
                ast_free(node->data.function.params[i]);
            }
            free(node->data.function.params);
            ast_free(node->data.function.body);
            break;
            
        case NODE_BLOCK:
            for (int i = 0; i < node->data.block.count; i++) {
                ast_free(node->data.block.statements[i]);
            }
            free(node->data.block.statements);
            break;
            
        case NODE_RETURN:
            for (int i = 0; i < node->data.return_stmt.count; i++) {
                ast_free(node->data.return_stmt.values[i]);
            }
            free(node->data.return_stmt.values);
            break;
            
        case NODE_SECTION:
            free(node->data.section.name);
            for (int i = 0; i < node->data.section.var_count; i++) {
                ast_free(node->data.section.variables[i]);
            }
            free(node->data.section.variables);
            break;
            
        case NODE_ASM_BLOCK:
            free(node->data.asm_block.instructions);
            break;
            
        case NODE_JMPTO:
            free(node->data.jmpto.filename);
            for (int i = 0; i < node->data.jmpto.var_count; i++) {
                ast_free(node->data.jmpto.vars[i]);
            }
            free(node->data.jmpto.vars);
            ast_free(node->data.jmpto.block);
            break;
            
        case NODE_INPUT:
            ast_free(node->data.input.prompt);
            ast_free(node->data.input.target);
            break;
            
        case NODE_MLOC:
            ast_free(node->data.mloc.owner);
            ast_free(node->data.mloc.size);
            ast_free(node->data.mloc.align);
            break;
            
        case NODE_BMLOC:
            ast_free(node->data.bmloc.address);
            ast_free(node->data.bmloc.size);
            break;
            
        case NODE_MFREE:
            ast_free(node->data.mfree.target);
            break;
            
        case NODE_INB:
            ast_free(node->data.inb.port);
            break;
            
        case NODE_OUTB:
            ast_free(node->data.outb.port);
            ast_free(node->data.outb.value);
            break;
            
        case NODE_E820F:
            break;
            
        case NODE_STRUCT:
            free(node->data.struct_def.name);
            for (int i = 0; i < node->data.struct_def.field_count; i++) {
                free(node->data.struct_def.fields[i]->name);
                free(node->data.struct_def.fields[i]);
            }
            free(node->data.struct_def.fields);
            break;
            
        case NODE_ENUM:
            free(node->data.enum_def.name);
            for (int i = 0; i < node->data.enum_def.value_count; i++) {
                free(node->data.enum_def.values[i]->name);
                free(node->data.enum_def.values[i]);
            }
            free(node->data.enum_def.values);
            break;
            
        case NODE_PROGRAM:
            for (int i = 0; i < node->data.program.count; i++) {
                ast_free(node->data.program.items[i]);
            }
            free(node->data.program.items);
            break;
            
        default:
            break;
    }
    free(node);
}

AstNode* ast_create_import(char* module, char* func, char* alias, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_IMPORT;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.import.module = module;
    node->data.import.func = func;
    node->data.import.alias = alias;
    return node;
}

AstNode* ast_create_directive(char* name, char* value, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_DIRECTIVE;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.directive.name = name;
    node->data.directive.value = value;
    return node;
}

AstNode* ast_create_number(char* value, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_NUMBER;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.number.value = value;
    return node;
}

AstNode* ast_create_string(char* value, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_STRING;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.string.value = value;
    return node;
}

AstNode* ast_create_identifier(char* name, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_IDENTIFIER;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.identifier.name = name;
    return node;
}

AstNode* ast_create_section_ref(char* section, char* member, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_SECTION_REF;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.section_ref.section = section;
    node->data.section_ref.member = member;
    return node;
}

AstNode* ast_create_variable(VarType type, int is_locate, char* name, AstNode* value, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_VARIABLE;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.variable.var_type = type;
    node->data.variable.is_locate = is_locate;
    node->data.variable.name = name;
    node->data.variable.value = value;
    return node;
}

AstNode* ast_create_assign(char* name, AstNode* value, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_ASSIGN;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.assign.name = name;
    node->data.assign.value = value;
    return node;
}

AstNode* ast_create_binary_op(OperatorType op, AstNode* left, AstNode* right, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_BINARY_OP;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.binary.op = op;
    node->data.binary.left = left;
    node->data.binary.right = right;
    return node;
}

AstNode* ast_create_unary_op(OperatorType op, AstNode* expr, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_UNARY_OP;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.unary.op = op;
    node->data.unary.expr = expr;
    return node;
}

AstNode* ast_create_call(char* name, AstNode** args, int arg_count, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_CALL;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.call.name = name;
    node->data.call.args = args;
    node->data.call.arg_count = arg_count;
    return node;
}

AstNode* ast_create_if(AstNode* cond, AstNode* then_branch, AstNode* else_branch, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_IF;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.if_stmt.condition = cond;
    node->data.if_stmt.then_branch = then_branch;
    node->data.if_stmt.else_branch = else_branch;
    return node;
}

AstNode* ast_create_while(AstNode* cond, AstNode* body, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_WHILE;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.while_loop.condition = cond;
    node->data.while_loop.body = body;
    return node;
}

AstNode* ast_create_for(AstNode* init, AstNode* cond, AstNode* body, AstNode* post, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_FOR;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.for_loop.init = init;
    node->data.for_loop.condition = cond;
    node->data.for_loop.body = body;
    node->data.for_loop.post = post;
    return node;
}

AstNode* ast_create_function(char* name, AstNode** params, int param_count, AstNode* body, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_FUNCTION;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.function.name = name;
    node->data.function.params = params;
    node->data.function.param_count = param_count;
    node->data.function.body = body;
    return node;
}

AstNode* ast_create_block(AstNode** statements, int count, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_BLOCK;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.block.statements = statements;
    node->data.block.count = count;
    return node;
}

AstNode* ast_create_return(AstNode** values, int count, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_RETURN;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.return_stmt.values = values;
    node->data.return_stmt.count = count;
    return node;
}

AstNode* ast_create_section(char* name, AstNode** vars, int var_count, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_SECTION;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.section.name = name;
    node->data.section.variables = vars;
    node->data.section.var_count = var_count;
    return node;
}

AstNode* ast_create_asm_block(char* instructions, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_ASM_BLOCK;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.asm_block.instructions = instructions;
    return node;
}

AstNode* ast_create_jmpto(char* filename, AstNode** vars, int var_count, AstNode* block, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_JMPTO;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.jmpto.filename = filename;
    node->data.jmpto.vars = vars;
    node->data.jmpto.var_count = var_count;
    node->data.jmpto.block = block;
    return node;
}

AstNode* ast_create_input(AstNode* prompt, AstNode* target, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_INPUT;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.input.prompt = prompt;
    node->data.input.target = target;
    return node;
}

AstNode* ast_create_mloc(AstNode* owner, AstNode* size, AstNode* align, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_MLOC;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.mloc.owner = owner;
    node->data.mloc.size = size;
    node->data.mloc.align = align;
    return node;
}

AstNode* ast_create_bmloc(AstNode* addr, AstNode* size, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_BMLOC;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.bmloc.address = addr;
    node->data.bmloc.size = size;
    return node;
}

AstNode* ast_create_mfree(AstNode* target, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_MFREE;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.mfree.target = target;
    return node;
}

AstNode* ast_create_inb(AstNode* port, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_INB;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.inb.port = port;
    return node;
}

AstNode* ast_create_outb(AstNode* port, AstNode* value, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_OUTB;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.outb.port = port;
    node->data.outb.value = value;
    return node;
}

AstNode* ast_create_e820f(int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_E820F;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    return node;
}

AstNode* ast_create_program(AstNode** items, int count, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_PROGRAM;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.program.items = items;
    node->data.program.count = count;
    return node;
}

AstNode* ast_create_struct(char* name, int version, int is_reflect, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_STRUCT;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.struct_def.name = name;
    node->data.struct_def.version = version;
    node->data.struct_def.fields = NULL;
    node->data.struct_def.field_count = 0;
    node->data.struct_def.is_reflect = is_reflect;
    node->data.struct_def.size = 0;
    return node;
}

void ast_struct_add_field(AstNode* struct_node, char* name, VarType type, 
                          int version_added, int version_removed, int is_pointer, int array_size) {
    if (!struct_node || struct_node->type != NODE_STRUCT) return;
    
    int count = struct_node->data.struct_def.field_count;
    struct_node->data.struct_def.fields = realloc(
        struct_node->data.struct_def.fields,
        (count + 1) * sizeof(FieldInfo*)
    );
    
    FieldInfo* field = malloc(sizeof(FieldInfo));
    field->name = name;
    field->type = type;
    field->version_added = version_added;
    field->version_removed = version_removed;
    field->offset = 0;
    field->is_pointer = is_pointer;
    field->array_size = array_size;
    
    struct_node->data.struct_def.fields[count] = field;
    struct_node->data.struct_def.field_count++;
}

AstNode* ast_create_enum(char* name, int version, int line, int column) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = NODE_ENUM;
    node->line = line;
    node->column = column;
    node->attr_baint = 0;
    node->attr_bclear = 0;
    node->data.enum_def.name = name;
    node->data.enum_def.version = version;
    node->data.enum_def.values = NULL;
    node->data.enum_def.value_count = 0;
    node->data.enum_def.size = 4;
    return node;
}

void ast_enum_add_value(AstNode* enum_node, char* name, uint64_t value, 
                        int version_added, int version_removed) {
    if (!enum_node || enum_node->type != NODE_ENUM) return;
    
    int count = enum_node->data.enum_def.value_count;
    enum_node->data.enum_def.values = realloc(
        enum_node->data.enum_def.values,
        (count + 1) * sizeof(EnumValue*)
    );
    
    EnumValue* val = malloc(sizeof(EnumValue));
    val->name = name;
    val->value = value;
    val->version_added = version_added;
    val->version_removed = version_removed;
    
    enum_node->data.enum_def.values[count] = val;
    enum_node->data.enum_def.value_count++;
    
    // Определяем размер enum (наибольшее значение)
    if (value > 0xFFFFFFFF) {
        enum_node->data.enum_def.size = 8;
    } else if (value > 0xFFFF) {
        if (enum_node->data.enum_def.size < 4) enum_node->data.enum_def.size = 4;
    } else if (value > 0xFF) {
        if (enum_node->data.enum_def.size < 2) enum_node->data.enum_def.size = 2;
    }
}
