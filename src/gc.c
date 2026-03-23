#include "gc.h"
#include <stdlib.h>
#include <string.h>

#define GC_INITIAL_THRESHOLD (1024 * 1024) /* 1 MB */
#define GC_GROWTH_FACTOR 2

AgoGc *ago_gc_new(void) {
    AgoGc *gc = calloc(1, sizeof(AgoGc));
    if (!gc) return NULL;
    gc->next_gc = GC_INITIAL_THRESHOLD;
    return gc;
}

void ago_gc_free(AgoGc *gc) {
    if (!gc) return;
    AgoObj *obj = gc->objects;
    while (obj) {
        AgoObj *next = obj->next;
        if (obj->cleanup) obj->cleanup(obj);
        free(obj);
        obj = next;
    }
    free(gc);
}

void *ago_gc_alloc(AgoGc *gc, size_t size, void (*cleanup)(void *)) {
    void *mem = calloc(1, size);
    if (!mem) return NULL;
    AgoObj *obj = mem;
    obj->next = gc->objects;
    obj->size = size;
    obj->marked = false;
    obj->cleanup = cleanup;
    gc->objects = obj;
    gc->object_count++;
    gc->bytes_allocated += size;
    return mem;
}

void ago_gc_sweep(AgoGc *gc) {
    AgoObj **p = &gc->objects;
    while (*p) {
        if (!(*p)->marked) {
            AgoObj *unreached = *p;
            *p = unreached->next;
            gc->bytes_allocated -= unreached->size;
            if (unreached->cleanup) unreached->cleanup(unreached);
            free(unreached);
            gc->object_count--;
        } else {
            (*p)->marked = false;
            p = &(*p)->next;
        }
    }
    /* Grow threshold after collection */
    gc->next_gc = gc->bytes_allocated * GC_GROWTH_FACTOR;
    if (gc->next_gc < GC_INITIAL_THRESHOLD) gc->next_gc = GC_INITIAL_THRESHOLD;
}

int ago_gc_object_count(const AgoGc *gc) {
    return gc->object_count;
}
