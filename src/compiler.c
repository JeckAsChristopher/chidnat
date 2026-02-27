// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#define _POSIX_C_SOURCE 200809L
#include "compiler.h"
#include "native.h"
#include "lexer.h"
#include "gc.h"

extern bool (*chn_import_handler)(const char *path, Compiler *C);


#define READ_U16_AT(arr,pos) \
    ((uint16_t)(arr)[(pos)] | ((uint16_t)(arr)[(pos)+1]<<8))


static void emit_byte(Compiler *C, uint8_t b, int line){
    Chunk *ch=C->current_chunk;
    if(ch->code_len>=MAX_CODE){ error_compile(line,"program too large"); C->had_error=true; return; }
    ch->line_info[ch->code_len]=line;
    ch->code[ch->code_len++]=b;
}
static void emit_op  (Compiler *C, OpCode op,   int line){ emit_byte(C,(uint8_t)op,line); }
static void emit_op1 (Compiler *C, OpCode op, uint16_t v, int line){
    emit_byte(C,(uint8_t)op,line);
    emit_byte(C,(uint8_t)(v&0xFF),line);
    emit_byte(C,(uint8_t)((v>>8)&0xFF),line);
}
static int current_offset(Compiler *C){ return C->current_chunk->code_len; }

static int add_constant(Compiler *C, Value val, int line){
    Chunk *ch=C->current_chunk;
    if(ch->const_count>=MAX_CONSTANTS){ error_compile(line,"too many constants"); C->had_error=true; return 0; }
    ch->constants[ch->const_count]=val;
    return ch->const_count++;
}
static void emit_const(Compiler *C, Value val, int line){
    emit_op1(C,OP_CONST,(uint16_t)add_constant(C,val,line),line);
}


static int emit_jump(Compiler *C, OpCode op, int line){
    emit_byte(C,(uint8_t)op,line);
    int patch=C->current_chunk->code_len;
    emit_byte(C,0xFF,line); emit_byte(C,0xFF,line);
    return patch;
}
static void patch_jump(Compiler *C, int pos){
    uint16_t t=(uint16_t)C->current_chunk->code_len;
    C->current_chunk->code[pos]  =(uint8_t)(t&0xFF);
    C->current_chunk->code[pos+1]=(uint8_t)((t>>8)&0xFF);
}

static void patch_jump_to(Compiler *C, int patch_pos, int target){
    C->current_chunk->code[patch_pos]  =(uint8_t)((uint16_t)target&0xFF);
    C->current_chunk->code[patch_pos+1]=(uint8_t)(((uint16_t)target>>8)&0xFF);
}
static void emit_loop(Compiler *C, int loop_start, int line){
    emit_op(C, OP_GC_SAFEPOINT, line);  
    emit_byte(C,(uint8_t)OP_JUMP,line);
    uint16_t t=(uint16_t)loop_start;
    emit_byte(C,(uint8_t)(t&0xFF),line); emit_byte(C,(uint8_t)((t>>8)&0xFF),line);
}


static void push_ctx(Compiler *C, CtxKind kind){
    if(C->ctx_depth>=MAX_CTX_DEPTH){ C->had_error=true; return; }
    CtxFrame *f=&C->ctx_stack[C->ctx_depth++];
    f->kind=kind; f->break_count=0; f->continue_count=0; f->continue_target=-1;
}


static void pop_ctx_patch_breaks(Compiler *C){
    if(C->ctx_depth<=0) return;
    CtxFrame *f=&C->ctx_stack[--C->ctx_depth];
    int end=current_offset(C);
    for(int i=0;i<f->break_count;i++) patch_jump_to(C,f->break_patches[i],end);
}


static void emit_break(Compiler *C, int line){
    if(C->ctx_depth<=0){
        error_compile(line,"'break' outside loop or switch"); C->had_error=true; return;
    }
    CtxFrame *f=&C->ctx_stack[C->ctx_depth-1];
    if(f->break_count>=MAX_BREAKS){ error_compile(line,"too many breaks"); C->had_error=true; return; }
    int p=emit_jump(C,OP_JUMP,line);
    f->break_patches[f->break_count++]=p;
}


static void emit_continue(Compiler *C, int line){
    if(C->ctx_depth<=0){
        error_compile(line,"'continue' outside a loop"); C->had_error=true; return;
    }
    
    for(int i=C->ctx_depth-1;i>=0;i--){
        if(C->ctx_stack[i].kind==CTX_LOOP){
            CtxFrame *f=&C->ctx_stack[i];
            if(f->continue_count>=MAX_BREAKS){ error_compile(line,"too many continues"); C->had_error=true; return; }
            int p=emit_jump(C,OP_JUMP,line);
            f->continue_patches[f->continue_count++]=p;
            return;
        }
    }
    error_compile(line,"'continue' not inside a loop (can't continue from switch)"); C->had_error=true;
}


static void patch_continues(Compiler *C, int target){
    if(C->ctx_depth<=0) return;
    CtxFrame *f=&C->ctx_stack[C->ctx_depth-1];
    for(int i=0;i<f->continue_count;i++) patch_jump_to(C,f->continue_patches[i],target);
}


static int global_find(Compiler *C, const char *name){
    for(int i=0;i<C->global_count;i++) if(!strcmp(C->globals[i].name,name)) return i;
    return -1;
}
static int global_define(Compiler *C, const char *name, bool is_let, int line){
    
    int existing = global_find(C, name);
    if (existing >= 0) {
        if (is_let && !C->globals[existing].is_let) {
            
            error_compile(line, "cannot redeclare 'var %s' as 'let' — use a different name", name);
            C->had_error = true;
            return existing;
        }
        if (!is_let && C->globals[existing].is_let) {
            
            error_compile(line, "cannot redeclare 'let %s' as 'var' — '%s' is immutable", name, name);
            C->had_error = true;
            return existing;
        }
        
        C->globals[existing].defined = false;
        return existing;
    }
    if(C->global_count>=MAX_VARIABLES){ error_compile(line,"too many globals"); C->had_error=true; return 0; }
    int idx=C->global_count++;
    strncpy(C->globals[idx].name,name,MAX_IDENT_LEN-1);
    C->globals[idx].index=idx; C->globals[idx].is_let=is_let; C->globals[idx].defined=false;
    strncpy(C->top_chunk.var_names[idx],name,MAX_IDENT_LEN-1);
    C->top_chunk.var_count=C->global_count;
    return idx;
}
static int local_find(Compiler *C, const char *name){
    for(int i=C->local_count-1;i>=0;i--) if(!strcmp(C->locals[i].name,name)) return i;
    return -1;
}
static int local_define(Compiler *C, const char *name, bool is_let, int line){
    if(C->local_count>=MAX_LOCALS){ error_compile(line,"too many locals"); C->had_error=true; return 0; }
    int slot=C->local_count++;
    strncpy(C->locals[slot].name,name,MAX_IDENT_LEN-1);
    C->locals[slot].slot=slot; C->locals[slot].is_let=is_let; C->locals[slot].defined=false;
    return slot;
}


static void pop_scope(Compiler *C, int saved_lc, int line){
    int n = C->local_count - saved_lc;
    for(int i=0;i<n;i++) emit_op(C,OP_POP,line);
    C->local_count = saved_lc;
}


static void suggest_variable(Compiler *C, const char *name, int line){
    const char *cands[MAX_VARIABLES+MAX_LOCALS+4]; int nc=0;
    if(C->in_function) for(int i=0;i<C->local_count;i++) cands[nc++]=C->locals[i].name;
    for(int i=0;i<C->global_count;i++) cands[nc++]=C->globals[i].name;
    int dist; const char *m=best_match(name,(const char**)cands,nc,&dist);
    if(m) error_compile(line,"undefined variable '%s' — did you mean '%s'?",name,m);
    else  error_compile(line,"undefined variable '%s' — use 'var %s = ...' or 'let %s = ...'",name,name,name);
}
static void suggest_function(Compiler *C, const char *name, int line){
    const char *cands[MAX_FUNCS*2+4]; int nc=0;
    for(int i=0;i<C->func_count;i++)  cands[nc++]=C->functions[i]->name;
    for(int i=0;i<C->import_count;i++) cands[nc++]=C->imports[i]->name;
    int dist; const char *m=best_match(name,(const char**)cands,nc,&dist);
    if(m) error_compile(line,"undefined function '%s' — did you mean '%s'?",name,m);
    else  error_compile(line,"undefined function '%s' — declare it with 'public func %s(...) { }'",name,name);
}


typedef struct { FunctionObject *func; } FuncRef;
static FuncRef resolve_function(Compiler *C, const char *name, int line){
    FuncRef ref={NULL};
    
    for(int i=0;i<C->func_count;i++){
        if(!strcmp(C->functions[i]->name,name)){
            FunctionObject *callee=C->functions[i];
            if(callee->visibility==VIS_PROTECTED){
                if(!func_can_access(C->current_func,callee)){
                    FunctionObject *owner=callee->parent;
                    error_compile(line,
                        "protected function '%s' can only be called from within '%s' or its nested functions",
                        name, owner?owner->name:"<top-level>");
                    C->had_error=true; return ref;
                }
            }
            ref.func=callee; return ref;
        }
    }
    
    for(int i=0;i<C->import_count;i++){
        FunctionObject *f=C->imports[i];
        if(strcmp(f->name,name)) continue;
        if(f->visibility==VIS_PRIVATE){
            error_compile(line,"function '%s' is private to '%s' — change 'private' to 'public'",name,f->source_file);
            C->had_error=true; return ref;
        }
        if(f->visibility==VIS_PROTECTED){
            error_compile(line,"protected function '%s' from '%s' is only accessible to its own nested functions",name,f->source_file);
            C->had_error=true; return ref;
        }
        if(!f->exported){
            error_compile(line,"function '%s' from '%s' is not exported — add 'export %s'",name,f->source_file,name);
            C->had_error=true; return ref;
        }
        ref.func=f; return ref;
    }
    suggest_function(C,name,line); C->had_error=true;
    return ref;
}


static void emit_load(Compiler *C, const char *name, int line){
    if(C->in_function){
        int slot=local_find(C,name);
        if(slot>=0){ emit_op1(C,OP_GET_LOCAL,(uint16_t)slot,line); return; }
    }
    int idx=global_find(C,name);
    if(idx<0){ suggest_variable(C,name,line); C->had_error=true; return; }
    emit_op1(C,OP_GET_VAR,(uint16_t)C->globals[idx].index,line);
}


static void compile_expr(Compiler *C, ASTNode *node);
static void compile_stmt(Compiler *C, ASTNode *node);


static void compile_expr(Compiler *C, ASTNode *node){
    if(!node||C->had_error) return;
    int line=node->line;
    switch(node->kind){

        case NODE_NUMBER:
            emit_const(C,NUMBER_VAL(node->num.value),line); break;

        case NODE_STRING:{
            ObjString *s=gc_cstring(node->str.value);
            emit_const(C,STRING_VAL(s),line); break;
        }

        case NODE_BOOL:
            emit_op(C,node->boolean.value?OP_TRUE:OP_FALSE,line); break;

        case NODE_NIL:
            emit_op(C,OP_NIL,line); break;

        case NODE_ARRAY_LITERAL:{
            emit_op(C,OP_ARRAY_NEW,line);
            for(int i=0;i<node->arr_lit.count;i++){
                compile_expr(C,node->arr_lit.elements[i]);
                emit_op(C,OP_ARRAY_PUSH,line);
            }
            break;
        }

        case NODE_IDENT:
            emit_load(C,node->ident.name,line); break;

        case NODE_ASSIGN:{
            if(C->in_function){
                int slot=local_find(C,node->assign.name);
                if(slot>=0){
                    if(C->locals[slot].is_let&&C->locals[slot].defined){
                        error_compile(line,"cannot reassign 'let' variable '%s'",node->assign.name);
                        C->had_error=true; return;
                    }
                    compile_expr(C,node->assign.value);
                    C->locals[slot].defined=true;
                    emit_op1(C,OP_SET_LOCAL,(uint16_t)slot,line); break;
                }
            }
            int idx=global_find(C,node->assign.name);
            if(idx<0){ suggest_variable(C,node->assign.name,line); C->had_error=true; return; }
            if(C->globals[idx].is_let&&C->globals[idx].defined){
                error_compile(line,"cannot reassign 'let' variable '%s'",node->assign.name);
                C->had_error=true; return;
            }
            compile_expr(C,node->assign.value);
            C->globals[idx].defined=true;
            emit_op1(C,OP_SET_VAR,(uint16_t)C->globals[idx].index,line); break;
        }

        case NODE_UNARY:
            compile_expr(C,node->unary.operand);
            switch(node->unary.op){
                case TK_MINUS: emit_op(C,OP_NEG,line); break;
                case TK_BANG:  emit_op(C,OP_NOT,line); break;
                default: error_compile(line,"unknown unary op"); C->had_error=true;
            } break;

        case NODE_BINARY:{
            if(node->binary.op==TK_AND){
                compile_expr(C,node->binary.left);
                int j=emit_jump(C,OP_JUMP_IF_FALSE,line);
                emit_op(C,OP_POP,line);
                compile_expr(C,node->binary.right);
                patch_jump(C,j); break;
            }
            if(node->binary.op==TK_OR){
                compile_expr(C,node->binary.left);
                int jt=emit_jump(C,OP_JUMP_IF_TRUE,line);
                emit_op(C,OP_POP,line);
                compile_expr(C,node->binary.right);
                patch_jump(C,jt); break;
            }
            compile_expr(C,node->binary.left);
            compile_expr(C,node->binary.right);
            switch(node->binary.op){
                case TK_PLUS:    emit_op(C,OP_ADD,line); break;
                case TK_MINUS:   emit_op(C,OP_SUB,line); break;
                case TK_STAR:    emit_op(C,OP_MUL,line); break;
                case TK_SLASH:   emit_op(C,OP_DIV,line); break;
                case TK_PERCENT: emit_op(C,OP_MOD,line); break;
                case TK_EQ:      emit_op(C,OP_EQ, line); break;
                case TK_NEQ:     emit_op(C,OP_NEQ,line); break;
                case TK_LT:      emit_op(C,OP_LT, line); break;
                case TK_GT:      emit_op(C,OP_GT, line); break;
                case TK_LE:      emit_op(C,OP_LE, line); break;
                case TK_GE:      emit_op(C,OP_GE, line); break;
                default: error_compile(line,"unknown binary op"); C->had_error=true;
            } break;
        }

        case NODE_NATIVE_CALL:{
            for(int i=0;i<node->native_call.argc;i++)
                compile_expr(C,node->native_call.args[i]);
            emit_op(C,OP_NATIVE,line);
            uint16_t nid=node->native_call.call_id;
            emit_byte(C,(uint8_t)(nid&0xFF),line);
            emit_byte(C,(uint8_t)(nid>>8),line);
            emit_byte(C,(uint8_t)node->native_call.argc,line);
            break;
        }
        case NODE_CALL:{
            FuncRef ref=resolve_function(C,node->call.name,line);
            if(!ref.func||C->had_error) return;
            if(node->call.arg_count!=ref.func->arity){
                error_compile(line,"'%s' expects %d arg(s) but got %d",
                              node->call.name,ref.func->arity,node->call.arg_count);
                C->had_error=true; return;
            }
            emit_const(C,FUNC_VAL(ref.func),line);
            for(int i=0;i<node->call.arg_count;i++) compile_expr(C,node->call.args[i]);
            emit_op1(C,OP_CALL,(uint16_t)node->call.arg_count,line);
            break;
        }

        case NODE_METHOD_CALL:{
            ArrayMethod mid=method_id(node->method_call.method);
            if(mid==METHOD_UNKNOWN){
                const char *ms[]={"add","insert","cut","remove","rall","length"};
                int dist; const char *m=best_match(node->method_call.method,(const char**)ms,6,&dist);
                if(m) error_compile(line,"unknown method '%s' — did you mean '%s'?",node->method_call.method,m);
                else  error_compile(line,"unknown array method '%s'",node->method_call.method);
                C->had_error=true; return;
            }
            static const int exp_argc[]={1,2,1,1,1,0};
            if(node->method_call.arg_count!=exp_argc[mid]){
                error_compile(line,"method '%s' expects %d arg(s) but got %d",
                              node->method_call.method,exp_argc[mid],node->method_call.arg_count);
                C->had_error=true; return;
            }
            
            if(mid==METHOD_LENGTH){
                compile_expr(C, node->method_call.object_expr);
                emit_op(C, OP_ARRAY_LEN, line);
                break;
            }
            
            compile_expr(C, node->method_call.object_expr);
            for(int i=0;i<node->method_call.arg_count;i++) compile_expr(C,node->method_call.args[i]);
            emit_byte(C,(uint8_t)OP_METHOD_CALL,line);
            emit_byte(C,(uint8_t)mid,line);
            emit_byte(C,(uint8_t)node->method_call.arg_count,line);
            break;
        }

        case NODE_INDEX:{
            
            compile_expr(C, node->index.object_expr);
            compile_expr(C, node->index.index);
            emit_op(C, OP_ARRAY_INDEX, line);
            break;
        }

        case NODE_INDEX_SET:{
            
            compile_expr(C, node->index_set.object_expr);
            compile_expr(C, node->index_set.index);
            compile_expr(C, node->index_set.value);
            emit_op(C, OP_ARRAY_SET, line);
            break;
        }

        default:
            error_compile(line,"unexpected expr node %d",node->kind); C->had_error=true;
    }
}


static void compile_stmt(Compiler *C, ASTNode *node){
    if(!node||C->had_error) return;
    int line=node->line;
    switch(node->kind){

        case NODE_VAR_DECL:{
            if(node->var_decl.initializer) compile_expr(C,node->var_decl.initializer);
            else emit_op(C,OP_NIL,line);
            if(C->in_function){
                int slot=local_define(C,node->var_decl.name,node->var_decl.is_let,line);
                C->locals[slot].defined=true;
            } else {
                int idx=global_define(C,node->var_decl.name,node->var_decl.is_let,line);
                C->globals[idx].defined=true;
                emit_op1(C,OP_DEF_VAR,(uint16_t)idx,line);
            }
            break;
        }

        case NODE_ARRAY_DECL:{
            if(node->arr_decl.initializer) compile_expr(C,node->arr_decl.initializer);
            else emit_op(C,OP_ARRAY_NEW,line);
            if(C->in_function){
                int slot=local_define(C,node->arr_decl.name,false,line);
                C->locals[slot].defined=true;
            } else {
                int idx=global_define(C,node->arr_decl.name,false,line);
                C->globals[idx].defined=true;
                emit_op1(C,OP_DEF_VAR,(uint16_t)idx,line);
            }
            break;
        }

        case NODE_PRINT:{
            for(int i=0;i<node->print.arg_count;i++) compile_expr(C,node->print.args[i]);
            emit_op1(C,OP_PRINT,(uint16_t)node->print.arg_count,line);
            break;
        }

        case NODE_INPUT:{
            int var_idx=-1; bool is_local=false;
            if(C->in_function){
                int slot=local_find(C,node->input.target);
                if(slot>=0){ var_idx=slot; is_local=true; }
            }
            if(!is_local){
                int idx=global_find(C,node->input.target);
                if(idx<0){ suggest_variable(C,node->input.target,line); C->had_error=true; return; }
                var_idx=C->globals[idx].index;
            }
            if(node->input.prompt_expr){ compile_expr(C,node->input.prompt_expr); emit_op(C,OP_PROMPT,line); }
            emit_byte(C,(uint8_t)OP_INPUT,line);
            emit_byte(C,(uint8_t)(var_idx&0xFF),line);
            emit_byte(C,(uint8_t)((var_idx>>8)&0xFF),line);
            emit_byte(C,(uint8_t)(is_local?1:0),line);
            break;
        }

        
        case NODE_IF:{
            compile_expr(C,node->if_stmt.condition);
            int jf=emit_jump(C,OP_JUMP_IF_FALSE,line);
            emit_op(C,OP_POP,line);                          
            {
                int saved_lc = C->in_function ? C->local_count : -1;
                compile_stmt(C,node->if_stmt.then_branch);
                if(saved_lc >= 0) pop_scope(C, saved_lc, line);
            }
            int je=emit_jump(C,OP_JUMP,line);               
            patch_jump(C,jf);                                
            emit_op(C,OP_POP,line);                          
            if(node->if_stmt.else_branch){
                int saved_lc = C->in_function ? C->local_count : -1;
                compile_stmt(C,node->if_stmt.else_branch);
                if(saved_lc >= 0) pop_scope(C, saved_lc, line);
            }
            patch_jump(C,je);                                
            break;
        }

        
        case NODE_WHILE:{
            push_ctx(C,CTX_LOOP);
            int loop_start=current_offset(C);
            compile_expr(C,node->while_stmt.condition);
            int jo=emit_jump(C,OP_JUMP_IF_FALSE,line);
            emit_op(C,OP_POP,line);
            {   
                int saved_lc = C->local_count;
                compile_stmt(C,node->while_stmt.body);
                pop_scope(C, saved_lc, line);
            }
            
            patch_continues(C,loop_start);
            emit_loop(C,loop_start,line);
            patch_jump(C,jo);
            emit_op(C,OP_POP,line);
            pop_ctx_patch_breaks(C);
            break;
        }

        
        case NODE_FOR:{
            push_ctx(C,CTX_LOOP);
            if(node->for_stmt.init) compile_stmt(C,node->for_stmt.init);
            int loop_start=current_offset(C);
            int jo=-1;
            if(node->for_stmt.condition){
                compile_expr(C,node->for_stmt.condition);
                jo=emit_jump(C,OP_JUMP_IF_FALSE,line);
                emit_op(C,OP_POP,line);
            }
            {   
                int saved_lc = C->local_count;
                compile_stmt(C,node->for_stmt.body);
                pop_scope(C, saved_lc, line);
            }
            
            int post_start=current_offset(C);
            patch_continues(C,post_start);
            if(node->for_stmt.post){
                compile_expr(C,node->for_stmt.post);
                emit_op(C,OP_POP,line);
            }
            emit_loop(C,loop_start,line);
            if(jo>=0){ patch_jump(C,jo); emit_op(C,OP_POP,line); }
            pop_ctx_patch_breaks(C);
            break;
        }

        
        case NODE_SWITCH:{
            
            push_ctx(C, CTX_SWITCH);
            compile_expr(C, node->switch_stmt.subject);

            int n_cases = node->switch_stmt.case_count;
            for (int i = 0; i < n_cases && i < 512; i++) {
                SwitchCase *sc = &node->switch_stmt.cases[i];
                emit_op(C, OP_DUP, line);
                compile_expr(C, sc->value);
                emit_op(C, OP_EQ, line);
                int skip = emit_jump(C, OP_JUMP_IF_FALSE, line);
                emit_op(C, OP_POP, line);  
                emit_op(C, OP_POP, line);  
                compile_stmt(C, sc->body);
                emit_break(C, line);       
                patch_jump(C, skip);       
                emit_op(C, OP_POP, line);  
            }

            if (node->switch_stmt.default_body)
                compile_stmt(C, node->switch_stmt.default_body);

            emit_op(C, OP_POP, line);      
            pop_ctx_patch_breaks(C);
            break;
        }

        case NODE_BREAK:
            emit_break(C,line); break;

        case NODE_CONTINUE:
            emit_continue(C,line); break;

        case NODE_BLOCK:
            for(int i=0;i<node->block.count;i++) compile_stmt(C,node->block.stmts[i]);
            break;

        case NODE_EXPR_STMT:
            compile_expr(C,node->expr_stmt.expr);
            emit_op(C,OP_POP,line);
            break;

        case NODE_RETURN:{
            if(!C->in_function){ error_compile(line,"'return' outside a function"); C->had_error=true; return; }
            if(node->ret.value) compile_expr(C,node->ret.value);
            else emit_op(C,OP_NIL,line);
            emit_op(C,OP_RETURN,line);
            break;
        }

        case NODE_FUNC_DECL:{
            
            FunctionObject *f = NULL;
            for(int i=0;i<C->func_count;i++){
                if(!strcmp(C->functions[i]->name,node->func_decl.name)){
                    f = C->functions[i]; break;
                }
            }
            if(!f){
                f=func_new(node->func_decl.name,node->func_decl.visibility,C->source_file);
                f->arity=node->func_decl.param_count;
                for(int i=0;i<f->arity;i++) strncpy(f->params[i],node->func_decl.params[i],MAX_IDENT_LEN-1);
                if(C->func_count<MAX_FUNCS) C->functions[C->func_count++]=f;
            } else {
                
                f->visibility = node->func_decl.visibility;
                strncpy(f->source_file, C->source_file, sizeof(f->source_file)-1);
            }
            f->parent=C->current_func;
            if(C->current_func&&C->current_func->nested_count<MAX_FUNCS)
                C->current_func->nested[C->current_func->nested_count++]=f;
            func_register(f);

            
            Chunk          *saved_chunk=C->current_chunk;
            Local           saved_locs[MAX_LOCALS]; memcpy(saved_locs,C->locals,sizeof(C->locals));
            int             saved_lc=C->local_count;
            bool            saved_if=C->in_function;
            FunctionObject *saved_cf=C->current_func;
            CtxFrame        saved_ctx[MAX_CTX_DEPTH]; memcpy(saved_ctx,C->ctx_stack,sizeof(C->ctx_stack));
            int             saved_cd=C->ctx_depth;

            C->current_chunk=&f->chunk; C->local_count=0;
            C->in_function=true; C->current_func=f; C->ctx_depth=0;

            emit_op(C, OP_GC_SAFEPOINT, line);  

            for(int i=0;i<f->arity;i++){
                int slot=local_define(C,f->params[i],false,line);
                C->locals[slot].defined=true;
            }
            compile_stmt(C,node->func_decl.body);
            emit_op(C,OP_NIL,line); emit_op(C,OP_RETURN,line);

            
            C->current_chunk=saved_chunk; C->local_count=saved_lc;
            C->in_function=saved_if; C->current_func=saved_cf; C->ctx_depth=saved_cd;
            memcpy(C->locals,saved_locs,sizeof(C->locals));
            memcpy(C->ctx_stack,saved_ctx,sizeof(C->ctx_stack));
            break;
        }

        case NODE_IMPORT:{
            bool (*handler)(const char*, Compiler*) =
                C->import_handler ? C->import_handler : chn_import_handler;
            if(handler){
                if(!handler(node->import.path,C)){
                    error_compile(line,"failed to import '%s'",node->import.path); C->had_error=true;
                }
            } else { error_compile(line,"import not supported"); C->had_error=true; }
            break;
        }

        case NODE_EXPORT:{
            
            if (node->export_node.func_def) {
                compile_stmt(C, node->export_node.func_def);
                if (C->had_error) break;
            }
            bool found=false;
            for(int i=0;i<C->func_count;i++){
                if(!strcmp(C->functions[i]->name,node->export_node.name)){
                    if(C->functions[i]->visibility==VIS_PRIVATE){
                        error_compile(line,"cannot export private function '%s'",node->export_node.name);
                        C->had_error=true;
                    } else { C->functions[i]->exported=true; }
                    found=true; break;
                }
            }
            if(!found){
                const char *cands[MAX_FUNCS]; int nc=0;
                for(int i=0;i<C->func_count;i++) cands[nc++]=C->functions[i]->name;
                int dist; const char *m=best_match(node->export_node.name,(const char**)cands,nc,&dist);
                if(m) error_compile(line,"cannot export '%s' — did you mean '%s'?",node->export_node.name,m);
                else  error_compile(line,"cannot export '%s' — no such function",node->export_node.name);
                C->had_error=true;
            }
            break;
        }

        case NODE_NATIVE_CALL:{
            
            for(int i=0;i<node->native_call.argc;i++)
                compile_expr(C,node->native_call.args[i]);
            emit_op(C,OP_NATIVE,line);
            uint16_t nid=node->native_call.call_id;
            emit_byte(C,(uint8_t)(nid&0xFF),line);
            emit_byte(C,(uint8_t)(nid>>8),line);
            emit_byte(C,(uint8_t)node->native_call.argc,line);
            break;
        }
        default:
            error_compile(line,"unexpected statement node %d",node->kind); C->had_error=true;
    }
}


void compiler_init(Compiler *C, const char *source_file){
    memset(C,0,sizeof(Compiler));
    C->current_chunk=&C->top_chunk;
    strncpy(C->source_file,source_file?source_file:"<unknown>",1023);
}


static void prescan_stmt(Compiler *C, ASTNode *node){
    if (!node) return;
    switch(node->kind){
        case NODE_FUNC_DECL:{
            
            const char *fname = node->func_decl.name;
            bool already = false;
            for(int i=0;i<C->func_count;i++) if(!strcmp(C->functions[i]->name,fname)){already=true;break;}
            if(!already && C->func_count<MAX_FUNCS){
                FunctionObject *f = func_new(fname, node->func_decl.visibility, C->source_file);
                f->arity = node->func_decl.param_count;
                for(int i=0;i<f->arity;i++) strncpy(f->params[i],node->func_decl.params[i],MAX_IDENT_LEN-1);
                C->functions[C->func_count++] = f;
            }
            break;
        }
        case NODE_EXPORT:{
            if(node->export_node.func_def) prescan_stmt(C, node->export_node.func_def);
            break;
        }
        case NODE_PROGRAM:{
            for(int i=0;i<node->program.count;i++) prescan_stmt(C, node->program.stmts[i]);
            break;
        }
        default: break;
    }
}

bool compiler_compile(Compiler *C, ASTNode *ast){
    if(!ast) return false;
    
    prescan_stmt(C, ast);
    if(ast->kind==NODE_PROGRAM)
        for(int i=0;i<ast->program.count&&!C->had_error;i++) compile_stmt(C,ast->program.stmts[i]);
    else
        compile_stmt(C,ast);
    emit_op(C,OP_HALT,0);
    return !C->had_error;
}


static void print_val(Value v){
    switch(v.type){
        case VAL_NUMBER:
            if(v.as.number==(long long)v.as.number) printf("%lld",(long long)v.as.number);
            else printf("%.14g",v.as.number); break;
        case VAL_STRING:   printf("\"%s\"",v.as.string->chars); break;
        case VAL_BOOL:     printf("%s",v.as.boolean?"true":"false"); break;
        case VAL_NIL:      printf("nil"); break;
        case VAL_FUNCTION: printf("<func %s>",v.as.function->name); break;
        case VAL_ARRAY:    printf("<array[%d]>",v.as.array->len); break;
    }
}

void chunk_disasm(Chunk *ch, const char *name){
    printf("=== %s ===\n",name);
    printf("%-6s %-4s  %-22s %s\n","OFFSET","LINE","OPCODE","OPERAND");
    printf("%.58s\n","----------------------------------------------------------");
    int ip=0;
    while(ip<ch->code_len){
        int off=ip,ln=ch->line_info[ip];
        OpCode op=(OpCode)ch->code[ip++];
        printf("%06d  %3d   %-22s",off,ln,opcode_name(op));
        switch(op){
            case OP_CONST:{
                uint16_t i=READ_U16_AT(ch->code,ip); ip+=2;
                printf(" [%d] ",i); print_val(ch->constants[i]); break;}
            case OP_GET_VAR: case OP_SET_VAR: case OP_DEF_VAR:{
                uint16_t i=READ_U16_AT(ch->code,ip); ip+=2;
                printf(" [%d] %s",i,ch->var_names[i]); break;}
            case OP_GET_LOCAL: case OP_SET_LOCAL:{
                uint16_t i=READ_U16_AT(ch->code,ip); ip+=2;
                printf(" slot[%d]",i); break;}
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:{
                uint16_t t=READ_U16_AT(ch->code,ip); ip+=2;
                printf(" -> %d",t); break;}
            case OP_CALL: case OP_PRINT:{
                uint16_t n=READ_U16_AT(ch->code,ip); ip+=2;
                printf(" (%d args)",n); break;}
            case OP_METHOD_CALL:{
                uint8_t mid=ch->code[ip++],argc=ch->code[ip++];
                printf(" .%s(%d)",method_name((ArrayMethod)mid),argc); break;}
            case OP_INPUT:{
                uint16_t idx=READ_U16_AT(ch->code,ip); uint8_t il=ch->code[ip+2]; ip+=3;
                printf(" -> %s[%d]",il?"local":"global",idx); break;}
            case OP_NATIVE:{
                uint16_t nid=READ_U16_AT(ch->code,ip); uint8_t nac=ch->code[ip+2]; ip+=3;
                printf(" native#%d (%d args)",nid,nac); break;}
            default: break;
        }
        printf("\n");
    }
    printf("\n");
}
