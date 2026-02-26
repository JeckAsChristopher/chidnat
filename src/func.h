#ifndef FUNC_H
#define FUNC_H

#include "common.h"

/* ─── FunctionObject ─────────────────────────────────────────────────────── */
typedef struct FunctionObject {
    char               name[MAX_IDENT_LEN];
    int                arity;
    char               params[MAX_PARAMS][MAX_IDENT_LEN];
    Chunk              chunk;
    FunctionVisibility visibility;
    bool               exported;
    char               source_file[1024];

    /* Nested (inner) functions declared inside this function.
       Protected functions are accessible by any function in this list. */
    struct FunctionObject *nested[MAX_FUNCS];
    int                    nested_count;

    /* Parent function (NULL for top-level) */
    struct FunctionObject *parent;
} FunctionObject;

/* ─── Global registry ────────────────────────────────────────────────────── */
#define MAX_REGISTRY MAX_FUNCS
extern FunctionObject *func_registry[MAX_REGISTRY];
extern int             func_registry_count;

/* ─── API ────────────────────────────────────────────────────────────────── */
FunctionObject *func_new     (const char *name, FunctionVisibility vis,
                              const char *source_file);
int             func_register(FunctionObject *f);
FunctionObject *func_lookup  (const char *name);
int             func_index   (const char *name);

/* Check whether caller_func can access callee_func */
bool func_can_access(FunctionObject *caller, FunctionObject *callee);

#endif /* FUNC_H */
