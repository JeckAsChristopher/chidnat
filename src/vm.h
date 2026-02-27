// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#ifndef VM_H
#define VM_H

#include "common.h"
#include "func.h"
#include "compiler.h"

typedef enum { VM_OK, VM_RUNTIME_ERROR } VMResult;

typedef struct {
    FunctionObject *function;
    uint8_t        *ip;
    int             base_idx;
} CallFrame;

typedef struct VM {
    Chunk      *top_chunk;
    CallFrame   frames[MAX_CALL_DEPTH];
    int         frame_count;
    Value       stack[MAX_STACK];
    int         stack_top;
    Value       globals[MAX_VARIABLES];
} VM;

void     vm_init(VM *vm);
VMResult vm_run (VM *vm, Chunk *top_chunk);
void     vm_free(VM *vm);

#endif 
