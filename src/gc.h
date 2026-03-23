#pragma once

#include <stdbool.h>
#include <stddef.h>

/* GC object header — must be first field of every GC-tracked object.
 * The interpreter casts between AgoObj* and the concrete type. */
typedef struct AgoObj {
    struct AgoObj *next;            /* intrusive linked list of all objects */
    size_t size;                    /* allocation size for GC accounting */
    void (*cleanup)(void *obj);     /* free internal buffers before sweep */
    bool marked;
} AgoObj;

/* GC state */
typedef struct {
    AgoObj *objects;        /* head of all-objects linked list */
    int object_count;
    size_t bytes_allocated;
    size_t next_gc;         /* collection threshold in bytes */
} AgoGc;

/* Create / destroy */
AgoGc *ago_gc_new(void);
void ago_gc_free(AgoGc *gc);

/* Allocate a GC-tracked object of `size` bytes (includes AgoObj header).
 * `cleanup` is called before free (NULL if none needed).
 * Returns pointer to the object (first field is AgoObj). */
void *ago_gc_alloc(AgoGc *gc, size_t size, void (*cleanup)(void *));

/* Mark an object as reachable (no-op if NULL or already marked) */
static inline void ago_gc_mark(AgoObj *obj) {
    if (obj && !obj->marked) obj->marked = true;
}

/* Sweep: free all unmarked objects, reset marks on survivors */
void ago_gc_sweep(AgoGc *gc);

/* Number of live tracked objects */
int ago_gc_object_count(const AgoGc *gc);

/* Check if collection should run (bytes_allocated > next_gc) */
static inline bool ago_gc_should_collect(const AgoGc *gc) {
    return gc->bytes_allocated > gc->next_gc;
}
