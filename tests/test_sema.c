#include "test_harness.h"
#include "../src/sema.h"
#include "../src/parser.h"
#include "../src/arena.h"

/* ---- Helper: parse + sema check ---- */

static bool sema_ok(const char *source) {
    AgoCtx *ctx = ago_ctx_new();
    AgoArena *arena = ago_arena_new();
    AgoParser parser;
    ago_parser_init(&parser, source, "test.ago", arena, ctx);
    AgoNode *program = ago_parser_parse(&parser);
    bool result = false;
    if (program && !ago_error_occurred(ctx)) {
        AgoSema *sema = ago_sema_new(ctx, arena);
        result = ago_sema_check(sema, program);
        ago_sema_free(sema);
    }
    if (ago_error_occurred(ctx)) {
        ago_error_print(ago_error_get(ctx));
    }
    ago_arena_free(arena);
    ago_ctx_free(ctx);
    return result;
}

/* ---- Valid programs pass ---- */

AGO_TEST(test_sema_hello_world) {
    AGO_ASSERT(ctx, sema_ok("print(\"hello\")"));
}

AGO_TEST(test_sema_let_binding) {
    AGO_ASSERT(ctx, sema_ok("let x = 42\nprint(x)"));
}

AGO_TEST(test_sema_var_reassign) {
    AGO_ASSERT(ctx, sema_ok("var x = 1\nx = 10\nprint(x)"));
}

AGO_TEST(test_sema_fn_decl_and_call) {
    AGO_ASSERT(ctx, sema_ok(
        "fn add(a: int, b: int) -> int { return a + b }\n"
        "print(add(1, 2))"));
}

AGO_TEST(test_sema_if_else) {
    AGO_ASSERT(ctx, sema_ok(
        "let x = 5\n"
        "if x > 3 {\n"
        "    print(x)\n"
        "} else {\n"
        "    print(0)\n"
        "}"));
}

AGO_TEST(test_sema_while) {
    AGO_ASSERT(ctx, sema_ok(
        "var i = 0\n"
        "while i < 5 {\n"
        "    i = i + 1\n"
        "}"));
}

AGO_TEST(test_sema_for_in) {
    AGO_ASSERT(ctx, sema_ok(
        "let arr = [1, 2, 3]\n"
        "for x in arr {\n"
        "    print(x)\n"
        "}"));
}

AGO_TEST(test_sema_lambda) {
    AGO_ASSERT(ctx, sema_ok(
        "let f = fn(x: int) -> int { return x * 2 }\n"
        "print(f(5))"));
}

AGO_TEST(test_sema_result_match) {
    AGO_ASSERT(ctx, sema_ok(
        "let r = ok(42)\n"
        "match r {\n"
        "    ok(v) -> print(v)\n"
        "    err(e) -> print(e)\n"
        "}"));
}

AGO_TEST(test_sema_struct) {
    AGO_ASSERT(ctx, sema_ok(
        "struct Point { x: int\n y: int }\n"
        "let p = Point { x: 1, y: 2 }\n"
        "print(p.x)"));
}

/* ---- Undefined variable errors ---- */

AGO_TEST(test_sema_err_undefined_var) {
    AGO_ASSERT(ctx, !sema_ok("print(x)"));
}

AGO_TEST(test_sema_err_undefined_in_expr) {
    AGO_ASSERT(ctx, !sema_ok("let y = x + 1"));
}

AGO_TEST(test_sema_err_var_used_before_decl) {
    AGO_ASSERT(ctx, !sema_ok("print(x)\nlet x = 1"));
}

/* ---- Immutability errors ---- */

AGO_TEST(test_sema_err_assign_to_let) {
    AGO_ASSERT(ctx, !sema_ok("let x = 1\nx = 2"));
}

AGO_TEST(test_sema_err_assign_to_fn_param) {
    /* Function params are immutable (like let) */
    AGO_ASSERT(ctx, !sema_ok(
        "fn foo(x: int) {\n"
        "    x = 10\n"
        "}"));
}

/* ---- Assign to undefined ---- */

AGO_TEST(test_sema_err_assign_undefined) {
    AGO_ASSERT(ctx, !sema_ok("x = 10"));
}

/* ---- Function arity errors ---- */

AGO_TEST(test_sema_err_too_few_args) {
    AGO_ASSERT(ctx, !sema_ok(
        "fn add(a: int, b: int) -> int { return a + b }\n"
        "print(add(1))"));
}

AGO_TEST(test_sema_err_too_many_args) {
    AGO_ASSERT(ctx, !sema_ok(
        "fn add(a: int, b: int) -> int { return a + b }\n"
        "print(add(1, 2, 3))"));
}

/* ---- Scope isolation ---- */

AGO_TEST(test_sema_err_block_scope_leak) {
    /* Variable declared inside if block not visible outside */
    AGO_ASSERT(ctx, !sema_ok(
        "if true {\n"
        "    let inner = 1\n"
        "}\n"
        "print(inner)"));
}

AGO_TEST(test_sema_err_for_var_leak) {
    /* for loop variable not visible outside */
    AGO_ASSERT(ctx, !sema_ok(
        "for x in [1, 2, 3] {\n"
        "    print(x)\n"
        "}\n"
        "print(x)"));
}

AGO_TEST(test_sema_err_fn_param_leak) {
    /* Function params not visible outside */
    AGO_ASSERT(ctx, !sema_ok(
        "fn foo(a: int) { print(a) }\n"
        "print(a)"));
}

/* ---- Main ---- */

int main(void) {
    AgoTestCtx ctx = {0, 0};

    printf("=== Semantic Analysis Tests ===\n");

    /* Valid programs */
    AGO_RUN_TEST(&ctx, test_sema_hello_world);
    AGO_RUN_TEST(&ctx, test_sema_let_binding);
    AGO_RUN_TEST(&ctx, test_sema_var_reassign);
    AGO_RUN_TEST(&ctx, test_sema_fn_decl_and_call);
    AGO_RUN_TEST(&ctx, test_sema_if_else);
    AGO_RUN_TEST(&ctx, test_sema_while);
    AGO_RUN_TEST(&ctx, test_sema_for_in);
    AGO_RUN_TEST(&ctx, test_sema_lambda);
    AGO_RUN_TEST(&ctx, test_sema_result_match);
    AGO_RUN_TEST(&ctx, test_sema_struct);

    /* Undefined variable */
    AGO_RUN_TEST(&ctx, test_sema_err_undefined_var);
    AGO_RUN_TEST(&ctx, test_sema_err_undefined_in_expr);
    AGO_RUN_TEST(&ctx, test_sema_err_var_used_before_decl);

    /* Immutability */
    AGO_RUN_TEST(&ctx, test_sema_err_assign_to_let);
    AGO_RUN_TEST(&ctx, test_sema_err_assign_to_fn_param);

    /* Assign to undefined */
    AGO_RUN_TEST(&ctx, test_sema_err_assign_undefined);

    /* Arity */
    AGO_RUN_TEST(&ctx, test_sema_err_too_few_args);
    AGO_RUN_TEST(&ctx, test_sema_err_too_many_args);

    /* Scope isolation */
    AGO_RUN_TEST(&ctx, test_sema_err_block_scope_leak);
    AGO_RUN_TEST(&ctx, test_sema_err_for_var_leak);
    AGO_RUN_TEST(&ctx, test_sema_err_fn_param_leak);

    AGO_SUMMARY(&ctx);
}
