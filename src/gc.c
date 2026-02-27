// This file is licensed under the Apache License, Version 2.0 (the "License").
// You may not use, modify, copy, merge, publish, distribute, sublicense,
// or sell copies of this software without explicit compliance with the License.
// Unauthorized use, reproduction, or distribution of this file or its contents,
// in whole or in part, is strictly prohibited and may result in legal consequences.
// You must retain this notice in all copies or substantial portions of the software.
// For full license terms, see: https://www.apache.org/licenses/LICENSE-2.0
#include "gc.h"


static void gc_arr_grow(ObjArray *a){
    int nc=a->cap<8?8:a->cap*2;
    a->items=(Value*)GC_GROW(a->items,(size_t)a->cap*sizeof(Value),(size_t)nc*sizeof(Value));
    a->cap=nc;
}
void gc_arr_push(ObjArray *a, Value v){
    if(a->len>=a->cap) gc_arr_grow(a);
    a->items[a->len++]=v;
}

#include "vm.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

GCState gc;


void gc_init(void) {
    memset(&gc, 0, sizeof(GCState));
    gc.next_gc_young = GC_YOUNG_THRESHOLD;
    gc.next_gc_major = GC_MAJOR_THRESHOLD;
}


static void gray_push(Obj *obj) {
    if (!obj || obj->color != GC_WHITE) return;
    obj->color = GC_GRAY;
    if (gc.gray.top >= gc.gray.cap) {
        int nc = gc.gray.cap < 64 ? 64 : gc.gray.cap * 2;
        gc.gray.stack = (Obj**)realloc(gc.gray.stack, (size_t)nc * sizeof(Obj*));
        if (!gc.gray.stack) { fprintf(stderr, "GC: gray stack OOM\n"); exit(1); }
        gc.gray.cap = nc;
    }
    gc.gray.stack[gc.gray.top++] = obj;
}


static void trace_obj(Obj *obj) {
    if (!obj || obj->color == GC_BLACK) return;
    obj->color = GC_BLACK;
    if (obj->type == OBJ_ARRAY) {
        ObjArray *arr = (ObjArray*)obj;
        for (int i = 0; i < arr->len; i++) {
            Value v = arr->items[i];
            if (v.type == VAL_STRING) gray_push((Obj*)v.as.string);
            else if (v.type == VAL_ARRAY)  gray_push((Obj*)v.as.array);
        }
    }
    
}


static uint32_t fnv1a(const char *data, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 16777619u;
    }
    return h;
}

static ObjString *intern_find(const char *chars, int len, uint32_t hash) {
    uint32_t idx = hash & (INTERN_CAP - 1);
    for (int i = 0; i < INTERN_CAP; i++) {
        uint32_t slot = (idx + (uint32_t)i) & (INTERN_CAP - 1);
        InternEntry *e = &gc.intern[slot];
        if (!e->used) return NULL;
        if (e->str && e->str->hash == hash && e->str->len == len
            && memcmp(e->str->chars, chars, (size_t)len) == 0)
            return e->str;
    }
    return NULL;
}

static void intern_insert(ObjString *s) {
    uint32_t idx = s->hash & (INTERN_CAP - 1);
    for (int i = 0; i < INTERN_CAP; i++) {
        uint32_t slot = (idx + (uint32_t)i) & (INTERN_CAP - 1);
        InternEntry *e = &gc.intern[slot];
        if (!e->used || e->str == NULL) {
            e->used = true; e->str = s; return;
        }
    }
    
}

static void intern_remove(ObjString *s) {
    uint32_t idx = s->hash & (INTERN_CAP - 1);
    for (int i = 0; i < INTERN_CAP; i++) {
        uint32_t slot = (idx + (uint32_t)i) & (INTERN_CAP - 1);
        InternEntry *e = &gc.intern[slot];
        if (!e->used) return;
        if (e->str == s) { e->str = NULL; return; }
    }
}


static Obj *alloc_obj(ObjType type, size_t size) {
    Obj *obj = (Obj*)GC_ALLOC(size);
    obj->type       = type;
    obj->color      = GC_WHITE;
    obj->generation = GEN_YOUNG;
    obj->age        = 0;
    obj->pinned     = false;
    
    obj->next       = gc.objects;  gc.objects   = obj;
    obj->gen_next   = gc.young_list; gc.young_list = obj;
    return obj;
}

ObjString *gc_string(const char *chars, int len) {
    uint32_t hash = fnv1a(chars ? chars : "", len);
    
    ObjString *existing = intern_find(chars ? chars : "", len, hash);
    if (existing) return existing;

    ObjString *s = (ObjString*)alloc_obj(OBJ_STRING,
                       sizeof(ObjString) + (size_t)(len + 1));
    s->len  = len;
    s->hash = hash;
    if (chars && len > 0) memcpy(s->chars, chars, (size_t)len);
    s->chars[len] = '\0';
    s->header.pinned = true;  
    intern_insert(s);
    return s;
}

ObjString *gc_cstring(const char *cstr) {
    return cstr ? gc_string(cstr, (int)strlen(cstr)) : gc_string("", 0);
}

ObjArray *gc_array(void) {
    ObjArray *a = (ObjArray*)alloc_obj(OBJ_ARRAY, sizeof(ObjArray));
    a->items = NULL; a->len = 0; a->cap = 0;
    return a;
}


void gc_mark_obj(Obj *obj) {
    gray_push(obj);
}

void gc_mark_value(Value v) {
    if      (v.type == VAL_STRING) gray_push((Obj*)v.as.string);
    else if (v.type == VAL_ARRAY)  gray_push((Obj*)v.as.array);
}


void gc_barrier_slow(Value v) {
    if (gc.marking) gc_mark_value(v);
}


static void mark_vm_roots(VM *vm) {
    
    for (int i = 0; i < vm->stack_top; i++)
        gc_mark_value(vm->stack[i]);
    
    for (int i = 0; i < MAX_VARIABLES; i++)
        gc_mark_value(vm->globals[i]);
    
    for (int i = 0; i < vm->frame_count; i++) {
        CallFrame *f = &vm->frames[i];
        Chunk *ch = f->function ? &f->function->chunk : vm->top_chunk;
        if (!ch) continue;
        for (int c = 0; c < ch->const_count; c++)
            gc_mark_value(ch->constants[c]);
    }
    
}


static void free_obj(Obj *obj) {
    size_t sz = 0;
    if (obj->type == OBJ_STRING) {
        ObjString *s = (ObjString*)obj;
        if (s->header.pinned) return;  
        intern_remove(s);
        sz = sizeof(ObjString) + (size_t)s->len + 1;
    } else if (obj->type == OBJ_ARRAY) {
        ObjArray *a = (ObjArray*)obj;
        if (a->items) GC_FREE(a->items, (size_t)a->cap * sizeof(Value));
        sz = sizeof(ObjArray);
    }
    GC_FREE(obj, sz);
}

static void sweep_generation(bool major) {
    Obj **link = &gc.objects;
    while (*link) {
        Obj *obj = *link;
        
        if (!major && obj->generation == GEN_OLD) {
            obj->color = GC_WHITE;  
            link = &obj->next;
            continue;
        }
        if (obj->color == GC_BLACK || obj->pinned) {
            
            if (obj->generation == GEN_YOUNG) {
                obj->age++;
                if (obj->age >= GC_PROMOTE_AGE) {
                    obj->generation = GEN_OLD;
                }
            }
            obj->color = GC_WHITE;
            link = &obj->next;
        } else {
            
            *link = obj->next;
            free_obj(obj);
        }
    }
    
    gc.young_list = NULL;
    gc.old_list   = NULL;
    for (Obj *o = gc.objects; o; o = o->next) {
        if (o->generation == GEN_YOUNG) {
            o->gen_next = gc.young_list; gc.young_list = o;
        } else {
            o->gen_next = gc.old_list;   gc.old_list   = o;
        }
    }
    
    gc.remset.count = 0;
}


void gc_collect(void *vm_ptr) {
    if (gc.paused || !vm_ptr) return;
    gc.paused  = true;
    gc.marking = true;

    bool major = gc.major_pending ||
                 (gc.bytes_allocated > gc.next_gc_major) ||
                 ((gc.minor_collections % GC_MAJOR_EVERY) == 0 &&
                   gc.minor_collections > 0);

    
    mark_vm_roots((VM*)vm_ptr);
    
    if (!major) {
        for (int i = 0; i < gc.remset.count; i++)
            gray_push(gc.remset.entries[i]);
    }
    
    while (gc.gray.top > 0)
        trace_obj(gc.gray.stack[--gc.gray.top]);

    gc.marking = false;

    
    sweep_generation(major);

    
    size_t ba = gc.bytes_allocated;
    gc.next_gc_young = ba < GC_YOUNG_THRESHOLD
                     ? GC_YOUNG_THRESHOLD
                     : ba * GC_GROWTH_FACTOR;
    gc.next_gc_major = ba < (GC_MAJOR_THRESHOLD / 2)
                     ? GC_MAJOR_THRESHOLD
                     : ba * GC_GROWTH_FACTOR * 2;

    if (major) { gc.major_collections++; gc.major_pending = false; }
    else        gc.minor_collections++;

    gc.paused = false;
}


void gc_step(void *vm_ptr) {
    if (gc.paused || !vm_ptr) return;

    
    if (gc.marking && gc.gray.top > 0) {
        int n = GC_STEP_SIZE;
        while (gc.gray.top > 0 && n-- > 0)
            trace_obj(gc.gray.stack[--gc.gray.top]);
        if (gc.gray.top == 0) {
            
            gc.marking = false;
            gc.paused  = true;
            sweep_generation(gc.major_pending);
            size_t ba = gc.bytes_allocated;
            gc.next_gc_young = ba < GC_YOUNG_THRESHOLD
                             ? GC_YOUNG_THRESHOLD
                             : ba * GC_GROWTH_FACTOR;
            gc.next_gc_major = ba * GC_GROWTH_FACTOR * 2;
            gc.minor_collections++;
            gc.major_pending = false;
            gc.paused = false;
        }
        return;
    }

    
    if (gc.bytes_allocated > gc.next_gc_major) {
        gc_collect(vm_ptr);
    } else if (gc.bytes_allocated > gc.next_gc_young) {
        
        gc.marking = true;
        gc.paused  = true;
        mark_vm_roots((VM*)vm_ptr);
        for (int i = 0; i < gc.remset.count; i++)
            gray_push(gc.remset.entries[i]);
        gc.paused = false;
        
        int n = GC_STEP_SIZE;
        while (gc.gray.top > 0 && n-- > 0)
            trace_obj(gc.gray.stack[--gc.gray.top]);
        if (gc.gray.top == 0) {
            gc.marking = false;
            gc.paused  = true;
            sweep_generation(false);
            size_t ba = gc.bytes_allocated;
            gc.next_gc_young = ba < GC_YOUNG_THRESHOLD
                             ? GC_YOUNG_THRESHOLD
                             : ba * GC_GROWTH_FACTOR;
            gc.minor_collections++;
            gc.paused = false;
        }
    }
}


void gc_free_all(void) {
    Obj *obj = gc.objects;
    while (obj) {
        Obj *next = obj->next;
        obj->pinned = false;  
        free_obj(obj);
        obj = next;
    }
    gc.objects    = NULL;
    gc.young_list = NULL;
    gc.old_list   = NULL;
    free(gc.gray.stack);
    gc.gray.stack = NULL;
    gc.gray.top   = gc.gray.cap = 0;
    memset(gc.intern, 0, sizeof(gc.intern));
    gc.remset.count      = 0;
    gc.bytes_allocated   = 0;
    gc.minor_collections = 0;
    gc.major_collections = 0;
}
