// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#ifndef GC_H
#define GC_H

#include "common.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>




typedef enum { OBJ_STRING, OBJ_ARRAY } ObjType;


typedef enum { GC_WHITE = 0, GC_GRAY = 1, GC_BLACK = 2 } GCColor;


typedef enum { GEN_YOUNG = 0, GEN_OLD = 1 } GCGen;

#define GC_PROMOTE_AGE  3   


typedef struct Obj {
    ObjType     type;
    GCColor     color;        
    GCGen       generation;   
    uint8_t     age;          
    bool        pinned;       
    struct Obj *next;         
    struct Obj *gen_next;     
} Obj;


typedef struct ObjString {
    Obj  header;
    int  len;
    uint32_t hash;            
    char chars[];
} ObjString;


typedef struct ObjArray {
    Obj    header;
    Value *items;
    int    len;
    int    cap;
} ObjArray;


#define INTERN_CAP  4096   
typedef struct InternEntry {
    ObjString *str;
    bool       used;
} InternEntry;


#define REMSET_CAP  1024
typedef struct { Obj *entries[REMSET_CAP]; int count; } RememberedSet;


#define GRAY_CAP    4096
typedef struct { Obj **stack; int top, cap; } GrayStack;


#define GC_YOUNG_THRESHOLD   (256 * 1024)   
#define GC_MAJOR_THRESHOLD   (4  * 1024 * 1024) 
#define GC_GROWTH_FACTOR     2
#define GC_STEP_SIZE         64             
#define GC_MAJOR_EVERY       8              


typedef struct {
    
    Obj        *objects;          
    Obj        *young_list;       
    Obj        *old_list;         

    
    GrayStack   gray;

    
    InternEntry intern[INTERN_CAP];

    
    RememberedSet remset;

    
    size_t      bytes_allocated;
    size_t      next_gc_young;
    size_t      next_gc_major;
    int         minor_collections;
    int         major_collections;

    
    bool        paused;           
    bool        marking;          
    bool        major_pending;    
} GCState;

extern GCState gc;


static inline void *gc_realloc(void *ptr, size_t old_sz, size_t new_sz) {
    if (new_sz == 0) {
        gc.bytes_allocated = gc.bytes_allocated > old_sz
                           ? gc.bytes_allocated - old_sz : 0;
        free(ptr);
        return NULL;
    }
    if (old_sz <= gc.bytes_allocated)
        gc.bytes_allocated += new_sz - old_sz;
    else
        gc.bytes_allocated = new_sz;
    void *result = realloc(ptr, new_sz);
    if (!result) { fprintf(stderr, "GC: out of memory\n"); exit(1); }
    return result;
}

#define GC_ALLOC(sz)      gc_realloc(NULL, 0, (sz))
#define GC_FREE(p, sz)    gc_realloc((p), (sz), 0)
#define GC_GROW(p,os,ns)  gc_realloc((p),(os),(ns))


#define GC_WRITE_BARRIER(container_obj, new_val) \
    do { if ((container_obj)->generation == GEN_OLD) gc_barrier_slow(new_val); } while(0)


void       gc_init      (void);
ObjString *gc_string    (const char *chars, int len);   
ObjString *gc_cstring   (const char *cstr);             
ObjArray  *gc_array     (void);
void       gc_arr_push  (ObjArray *a, Value v); 

void       gc_mark_value (Value v);
void       gc_mark_obj   (Obj *obj);
void       gc_barrier_slow(Value v);    


void       gc_collect   (void *vm_ptr);

void       gc_step      (void *vm_ptr);
void       gc_free_all  (void);

#endif 
