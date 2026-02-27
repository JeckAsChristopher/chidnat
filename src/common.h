// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>

#define MAX_CONSTANTS    512
#define MAX_VARIABLES    512
#define MAX_STACK       4096
#define MAX_CODE       65536
#define MAX_IDENT_LEN    256
#define MAX_STRING_LEN  4096
#define MAX_FUNCS        512
#define MAX_LOCALS       256
#define MAX_PARAMS        32
#define MAX_CALL_DEPTH   256


typedef struct ObjString    ObjString;
typedef struct ObjArray     ObjArray;
typedef struct FunctionObject FunctionObject;


typedef enum { VIS_PUBLIC, VIS_PRIVATE, VIS_PROTECTED } FunctionVisibility;


typedef enum {
    VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NIL, VAL_FUNCTION, VAL_ARRAY
} ValueType;

typedef struct Value_s {
    ValueType type;
    union {
        double          number;
        ObjString      *string;
        bool            boolean;
        FunctionObject *function;
        ObjArray       *array;
    } as;
} Value;

#define IS_NUMBER(v)   ((v).type==VAL_NUMBER)
#define IS_STRING(v)   ((v).type==VAL_STRING)
#define IS_BOOL(v)     ((v).type==VAL_BOOL)
#define IS_NIL(v)      ((v).type==VAL_NIL)
#define IS_FUNCTION(v) ((v).type==VAL_FUNCTION)
#define IS_ARRAY(v)    ((v).type==VAL_ARRAY)

#define AS_NUMBER(v)   ((v).as.number)
#define AS_STRING(v)   ((v).as.string)
#define AS_BOOL(v)     ((v).as.boolean)
#define AS_FUNCTION(v) ((v).as.function)
#define AS_ARRAY(v)    ((v).as.array)
#define AS_CSTR(v)     ((v).as.string->chars)

#define NUMBER_VAL(n)  ((Value){VAL_NUMBER,  {.number   =(double)(n)}})
#define STRING_VAL(s)  ((Value){VAL_STRING,  {.string   =(ObjString*)(s)}})
#define BOOL_VAL(b)    ((Value){VAL_BOOL,    {.boolean  =(b)}})
#define NIL_VAL        ((Value){VAL_NIL,     {.number   =0}})
#define FUNC_VAL(f)    ((Value){VAL_FUNCTION,{.function =(FunctionObject*)(f)}})
#define ARRAY_VAL(a)   ((Value){VAL_ARRAY,   {.array    =(ObjArray*)(a)}})


typedef struct {
    uint8_t  code[MAX_CODE];
    int      code_len;
    Value    constants[MAX_CONSTANTS];
    int      const_count;
    int      line_info[MAX_CODE];
    char     var_names[MAX_VARIABLES][MAX_IDENT_LEN];
    int      var_count;
} Chunk;


typedef enum {
    OP_CONST,
    OP_NIL, OP_TRUE, OP_FALSE,
    OP_POP, OP_DUP,

    OP_GET_VAR, OP_SET_VAR, OP_DEF_VAR,
    OP_GET_LOCAL, OP_SET_LOCAL,

    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE, OP_NOT,

    OP_JUMP, OP_JUMP_IF_FALSE, OP_JUMP_IF_TRUE,

    OP_CALL, OP_RETURN,

    OP_ARRAY_NEW, OP_ARRAY_PUSH, OP_ARRAY_INDEX, OP_ARRAY_SET, OP_ARRAY_LEN,
    OP_METHOD_CALL,

    OP_GC_SAFEPOINT,   

    OP_PRINT, OP_PROMPT, OP_INPUT,
    OP_NATIVE,  
    OP_HALT
} OpCode;


typedef enum {
    METHOD_ADD=0, METHOD_INSERT, METHOD_CUT,
    METHOD_REMOVE, METHOD_RALL, METHOD_LENGTH, METHOD_UNKNOWN
} ArrayMethod;

static inline ArrayMethod method_id(const char *n){
    if(!strcmp(n,"add"))    return METHOD_ADD;
    if(!strcmp(n,"insert")) return METHOD_INSERT;
    if(!strcmp(n,"cut"))    return METHOD_CUT;
    if(!strcmp(n,"remove")) return METHOD_REMOVE;
    if(!strcmp(n,"rall"))   return METHOD_RALL;
    if(!strcmp(n,"length")) return METHOD_LENGTH;
    return METHOD_UNKNOWN;
}
static inline const char *method_name(ArrayMethod m){
    switch(m){
        case METHOD_ADD:    return "add";
        case METHOD_INSERT: return "insert";
        case METHOD_CUT:    return "cut";
        case METHOD_REMOVE: return "remove";
        case METHOD_RALL:   return "rall";
        case METHOD_LENGTH: return "length";
        default:            return "?";
    }
}
static inline const char *visibility_name(FunctionVisibility v){
    switch(v){
        case VIS_PUBLIC:    return "public";
        case VIS_PRIVATE:   return "private";
        case VIS_PROTECTED: return "protected";
        default:            return "unknown";
    }
}
static inline const char *opcode_name(OpCode op){
    switch(op){
        case OP_CONST:         return "CONST";
        case OP_NIL:           return "NIL";
        case OP_TRUE:          return "TRUE";
        case OP_FALSE:         return "FALSE";
        case OP_POP:           return "POP";
        case OP_DUP:           return "DUP";
        case OP_GET_VAR:       return "GET_VAR";
        case OP_SET_VAR:       return "SET_VAR";
        case OP_DEF_VAR:       return "DEF_VAR";
        case OP_GET_LOCAL:     return "GET_LOCAL";
        case OP_SET_LOCAL:     return "SET_LOCAL";
        case OP_ADD:           return "ADD";
        case OP_SUB:           return "SUB";
        case OP_MUL:           return "MUL";
        case OP_DIV:           return "DIV";
        case OP_MOD:           return "MOD";
        case OP_NEG:           return "NEG";
        case OP_EQ:            return "EQ";
        case OP_NEQ:           return "NEQ";
        case OP_LT:            return "LT";
        case OP_GT:            return "GT";
        case OP_LE:            return "LE";
        case OP_GE:            return "GE";
        case OP_NOT:           return "NOT";
        case OP_JUMP:          return "JUMP";
        case OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
        case OP_JUMP_IF_TRUE:  return "JUMP_IF_TRUE";
        case OP_CALL:          return "CALL";
        case OP_RETURN:        return "RETURN";
        case OP_ARRAY_NEW:     return "ARRAY_NEW";
        case OP_ARRAY_PUSH:    return "ARRAY_PUSH";
        case OP_ARRAY_INDEX:   return "ARRAY_INDEX";
        case OP_ARRAY_SET:     return "ARRAY_SET";
        case OP_ARRAY_LEN:     return "ARRAY_LEN";
        case OP_METHOD_CALL:   return "METHOD_CALL";
        case OP_GC_SAFEPOINT:  return "GC_SAFEPOINT";
        case OP_PRINT:         return "PRINT";
        case OP_PROMPT:        return "PROMPT";
        case OP_INPUT:         return "INPUT";
        case OP_NATIVE:        return "NATIVE";
        case OP_HALT:          return "HALT";
        default:               return "UNKNOWN";
    }
}

#endif 
