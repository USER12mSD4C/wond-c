#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer* lexer;
    Token current;
    Token next;
    int safe_code;        // 0 = sc.false (bare metal), 1 = sc.true (OS mode)
    int current_bits;     // 16, 32, или 64
    char* current_adrload; // текущий adrload адрес
} Parser;

void parser_init(Parser* parser, Lexer* lexer);
AstNode* parser_parse(Parser* parser);
void parser_error(Parser* parser, const char* message);
void parser_consume(Parser* parser, TokenType type, const char* expected);

#endif
