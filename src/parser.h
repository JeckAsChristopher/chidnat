#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"
#include "error.h"

typedef struct {
    Lexer   lexer;
    Token   current;
    Token   lookahead;
    bool    panic_mode;
} Parser;

void     parser_init (Parser *P, const char *source);
ASTNode *parser_parse(Parser *P);

#endif /* PARSER_H */
