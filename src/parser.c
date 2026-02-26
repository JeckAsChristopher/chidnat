#define _POSIX_C_SOURCE 200809L
#include "parser.h"

typedef struct { ASTNode **data; int len, cap; } NL;
static void nl_push(NL *nl, ASTNode *n){
    if(nl->len>=nl->cap){
        nl->cap=nl->cap?nl->cap*2:8;
        nl->data=(ASTNode**)realloc(nl->data,nl->cap*sizeof(ASTNode*));
        if(!nl->data){fprintf(stderr,"oom\n");exit(1);}
    }
    nl->data[nl->len++]=n;
}
static ASTNode *nl_to_block(NL *nl, int line){
    ASTNode *b=node_alloc(NODE_BLOCK,line);
    b->block.stmts=nl->data; b->block.count=nl->len;
    /* nl->data now owned by block — reset nl without freeing */
    nl->data=NULL; nl->len=nl->cap=0;
    return b;
}

static void adv(Parser *P){ P->current=P->lookahead; P->lookahead=lexer_next(&P->lexer); }
static bool check(Parser *P, TokenKind k){ return P->current.kind==k; }
static bool check2(Parser *P, TokenKind k){ return P->lookahead.kind==k; }
static bool mat(Parser *P, TokenKind k){ if(!check(P,k)) return false; adv(P); return true; }
static Token consume(Parser *P, TokenKind k, const char *expected, const char *hint){
    if(check(P,k)){ Token t=P->current; adv(P); return t; }
    error_parse(P->current.line,expected,hint,"got %s",token_kind_name(P->current.kind));
    P->panic_mode=true; return P->current;
}
static void skip_nl(Parser *P){ while(check(P,TK_NEWLINE)) adv(P); }
static void consume_stmt_end(Parser *P){
    if(check(P,TK_SEMICOLON)){adv(P);skip_nl(P);}
    else if(check(P,TK_NEWLINE)) skip_nl(P);
}

/* forward decls */
static ASTNode *parse_stmt(Parser *P);
static ASTNode *parse_var_decl(Parser *P, bool is_let);
static ASTNode *parse_array_decl(Parser *P);
static ASTNode *parse_func_decl(Parser *P, FunctionVisibility vis);
static ASTNode *parse_if(Parser *P);
static ASTNode *parse_while(Parser *P);
static ASTNode *parse_for(Parser *P);
static ASTNode *parse_switch(Parser *P);
static ASTNode *parse_block(Parser *P);
static ASTNode *parse_stdo(Parser *P);
static ASTNode *parse_native_call(Parser *P);
static ASTNode *parse_stdi(Parser *P);
static ASTNode *parse_return(Parser *P);
static ASTNode *parse_import(Parser *P);
static ASTNode *parse_export(Parser *P);
static ASTNode *parse_expr(Parser *P);
static ASTNode *parse_assignment(Parser *P);
static ASTNode *parse_or(Parser *P);
static ASTNode *parse_and(Parser *P);
static ASTNode *parse_equality(Parser *P);
static ASTNode *parse_comparison(Parser *P);
static ASTNode *parse_additive(Parser *P);
static ASTNode *parse_multiply(Parser *P);
static ASTNode *parse_unary(Parser *P);
static ASTNode *parse_postfix(Parser *P);
static ASTNode *parse_primary(Parser *P);

void parser_init(Parser *P, const char *source){
    lexer_init(&P->lexer,source);
    P->panic_mode=false;
    P->current=lexer_next(&P->lexer);
    P->lookahead=lexer_next(&P->lexer);
}

ASTNode *parser_parse(Parser *P){
    skip_nl(P);
    NL nl={0};
    while(!check(P,TK_EOF)){
        if(P->panic_mode){
            while(!check(P,TK_EOF)&&!check(P,TK_NEWLINE)&&!check(P,TK_SEMICOLON)) adv(P);
            P->panic_mode=false; skip_nl(P); continue;
        }
        ASTNode *s=parse_stmt(P);
        if(s) nl_push(&nl,s);
        skip_nl(P);
    }
    ASTNode *prog=node_alloc(NODE_PROGRAM,1);
    prog->program.stmts=nl.data; prog->program.count=nl.len;
    return prog;
}

static ASTNode *parse_stmt(Parser *P){
    skip_nl(P);
    if(check(P,TK_EOF)) return NULL;

    /* visibility + func */
    if((check(P,TK_PUBLIC)||check(P,TK_PRIVATE)||check(P,TK_PROTECTED))&&check2(P,TK_FUNC)){
        FunctionVisibility vis=check(P,TK_PUBLIC)?VIS_PUBLIC:check(P,TK_PROTECTED)?VIS_PROTECTED:VIS_PRIVATE;
        adv(P); adv(P);
        return parse_func_decl(P,vis);
    }
    if(check(P,TK_FUNC)){ adv(P); return parse_func_decl(P,VIS_PRIVATE); }

    if(check(P,TK_LET))    { adv(P); return parse_var_decl(P,true);  }
    if(check(P,TK_VAR))    { adv(P); return parse_var_decl(P,false); }
    if(check(P,TK_ARRAY))  { adv(P); return parse_array_decl(P);     }
    if(check(P,TK_IF))     { adv(P); return parse_if(P);             }
    if(check(P,TK_WHILE))  { adv(P); return parse_while(P);          }
    if(check(P,TK_FOR))    { adv(P); return parse_for(P);            }
    if(check(P,TK_SWITCH)) { adv(P); return parse_switch(P);         }
    if(check(P,TK_RETURN)) { adv(P); return parse_return(P);         }
    if(check(P,TK_LBRACE))          return parse_block(P);
    if(check(P,TK_STDO))   { adv(P); return parse_stdo(P);           }
    if(check(P,TK_STDI))   { adv(P); return parse_stdi(P);           }
    if(check(P,TK_IMP))    { adv(P); return parse_import(P);         }
    if(check(P,TK_EXPORT)) { adv(P); return parse_export(P);         }

    if(check(P,TK_BREAK)){
        int line=P->current.line; adv(P); consume_stmt_end(P);
        return node_alloc(NODE_BREAK,line);
    }
    if(check(P,TK_CONTINUE)){
        int line=P->current.line; adv(P); consume_stmt_end(P);
        return node_alloc(NODE_CONTINUE,line);
    }

    ASTNode *expr=parse_expr(P);
    ASTNode *s=node_alloc(NODE_EXPR_STMT,expr?expr->line:0);
    s->expr_stmt.expr=expr;
    consume_stmt_end(P);
    return s;
}

static ASTNode *parse_func_decl(Parser *P, FunctionVisibility vis){
    int line=P->current.line;
    if(!check(P,TK_IDENT)){
        error_parse(line,"function name","func name(params) { }","expected function name");
        P->panic_mode=true; return NULL;
    }
    char name[MAX_IDENT_LEN];
    int nl2=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,nl2); name[nl2]='\0';
    adv(P);
    consume(P,TK_LPAREN,"'('","func name(params) { }");
    if(P->panic_mode) return NULL;
    char params[MAX_PARAMS][MAX_IDENT_LEN]; int pc=0;
    skip_nl(P);
    if(!check(P,TK_RPAREN)){
        do {
            skip_nl(P);
            if(!check(P,TK_IDENT)){ error_parse(P->current.line,"param name",NULL,"expected param name"); P->panic_mode=true; return NULL; }
            if(pc>=MAX_PARAMS){ error_parse(P->current.line,NULL,NULL,"too many parameters"); P->panic_mode=true; return NULL; }
            int plen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
            memcpy(params[pc],P->current.start,plen); params[pc][plen]='\0'; pc++;
            adv(P);
        } while(mat(P,TK_COMMA));
    }
    consume(P,TK_RPAREN,"')'","close params with ')'");
    skip_nl(P);
    ASTNode *body=parse_block(P);
    ASTNode *n=node_alloc(NODE_FUNC_DECL,line);
    strncpy(n->func_decl.name,name,MAX_IDENT_LEN-1);
    for(int i=0;i<pc;i++) strncpy(n->func_decl.params[i],params[i],MAX_IDENT_LEN-1);
    n->func_decl.param_count=pc;
    n->func_decl.body=body;
    n->func_decl.visibility=vis;
    return n;
}

static ASTNode *parse_var_decl(Parser *P, bool is_let){
    int line=P->current.line;
    if(!check(P,TK_IDENT)){
        error_parse(line,"variable name","var name = value","expected name"); P->panic_mode=true; return NULL;
    }
    char name[MAX_IDENT_LEN];
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,nlen); name[nlen]='\0';
    adv(P);
    ASTNode *init=mat(P,TK_ASSIGN)?parse_expr(P):node_alloc(NODE_NIL,line);
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_VAR_DECL,line);
    strncpy(n->var_decl.name,name,MAX_IDENT_LEN-1);
    n->var_decl.initializer=init; n->var_decl.is_let=is_let;
    return n;
}

static ASTNode *parse_array_decl(Parser *P){
    int line=P->current.line;
    if(!check(P,TK_IDENT)){
        error_parse(line,"array name","array myList = [1,2,3]","expected name"); P->panic_mode=true; return NULL;
    }
    char name[MAX_IDENT_LEN];
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,nlen); name[nlen]='\0'; adv(P);
    ASTNode *init=NULL;
    if(mat(P,TK_ASSIGN)){
        if(check(P,TK_LBRACKET)){
            /* Literal array: [e1, e2, ...] */
            adv(P);
            NL elems={0}; skip_nl(P);
            if(!check(P,TK_RBRACKET)){
                do{ skip_nl(P); ASTNode *e=parse_expr(P); if(e) nl_push(&elems,e); skip_nl(P); } while(mat(P,TK_COMMA));
            }
            consume(P,TK_RBRACKET,"']'","close array literal with ']'");
            ASTNode *al=node_alloc(NODE_ARRAY_LITERAL,line);
            al->arr_lit.elements=elems.data; al->arr_lit.count=elems.len;
            init=al;
        } else {
            /* Expression that returns an array (e.g. function call) */
            init=parse_expr(P);
        }
    } else {
        ASTNode *al=node_alloc(NODE_ARRAY_LITERAL,line);
        al->arr_lit.elements=NULL; al->arr_lit.count=0; init=al;
    }
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_ARRAY_DECL,line);
    strncpy(n->arr_decl.name,name,MAX_IDENT_LEN-1);
    n->arr_decl.initializer=init;
    return n;
}

static ASTNode *parse_return(Parser *P){
    int line=P->current.line;
    ASTNode *val=NULL;
    if(!check(P,TK_NEWLINE)&&!check(P,TK_SEMICOLON)&&!check(P,TK_EOF)&&!check(P,TK_RBRACE))
        val=parse_expr(P);
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_RETURN,line);
    n->ret.value=val; return n;
}

static ASTNode *parse_import(Parser *P){
    int line=P->current.line;
    ASTNode *n=node_alloc(NODE_IMPORT,line);
    if(check(P,TK_STRING)){
        /* imp "path/to/file"  — explicit path with quotes */
        strncpy(n->import.path,P->current.value.string,1023);
        adv(P);
    } else if(check(P,TK_IDENT)){
        /* imp modulename  — bare identifier, resolved via chn-libs/ search */
        int len = P->current.length < 1023 ? P->current.length : 1023;
        memcpy(n->import.path, P->current.start, len);
        n->import.path[len] = '\0';
        adv(P);
    } else {
        error_parse(line,"module name","imp os  or  imp \"path/file.chn\"","expected module name or path");
        P->panic_mode=true; return NULL;
    }
    consume_stmt_end(P); return n;
}

static ASTNode *parse_export(Parser *P){
    int line=P->current.line;
    /* Support: export func name()
     *          export public func name()
     *          export private func name()
     *          export protected func name()
     *          export funcName   (legacy: mark already-declared func as exported)
     */
    FunctionVisibility vis = VIS_PUBLIC;  /* default visibility for export */
    if (check(P,TK_PUBLIC)||check(P,TK_PRIVATE)||check(P,TK_PROTECTED)){
        vis = check(P,TK_PUBLIC)?VIS_PUBLIC:check(P,TK_PROTECTED)?VIS_PROTECTED:VIS_PRIVATE;
        adv(P);
    }
    if (check(P,TK_FUNC)){
        adv(P);
        ASTNode *fn = parse_func_decl(P, vis);
        if (!fn) return NULL;
        /* Mark as exported: wrap in a NODE_EXPORT that names this function.
           The compiler handles NODE_FUNC_DECL first, then NODE_EXPORT uses name. */
        /* Simplest approach: return a NODE_EXPORT whose name = func_name,
           and also compile the function by embedding it.
           We actually just need to set the func as exported during compilation.
           Use a dedicated flag by storing as NODE_EXPORT wrapping the func decl. */
        ASTNode *ex = node_alloc(NODE_EXPORT, line);
        strncpy(ex->export_node.name, fn->func_decl.name, MAX_IDENT_LEN-1);
        /* Store the func_decl node so the compiler sees both */
        ex->export_node.func_def = fn;
        return ex;
    }
    /* Legacy form: export funcName */
    if(!check(P,TK_IDENT)){
        error_parse(line,"function name","export funcName","expected name"); P->panic_mode=true; return NULL;
    }
    ASTNode *n=node_alloc(NODE_EXPORT,line);
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(n->export_node.name,P->current.start,nlen); n->export_node.name[nlen]='\0';
    n->export_node.func_def = NULL;
    adv(P); consume_stmt_end(P); return n;
}


static ASTNode *parse_native_call(Parser *P){
    /* __native__(call_id, arg0, arg1, ...) */
    int line=P->current.line;
    consume(P,TK_LPAREN,"'('","__native__(id, ...)");
    if(!check(P,TK_NUMBER)){ P->panic_mode=true; return NULL; }
    int call_id=(int)P->current.value.number; adv(P);
    ASTNode *n=node_alloc(NODE_NATIVE_CALL,line);
    n->native_call.call_id=(uint16_t)call_id;
    n->native_call.argc=0;
    while(check(P,TK_COMMA)){
        adv(P);
        if(n->native_call.argc<16)
            n->native_call.args[n->native_call.argc++]=parse_expr(P);
    }
    consume(P,TK_RPAREN,"')' after __native__ args","__native__(id, ...)");
    return n;
}

static ASTNode *parse_stdo(Parser *P){
    int line=P->current.line;
    consume(P,TK_LPAREN,"'('","stdo(expr)");
    if(P->panic_mode) return NULL;
    NL args={0};
    if(!check(P,TK_RPAREN)){
        do{ skip_nl(P); ASTNode *a=parse_expr(P); if(a) nl_push(&args,a); } while(mat(P,TK_COMMA));
    }
    consume(P,TK_RPAREN,"')'","close stdo with ')'");
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_PRINT,line);
    n->print.args=args.data; n->print.arg_count=args.len;
    return n;
}

static ASTNode *parse_stdi(Parser *P){
    int line=P->current.line;
    consume(P,TK_LPAREN,"'('","stdi(var)");
    if(P->panic_mode) return NULL;
    if(!check(P,TK_IDENT)){
        error_parse(P->current.line,"variable name","stdi(myVar)","expected variable"); P->panic_mode=true; return NULL;
    }
    char name[MAX_IDENT_LEN];
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,nlen); name[nlen]='\0'; adv(P);
    ASTNode *prompt=NULL;
    if(mat(P,TK_COMMA)) prompt=parse_expr(P);
    consume(P,TK_RPAREN,"')'","close stdi with ')'");
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_INPUT,line);
    strncpy(n->input.target,name,MAX_IDENT_LEN-1);
    n->input.prompt_expr=prompt;
    return n;
}

static ASTNode *parse_if(Parser *P){
    int line=P->current.line;
    consume(P,TK_LPAREN,"'('","if (cond) { }");
    if(P->panic_mode) return NULL;
    ASTNode *cond=parse_expr(P);
    consume(P,TK_RPAREN,"')'","close condition with ')'");
    skip_nl(P);
    ASTNode *then_b=parse_block(P);
    ASTNode *else_b=NULL;
    skip_nl(P);
    if(mat(P,TK_ELSE)){
        skip_nl(P);
        else_b=check(P,TK_IF)?(adv(P),parse_if(P)):parse_block(P);
    }
    ASTNode *n=node_alloc(NODE_IF,line);
    n->if_stmt.condition=cond; n->if_stmt.then_branch=then_b; n->if_stmt.else_branch=else_b;
    return n;
}

static ASTNode *parse_while(Parser *P){
    int line=P->current.line;
    consume(P,TK_LPAREN,"'('","while (cond) { }");
    if(P->panic_mode) return NULL;
    ASTNode *cond=parse_expr(P);
    consume(P,TK_RPAREN,"')'","close condition with ')'");
    skip_nl(P);
    ASTNode *body=parse_block(P);
    ASTNode *n=node_alloc(NODE_WHILE,line);
    n->while_stmt.condition=cond; n->while_stmt.body=body;
    return n;
}

static ASTNode *parse_for(Parser *P){
    int line=P->current.line;
    consume(P,TK_LPAREN,"'('","for (init; cond; post) { }");
    if(P->panic_mode) return NULL;
    ASTNode *init=NULL;
    if(!check(P,TK_SEMICOLON)){
        if(check(P,TK_VAR)){ adv(P); init=parse_var_decl(P,false); }
        else if(check(P,TK_LET)){ adv(P); init=parse_var_decl(P,true); }
        else {
            ASTNode *e=parse_expr(P);
            ASTNode *es=node_alloc(NODE_EXPR_STMT,e?e->line:line);
            es->expr_stmt.expr=e; init=es;
            if(check(P,TK_SEMICOLON)) adv(P);
        }
        if(check(P,TK_SEMICOLON)) adv(P);
        skip_nl(P);
    } else { adv(P); }
    ASTNode *cond=(!check(P,TK_SEMICOLON))?parse_expr(P):NULL;
    consume(P,TK_SEMICOLON,"';'","separate for clauses with ';'");
    skip_nl(P);
    ASTNode *post=(!check(P,TK_RPAREN))?parse_expr(P):NULL;
    consume(P,TK_RPAREN,"')'","close for with ')'");
    skip_nl(P);
    ASTNode *body=parse_block(P);
    ASTNode *n=node_alloc(NODE_FOR,line);
    n->for_stmt.init=init; n->for_stmt.condition=cond;
    n->for_stmt.post=post; n->for_stmt.body=body;
    return n;
}

/* ── Switch: switch (expr) { case val { body } ... default { body } } ──── */
static ASTNode *parse_switch(Parser *P){
    int line=P->current.line;
    consume(P,TK_LPAREN,"'('","switch (expr) { case v: stmts break; }");
    if(P->panic_mode) return NULL;
    ASTNode *subject=parse_expr(P);
    consume(P,TK_RPAREN,"')'","close switch expr with ')'");
    skip_nl(P);
    consume(P,TK_LBRACE,"'{'","switch body must be a block");
    if(P->panic_mode) return NULL;

    SwitchCase *cases=NULL; int cap=0,count=0;
    ASTNode *default_body=NULL;
    skip_nl(P);

    while(!check(P,TK_RBRACE)&&!check(P,TK_EOF)){
        if(check(P,TK_CASE)){
            adv(P);
            ASTNode *val=parse_expr(P);
            skip_nl(P);
            if(check(P,TK_COLON)) adv(P);
            skip_nl(P);
            /* collect statements until case / default / } — break is left
               as a normal statement (NODE_BREAK), NOT consumed here      */
            NL nl={0};
            while(!check(P,TK_RBRACE)&&!check(P,TK_EOF)
                  &&!check(P,TK_CASE)&&!check(P,TK_DEFAULT)){
                if(P->panic_mode){
                    while(!check(P,TK_EOF)&&!check(P,TK_NEWLINE)&&!check(P,TK_RBRACE)) adv(P);
                    P->panic_mode=false; skip_nl(P); continue;
                }
                ASTNode *s=parse_stmt(P);
                if(s) nl_push(&nl,s);
                skip_nl(P);
            }
            ASTNode *body=nl_to_block(&nl,line);
            if(count>=cap){ cap=cap?cap*2:4; cases=(SwitchCase*)realloc(cases,cap*sizeof(SwitchCase)); }
            cases[count].value=val; cases[count].body=body; count++;
        } else if(check(P,TK_DEFAULT)){
            adv(P);
            if(check(P,TK_COLON)) adv(P);
            skip_nl(P);
            NL nl={0};
            while(!check(P,TK_RBRACE)&&!check(P,TK_EOF)&&!check(P,TK_CASE)&&!check(P,TK_DEFAULT)){
                ASTNode *s=parse_stmt(P);
                if(s) nl_push(&nl,s);
                skip_nl(P);
            }
            default_body=nl_to_block(&nl,line);
        } else {
            error_parse(P->current.line,"'case' or 'default'",NULL,"expected case or default inside switch");
            P->panic_mode=true; break;
        }
        skip_nl(P);
    }
    consume(P,TK_RBRACE,"'}'","close switch with '}'");

    ASTNode *n=node_alloc(NODE_SWITCH,line);
    n->switch_stmt.subject=subject;
    n->switch_stmt.cases=cases; n->switch_stmt.case_count=count;
    n->switch_stmt.default_body=default_body;
    return n;
}

static ASTNode *parse_block(Parser *P){
    int line=P->current.line;
    consume(P,TK_LBRACE,"'{'","start block with '{'");
    if(P->panic_mode) return NULL;
    NL nl={0}; skip_nl(P);
    while(!check(P,TK_RBRACE)&&!check(P,TK_EOF)){
        if(P->panic_mode){
            while(!check(P,TK_EOF)&&!check(P,TK_NEWLINE)&&!check(P,TK_RBRACE)) adv(P);
            P->panic_mode=false; skip_nl(P); continue;
        }
        ASTNode *s=parse_stmt(P);
        if(s) nl_push(&nl,s);
        skip_nl(P);
    }
    consume(P,TK_RBRACE,"'}'","close block with '}'");
    ASTNode *n=node_alloc(NODE_BLOCK,line);
    n->block.stmts=nl.data; n->block.count=nl.len;
    return n;
}

/* ── Expressions ──────────────────────────────────────────────────────────── */
static ASTNode *parse_expr(Parser *P) { return parse_assignment(P); }

static ASTNode *parse_assignment(Parser *P){
    /* Simple variable assignment: ident = expr */
    if(check(P,TK_IDENT)&&check2(P,TK_ASSIGN)){
        char name[MAX_IDENT_LEN];
        int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
        memcpy(name,P->current.start,nlen); name[nlen]='\0';
        int line=P->current.line; adv(P); adv(P);
        ASTNode *val=parse_expr(P);
        ASTNode *n=node_alloc(NODE_ASSIGN,line);
        strncpy(n->assign.name,name,MAX_IDENT_LEN-1);
        n->assign.value=val; return n;
    }
    /* Index assignment: expr[idx] = val
       Parse postfix first; if result is NODE_INDEX and next token is '=',
       convert to NODE_INDEX_SET. */
    ASTNode *lhs = parse_or(P);
    if(lhs && lhs->kind==NODE_INDEX && check(P,TK_ASSIGN)){
        int line=P->current.line; adv(P);
        ASTNode *val=parse_expr(P);
        ASTNode *n=node_alloc(NODE_INDEX_SET,line);
        /* steal object_expr and index from the INDEX node */
        n->index_set.object_expr = lhs->index.object_expr;
        n->index_set.index       = lhs->index.index;
        n->index_set.value       = val;
        lhs->index.object_expr   = NULL;
        lhs->index.index         = NULL;
        ast_free(lhs);
        return n;
    }
    return lhs;
}
static ASTNode *parse_or(Parser *P){
    ASTNode *l=parse_and(P);
    while(check(P,TK_OR)){
        int op=P->current.kind,line=P->current.line; adv(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
        n->binary.op=op; n->binary.left=l; n->binary.right=parse_and(P); l=n;
    } return l;
}
static ASTNode *parse_and(Parser *P){
    ASTNode *l=parse_equality(P);
    while(check(P,TK_AND)){
        int op=P->current.kind,line=P->current.line; adv(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
        n->binary.op=op; n->binary.left=l; n->binary.right=parse_equality(P); l=n;
    } return l;
}
static ASTNode *parse_equality(Parser *P){
    ASTNode *l=parse_comparison(P);
    while(check(P,TK_EQ)||check(P,TK_NEQ)){
        int op=P->current.kind,line=P->current.line; adv(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
        n->binary.op=op; n->binary.left=l; n->binary.right=parse_comparison(P); l=n;
    } return l;
}
static ASTNode *parse_comparison(Parser *P){
    ASTNode *l=parse_additive(P);
    while(check(P,TK_LT)||check(P,TK_GT)||check(P,TK_LE)||check(P,TK_GE)){
        int op=P->current.kind,line=P->current.line; adv(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
        n->binary.op=op; n->binary.left=l; n->binary.right=parse_additive(P); l=n;
    } return l;
}
static ASTNode *parse_additive(Parser *P){
    ASTNode *l=parse_multiply(P);
    while(check(P,TK_PLUS)||check(P,TK_MINUS)){
        int op=P->current.kind,line=P->current.line; adv(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
        n->binary.op=op; n->binary.left=l; n->binary.right=parse_multiply(P); l=n;
    } return l;
}
static ASTNode *parse_multiply(Parser *P){
    ASTNode *l=parse_unary(P);
    while(check(P,TK_STAR)||check(P,TK_SLASH)||check(P,TK_PERCENT)){
        int op=P->current.kind,line=P->current.line; adv(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
        n->binary.op=op; n->binary.left=l; n->binary.right=parse_unary(P); l=n;
    } return l;
}
static ASTNode *parse_unary(Parser *P){
    if(check(P,TK_MINUS)||check(P,TK_BANG)){
        int op=P->current.kind,line=P->current.line; adv(P);
        ASTNode *n=node_alloc(NODE_UNARY,line);
        n->unary.op=op; n->unary.operand=parse_unary(P); return n;
    }
    return parse_postfix(P);
}

static ASTNode *parse_postfix(Parser *P){
    ASTNode *base=parse_primary(P);
    if(!base) return base;
    for(;;){
        if(check(P,TK_DOT)){
            int line=P->current.line; adv(P);
            if(!check(P,TK_IDENT)){
                error_parse(P->current.line,"method name","obj.method(args)","expected method name");
                P->panic_mode=true; return base;
            }
            char method[MAX_IDENT_LEN];
            int ml=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
            memcpy(method,P->current.start,ml); method[ml]='\0'; adv(P);
            consume(P,TK_LPAREN,"'('","method call: obj.method(args)");
            if(P->panic_mode) return base;
            NL args={0}; skip_nl(P);
            if(!check(P,TK_RPAREN)){
                do{ skip_nl(P); ASTNode *a=parse_expr(P); if(a) nl_push(&args,a); } while(mat(P,TK_COMMA));
            }
            skip_nl(P);
            consume(P,TK_RPAREN,"')'","close args with ')'");
            ASTNode *n=node_alloc(NODE_METHOD_CALL,line);
            n->method_call.object_expr = base;   /* ANY expression as object */
            strncpy(n->method_call.method,method,MAX_IDENT_LEN-1);
            n->method_call.args=args.data; n->method_call.arg_count=args.len;
            base=n;
        } else if(check(P,TK_LBRACKET)){
            int line=P->current.line; adv(P);
            ASTNode *idx=parse_expr(P);
            consume(P,TK_RBRACKET,"']'","close index with ']'");
            ASTNode *n=node_alloc(NODE_INDEX,line);
            n->index.object_expr = base;    /* ANY expression as object */
            n->index.index=idx;
            base=n;
        } else break;
    }
    return base;
}

static ASTNode *parse_primary(Parser *P){
    int line=P->current.line;
    if(check(P,TK_NATIVE_CALL)){ adv(P); return parse_native_call(P); }
    if(check(P,TK_NUMBER)){
        ASTNode *n=node_alloc(NODE_NUMBER,line);
        n->num.value=P->current.value.number; adv(P); return n;
    }
    if(check(P,TK_STRING)){
        ASTNode *n=node_alloc(NODE_STRING,line);
        n->str.value=strdup(P->current.value.string); adv(P); return n;
    }
    if(check(P,TK_TRUE)){ adv(P); ASTNode *n=node_alloc(NODE_BOOL,line); n->boolean.value=true; return n; }
    if(check(P,TK_FALSE)){ adv(P); ASTNode *n=node_alloc(NODE_BOOL,line); n->boolean.value=false; return n; }
    if(check(P,TK_NIL)){ adv(P); return node_alloc(NODE_NIL,line); }
    if(check(P,TK_LBRACKET)){
        adv(P); NL elems={0}; skip_nl(P);
        if(!check(P,TK_RBRACKET)){
            do{ skip_nl(P); ASTNode *e=parse_expr(P); if(e) nl_push(&elems,e); skip_nl(P); } while(mat(P,TK_COMMA));
        }
        consume(P,TK_RBRACKET,"']'","close array with ']'");
        ASTNode *al=node_alloc(NODE_ARRAY_LITERAL,line);
        al->arr_lit.elements=elems.data; al->arr_lit.count=elems.len;
        return al;
    }
    if(check(P,TK_IDENT)){
        char name[MAX_IDENT_LEN];
        int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
        memcpy(name,P->current.start,nlen); name[nlen]='\0'; adv(P);
        if(check(P,TK_LPAREN)){
            adv(P); NL args={0}; skip_nl(P);
            if(!check(P,TK_RPAREN)){
                do{ skip_nl(P); ASTNode *a=parse_expr(P); if(a) nl_push(&args,a); } while(mat(P,TK_COMMA));
            }
            consume(P,TK_RPAREN,"')'","close args with ')'");
            ASTNode *n=node_alloc(NODE_CALL,line);
            strncpy(n->call.name,name,MAX_IDENT_LEN-1);
            n->call.args=args.data; n->call.arg_count=args.len;
            return n;
        }
        ASTNode *n=node_alloc(NODE_IDENT,line);
        strncpy(n->ident.name,name,MAX_IDENT_LEN-1); return n;
    }
    if(check(P,TK_LPAREN)){
        adv(P); ASTNode *inner=parse_expr(P);
        consume(P,TK_RPAREN,"')'","close grouped expression");
        return inner;
    }
    if(!P->panic_mode){
        if(check(P,TK_EOF)) error_parse(line,"expression",NULL,"unexpected end of file");
        else error_parse(line,"expression","check for missing operand",
                         "unexpected token %s",token_kind_name(P->current.kind));
        P->panic_mode=true;
    }
    adv(P); return node_alloc(NODE_NIL,line);
}
