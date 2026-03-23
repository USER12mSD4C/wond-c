#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "lexer.h"

void lexer_init(Lexer* lexer, const char* source) {
    lexer->source = source;
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
}

static int lexer_current(Lexer* lexer) {
    if (lexer->source[lexer->position] == '\0') return -1;
    return (unsigned char)lexer->source[lexer->position];
}

static void lexer_advance(Lexer* lexer) {
    if (lexer->source[lexer->position] == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    lexer->position++;
}

static void lexer_skip_whitespace(Lexer* lexer) {
    while (1) {
        int c = lexer_current(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            lexer_advance(lexer);
        } else if (c == '\n') {
            lexer_advance(lexer);
        } else if (c == '/' && lexer->source[lexer->position + 1] == '/') {
            while (lexer_current(lexer) != '\n' && lexer_current(lexer) != -1) {
                lexer_advance(lexer);
            }
        } else {
            break;
        }
    }
}

static Token lexer_make_token(Lexer* lexer, TokenType type, const char* start, int length) {
    Token token;
    token.type = type;
    token.value = NULL;
    if (length > 0 && start) {
        token.value = malloc(length + 1);
        if (token.value) {
            memcpy(token.value, start, length);
            token.value[length] = '\0';
        }
    }
    token.line = lexer->line;
    token.column = lexer->column - length;
    return token;
}

static Token lexer_error(Lexer* lexer, const char* message) {
    Token token;
    token.type = TOK_ERROR;
    size_t len = strlen(message);
    token.value = malloc(len + 1);
    if (token.value) {
        memcpy(token.value, message, len + 1);
    }
    token.line = lexer->line;
    token.column = lexer->column;
    return token;
}

Token lexer_next_token(Lexer* lexer) {
    lexer_skip_whitespace(lexer);
    int start_pos = lexer->position;
    int c = lexer_current(lexer);
    if (c == -1) return lexer_make_token(lexer, TOK_EOF, NULL, 0);
    
    if (c == 's' && lexer->source[lexer->position + 1] == 'c' && 
        lexer->source[lexer->position + 2] == '.') {
        
        lexer_advance(lexer);
        lexer_advance(lexer);
        lexer_advance(lexer);
        
        int start = lexer->position;
        while (isalpha(lexer_current(lexer))) {
            lexer_advance(lexer);
        }
        int len = lexer->position - start;
        
        if (len == 5 && strncmp(lexer->source + start, "false", 5) == 0) {
            return lexer_make_token(lexer, TOK_SC_FALSE, "sc.false", 8);
        }
        if (len == 4 && strncmp(lexer->source + start, "true", 4) == 0) {
            return lexer_make_token(lexer, TOK_SC_TRUE, "sc.true", 7);
        }
        
        lexer->position = start_pos;
    }
    
    if (c == '#') {
        lexer_advance(lexer);
        int start = lexer->position;
        while (isalpha(lexer_current(lexer))) {
            lexer_advance(lexer);
        }
        int len = lexer->position - start;
        
        if (len == 6 && strncmp(lexer->source + start, "import", 6) == 0) {
            return lexer_make_token(lexer, TOK_IMPORT, "#import", 7);
        }
        
        lexer->position = start_pos;
    }
    
    if (isdigit(c) || (c == '0' && (lexer->source[lexer->position + 1] == 'x' || lexer->source[lexer->position + 1] == 'X'))) {
        if (c == '0' && (lexer->source[lexer->position + 1] == 'x' || lexer->source[lexer->position + 1] == 'X')) {
            lexer_advance(lexer);
            lexer_advance(lexer);
            int hex_start = lexer->position;
            while (isxdigit(lexer_current(lexer))) {
                lexer_advance(lexer);
            }
            int hex_len = lexer->position - hex_start;
            char* hex_str = malloc(hex_len + 3);
            if (hex_str) {
                hex_str[0] = '0';
                hex_str[1] = 'x';
                memcpy(hex_str + 2, lexer->source + hex_start, hex_len);
                hex_str[hex_len + 2] = '\0';
            }
            Token tok = lexer_make_token(lexer, TOK_NUMBER, hex_str, hex_len + 2);
            free(hex_str);
            return tok;
        } else {
            while (isdigit(lexer_current(lexer))) {
                lexer_advance(lexer);
            }
            int num_len = lexer->position - start_pos;
            return lexer_make_token(lexer, TOK_NUMBER, lexer->source + start_pos, num_len);
        }
    }
    
    if (c == '"') {
        lexer_advance(lexer);
        char* buf = malloc(4096);
        int pos = 0;
        int escape = 0;
        
        while (1) {
            int ch = lexer_current(lexer);
            if (ch == -1) {
                free(buf);
                return lexer_error(lexer, "unterminated string");
            }
            
            if (!escape && ch == '"') {
                lexer_advance(lexer);
                break;
            }
            
            if (!escape && ch == '\\') {
                escape = 1;
                lexer_advance(lexer);
                continue;
            }
            
            if (escape) {
                switch (ch) {
                    case 'n':  buf[pos++] = '\n'; break;
                    case 't':  buf[pos++] = '\t'; break;
                    case 'r':  buf[pos++] = '\r'; break;
                    case '\\': buf[pos++] = '\\'; break;
                    case '"':  buf[pos++] = '"'; break;
                    default:   buf[pos++] = (char)ch; break;
                }
                escape = 0;
                lexer_advance(lexer);
            } else {
                if (pos < 4095) {
                    buf[pos++] = (char)ch;
                } else {
                    free(buf);
                    return lexer_error(lexer, "string too long");
                }
                lexer_advance(lexer);
            }
        }
        
        buf[pos] = '\0';
        Token tok = lexer_make_token(lexer, TOK_STRING, buf, pos);
        free(buf);
        return tok;
    }
    
    if (c == ':' && strncmp(lexer->source + start_pos, "::nasm::{", 9) == 0) {
        lexer->position += 9;
        lexer->column += 9;
        
        int content_start = lexer->position;
        int brace_count = 1;
        
        while (brace_count > 0 && lexer_current(lexer) != -1) {
            if (lexer_current(lexer) == '{') brace_count++;
            if (lexer_current(lexer) == '}') brace_count--;
            lexer_advance(lexer);
        }
        
        int content_len = lexer->position - content_start - 1;
        char* content = malloc(content_len + 1);
        if (content) {
            memcpy(content, lexer->source + content_start, content_len);
            content[content_len] = '\0';
        }
        
        Token tok = lexer_make_token(lexer, TOK_NASM_BLOCK, content ? content : "", content_len);
        free(content);
        return tok;
    }
    
    if (isalpha(c) || c == '_') {
        while (isalnum(lexer_current(lexer)) || lexer_current(lexer) == '_') {
            lexer_advance(lexer);
        }
        int len = lexer->position - start_pos;
        const char* text = lexer->source + start_pos;
        
        if (len == 7 && memcmp(text, "adrload", 7) == 0) return lexer_make_token(lexer, TOK_ADRLOAD, "adrload", 7);
        if (len == 4 && memcmp(text, "bits", 4) == 0) return lexer_make_token(lexer, TOK_BITS, "bits", 4);
        if (len == 4 && memcmp(text, "sect", 4) == 0) return lexer_make_token(lexer, TOK_SECT, "sect", 4);
        if (len == 3 && memcmp(text, "EOS", 3) == 0) return lexer_make_token(lexer, TOK_EOS, "EOS", 3);
        if (len == 2 && memcmp(text, "fn", 2) == 0) return lexer_make_token(lexer, TOK_FN, "fn", 2);
        if (len == 4 && memcmp(text, "main", 4) == 0) return lexer_make_token(lexer, TOK_MAIN, "main", 4);
        if (len == 6 && memcmp(text, "return", 6) == 0) return lexer_make_token(lexer, TOK_RETURN, "return", 6);
        if (len == 2 && memcmp(text, "if", 2) == 0) return lexer_make_token(lexer, TOK_IF, "if", 2);
        if (len == 4 && memcmp(text, "else", 4) == 0) return lexer_make_token(lexer, TOK_ELSE, "else", 4);
        if (len == 5 && memcmp(text, "while", 5) == 0) return lexer_make_token(lexer, TOK_WHILE, "while", 5);
        if (len == 3 && memcmp(text, "for", 3) == 0) return lexer_make_token(lexer, TOK_FOR, "for", 3);
        if (len == 5 && memcmp(text, "jmpto", 5) == 0) return lexer_make_token(lexer, TOK_JMPTO, "jmpto", 5);
        if (len == 5 && memcmp(text, "input", 5) == 0) return lexer_make_token(lexer, TOK_INPUT, "input", 5);
        if (len == 6 && memcmp(text, "printf", 6) == 0) return lexer_make_token(lexer, TOK_PRINTF, "printf", 6);
        if (len == 4 && memcmp(text, "mloc", 4) == 0) return lexer_make_token(lexer, TOK_MLOC, "mloc", 4);
        if (len == 5 && memcmp(text, "mfree", 5) == 0) return lexer_make_token(lexer, TOK_MFREE, "mfree", 5);
        if (len == 5 && memcmp(text, "bmloc", 5) == 0) return lexer_make_token(lexer, TOK_BMLOC, "bmloc", 5);
        if (len == 5 && memcmp(text, "e820f", 5) == 0) return lexer_make_token(lexer, TOK_E820F, "e820f", 5);
        if (len == 3 && memcmp(text, "inb", 3) == 0) return lexer_make_token(lexer, TOK_INB, "inb", 3);
        if (len == 4 && memcmp(text, "outb", 4) == 0) return lexer_make_token(lexer, TOK_OUTB, "outb", 4);
        if (len == 3 && memcmp(text, "inw", 3) == 0) return lexer_make_token(lexer, TOK_INW, "inw", 3);
        if (len == 4 && memcmp(text, "outw", 4) == 0) return lexer_make_token(lexer, TOK_OUTW, "outw", 4);
        if (len == 3 && memcmp(text, "inl", 3) == 0) return lexer_make_token(lexer, TOK_INL, "inl", 3);
        if (len == 4 && memcmp(text, "outl", 4) == 0) return lexer_make_token(lexer, TOK_OUTL, "outl", 4);
        if (len == 4 && memcmp(text, "from", 4) == 0) return lexer_make_token(lexer, TOK_FROM, "from", 4);
        if (len == 2 && memcmp(text, "as", 2) == 0) return lexer_make_token(lexer, TOK_AS, "as", 2);
        if (len == 5 && memcmp(text, "baint", 5) == 0) return lexer_make_token(lexer, TOK_BAINT, "baint", 5);
        if (len == 6 && memcmp(text, "bclear", 6) == 0) return lexer_make_token(lexer, TOK_BCLEAR, "bclear", 6);
        if (len == 2 && memcmp(text, "u8", 2) == 0) return lexer_make_token(lexer, TOK_U8, "u8", 2);
        if (len == 3 && memcmp(text, "u16", 3) == 0) return lexer_make_token(lexer, TOK_U16, "u16", 3);
        if (len == 3 && memcmp(text, "u32", 3) == 0) return lexer_make_token(lexer, TOK_U32, "u32", 3);
        if (len == 3 && memcmp(text, "u64", 3) == 0) return lexer_make_token(lexer, TOK_U64, "u64", 3);
        if (len == 2 && memcmp(text, "i8", 2) == 0) return lexer_make_token(lexer, TOK_I8, "i8", 2);
        if (len == 3 && memcmp(text, "i16", 3) == 0) return lexer_make_token(lexer, TOK_I16, "i16", 3);
        if (len == 3 && memcmp(text, "i32", 3) == 0) return lexer_make_token(lexer, TOK_I32, "i32", 3);
        if (len == 3 && memcmp(text, "i64", 3) == 0) return lexer_make_token(lexer, TOK_I64, "i64", 3);
        if (len == 6 && memcmp(text, "string", 6) == 0) return lexer_make_token(lexer, TOK_STRING_TYPE, "string", 6);
        if (len == 6 && memcmp(text, "locate", 6) == 0) return lexer_make_token(lexer, TOK_LOCATE, "locate", 6);
        if (len == 6 && memcmp(text, "struct", 6) == 0) return lexer_make_token(lexer, TOK_STRUCT, "struct", 6);
        
        return lexer_make_token(lexer, TOK_IDENTIFIER, text, len);
    }
    
    if (c == ';') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_SEMICOLON, ";", 1); }
    if (c == '{') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_LBRACE, "{", 1); }
    if (c == '}') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_RBRACE, "}", 1); }
    if (c == '(') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_LPAREN, "(", 1); }
    if (c == ')') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_RPAREN, ")", 1); }
    if (c == ':') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_COLON, ":", 1); }
    if (c == ',') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_COMMA, ",", 1); }
    if (c == '.') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_DOT, ".", 1); }
    if (c == '+') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_PLUS, "+", 1); }
    if (c == '-') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_MINUS, "-", 1); }
    if (c == '*') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_ASTERISK, "*", 1); }
    if (c == '/') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_SLASH, "/", 1); }
    if (c == '%') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_PERCENT, "%", 1); }
    if (c == '[') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_LBRACKET, "[", 1); }
    if (c == ']') { lexer_advance(lexer); return lexer_make_token(lexer, TOK_RBRACKET, "]", 1); }
    
    if (c == '=') {
        if (lexer->source[lexer->position + 1] == '=') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOK_EQ, "==", 2);
        }
        lexer_advance(lexer);
        return lexer_make_token(lexer, TOK_EQUALS, "=", 1);
    }
    
    if (c == '!') {
        if (lexer->source[lexer->position + 1] == '=') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOK_NE, "!=", 2);
        }
        lexer_advance(lexer);
        return lexer_make_token(lexer, TOK_NOT, "!", 1);
    }
    
    if (c == '<') {
        if (lexer->source[lexer->position + 1] == '=') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOK_LE, "<=", 2);
        }
        lexer_advance(lexer);
        return lexer_make_token(lexer, TOK_LT, "<", 1);
    }
    
    if (c == '>') {
        if (lexer->source[lexer->position + 1] == '=') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOK_GE, ">=", 2);
        }
        lexer_advance(lexer);
        return lexer_make_token(lexer, TOK_GT, ">", 1);
    }
    
    if (c == '&' && lexer->source[lexer->position + 1] == '&') {
        lexer_advance(lexer);
        lexer_advance(lexer);
        return lexer_make_token(lexer, TOK_AND, "&&", 2);
    }
    
    if (c == '|' && lexer->source[lexer->position + 1] == '|') {
        lexer_advance(lexer);
        lexer_advance(lexer);
        return lexer_make_token(lexer, TOK_OR, "||", 2);
    }
    
    lexer_advance(lexer);
    char msg[2] = { (char)c, '\0' };
    return lexer_error(lexer, msg);
}

void token_free(Token* token) {
    if (token->value) {
        free(token->value);
        token->value = NULL;
    }
}
