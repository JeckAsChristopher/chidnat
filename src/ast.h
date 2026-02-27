// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#ifndef AST_H
#define AST_H

#include "common.h"
#include "func.h"

typedef enum {
    NODE_NUMBER, NODE_STRING, NODE_BOOL, NODE_NIL, NODE_IDENT,
    NODE_ARRAY_LITERAL,
    NODE_BINARY, NODE_UNARY, NODE_ASSIGN,
    NODE_CALL, NODE_METHOD_CALL, NODE_INDEX, NODE_INDEX_SET,
    NODE_VAR_DECL, NODE_ARRAY_DECL,
    NODE_PRINT, NODE_INPUT,
    NODE_IF, NODE_WHILE, NODE_FOR,
    NODE_SWITCH,
    NODE_BREAK, NODE_CONTINUE,
    NODE_BLOCK, NODE_EXPR_STMT, NODE_RETURN,
    NODE_FUNC_DECL,
    NODE_IMPORT, NODE_EXPORT,
    NODE_NATIVE_CALL,
    NODE_PROGRAM,
} NodeKind;

typedef struct ASTNode ASTNode;


typedef struct { double value; }            NumNode;
typedef struct { char  *value; }            StrNode;
typedef struct { bool   value; }            BoolNode;
typedef struct { char   name[MAX_IDENT_LEN]; } IdentNode;


typedef struct { ASTNode **elements; int count; } ArrayLiteralNode;


typedef struct { int op; ASTNode *left; ASTNode *right; } BinaryNode;
typedef struct { int op; ASTNode *operand; }               UnaryNode;
typedef struct { char name[MAX_IDENT_LEN]; ASTNode *value; } AssignNode;


typedef struct { char name[MAX_IDENT_LEN]; ASTNode *initializer; bool is_let; } VarDeclNode;
typedef struct { char name[MAX_IDENT_LEN]; ASTNode *initializer; }              ArrayDeclNode;


typedef struct { ASTNode **args; int arg_count; }                        PrintNode;
typedef struct { char target[MAX_IDENT_LEN]; ASTNode *prompt_expr; }    InputNode;


typedef struct { ASTNode *condition; ASTNode *then_branch; ASTNode *else_branch; } IfNode;
typedef struct { ASTNode *condition; ASTNode *body; }                               WhileNode;
typedef struct { ASTNode *init; ASTNode *condition; ASTNode *post; ASTNode *body; } ForNode;


typedef struct {
    ASTNode  *value;         
    ASTNode  *body;
} SwitchCase;

typedef struct {
    ASTNode    *subject;
    SwitchCase *cases;
    int         case_count;
    ASTNode    *default_body; 
} SwitchNode;


typedef struct { ASTNode **stmts; int count; } BlockNode;
typedef struct { ASTNode *expr; }              ExprStmtNode;
typedef struct { ASTNode *value; }             ReturnNode;



typedef struct { char name[MAX_IDENT_LEN]; ASTNode **args; int arg_count; } CallNode;
typedef struct {
    ASTNode  *object_expr;       
    char      method[MAX_IDENT_LEN];
    ASTNode **args; int arg_count;
} MethodCallNode;
typedef struct {
    ASTNode *object_expr;        
    ASTNode *index;              
} IndexNode;
typedef struct {
    ASTNode *object_expr;        
    ASTNode *index;              
    ASTNode *value;              
} IndexSetNode;


typedef struct {
    char               name[MAX_IDENT_LEN];
    char               params[MAX_PARAMS][MAX_IDENT_LEN];
    int                param_count;
    ASTNode           *body;
    FunctionVisibility visibility;
} FuncDeclNode;


typedef struct {
    uint16_t  call_id;        
    ASTNode  *args[16];
    int       argc;
} NativeCallNode;


typedef struct { char path[1024]; }           ImportNode;
typedef struct {
    char      name[MAX_IDENT_LEN];
    ASTNode  *func_def;   
} ExportNode;
typedef struct { ASTNode **stmts; int count; } ProgramNode;


struct ASTNode {
    NodeKind kind;
    int      line;
    union {
        NumNode         num;
        StrNode         str;
        BoolNode        boolean;
        IdentNode       ident;
        ArrayLiteralNode arr_lit;
        BinaryNode      binary;
        UnaryNode       unary;
        AssignNode      assign;
        VarDeclNode     var_decl;
        ArrayDeclNode   arr_decl;
        PrintNode       print;
        InputNode       input;
        IfNode          if_stmt;
        WhileNode       while_stmt;
        ForNode         for_stmt;
        SwitchNode      switch_stmt;
        BlockNode       block;
        ExprStmtNode    expr_stmt;
        ReturnNode      ret;
        CallNode        call;
        MethodCallNode  method_call;
        IndexNode       index;
        IndexSetNode    index_set;
        FuncDeclNode    func_decl;
        ImportNode      import;
        ExportNode      export_node;
        NativeCallNode  native_call;
        ProgramNode     program;
    };
};

static inline ASTNode *node_alloc(NodeKind kind, int line){
    ASTNode *n=(ASTNode*)calloc(1,sizeof(ASTNode));
    if(!n){fprintf(stderr,"oom\n");exit(1);}
    n->kind=kind; n->line=line; return n;
}

void ast_print(ASTNode *node, int indent);
void ast_free (ASTNode *node);

#endif 
