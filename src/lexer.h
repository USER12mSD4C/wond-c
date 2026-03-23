#ifndef LEXER_H
#define LEXER_H
#include <stdio.h>

typedef enum {
    TOK_EOF,
    TOK_ERROR,
    
    TOK_SC_FALSE,
    TOK_SC_TRUE,
    
    TOK_ADRLOAD,
    TOK_BITS,
    TOK_SECTION,
    
    TOK_SECT,
    TOK_EOS,
    TOK_FN,
    TOK_MAIN,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_JMPTO,
    TOK_INPUT,
    TOK_PRINTF,
    TOK_MLOC,
    TOK_MFREE,
    TOK_BMLOC,
    TOK_E820F,
    TOK_INB,
    TOK_OUTB,
    TOK_INW,
    TOK_OUTW,
    TOK_INL,
    TOK_OUTL,
    
    TOK_IMPORT,
    TOK_FROM,
    TOK_AS,
    
    TOK_BAINT,
    TOK_BCLEAR,
    
    TOK_NASM_BLOCK,
    
    TOK_U8, TOK_U16, TOK_U32, TOK_U64,
    TOK_I8, TOK_I16, TOK_I32, TOK_I64,
    TOK_STRING_TYPE,
    TOK_LOCATE,
    
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,
    
    TOK_SEMICOLON,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COLON,
    TOK_COMMA,
    TOK_DOT,
    TOK_PLUS,
    TOK_MINUS,
    TOK_ASTERISK,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQUALS,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    
    TOK_STRUCT,
    TOK_VERSION,
    TOK_REFLECT,
    TOK_LBRACKET,
    TOK_RBRACKET
} TokenType;

typedef struct {
    TokenType type;
    char* value;
    int line;
    int column;
} Token;

typedef struct {
    const char* source;
    int position;
    int line;
    int column;
} Lexer;

void lexer_init(Lexer* lexer, const char* source);
Token lexer_next_token(Lexer* lexer);
void token_free(Token* token);

#endif
