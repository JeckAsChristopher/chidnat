#ifndef COMPILER_H
#define COMPILER_H

#include "common.h"
#include "func.h"
#include "ast.h"
#include "error.h"

/* ── Symbol tables ─────────────────────────────────────────────────────────── */
typedef struct { char name[MAX_IDENT_LEN]; int index; bool is_let; bool defined; } Symbol;
typedef struct { char name[MAX_IDENT_LEN]; int slot;  bool is_let; bool defined; } Local;

/* ── Loop / switch context for break & continue ────────────────────────────── */
#define MAX_BREAKS    128
#define MAX_CTX_DEPTH  64

typedef enum { CTX_LOOP, CTX_SWITCH } CtxKind;

typedef struct {
    CtxKind kind;
    /* break patches jump to end */
    int break_patches[MAX_BREAKS];
    int break_count;
    /* continue patches jump to loop_start (loops only) */
    int continue_patches[MAX_BREAKS];
    int continue_count;
    /* for loops: where to patch continues (post-expression start) */
    int continue_target;   /* -1 = not yet known, set after compiling body */
} CtxFrame;

/* ── Compiler ──────────────────────────────────────────────────────────────── */
typedef struct Compiler {
    Chunk  *current_chunk;
    Chunk   top_chunk;

    Symbol  globals[MAX_VARIABLES];
    int     global_count;

    Local   locals[MAX_LOCALS];
    int     local_count;
    bool    in_function;
    FunctionObject *current_func;

    /* functions defined here */
    FunctionObject *functions[MAX_FUNCS];
    int             func_count;
    /* imported functions */
    FunctionObject *imports[MAX_FUNCS];
    int             import_count;

    char exports[MAX_FUNCS][MAX_IDENT_LEN];
    int  export_count;
    char source_file[1024];
    bool had_error;

    /* break/continue context stack */
    CtxFrame ctx_stack[MAX_CTX_DEPTH];
    int      ctx_depth;

    /* pluggable import handler (set by main.c) */
    bool (*import_handler)(const char *path, struct Compiler *C);
} Compiler;

extern bool (*chn_import_handler)(const char *path, Compiler *C);

void  compiler_init   (Compiler *C, const char *source_file);
bool  compiler_compile(Compiler *C, ASTNode *ast);
void  chunk_disasm    (Chunk *ch, const char *name);

#endif /* COMPILER_H */
