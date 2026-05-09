#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"
#include "ast.h"

extern int verbose;

static char* parser_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* d = malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

static const char* v_spec_for_arg(AstNode* arg) {
    if (!arg) return "%lld";
    if (arg->type == NODE_STRING) return "%s";
    return "%lld";
}

static char* normalize_printf_format(const char* fmt, AstNode** args, int arg_count) {
    size_t in_len, i, out_pos;
    int v_index;
    char* out;

    if (!fmt) return parser_strdup("");

    in_len = strlen(fmt);
    out = malloc(in_len * 4 + 1);
    if (!out) { fprintf(stderr, "Out of memory\n"); exit(1); }

    out_pos = 0;
    v_index = 0;
    for (i = 0; i < in_len; i++) {
        if (fmt[i] == '%' && i + 1 < in_len) {
            if (fmt[i + 1] == '%') {
                out[out_pos++] = '%';
                out[out_pos++] = '%';
                i++;
                continue;
            }
            if (fmt[i + 1] == 'v') {
                const char* spec = "%lld";
                if (v_index < arg_count) spec = v_spec_for_arg(args[v_index]);
                size_t spec_len = strlen(spec);
                memcpy(out + out_pos, spec, spec_len);
                out_pos += spec_len;
                v_index++;
                i++;
                continue;
            }
        }
        out[out_pos++] = fmt[i];
    }
    out[out_pos] = '\0';
    return out;
}

static void* safe_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr) { fprintf(stderr, "Out of memory\n"); exit(1); }
    return new_ptr;
}

void parser_init(Parser* parser, Lexer* lexer) {
    parser->lexer = lexer;
    parser->current = lexer_next_token(lexer);
    parser->next = lexer_next_token(lexer);
    parser->safe_code = -1;
    parser->current_bits = 0;
    parser->current_adrload = NULL;
    parser->first_pass = 0;
    parser->symbol_table = NULL;
    parser->symbol_count = 0;
    parser->symbol_cap = 0;
}

static void parser_advance(Parser* parser) {
    token_free(&parser->current);
    parser->current = parser->next;
    parser->next = lexer_next_token(parser->lexer);
}

void parser_error(Parser* parser, const char* message) {
    fprintf(stderr, "[UTMS] ERR %d.%d: %s\n",
            parser->current.line, parser->current.column, message);
    exit(1);
}

void parser_consume(Parser* parser, TokenType type, const char* expected) {
    if (parser->current.type == type) {
        parser_advance(parser);
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "expected '%s', got '%s'",
             expected, parser->current.value ? parser->current.value : "EOF");
    parser_error(parser, buf);
}

static void add_symbol(Parser* parser, const char* name, int type) {
    if (parser->symbol_count >= parser->symbol_cap) {
        parser->symbol_cap = parser->symbol_cap ? parser->symbol_cap * 2 : 128;
        parser->symbol_table = safe_realloc(parser->symbol_table, parser->symbol_cap * sizeof(Symbol));
    }
    parser->symbol_table[parser->symbol_count].name = parser_strdup(name);
    parser->symbol_table[parser->symbol_count].type = type;
    parser->symbol_count++;
}

static int find_symbol(Parser* parser, const char* name) {
    for (int i = 0; i < parser->symbol_count; i++) {
        if (strcmp(parser->symbol_table[i].name, name) == 0) {
            return parser->symbol_table[i].type;
        }
    }
    return -1;
}

static AstNode* parse_expression(Parser* parser);
static AstNode* parse_block(Parser* parser);
static AstNode* parse_statement(Parser* parser);
static AstNode* parse_import(Parser* parser);
static AstNode* parse_extern(Parser* parser);
static AstNode* parse_struct(Parser* parser);
static AstNode* parse_enum(Parser* parser);

static VarType token_to_type(TokenType type) {
    switch (type) {
        case TOK_U8: return TYPE_U8;
        case TOK_U16: return TYPE_U16;
        case TOK_U32: return TYPE_U32;
        case TOK_U64: return TYPE_U64;
        case TOK_I8: return TYPE_I8;
        case TOK_I16: return TYPE_I16;
        case TOK_I32: return TYPE_I32;
        case TOK_I64: return TYPE_I64;
        case TOK_STRING_TYPE: return TYPE_STRING;
        default: return TYPE_U64;
    }
}

static VarType parse_type(Parser* parser, int* is_locate) {
    *is_locate = 0;

    if (parser->current.type == TOK_LOCATE) {
        *is_locate = 1;
        parser_advance(parser);
    }

    VarType type = token_to_type(parser->current.type);
    parser_advance(parser);
    return type;
}

static int parse_identifier_with_ptr(Parser* parser, char** name) {
    if (parser->current.type != TOK_IDENTIFIER) {
        parser_error(parser, "expected identifier");
    }

    *name = parser_strdup(parser->current.value);
    parser_advance(parser);

    if (parser->current.type == TOK_ASTERISK) {
        if (parser->next.type == TOK_IDENTIFIER) {
            if (strcmp(parser->next.value, "i") == 0) {
                parser_advance(parser);
                parser_advance(parser);
                return 1;
            } else if (strcmp(parser->next.value, "o") == 0) {
                parser_advance(parser);
                parser_advance(parser);
                return 2;
            }
        }
    }

    return 0;
}

static AstNode* parse_import(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_advance(parser);

    char* module_name = NULL;
    char* func_name = NULL;
    char* alias = NULL;

    if (parser->current.type == TOK_FROM) {
        parser_advance(parser);

        if (parser->current.type != TOK_IDENTIFIER) {
            parser_error(parser, "expected module name after 'from'");
        }
        module_name = parser_strdup(parser->current.value);
        parser_advance(parser);

        if (parser->current.type != TOK_IDENTIFIER) {
            parser_error(parser, "expected function name");
        }
        func_name = parser_strdup(parser->current.value);
        parser_advance(parser);

        if (parser->current.type == TOK_AS) {
            parser_advance(parser);
            if (parser->current.type != TOK_IDENTIFIER) {
                parser_error(parser, "expected alias name after 'as'");
            }
            alias = parser_strdup(parser->current.value);
            parser_advance(parser);
        }
    } else if (parser->current.type == TOK_IDENTIFIER) {
        module_name = parser_strdup(parser->current.value);
        parser_advance(parser);
    } else if (parser->current.type == TOK_LT) {
        parser_advance(parser);
        if (parser->current.type != TOK_IDENTIFIER) {
            parser_error(parser, "expected library name");
        }
        module_name = malloc(strlen(parser->current.value) + 3);
        sprintf(module_name, "<%s>", parser->current.value);
        parser_advance(parser);
        parser_consume(parser, TOK_GT, ">");
    }

    if (!module_name) parser_error(parser, "invalid import syntax");

    AstNode* node = ast_create_import(module_name, func_name, alias, line, col);
    add_symbol(parser, module_name, func_name ? 1 : 0);
    return node;
}

static AstNode* parse_extern(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_advance(parser);

    VarType return_type = TYPE_U64;
    if (parser->current.type == TOK_U8 || parser->current.type == TOK_U16 ||
        parser->current.type == TOK_U32 || parser->current.type == TOK_U64 ||
        parser->current.type == TOK_I8 || parser->current.type == TOK_I16 ||
        parser->current.type == TOK_I32 || parser->current.type == TOK_I64 ||
        parser->current.type == TOK_STRING_TYPE) {
        int dummy;
        return_type = parse_type(parser, &dummy);
    }

    parser_consume(parser, TOK_FN, "fn");

    if (parser->current.type != TOK_IDENTIFIER && parser->current.type != TOK_MAIN) {
        parser_error(parser, "expected function name after 'fn'");
    }

    char* name = parser_strdup(parser->current.type == TOK_MAIN ? "main" : parser->current.value);
    parser_advance(parser);

    parser_consume(parser, TOK_LPAREN, "(");

    AstNode** params = NULL;
    int param_count = 0;

    if (parser->current.type != TOK_RPAREN) {
        do {
            int is_locate = 0;
            VarType type = parse_type(parser, &is_locate);

            if (parser->current.type != TOK_IDENTIFIER) {
                parser_error(parser, "expected parameter name");
            }

            char* pname = parser_strdup(parser->current.value);
            parser_advance(parser);

            AstNode* param = ast_create_variable(type, is_locate, pname, NULL, line, col);
            param_count++;
            params = safe_realloc(params, param_count * sizeof(AstNode*));
            params[param_count - 1] = param;

            if (parser->current.type == TOK_COMMA) {
                parser_advance(parser);
            } else {
                break;
            }
        } while (1);
    }

    parser_consume(parser, TOK_RPAREN, ")");
    parser_consume(parser, TOK_SEMICOLON, ";");

    AstNode* node = ast_create_extern_func(name, return_type, params, param_count, line, col);
    add_symbol(parser, name, 2);
    return node;
}

static AstNode* parse_primary(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    switch (parser->current.type) {
        case TOK_NULL:
            parser_advance(parser);
            return ast_create_null(line, col);

        case TOK_NUMBER: {
            char* val = parser_strdup(parser->current.value);
            parser_advance(parser);
            return ast_create_number(val, line, col);
        }
        case TOK_STRING: {
            char* val = parser_strdup(parser->current.value);
            parser_advance(parser);
            return ast_create_string(val, line, col);
        }
        case TOK_ASTERISK: {
            parser_advance(parser);
            AstNode* ptr = parse_expression(parser);
            return ast_create_deref(ptr, line, col);
        }
        case TOK_IDENTIFIER:
        case TOK_MAIN: {
            char* name = parser_strdup(parser->current.type == TOK_MAIN ? "main" : parser->current.value);
            parser_advance(parser);

            if (parser->current.type == TOK_ASTERISK) {
                if (parser->next.type == TOK_IDENTIFIER && strcmp(parser->next.value, "adr") == 0) {
                    parser_advance(parser);
                    parser_advance(parser);
                    AstNode* expr = ast_create_identifier(name, line, col);
                    return ast_create_adr(expr, line, col);
                }
            }

            if (parser->current.type == TOK_COLON) {
                parser_advance(parser);
                if (parser->current.type != TOK_IDENTIFIER) {
                    free(name);
                    parser_error(parser, "expected member name");
                }
                char* member = parser_strdup(parser->current.value);
                parser_advance(parser);
                AstNode* ref = ast_create_section_ref(name, member, line, col);

                if (parser->current.type == TOK_ASTERISK && parser->next.type == TOK_IDENTIFIER && strcmp(parser->next.value, "adr") == 0) {
                    parser_advance(parser);
                    parser_advance(parser);
                    return ast_create_adr(ref, line, col);
                }

                if (parser->current.type == TOK_ARROW) {
                    parser_advance(parser);
                    if (parser->current.type != TOK_IDENTIFIER) parser_error(parser, "expected field name");
                    char* field = parser_strdup(parser->current.value);
                    parser_advance(parser);
                    return ast_create_arrow(ref, field, line, col);
                }

                return ref;
            }

            if (parser->current.type == TOK_ARROW) {
                parser_advance(parser);
                if (parser->current.type != TOK_IDENTIFIER) {
                    free(name);
                    parser_error(parser, "expected field name after '->'");
                }
                char* field = parser_strdup(parser->current.value);
                parser_advance(parser);
                AstNode* ident = ast_create_identifier(name, line, col);
                return ast_create_arrow(ident, field, line, col);
            }

            if (parser->current.type == TOK_DOT) {
                parser_advance(parser);
                if (parser->current.type != TOK_IDENTIFIER) {
                    free(name);
                    parser_error(parser, "expected field name after '.'");
                }
                char full_name[512];
                snprintf(full_name, sizeof(full_name), "%s.%s", name, parser->current.value);
                free(name);
                name = parser_strdup(full_name);
                parser_advance(parser);

                if (parser->current.type == TOK_ASTERISK && parser->next.type == TOK_IDENTIFIER && strcmp(parser->next.value, "adr") == 0) {
                    parser_advance(parser);
                    parser_advance(parser);
                    AstNode* ident = ast_create_identifier(name, line, col);
                    return ast_create_adr(ident, line, col);
                }

                if (parser->current.type == TOK_ARROW) {
                    parser_advance(parser);
                    if (parser->current.type != TOK_IDENTIFIER) parser_error(parser, "expected field name");
                    char* field = parser_strdup(parser->current.value);
                    parser_advance(parser);
                    AstNode* ident = ast_create_identifier(name, line, col);
                    return ast_create_arrow(ident, field, line, col);
                }

                return ast_create_identifier(name, line, col);
            }

            return ast_create_identifier(name, line, col);
        }
        case TOK_LPAREN: {
            parser_advance(parser);
            AstNode* expr = parse_expression(parser);
            parser_consume(parser, TOK_RPAREN, ")");
            return expr;
        }
        default:
            parser_error(parser, "expected expression");
            return NULL;
    }
}

static AstNode* parse_postfix(Parser* parser) {
    AstNode* node = parse_primary(parser);
    int line, col;

    while (1) {
        line = parser->current.line;
        col = parser->current.column;

        if (parser->current.type == TOK_LPAREN) {
            if (node->type != NODE_IDENTIFIER && node->type != NODE_SECTION_REF) {
                parser_error(parser, "only functions can be called");
            }

            char* name;
            if (node->type == NODE_IDENTIFIER) {
                name = parser_strdup(node->data.identifier.name);
            } else {
                char full_name[512];
                snprintf(full_name, sizeof(full_name), "%s_%s",
                         node->data.section_ref.section,
                         node->data.section_ref.member);
                name = parser_strdup(full_name);
            }
            ast_free(node);

            parser_advance(parser);

            AstNode** args = NULL;
            int arg_count = 0;

            if (parser->current.type != TOK_RPAREN) {
                do {
                    AstNode* arg = parse_expression(parser);
                    arg_count++;
                    args = safe_realloc(args, arg_count * sizeof(AstNode*));
                    args[arg_count - 1] = arg;

                    if (parser->current.type == TOK_COMMA) {
                        parser_advance(parser);
                    } else {
                        break;
                    }
                } while (1);
            }

            parser_consume(parser, TOK_RPAREN, ")");
            node = ast_create_call(name, args, arg_count, line, col);
            continue;
        }

        if (parser->current.type == TOK_ASTERISK && parser->next.type == TOK_IDENTIFIER && strcmp(parser->next.value, "adr") == 0) {
            parser_advance(parser);
            parser_advance(parser);
            node = ast_create_adr(node, line, col);
            continue;
        }

        if (parser->current.type == TOK_ARROW) {
            parser_advance(parser);
            if (parser->current.type != TOK_IDENTIFIER) {
                parser_error(parser, "expected field name after '->'");
            }
            char* field = parser_strdup(parser->current.value);
            parser_advance(parser);
            node = ast_create_arrow(node, field, line, col);
            continue;
        }

        if (parser->current.type == TOK_DOT) {
            parser_advance(parser);
            if (parser->current.type != TOK_IDENTIFIER) {
                parser_error(parser, "expected field name after '.'");
            }
            char full_name[512];
            if (node->type == NODE_IDENTIFIER) {
                snprintf(full_name, sizeof(full_name), "%s.%s", node->data.identifier.name, parser->current.value);
            } else if (node->type == NODE_SECTION_REF) {
                snprintf(full_name, sizeof(full_name), "%s:%s.%s",
                         node->data.section_ref.section, node->data.section_ref.member, parser->current.value);
            } else {
                parser_error(parser, "invalid left side of '.'");
            }
            ast_free(node);
            parser_advance(parser);
            node = ast_create_identifier(parser_strdup(full_name), line, col);
            continue;
        }

        break;
    }

    return node;
}

static AstNode* parse_unary(Parser* parser) {
    if (parser->current.type == TOK_NOT || parser->current.type == TOK_MINUS) {
        int line = parser->current.line;
        int col = parser->current.column;
        OperatorType op = (parser->current.type == TOK_NOT) ? OP_NOT : OP_SUB;
        parser_advance(parser);
        AstNode* expr = parse_unary(parser);
        return ast_create_unary_op(op, expr, line, col);
    }

    if (parser->current.type == TOK_INC) {
        int line = parser->current.line;
        int col = parser->current.column;
        parser_advance(parser);
        AstNode* expr = parse_unary(parser);
        return ast_create_inc(expr, line, col);
    }

    if (parser->current.type == TOK_DEC) {
        int line = parser->current.line;
        int col = parser->current.column;
        parser_advance(parser);
        AstNode* expr = parse_unary(parser);
        return ast_create_dec(expr, line, col);
    }

    return parse_postfix(parser);
}

static int op_precedence(TokenType type) {
    switch (type) {
        case TOK_OR: return 5;
        case TOK_AND: return 10;
        case TOK_EQ: case TOK_NE: return 15;
        case TOK_LT: case TOK_LE: case TOK_GT: case TOK_GE: return 20;
        case TOK_PLUS: case TOK_MINUS: return 30;
        case TOK_ASTERISK: case TOK_SLASH: case TOK_PERCENT: return 40;
        default: return 0;
    }
}

static OperatorType token_to_operator(TokenType type) {
    switch (type) {
        case TOK_PLUS: return OP_ADD;
        case TOK_MINUS: return OP_SUB;
        case TOK_ASTERISK: return OP_MUL;
        case TOK_SLASH: return OP_DIV;
        case TOK_PERCENT: return OP_MOD;
        case TOK_EQ: return OP_EQ;
        case TOK_NE: return OP_NE;
        case TOK_LT: return OP_LT;
        case TOK_LE: return OP_LE;
        case TOK_GT: return OP_GT;
        case TOK_GE: return OP_GE;
        case TOK_AND: return OP_AND;
        case TOK_OR: return OP_OR;
        default: return OP_ADD;
    }
}

static AstNode* parse_expression_prec(Parser* parser, int min_prec) {
    AstNode* left = parse_unary(parser);

    while (1) {
        if (parser->current.type == TOK_INC) {
            int line = parser->current.line;
            int col = parser->current.column;
            parser_advance(parser);
            left = ast_create_inc(left, line, col);
            continue;
        }

        if (parser->current.type == TOK_DEC) {
            int line = parser->current.line;
            int col = parser->current.column;
            parser_advance(parser);
            left = ast_create_dec(left, line, col);
            continue;
        }

        int prec = op_precedence(parser->current.type);
        if (prec == 0 || prec < min_prec) break;

        TokenType op_type = parser->current.type;
        int line = parser->current.line;
        int col = parser->current.column;
        parser_advance(parser);

        AstNode* right = parse_expression_prec(parser, prec + 1);
        left = ast_create_binary_op(token_to_operator(op_type), left, right, line, col);
    }

    return left;
}

static AstNode* parse_expression(Parser* parser) {
    return parse_expression_prec(parser, 1);
}

static AstNode* parse_variable_decl(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    int is_locate = 0;
    VarType type = parse_type(parser, &is_locate);

    if (parser->current.type != TOK_IDENTIFIER) {
        parser_error(parser, "expected variable name");
    }

    char* name = parser_strdup(parser->current.value);
    parser_advance(parser);

    int ptr_type = 0;
    if (parser->current.type == TOK_ASTERISK) {
        if (parser->next.type == TOK_IDENTIFIER) {
            if (strcmp(parser->next.value, "i") == 0) {
                ptr_type = 1;
            } else if (strcmp(parser->next.value, "o") == 0) {
                ptr_type = 2;
            }
            if (ptr_type > 0) {
                parser_advance(parser);
                parser_advance(parser);
            }
        }
    }
    (void)ptr_type;

    AstNode* init = NULL;
    if (parser->current.type == TOK_EQUALS) {
        parser_advance(parser);
        init = parse_expression(parser);
    }

    parser_consume(parser, TOK_SEMICOLON, ";");
    AstNode* var = ast_create_variable(type, is_locate, name, init, line, col);
    if (init) add_symbol(parser, name, 0);
    return var;
}

static AstNode* parse_multi_variable_decl(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    int is_locate = 0;
    VarType type = parse_type(parser, &is_locate);

    if (parser->current.type != TOK_IDENTIFIER) {
        parser_error(parser, "expected variable name");
    }

    char** names = NULL;
    int name_count = 0;

    do {
        if (parser->current.type != TOK_IDENTIFIER) {
            parser_error(parser, "expected variable name");
        }
        name_count++;
        names = safe_realloc(names, name_count * sizeof(char*));
        names[name_count - 1] = parser_strdup(parser->current.value);
        parser_advance(parser);

        if (parser->current.type == TOK_COMMA) {
            parser_advance(parser);
        } else {
            break;
        }
    } while (1);

    AstNode* init = NULL;
    if (parser->current.type == TOK_EQUALS) {
        parser_advance(parser);
        init = parse_expression(parser);
    }

    parser_consume(parser, TOK_SEMICOLON, ";");

    if (name_count == 1) {
        AstNode* var = ast_create_variable(type, is_locate, names[0], init, line, col);
        free(names);
        return var;
    }

    AstNode* var = ast_create_multi_variable(type, is_locate, names, name_count, init, line, col);
    for (int i = 0; i < name_count; i++) {
        add_symbol(parser, names[i], 0);
    }
    return var;
}

static AstNode* parse_return(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_advance(parser);

    if (parser->current.type == TOK_LPAREN) {
        parser_advance(parser);

        AstNode** values = NULL;
        int count = 0;

        if (parser->current.type != TOK_RPAREN) {
            do {
                AstNode* val = parse_expression(parser);
                count++;
                values = safe_realloc(values, count * sizeof(AstNode*));
                values[count - 1] = val;

                if (parser->current.type == TOK_COMMA) {
                    parser_advance(parser);
                } else {
                    break;
                }
            } while (1);
        }

        parser_consume(parser, TOK_RPAREN, ")");
        parser_consume(parser, TOK_SEMICOLON, ";");

        if (count == 1) {
            AstNode* ret = ast_create_return(values[0], line, col);
            free(values);
            return ret;
        }

        return ast_create_multi_return(values, count, line, col);
    }

    AstNode* value = NULL;
    if (parser->current.type != TOK_SEMICOLON) {
        value = parse_expression(parser);
    }

    parser_consume(parser, TOK_SEMICOLON, ";");
    return ast_create_return(value, line, col);
}

static AstNode* parse_destructure(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_consume(parser, TOK_LBRACKET, "[");

    char** names = NULL;
    int count = 0;

    do {
        if (parser->current.type != TOK_IDENTIFIER) {
            parser_error(parser, "expected variable name in destructure");
        }
        count++;
        names = safe_realloc(names, count * sizeof(char*));
        names[count - 1] = parser_strdup(parser->current.value);
        parser_advance(parser);

        if (parser->current.type == TOK_COMMA) {
            parser_advance(parser);
        } else {
            break;
        }
    } while (1);

    parser_consume(parser, TOK_RBRACKET, "]");
    parser_consume(parser, TOK_EQUALS, "=");

    AstNode* call = parse_expression(parser);
    parser_consume(parser, TOK_SEMICOLON, ";");

    return ast_create_destructure(names, count, call, line, col);
}

static AstNode* parse_assignment_or_expr(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    if (parser->current.type == TOK_LBRACKET) {
        if (parser->next.type == TOK_IDENTIFIER) {
            return parse_destructure(parser);
        }
    }

    AstNode* expr = parse_expression(parser);

    if (parser->current.type == TOK_EQUALS) {
        parser_advance(parser);
        AstNode* value = parse_expression(parser);

        if (expr->type == NODE_IDENTIFIER) {
            char* name = parser_strdup(expr->data.identifier.name);
            ast_free(expr);
            parser_consume(parser, TOK_SEMICOLON, ";");
            return ast_create_assign(name, value, line, col);
        } else if (expr->type == NODE_SECTION_REF) {
            char full_name[512];
            snprintf(full_name, sizeof(full_name), "%s:%s",
                     expr->data.section_ref.section, expr->data.section_ref.member);
            ast_free(expr);
            parser_consume(parser, TOK_SEMICOLON, ";");
            return ast_create_assign(parser_strdup(full_name), value, line, col);
        } else if (expr->type == NODE_DEREF) {
            parser_consume(parser, TOK_SEMICOLON, ";");
            return ast_create_store_deref(expr->data.deref.ptr, value, 0, NULL, line, col);
        } else if (expr->type == NODE_ARROW) {
            char* field = parser_strdup(expr->data.arrow.field);
            AstNode* ptr = expr->data.arrow.ptr;
            free(expr);
            parser_consume(parser, TOK_SEMICOLON, ";");
            return ast_create_store_deref(ptr, value, 1, field, line, col);
        }
    }

    parser_consume(parser, TOK_SEMICOLON, ";");
    return expr;
}

static AstNode* parse_builtin(Parser* parser, TokenType type) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_advance(parser);
    parser_consume(parser, TOK_LPAREN, "(");

    if (type == TOK_INB || type == TOK_INW || type == TOK_INL) {
        AstNode* port = parse_expression(parser);
        parser_consume(parser, TOK_RPAREN, ")");
        char* name = parser_strdup(type == TOK_INB ? "inb" : type == TOK_INW ? "inw" : "inl");
        AstNode** args = malloc(sizeof(AstNode*));
        args[0] = port;
        return ast_create_call(name, args, 1, line, col);
    }

    if (type == TOK_OUTB || type == TOK_OUTW || type == TOK_OUTL) {
        AstNode* port = parse_expression(parser);
        parser_consume(parser, TOK_COMMA, ",");
        AstNode* value = parse_expression(parser);
        parser_consume(parser, TOK_RPAREN, ")");
        char* name = parser_strdup(type == TOK_OUTB ? "outb" : type == TOK_OUTW ? "outw" : "outl");
        AstNode** args = malloc(2 * sizeof(AstNode*));
        args[0] = port;
        args[1] = value;
        return ast_create_call(name, args, 2, line, col);
    }

    if (type == TOK_MLOC) {
        AstNode* owner = parse_expression(parser);
        parser_consume(parser, TOK_COMMA, ",");
        AstNode* size = parse_expression(parser);
        parser_consume(parser, TOK_RPAREN, ")");
        return ast_create_mloc(owner, size, NULL, line, col);
    }

    if (type == TOK_BMLOC) {
        AstNode* addr = parse_expression(parser);
        parser_consume(parser, TOK_COMMA, ",");
        AstNode* size = parse_expression(parser);
        parser_consume(parser, TOK_RPAREN, ")");
        return ast_create_bmloc(addr, size, line, col);
    }

    if (type == TOK_MFREE) {
        AstNode* target = parse_expression(parser);
        parser_consume(parser, TOK_RPAREN, ")");
        return ast_create_mfree(target, line, col);
    }

    if (type == TOK_E820F) {
        parser_consume(parser, TOK_RPAREN, ")");
        return ast_create_e820f(line, col);
    }

    parser_error(parser, "unknown builtin function");
    return NULL;
}

static AstNode* parse_printf(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;
    char* raw_format;
    char* format;
    AstNode* format_node;
    AstNode** vargs = NULL;
    int varg_count = 0;
    AstNode** args = NULL;
    int arg_count;

    parser_advance(parser);
    parser_consume(parser, TOK_LPAREN, "(");

    if (parser->current.type != TOK_STRING) {
        parser_error(parser, "printf requires format string");
    }

    raw_format = parser_strdup(parser->current.value);
    parser_advance(parser);

    while (parser->current.type == TOK_COMMA) {
        parser_advance(parser);
        varg_count++;
        vargs = safe_realloc(vargs, varg_count * sizeof(AstNode*));
        vargs[varg_count - 1] = parse_expression(parser);
    }

    format = normalize_printf_format(raw_format, vargs, varg_count);
    free(raw_format);
    format_node = ast_create_string(format, line, col);

    arg_count = varg_count + 1;
    args = malloc(arg_count * sizeof(AstNode*));
    args[0] = format_node;
    for (int i = 0; i < varg_count; i++) args[i + 1] = vargs[i];
    if (vargs) free(vargs);

    parser_consume(parser, TOK_RPAREN, ")");
    parser_consume(parser, TOK_SEMICOLON, ";");

    return ast_create_call(parser_strdup("printf"), args, arg_count, line, col);
}

static AstNode* parse_input(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_advance(parser);
    parser_consume(parser, TOK_LPAREN, "(");

    AstNode* prompt = NULL;
    AstNode* target = NULL;

    if (parser->current.type == TOK_STRING) {
        prompt = ast_create_string(parser_strdup(parser->current.value), line, col);
        parser_advance(parser);
        parser_consume(parser, TOK_COMMA, ",");
    }

    target = parse_expression(parser);

    parser_consume(parser, TOK_RPAREN, ")");
    parser_consume(parser, TOK_SEMICOLON, ";");

    return ast_create_input(prompt, target, line, col);
}

static int is_jmpto_name_token(TokenType t) {
    return t == TOK_IDENTIFIER || t == TOK_NUMBER || t == TOK_DOT ||
           t == TOK_SLASH || t == TOK_MINUS;
}

static AstNode* parse_jmpto(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;
    char* filename;
    size_t cap, len;

    parser_advance(parser);

    if (parser->current.type == TOK_STRING) {
        filename = parser_strdup(parser->current.value);
        parser_advance(parser);
    } else {
        cap = 128;
        len = 0;
        filename = malloc(cap);
        filename[0] = '\0';

        while (is_jmpto_name_token(parser->current.type)) {
            const char* part = parser->current.value ? parser->current.value : "";
            size_t p = strlen(part);
            while (len + p + 1 > cap) {
                cap *= 2;
                filename = safe_realloc(filename, cap);
            }
            memcpy(filename + len, part, p);
            len += p;
            filename[len] = '\0';
            parser_advance(parser);
        }

        if (len == 0) {
            free(filename);
            parser_error(parser, "expected module name");
        }
    }

    AstNode** vars = NULL;
    int var_count = 0;

    if (parser->current.type == TOK_LBRACE) {
        parser_advance(parser);

        while (parser->current.type != TOK_RBRACE && parser->current.type != TOK_EOF) {
            AstNode* var = parse_expression(parser);
            parser_consume(parser, TOK_SEMICOLON, ";");

            var_count++;
            vars = safe_realloc(vars, var_count * sizeof(AstNode*));
            vars[var_count - 1] = var;
        }

        parser_consume(parser, TOK_RBRACE, "}");
    }

    return ast_create_jmpto(filename, vars, var_count, NULL, line, col);
}

static AstNode* parse_struct(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_advance(parser);

    if (parser->current.type != TOK_IDENTIFIER) parser_error(parser, "expected struct name");
    char* name = parser_strdup(parser->current.value);
    parser_advance(parser);

    int version = 1;
    if (parser->current.type == TOK_VERSION) {
        version = atoi(parser->current.value);
        parser_advance(parser);
    }

    parser_consume(parser, TOK_LBRACE, "{");

    AstNode* struct_node = ast_create_struct(name, version, 0, line, col);

    while (parser->current.type != TOK_RBRACE && parser->current.type != TOK_EOF) {
        int field_version = 1;
        if (parser->current.type == TOK_VERSION) {
            field_version = atoi(parser->current.value);
            parser_advance(parser);
        }

        int is_locate = 0;
        VarType type = parse_type(parser, &is_locate);

        if (parser->current.type != TOK_IDENTIFIER) parser_error(parser, "expected field name");
        char* field_name = parser_strdup(parser->current.value);
        parser_advance(parser);

        parser_consume(parser, TOK_SEMICOLON, ";");

        ast_struct_add_field(struct_node, field_name, type, field_version, 0, 0, 0);
    }

    parser_consume(parser, TOK_RBRACE, "}");
    add_symbol(parser, name, 3);
    return struct_node;
}

static AstNode* parse_enum(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_advance(parser);

    if (parser->current.type != TOK_IDENTIFIER) parser_error(parser, "expected enum name");
    char* name = parser_strdup(parser->current.value);
    parser_advance(parser);

    int version = 1;
    if (parser->current.type == TOK_VERSION) {
        version = atoi(parser->current.value);
        parser_advance(parser);
    }

    parser_consume(parser, TOK_LBRACE, "{");

    AstNode* enum_node = ast_create_enum(name, version, line, col);
    uint64_t next_value = 0;

    while (parser->current.type != TOK_RBRACE && parser->current.type != TOK_EOF) {
        int val_version = 1;
        if (parser->current.type == TOK_VERSION) {
            val_version = atoi(parser->current.value);
            parser_advance(parser);
        }

        if (parser->current.type != TOK_IDENTIFIER) parser_error(parser, "expected enum value name");
        char* val_name = parser_strdup(parser->current.value);
        parser_advance(parser);

        uint64_t value = next_value;
        if (parser->current.type == TOK_EQUALS) {
            parser_advance(parser);
            if (parser->current.type != TOK_NUMBER) parser_error(parser, "expected number after '='");
            value = strtoull(parser->current.value, NULL, 0);
            parser_advance(parser);
            next_value = value + 1;
        } else {
            next_value++;
        }

        parser_consume(parser, TOK_SEMICOLON, ";");

        ast_enum_add_value(enum_node, val_name, value, val_version, 0);
    }

    parser_consume(parser, TOK_RBRACE, "}");
    add_symbol(parser, name, 4);
    return enum_node;
}

static AstNode* parse_statement(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    if (parser->current.type == TOK_IMPORT) return parse_import(parser);
    if (parser->current.type == TOK_EXTERN) return parse_extern(parser);
    if (parser->current.type == TOK_STRUCT) return parse_struct(parser);
    if (parser->current.type == TOK_ENUM) return parse_enum(parser);

    if (parser->current.type == TOK_ADRLOAD) {
        parser_advance(parser);
        if (parser->current.type != TOK_NUMBER) parser_error(parser, "expected address after adrload");
        char* addr = parser_strdup(parser->current.value);
        parser_advance(parser);
        return ast_create_directive(parser_strdup("adrload"), addr, line, col);
    }
    if (parser->current.type == TOK_BITS) {
        parser_advance(parser);
        if (parser->current.type != TOK_NUMBER) parser_error(parser, "expected bits value");
        char* bits = parser_strdup(parser->current.value);
        parser_advance(parser);
        return ast_create_directive(parser_strdup("bits"), bits, line, col);
    }

    if (parser->current.type == TOK_LOCATE || parser->current.type == TOK_U8 || parser->current.type == TOK_U16 ||
        parser->current.type == TOK_U32 || parser->current.type == TOK_U64 || parser->current.type == TOK_I8 ||
        parser->current.type == TOK_I16 || parser->current.type == TOK_I32 || parser->current.type == TOK_I64 ||
        parser->current.type == TOK_STRING_TYPE) {
        return parse_multi_variable_decl(parser);
    }

    if (parser->current.type == TOK_RETURN) return parse_return(parser);

    if (parser->current.type == TOK_IF) {
        parser_advance(parser);
        parser_consume(parser, TOK_LPAREN, "(");
        AstNode* cond = parse_expression(parser);
        parser_consume(parser, TOK_RPAREN, ")");
        AstNode* then_branch = parse_block(parser);
        AstNode* else_branch = NULL;

        if (parser->current.type == TOK_ELSE) {
            parser_advance(parser);
            else_branch = parse_block(parser);
        }

        return ast_create_if(cond, then_branch, else_branch, line, col);
    }

    if (parser->current.type == TOK_WHILE) {
        parser_advance(parser);
        parser_consume(parser, TOK_LPAREN, "(");
        AstNode* cond = parse_expression(parser);
        parser_consume(parser, TOK_RPAREN, ")");
        AstNode* body = parse_block(parser);
        return ast_create_while(cond, body, line, col);
    }

    if (parser->current.type == TOK_FOR) {
        parser_advance(parser);
        parser_consume(parser, TOK_LPAREN, "(");

        AstNode* init = NULL;
        if (parser->current.type != TOK_SEMICOLON) init = parse_expression(parser);
        parser_consume(parser, TOK_SEMICOLON, ";");

        AstNode* cond = NULL;
        if (parser->current.type != TOK_SEMICOLON) cond = parse_expression(parser);
        parser_consume(parser, TOK_SEMICOLON, ";");

        AstNode* post = NULL;
        if (parser->current.type != TOK_RPAREN) post = parse_expression(parser);
        parser_consume(parser, TOK_RPAREN, ")");

        AstNode* body = parse_block(parser);
        return ast_create_for(init, cond, body, post, line, col);
    }

    if (parser->current.type == TOK_JMPTO) return parse_jmpto(parser);
    if (parser->current.type == TOK_INPUT) return parse_input(parser);
    if (parser->current.type == TOK_PRINTF) return parse_printf(parser);

    if (parser->current.type == TOK_INB || parser->current.type == TOK_INW ||
        parser->current.type == TOK_INL || parser->current.type == TOK_OUTB ||
        parser->current.type == TOK_OUTW || parser->current.type == TOK_OUTL ||
        parser->current.type == TOK_MLOC || parser->current.type == TOK_BMLOC ||
        parser->current.type == TOK_MFREE || parser->current.type == TOK_E820F) {
        AstNode* expr = parse_builtin(parser, parser->current.type);
        parser_consume(parser, TOK_SEMICOLON, ";");
        return expr;
    }

    if (parser->current.type == TOK_NASM_BLOCK) {
        char* instr = parser_strdup(parser->current.value);
        parser_advance(parser);
        return ast_create_asm_block(instr, line, col);
    }

    if (parser->current.type == TOK_LBRACKET) {
        return parse_destructure(parser);
    }

    return parse_assignment_or_expr(parser);
}

static AstNode* parse_block(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_consume(parser, TOK_LBRACE, "{");

    AstNode** statements = NULL;
    int count = 0;

    while (parser->current.type != TOK_RBRACE && parser->current.type != TOK_EOF) {
        AstNode* stmt = parse_statement(parser);
        count++;
        statements = safe_realloc(statements, count * sizeof(AstNode*));
        statements[count - 1] = stmt;
    }

    parser_consume(parser, TOK_RBRACE, "}");
    return ast_create_block(statements, count, line, col);
}

static AstNode* parse_function(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    int attr_baint = 0;
    int attr_bclear = 0;

    if (parser->current.type == TOK_BAINT) { attr_baint = 1; parser_advance(parser); }
    if (parser->current.type == TOK_BCLEAR) { attr_bclear = 1; parser_advance(parser); }

    parser_consume(parser, TOK_FN, "fn");

    if (parser->current.type != TOK_IDENTIFIER && parser->current.type != TOK_MAIN) {
        parser_error(parser, "expected function name");
    }

    char* name = parser_strdup(parser->current.type == TOK_MAIN ? "main" : parser->current.value);
    parser_advance(parser);

    parser_consume(parser, TOK_LPAREN, "(");

    AstNode** params = NULL;
    int param_count = 0;

    if (parser->current.type != TOK_RPAREN) {
        do {
            int is_locate = 0;
            VarType type = parse_type(parser, &is_locate);

            if (parser->current.type != TOK_IDENTIFIER) {
                parser_error(parser, "expected parameter name");
            }

            char* pname = parser_strdup(parser->current.value);
            parser_advance(parser);

            AstNode* param = ast_create_variable(type, is_locate, pname, NULL, line, col);
            param_count++;
            params = safe_realloc(params, param_count * sizeof(AstNode*));
            params[param_count - 1] = param;

            if (parser->current.type == TOK_COMMA) {
                parser_advance(parser);
            } else {
                break;
            }
        } while (1);
    }

    parser_consume(parser, TOK_RPAREN, ")");

    AstNode* body = parse_block(parser);

    AstNode* func = ast_create_function(name, params, param_count, body, line, col);
    func->attr_baint = attr_baint;
    func->attr_bclear = attr_bclear;
    add_symbol(parser, name, 5);
    return func;
}

static AstNode* parse_section(Parser* parser) {
    int line = parser->current.line;
    int col = parser->current.column;

    parser_consume(parser, TOK_SECT, "sect");
    parser_consume(parser, TOK_DOT, ".");

    if (parser->current.type != TOK_IDENTIFIER) parser_error(parser, "expected section name");

    char* name = parser_strdup(parser->current.value);
    parser_advance(parser);

    AstNode** vars = NULL;
    int var_count = 0;

    while (parser->current.type != TOK_EOS && parser->current.type != TOK_EOF) {
        int is_locate = 0;
        VarType type = parse_type(parser, &is_locate);

        if (parser->current.type != TOK_IDENTIFIER) parser_error(parser, "expected variable name");

        char* vname = parser_strdup(parser->current.value);
        parser_advance(parser);

        AstNode* init = NULL;
        if (parser->current.type == TOK_EQUALS) {
            parser_advance(parser);
            init = parse_expression(parser);
        }

        parser_consume(parser, TOK_SEMICOLON, ";");

        AstNode* var = ast_create_variable(type, is_locate, vname, init, line, col);
        var_count++;
        vars = safe_realloc(vars, var_count * sizeof(AstNode*));
        vars[var_count - 1] = var;
    }

    if (parser->current.type != TOK_EOS) parser_error(parser, "expected EOS");
    parser_advance(parser);

    return ast_create_section(name, vars, var_count, line, col);
}

AstNode* parser_parse(Parser* parser) {
    if (parser->current.type == TOK_SC_FALSE) {
        parser->safe_code = 0;
        if (verbose) printf("DEBUG: safe_code set to 0\n");
        parser_advance(parser);
    } else if (parser->current.type == TOK_SC_TRUE) {
        parser->safe_code = 1;
        if (verbose) printf("DEBUG: safe_code set to 1\n");
        parser_advance(parser);
    } else {
        parser_error(parser, "file must start with sc.false or sc.true");
    }

    AstNode** items = NULL;
    int count = 0;

    while (parser->current.type != TOK_EOF) {
        AstNode* item = NULL;

        if (parser->current.type == TOK_IMPORT) {
            item = parse_import(parser);
        } else if (parser->current.type == TOK_EXTERN) {
            item = parse_extern(parser);
        } else if (parser->current.type == TOK_SECT) {
            item = parse_section(parser);
        } else if (parser->current.type == TOK_STRUCT) {
            item = parse_struct(parser);
        } else if (parser->current.type == TOK_ENUM) {
            item = parse_enum(parser);
        } else if (parser->current.type == TOK_FN || parser->current.type == TOK_BAINT || parser->current.type == TOK_BCLEAR) {
            item = parse_function(parser);
        } else {
            item = parse_statement(parser);
        }

        if (item) {
            count++;
            items = safe_realloc(items, count * sizeof(AstNode*));
            items[count - 1] = item;
        }
    }

    return ast_create_program(items, count, parser->current.line, parser->current.column);
}
