// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#include "func.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

FunctionObject *func_registry[MAX_REGISTRY];
int             func_registry_count = 0;

FunctionObject *func_new(const char *name, FunctionVisibility vis,
                         const char *source_file) {
    FunctionObject *f = (FunctionObject*)calloc(1, sizeof(FunctionObject));
    if (!f) { fprintf(stderr, "out of memory\n"); exit(1); }
    strncpy(f->name,        name,        MAX_IDENT_LEN - 1);
    strncpy(f->source_file, source_file, 1023);
    f->visibility     = vis;
    f->exported       = false;
    f->arity          = 0;
    f->nested_count   = 0;
    f->parent         = NULL;
    return f;
}

int func_register(FunctionObject *f) {
    
    for (int i = 0; i < func_registry_count; i++) {
        if (strcmp(func_registry[i]->name, f->name) == 0 &&
            strcmp(func_registry[i]->source_file, f->source_file) == 0) {
            func_registry[i] = f;
            return i;
        }
    }
    if (func_registry_count >= MAX_REGISTRY) {
        fprintf(stderr, "error: too many functions\n"); exit(1);
    }
    int idx = func_registry_count++;
    func_registry[idx] = f;
    return idx;
}

FunctionObject *func_lookup(const char *name) {
    for (int i = 0; i < func_registry_count; i++)
        if (strcmp(func_registry[i]->name, name) == 0)
            return func_registry[i];
    return NULL;
}

int func_index(const char *name) {
    for (int i = 0; i < func_registry_count; i++)
        if (strcmp(func_registry[i]->name, name) == 0)
            return i;
    return -1;
}


bool func_can_access(FunctionObject *caller, FunctionObject *callee) {
    if (!callee) return false;

    switch (callee->visibility) {
        case VIS_PUBLIC:
            return true;

        case VIS_PRIVATE:
            
            return (caller &&
                    strcmp(caller->source_file, callee->source_file) == 0);

        case VIS_PROTECTED: {
            
            FunctionObject *owner = callee->parent;
            if (!owner) {
                
                if (!caller) return true;  
                return strcmp(caller->source_file, callee->source_file) == 0;
            }
            
            if (caller == owner) return true;
            
            for (int i = 0; i < owner->nested_count; i++)
                if (owner->nested[i] == caller) return true;
            return false;
        }
    }
    return false;
}
