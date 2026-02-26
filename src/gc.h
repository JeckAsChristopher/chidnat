#ifndef GC_H
#define GC_H

#include "common.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ════════════════════════════════════════════════════════════════════════════
 *  CHN Advanced Garbage Collector
 *
 *  Design: Tri-color incremental mark-and-sweep with two generations.
 *
 *  Tri-color invariant:
 *    WHITE  — not yet visited this cycle (will be swept if still white at end)
 *    GRAY   — discovered, pushed onto gray_stack, children not yet traced
 *    BLACK  — fully traced (all children are at least gray)
 *
 *  Generational layout:
 *    YOUNG  — newly allocated objects (most die here quickly)
 *    OLD    — objects that survived GC_PROMOTE_AGE collections
 *
 *  Write barrier (Dijkstra/snapshot-at-start):
 *    When a pointer is written into an OLD object, the new referent is
 *    shaded gray to keep the tri-color invariant.  This is done via
 *    GC_WRITE_BARRIER(container_obj, new_child_value).
 *
 *  Incremental steps:
 *    OP_GC_SAFEPOINT in the bytecode calls gc_step(vm) which processes
 *    GC_STEP_SIZE objects from the gray stack before returning.  A full
 *    minor GC is triggered when bytes_allocated > next_gc_young; a major
 *    GC when bytes_allocated > next_gc_major.
 *
 *  String interning:
 *    A hash table maps (chars, len) → ObjString*.  gc_string() returns
 *    the canonical copy, so string equality becomes pointer equality.
 * ════════════════════════════════════════════════════════════════════════════ */

/* ── GC object types ─────────────────────────────────────────────────────── */
typedef enum { OBJ_STRING, OBJ_ARRAY } ObjType;

/* ── GC color for tri-color marking ─────────────────────────────────────── */
typedef enum { GC_WHITE = 0, GC_GRAY = 1, GC_BLACK = 2 } GCColor;

/* ── Generation ──────────────────────────────────────────────────────────── */
typedef enum { GEN_YOUNG = 0, GEN_OLD = 1 } GCGen;

#define GC_PROMOTE_AGE  3   /* survive this many minor GCs → promote to old */

/* ── Base object header ──────────────────────────────────────────────────── */
typedef struct Obj {
    ObjType     type;
    GCColor     color;        /* tri-color state                              */
    GCGen       generation;   /* young or old                                 */
    uint8_t     age;          /* minor GC survival count                      */
    bool        pinned;       /* pinned = never collected (interned strings)  */
    struct Obj *next;         /* intrusive linked list (all objects)          */
    struct Obj *gen_next;     /* linked list within generation                */
} Obj;

/* ── GC-managed string (interned) ───────────────────────────────────────── */
typedef struct ObjString {
    Obj  header;
    int  len;
    uint32_t hash;            /* pre-computed FNV-1a hash for intern table    */
    char chars[];
} ObjString;

/* ── GC-managed dynamic array ───────────────────────────────────────────── */
typedef struct ObjArray {
    Obj    header;
    Value *items;
    int    len;
    int    cap;
} ObjArray;

/* ── String intern table entry ───────────────────────────────────────────── */
#define INTERN_CAP  4096   /* must be power of 2 */
typedef struct InternEntry {
    ObjString *str;
    bool       used;
} InternEntry;

/* ── Remembered set (old→young pointers for minor GC) ───────────────────── */
#define REMSET_CAP  1024
typedef struct { Obj *entries[REMSET_CAP]; int count; } RememberedSet;

/* ── Gray worklist ───────────────────────────────────────────────────────── */
#define GRAY_CAP    4096
typedef struct { Obj **stack; int top, cap; } GrayStack;

/* ── GC tuning constants ──────────────────────────────────────────────────── */
#define GC_YOUNG_THRESHOLD   (256 * 1024)   /* 256 KB → minor GC            */
#define GC_MAJOR_THRESHOLD   (4  * 1024 * 1024) /* 4 MB → major GC          */
#define GC_GROWTH_FACTOR     2
#define GC_STEP_SIZE         64             /* gray objects per safepoint     */
#define GC_MAJOR_EVERY       8              /* major GC every N minor GCs     */

/* ── GC state ─────────────────────────────────────────────────────────────── */
typedef struct {
    /* Object lists */
    Obj        *objects;          /* all objects (intrusive list)             */
    Obj        *young_list;       /* young generation list                    */
    Obj        *old_list;         /* old generation list                      */

    /* Gray worklist for incremental marking */
    GrayStack   gray;

    /* String intern table */
    InternEntry intern[INTERN_CAP];

    /* Remembered set: old objects that hold references to young objects */
    RememberedSet remset;

    /* Stats */
    size_t      bytes_allocated;
    size_t      next_gc_young;
    size_t      next_gc_major;
    int         minor_collections;
    int         major_collections;

    /* State flags */
    bool        paused;           /* re-entrancy guard                        */
    bool        marking;          /* incremental mark in progress             */
    bool        major_pending;    /* next collection should be major          */
} GCState;

extern GCState gc;

/* ── Memory management macros ─────────────────────────────────────────────── */
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

/* ── Write barrier (shade new referent gray when writing into old obj) ────── */
#define GC_WRITE_BARRIER(container_obj, new_val) \
    do { if ((container_obj)->generation == GEN_OLD) gc_barrier_slow(new_val); } while(0)

/* ── Public API ──────────────────────────────────────────────────────────── */
void       gc_init      (void);
ObjString *gc_string    (const char *chars, int len);   /* interned           */
ObjString *gc_cstring   (const char *cstr);             /* interned           */
ObjArray  *gc_array     (void);
void       gc_arr_push  (ObjArray *a, Value v); /* append a value to an array */

void       gc_mark_value (Value v);
void       gc_mark_obj   (Obj *obj);
void       gc_barrier_slow(Value v);    /* write-barrier slow path            */

/* Full stop-the-world collection (minor or major) */
void       gc_collect   (void *vm_ptr);
/* Incremental step: process GC_STEP_SIZE gray objects */
void       gc_step      (void *vm_ptr);
void       gc_free_all  (void);

#endif /* GC_H */
