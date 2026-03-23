#include "test_harness.h"
#include "../src/gc.h"

/* ---- Basic lifecycle ---- */

AGO_TEST(test_gc_new_free) {
    AgoGc *gc = ago_gc_new();
    AGO_ASSERT(ctx, gc != NULL);
    AGO_ASSERT_INT_EQ(ctx, ago_gc_object_count(gc), 0);
    ago_gc_free(gc);
}

AGO_TEST(test_gc_alloc_one) {
    AgoGc *gc = ago_gc_new();
    void *obj = ago_gc_alloc(gc, sizeof(AgoObj) + 64, NULL);
    AGO_ASSERT(ctx, obj != NULL);
    AGO_ASSERT_INT_EQ(ctx, ago_gc_object_count(gc), 1);
    ago_gc_free(gc);
}

AGO_TEST(test_gc_alloc_multiple) {
    AgoGc *gc = ago_gc_new();
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    AGO_ASSERT_INT_EQ(ctx, ago_gc_object_count(gc), 3);
    ago_gc_free(gc);
}

/* ---- Sweep ---- */

AGO_TEST(test_gc_sweep_all_unmarked) {
    AgoGc *gc = ago_gc_new();
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    AGO_ASSERT_INT_EQ(ctx, ago_gc_object_count(gc), 2);
    ago_gc_sweep(gc);
    AGO_ASSERT_INT_EQ(ctx, ago_gc_object_count(gc), 0);
    ago_gc_free(gc);
}

AGO_TEST(test_gc_sweep_keeps_marked) {
    AgoGc *gc = ago_gc_new();
    AgoObj *obj1 = ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    AGO_ASSERT_INT_EQ(ctx, ago_gc_object_count(gc), 2);
    ago_gc_mark(obj1);
    ago_gc_sweep(gc);
    AGO_ASSERT_INT_EQ(ctx, ago_gc_object_count(gc), 1);
    ago_gc_free(gc);
}

AGO_TEST(test_gc_sweep_resets_marks) {
    AgoGc *gc = ago_gc_new();
    AgoObj *obj = ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    ago_gc_mark(obj);
    AGO_ASSERT(ctx, obj->marked);
    ago_gc_sweep(gc);
    AGO_ASSERT(ctx, !obj->marked);
    ago_gc_free(gc);
}

AGO_TEST(test_gc_sweep_partial) {
    AgoGc *gc = ago_gc_new();
    AgoObj *a = ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    AgoObj *c = ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, NULL);
    AGO_ASSERT_INT_EQ(ctx, ago_gc_object_count(gc), 4);
    ago_gc_mark(a);
    ago_gc_mark(c);
    ago_gc_sweep(gc);
    AGO_ASSERT_INT_EQ(ctx, ago_gc_object_count(gc), 2);
    ago_gc_free(gc);
}

/* ---- Cleanup callback ---- */

static int cleanup_count = 0;
static void test_cleanup(void *obj) { (void)obj; cleanup_count++; }

AGO_TEST(test_gc_cleanup_on_sweep) {
    cleanup_count = 0;
    AgoGc *gc = ago_gc_new();
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, test_cleanup);
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, test_cleanup);
    ago_gc_sweep(gc);
    AGO_ASSERT_INT_EQ(ctx, cleanup_count, 2);
    ago_gc_free(gc);
}

AGO_TEST(test_gc_cleanup_on_free) {
    cleanup_count = 0;
    AgoGc *gc = ago_gc_new();
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, test_cleanup);
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, test_cleanup);
    ago_gc_free(gc);
    AGO_ASSERT_INT_EQ(ctx, cleanup_count, 2);
}

AGO_TEST(test_gc_cleanup_skipped_for_marked) {
    cleanup_count = 0;
    AgoGc *gc = ago_gc_new();
    AgoObj *obj = ago_gc_alloc(gc, sizeof(AgoObj) + 32, test_cleanup);
    ago_gc_alloc(gc, sizeof(AgoObj) + 32, test_cleanup);
    ago_gc_mark(obj);
    ago_gc_sweep(gc);
    AGO_ASSERT_INT_EQ(ctx, cleanup_count, 1);
    ago_gc_free(gc);
}

/* ---- Threshold ---- */

AGO_TEST(test_gc_should_collect) {
    AgoGc *gc = ago_gc_new();
    AGO_ASSERT(ctx, !ago_gc_should_collect(gc));
    /* Allocate past threshold */
    while (!ago_gc_should_collect(gc)) {
        ago_gc_alloc(gc, 1024, NULL);
    }
    AGO_ASSERT(ctx, ago_gc_should_collect(gc));
    ago_gc_free(gc);
}

/* ---- Mark null safety ---- */

AGO_TEST(test_gc_mark_null) {
    /* Should not crash */
    ago_gc_mark(NULL);
    AGO_ASSERT(ctx, true);
}

/* ---- Main ---- */

int main(void) {
    AgoTestCtx ctx = {0, 0};

    printf("=== GC Tests ===\n");

    AGO_RUN_TEST(&ctx, test_gc_new_free);
    AGO_RUN_TEST(&ctx, test_gc_alloc_one);
    AGO_RUN_TEST(&ctx, test_gc_alloc_multiple);

    AGO_RUN_TEST(&ctx, test_gc_sweep_all_unmarked);
    AGO_RUN_TEST(&ctx, test_gc_sweep_keeps_marked);
    AGO_RUN_TEST(&ctx, test_gc_sweep_resets_marks);
    AGO_RUN_TEST(&ctx, test_gc_sweep_partial);

    AGO_RUN_TEST(&ctx, test_gc_cleanup_on_sweep);
    AGO_RUN_TEST(&ctx, test_gc_cleanup_on_free);
    AGO_RUN_TEST(&ctx, test_gc_cleanup_skipped_for_marked);

    AGO_RUN_TEST(&ctx, test_gc_should_collect);
    AGO_RUN_TEST(&ctx, test_gc_mark_null);

    AGO_SUMMARY(&ctx);
}
