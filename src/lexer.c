// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#define _POSIX_C_SOURCE 200809L
#include "lexer.h"
#include "error.h"

static inline bool is_digit(char c)  { return c>='0'&&c<='9'; }
static inline bool is_alpha(char c)  { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static inline bool is_alnum(char c)  { return is_alpha(c)||is_digit(c); }
static inline bool at_end(Lexer *L)  { return *L->current=='\0'; }
static inline char adv(Lexer *L)     { return *L->current++; }
static inline char pk(Lexer *L)      { return *L->current; }
static inline char pk2(Lexer *L)     { return at_end(L)?'\0':L->current[1]; }
static inline bool mat(Lexer *L,char e){
    if(at_end(L)||*L->current!=e) return false;
    L->current++; return true;
}

static Token mktok(Lexer *L, TokenKind k){
    Token t={0}; t.kind=k; t.start=L->start;
    t.length=(int)(L->current-L->start); t.line=L->line;
    return t;
}
static Token errtok(Lexer *L, const char *msg){
    error_lex(L->line, "%s", msg);
    Token t={0}; t.kind=TK_ERROR; t.start=msg;
    t.length=(int)strlen(msg); t.line=L->line; return t;
}

static void skip_ws(Lexer *L){
    for(;;){
        char c=pk(L);
        switch(c){
            case ' ': case '\r': case '\t': adv(L); break;
            case '\n': return;
            case '-': if(pk2(L)=='-'){
                while(!at_end(L)&&pk(L)!='\n') adv(L);
            } else return; break;
            default: return;
        }
    }
}

static Token scan_string(Lexer *L){
    char buf[MAX_STRING_LEN]; int pos=0;
    while(!at_end(L)&&pk(L)!='"'){
        char c=pk(L);
        if(c=='\n') L->line++;
        if(c=='\\'){
            adv(L); char e=adv(L);
            switch(e){
                case 'n':  buf[pos++]='\n'; break;
                case 't':  buf[pos++]='\t'; break;
                case 'r':  buf[pos++]='\r'; break;
                case '"':  buf[pos++]='"';  break;
                case '\\': buf[pos++]='\\'; break;
                case 'e':  buf[pos++]='\x1b'; break;  
                case '0':    break;
                case 'a':  buf[pos++]='\a';  break;  
                case 'b':  buf[pos++]='\b';  break;  
                default:   buf[pos++]='\\'; buf[pos++]=e;
            }
        } else { buf[pos++]=adv(L); }
        if(pos>=MAX_STRING_LEN-1) return errtok(L,"string literal too long");
    }
    if(at_end(L)) return errtok(L,"unterminated string — missing closing '\"'");
    adv(L); buf[pos]='\0';
    Token t=mktok(L,TK_STRING);
    memcpy(t.value.string, buf, pos+1);
    return t;
}

static int is_hex_digit(char c){
    return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
}
static Token scan_number(Lexer *L){
    
    if(*(L->current-1)=='0' && (pk(L)=='x'||pk(L)=='X')){
        adv(L); 
        while(is_hex_digit(pk(L))) adv(L);
        Token t=mktok(L,TK_NUMBER);
        char buf[64]; int len=t.length<63?t.length:63;
        memcpy(buf,t.start,len); buf[len]='\0';
        t.value.number=(double)strtoll(buf,NULL,16);
        return t;
    }
    while(is_digit(pk(L))) adv(L);
    if(pk(L)=='.'&&is_digit(pk2(L))){ adv(L); while(is_digit(pk(L))) adv(L); }
    if(pk(L)=='e'||pk(L)=='E'){
        adv(L);
        if(pk(L)=='+'||pk(L)=='-') adv(L);
        if(!is_digit(pk(L))) return errtok(L,"invalid number exponent");
        while(is_digit(pk(L))) adv(L);
    }
    Token t=mktok(L,TK_NUMBER);
    char buf[64]; int len=t.length<63?t.length:63;
    memcpy(buf,t.start,len); buf[len]='\0';
    t.value.number=strtod(buf,NULL);
    return t;
}

typedef struct { const char *w; TokenKind k; } KW;
static KW kws[]={
    {"let",TK_LET},{"var",TK_VAR},{"array",TK_ARRAY},
    {"if",TK_IF},{"else",TK_ELSE},
    {"while",TK_WHILE},{"for",TK_FOR},
    {"switch",TK_SWITCH},{"case",TK_CASE},{"default",TK_DEFAULT},
    {"break",TK_BREAK},{"continue",TK_CONTINUE},
    {"return",TK_RETURN},
    {"func",TK_FUNC},{"public",TK_PUBLIC},{"private",TK_PRIVATE},
    {"protected",TK_PROTECTED},{"imp",TK_IMP},{"export",TK_EXPORT},
    {"true",TK_TRUE},{"false",TK_FALSE},{"nil",TK_NIL},
    {"stdo",TK_STDO},{"stdi",TK_STDI},{"__native__",TK_NATIVE_CALL},
    {NULL,TK_ERROR}
};

static Token scan_ident(Lexer *L){
    while(is_alnum(pk(L))) adv(L);
    int len=(int)(L->current-L->start);
    for(int i=0;kws[i].w;i++){
        int kl=(int)strlen(kws[i].w);
        if(len==kl&&memcmp(L->start,kws[i].w,len)==0)
            return mktok(L,kws[i].k);
    }
    return mktok(L,TK_IDENT);
}

void lexer_init(Lexer *L, const char *source){
    L->source=source; L->start=source; L->current=source; L->line=1;
}

Token lexer_next(Lexer *L){
    skip_ws(L);
    L->start=L->current;
    if(at_end(L)) return mktok(L,TK_EOF);
    char c=adv(L);
    if(c=='\n'){  L->line++; return mktok(L,TK_NEWLINE); }
    if(is_digit(c)) return scan_number(L);
    if(is_alpha(c)) return scan_ident(L);
    switch(c){
        case '"':  return scan_string(L);
        case '(':  return mktok(L,TK_LPAREN);
        case ')':  return mktok(L,TK_RPAREN);
        case '{':  return mktok(L,TK_LBRACE);
        case '}':  return mktok(L,TK_RBRACE);
        case '[':  return mktok(L,TK_LBRACKET);
        case ']':  return mktok(L,TK_RBRACKET);
        case ',':  return mktok(L,TK_COMMA);
        case ';':  return mktok(L,TK_SEMICOLON);
        case ':':  return mktok(L,TK_COLON);
        case '.':  return mktok(L,TK_DOT);
        case '%':  return mktok(L,TK_PERCENT);
        case '+':  return mktok(L,TK_PLUS);
        case '*':  return mktok(L,TK_STAR);
        case '/':  return mktok(L,TK_SLASH);
        case '!':  return mktok(L,mat(L,'=')?TK_NEQ:TK_BANG);
        case '=':  return mktok(L,mat(L,'=')?TK_EQ:TK_ASSIGN);
        case '<':  return mktok(L,mat(L,'=')?TK_LE:TK_LT);
        case '>':  return mktok(L,mat(L,'=')?TK_GE:TK_GT);
        case '&':
            if(mat(L,'&')) return mktok(L,TK_AND);
            return errtok(L,"unexpected '&' — did you mean '&&'?");
        case '|':
            if(mat(L,'|')) return mktok(L,TK_OR);
            return errtok(L,"unexpected '|' — did you mean '||'?");
        case '-':  return mktok(L,TK_MINUS);
    }
    char em[64]; snprintf(em,sizeof(em),"unexpected character '%c'",c);
    return errtok(L,em);
}

Token lexer_peek(Lexer *L){
    const char *ss=L->start,*sc=L->current; int sl=L->line;
    Token t=lexer_next(L);
    L->start=ss; L->current=sc; L->line=sl;
    return t;
}

const char *token_kind_name(TokenKind k){
    switch(k){
        case TK_NUMBER:    return "number";
        case TK_STRING:    return "string";
        case TK_IDENT:     return "identifier";
        case TK_TRUE:      return "'true'";
        case TK_FALSE:     return "'false'";
        case TK_NIL:       return "'nil'";
        case TK_LET:       return "'let'";
        case TK_VAR:       return "'var'";
        case TK_ARRAY:     return "'array'";
        case TK_IF:        return "'if'";
        case TK_ELSE:      return "'else'";
        case TK_WHILE:     return "'while'";
        case TK_FOR:       return "'for'";
        case TK_SWITCH:    return "'switch'";
        case TK_CASE:      return "'case'";
        case TK_DEFAULT:   return "'default'";
        case TK_BREAK:     return "'break'";
        case TK_CONTINUE:  return "'continue'";
        case TK_RETURN:    return "'return'";
        case TK_FUNC:      return "'func'";
        case TK_PUBLIC:    return "'public'";
        case TK_PRIVATE:   return "'private'";
        case TK_PROTECTED: return "'protected'";
        case TK_IMP:       return "'imp'";
        case TK_EXPORT:    return "'export'";
        case TK_STDO:      return "'stdo'";
        case TK_STDI:      return "'stdi'";
        case TK_NATIVE_CALL: return "'__native__'";
        case TK_PLUS:      return "'+'";
        case TK_MINUS:     return "'-'";
        case TK_STAR:      return "'*'";
        case TK_SLASH:     return "'/'";
        case TK_PERCENT:   return "'%'";
        case TK_EQ:        return "'=='";
        case TK_NEQ:       return "'!='";
        case TK_LT:        return "'<'";
        case TK_GT:        return "'>'";
        case TK_LE:        return "'<='";
        case TK_GE:        return "'>='";
        case TK_ASSIGN:    return "'='";
        case TK_AND:       return "'&&'";
        case TK_OR:        return "'||'";
        case TK_BANG:      return "'!'";
        case TK_DOT:       return "'.'";
        case TK_LPAREN:    return "'('";
        case TK_RPAREN:    return "')'";
        case TK_LBRACE:    return "'{'";
        case TK_RBRACE:    return "'}'";
        case TK_LBRACKET:  return "'['";
        case TK_RBRACKET:  return "']'";
        case TK_COMMA:     return "','";
        case TK_SEMICOLON: return "';'";
        case TK_COLON:     return "':'";
        case TK_NEWLINE:   return "newline";
        case TK_EOF:       return "end-of-file";
        default:           return "<token>";
    }
}
