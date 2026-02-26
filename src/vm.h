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

#endif /* VM_H */
