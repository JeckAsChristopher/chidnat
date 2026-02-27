// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#ifndef FUNC_H
#define FUNC_H

#include "common.h"


typedef struct FunctionObject {
    char               name[MAX_IDENT_LEN];
    int                arity;
    char               params[MAX_PARAMS][MAX_IDENT_LEN];
    Chunk              chunk;
    FunctionVisibility visibility;
    bool               exported;
    char               source_file[1024];

    
    struct FunctionObject *nested[MAX_FUNCS];
    int                    nested_count;

    
    struct FunctionObject *parent;
} FunctionObject;


#define MAX_REGISTRY MAX_FUNCS
extern FunctionObject *func_registry[MAX_REGISTRY];
extern int             func_registry_count;


FunctionObject *func_new     (const char *name, FunctionVisibility vis,
                              const char *source_file);
int             func_register(FunctionObject *f);
FunctionObject *func_lookup  (const char *name);
int             func_index   (const char *name);


bool func_can_access(FunctionObject *caller, FunctionObject *callee);

#endif 
