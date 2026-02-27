// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#include "ast.h"
#include "lexer.h"

static void ind(int n){ for(int i=0;i<n;i++) printf("  "); }

void ast_print(ASTNode *node, int indent){
    if(!node){ind(indent);printf("<null>\n");return;}
    ind(indent);
    switch(node->kind){
        case NODE_NUMBER:    printf("Number(%g)\n",node->num.value); break;
        case NODE_STRING:    printf("String(\"%s\")\n",node->str.value); break;
        case NODE_BOOL:      printf("Bool(%s)\n",node->boolean.value?"true":"false"); break;
        case NODE_NIL:       printf("Nil\n"); break;
        case NODE_IDENT:     printf("Ident(%s)\n",node->ident.name); break;
        case NODE_BREAK:     printf("Break\n"); break;
        case NODE_CONTINUE:  printf("Continue\n"); break;
        case NODE_ARRAY_LITERAL:
            printf("Array[%d]\n",node->arr_lit.count);
            for(int i=0;i<node->arr_lit.count;i++) ast_print(node->arr_lit.elements[i],indent+1);
            break;
        case NODE_BINARY:
            printf("Binary(%s)\n",token_kind_name(node->binary.op));
            ast_print(node->binary.left,indent+1);
            ast_print(node->binary.right,indent+1);
            break;
        case NODE_UNARY:
            printf("Unary(%s)\n",token_kind_name(node->unary.op));
            ast_print(node->unary.operand,indent+1);
            break;
        case NODE_ASSIGN:
            printf("Assign(%s)\n",node->assign.name);
            ast_print(node->assign.value,indent+1);
            break;
        case NODE_VAR_DECL:
            printf("VarDecl(%s %s)\n",node->var_decl.is_let?"let":"var",node->var_decl.name);
            if(node->var_decl.initializer) ast_print(node->var_decl.initializer,indent+1);
            break;
        case NODE_ARRAY_DECL:
            printf("ArrayDecl(%s)\n",node->arr_decl.name);
            if(node->arr_decl.initializer) ast_print(node->arr_decl.initializer,indent+1);
            break;
        case NODE_CALL:
            printf("Call(%s/%d)\n",node->call.name,node->call.arg_count);
            for(int i=0;i<node->call.arg_count;i++) ast_print(node->call.args[i],indent+1);
            break;
        case NODE_METHOD_CALL:
            printf("MethodCall(.%s/%d)\n",node->method_call.method,node->method_call.arg_count);
            ast_print(node->method_call.object_expr,indent+1);
            for(int i=0;i<node->method_call.arg_count;i++) ast_print(node->method_call.args[i],indent+1);
            break;
        case NODE_INDEX:
            printf("Index\n");
            ast_print(node->index.object_expr,indent+1);
            ast_print(node->index.index,indent+1);
            break;
        case NODE_INDEX_SET:
            printf("IndexSet\n");
            ast_print(node->index_set.object_expr,indent+1);
            ast_print(node->index_set.index,indent+1);
            ast_print(node->index_set.value,indent+1);
            break;
        case NODE_PRINT:
            printf("Print(%d)\n",node->print.arg_count);
            for(int i=0;i<node->print.arg_count;i++) ast_print(node->print.args[i],indent+1);
            break;
        case NODE_INPUT:
            printf("Input(->%s)\n",node->input.target); break;
        case NODE_IF:
            printf("If\n");
            ast_print(node->if_stmt.condition,indent+1);
            ast_print(node->if_stmt.then_branch,indent+1);
            if(node->if_stmt.else_branch) ast_print(node->if_stmt.else_branch,indent+1);
            break;
        case NODE_WHILE:
            printf("While\n");
            ast_print(node->while_stmt.condition,indent+1);
            ast_print(node->while_stmt.body,indent+1);
            break;
        case NODE_FOR:
            printf("For\n");
            ast_print(node->for_stmt.init,indent+1);
            ast_print(node->for_stmt.condition,indent+1);
            ast_print(node->for_stmt.post,indent+1);
            ast_print(node->for_stmt.body,indent+1);
            break;
        case NODE_SWITCH:
            printf("Switch(%d cases%s)\n",node->switch_stmt.case_count,
                   node->switch_stmt.default_body?" +default":"");
            ast_print(node->switch_stmt.subject,indent+1);
            for(int i=0;i<node->switch_stmt.case_count;i++){
                ind(indent+1);
                printf("case:\n");
                ast_print(node->switch_stmt.cases[i].value,indent+2);
                ast_print(node->switch_stmt.cases[i].body,indent+2);
            }
            if(node->switch_stmt.default_body){
                ind(indent+1); printf("default:\n");
                ast_print(node->switch_stmt.default_body,indent+2);
            }
            break;
        case NODE_BLOCK:
            printf("Block(%d)\n",node->block.count);
            for(int i=0;i<node->block.count;i++) ast_print(node->block.stmts[i],indent+1);
            break;
        case NODE_EXPR_STMT:
            printf("ExprStmt\n"); ast_print(node->expr_stmt.expr,indent+1); break;
        case NODE_RETURN:
            printf("Return\n"); if(node->ret.value) ast_print(node->ret.value,indent+1); break;
        case NODE_FUNC_DECL:
            printf("FuncDecl(%s %s",visibility_name(node->func_decl.visibility),node->func_decl.name);
            for(int i=0;i<node->func_decl.param_count;i++) printf(", %s",node->func_decl.params[i]);
            printf(")\n"); ast_print(node->func_decl.body,indent+1);
            break;
        case NODE_IMPORT: printf("Import(\"%s\")\n",node->import.path); break;
        case NODE_EXPORT: printf("Export(%s)\n",node->export_node.name); break;
        case NODE_PROGRAM:
            printf("Program(%d)\n",node->program.count);
            for(int i=0;i<node->program.count;i++) ast_print(node->program.stmts[i],indent+1);
            break;
        default: printf("<node %d>\n",node->kind);
    }
}

void ast_free(ASTNode *node){
    if(!node) return;
    switch(node->kind){
        case NODE_STRING:        free(node->str.value); break;
        case NODE_ARRAY_LITERAL:
            for(int i=0;i<node->arr_lit.count;i++) ast_free(node->arr_lit.elements[i]);
            free(node->arr_lit.elements); break;
        case NODE_BINARY:    ast_free(node->binary.left); ast_free(node->binary.right); break;
        case NODE_UNARY:     ast_free(node->unary.operand); break;
        case NODE_ASSIGN:    ast_free(node->assign.value); break;
        case NODE_VAR_DECL:  ast_free(node->var_decl.initializer); break;
        case NODE_ARRAY_DECL:ast_free(node->arr_decl.initializer); break;
        case NODE_CALL:
            for(int i=0;i<node->call.arg_count;i++) ast_free(node->call.args[i]);
            free(node->call.args); break;
        case NODE_METHOD_CALL:
            ast_free(node->method_call.object_expr);
            for(int i=0;i<node->method_call.arg_count;i++) ast_free(node->method_call.args[i]);
            free(node->method_call.args); break;
        case NODE_INDEX:
            ast_free(node->index.object_expr);
            ast_free(node->index.index); break;
        case NODE_INDEX_SET:
            ast_free(node->index_set.object_expr);
            ast_free(node->index_set.index);
            ast_free(node->index_set.value); break;
        case NODE_PRINT:
            for(int i=0;i<node->print.arg_count;i++) ast_free(node->print.args[i]);
            free(node->print.args); break;
        case NODE_INPUT:     ast_free(node->input.prompt_expr); break;
        case NODE_IF:
            ast_free(node->if_stmt.condition);
            ast_free(node->if_stmt.then_branch);
            ast_free(node->if_stmt.else_branch); break;
        case NODE_WHILE:
            ast_free(node->while_stmt.condition);
            ast_free(node->while_stmt.body); break;
        case NODE_FOR:
            ast_free(node->for_stmt.init);  ast_free(node->for_stmt.condition);
            ast_free(node->for_stmt.post);  ast_free(node->for_stmt.body); break;
        case NODE_SWITCH:
            ast_free(node->switch_stmt.subject);
            for(int i=0;i<node->switch_stmt.case_count;i++){
                ast_free(node->switch_stmt.cases[i].value);
                ast_free(node->switch_stmt.cases[i].body);
            }
            free(node->switch_stmt.cases);
            ast_free(node->switch_stmt.default_body); break;
        case NODE_BLOCK:
            for(int i=0;i<node->block.count;i++) ast_free(node->block.stmts[i]);
            free(node->block.stmts); break;
        case NODE_EXPR_STMT: ast_free(node->expr_stmt.expr); break;
        case NODE_RETURN:    ast_free(node->ret.value); break;
        case NODE_FUNC_DECL: ast_free(node->func_decl.body); break;
        case NODE_PROGRAM:
            for(int i=0;i<node->program.count;i++) ast_free(node->program.stmts[i]);
            free(node->program.stmts); break;
        default: break;
    }
    free(node);
}
