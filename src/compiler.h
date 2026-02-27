// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#ifndef COMPILER_H
#define COMPILER_H

#include "common.h"
#include "func.h"
#include "ast.h"
#include "error.h"


typedef struct { char name[MAX_IDENT_LEN]; int index; bool is_let; bool defined; } Symbol;
typedef struct { char name[MAX_IDENT_LEN]; int slot;  bool is_let; bool defined; } Local;


#define MAX_BREAKS    128
#define MAX_CTX_DEPTH  64

typedef enum { CTX_LOOP, CTX_SWITCH } CtxKind;

typedef struct {
    CtxKind kind;
    
    int break_patches[MAX_BREAKS];
    int break_count;
    
    int continue_patches[MAX_BREAKS];
    int continue_count;
    
    int continue_target;   
} CtxFrame;


typedef struct Compiler {
    Chunk  *current_chunk;
    Chunk   top_chunk;

    Symbol  globals[MAX_VARIABLES];
    int     global_count;

    Local   locals[MAX_LOCALS];
    int     local_count;
    bool    in_function;
    FunctionObject *current_func;

    
    FunctionObject *functions[MAX_FUNCS];
    int             func_count;
    
    FunctionObject *imports[MAX_FUNCS];
    int             import_count;

    char exports[MAX_FUNCS][MAX_IDENT_LEN];
    int  export_count;
    char source_file[1024];
    bool had_error;

    
    CtxFrame ctx_stack[MAX_CTX_DEPTH];
    int      ctx_depth;

    
    bool (*import_handler)(const char *path, struct Compiler *C);
} Compiler;

extern bool (*chn_import_handler)(const char *path, Compiler *C);

void  compiler_init   (Compiler *C, const char *source_file);
bool  compiler_compile(Compiler *C, ASTNode *ast);
void  chunk_disasm    (Chunk *ch, const char *name);

#endif 
