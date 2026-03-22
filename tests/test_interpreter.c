#include "test_harness.h"
#include "../src/interpreter.h"
#include <unistd.h>

/* ---- Helper: run Ago source and capture stdout ---- */

#define MAX_OUTPUT 4096
static char captured_output[MAX_OUTPUT];

static int run_and_capture(const char *source) {
    /* Flush any pending stdout before redirecting */
    fflush(stdout);

    /* Redirect stdout to a pipe */
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    int saved_stdout = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    AgoCtx *c = ago_ctx_new();
    int result = ago_run(source, "test.ago", c);

    /* Flush and restore stdout */
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    /* Read captured output */
    ssize_t n = read(pipefd[0], captured_output, MAX_OUTPUT - 1);
    close(pipefd[0]);
    if (n < 0) n = 0;
    captured_output[n] = '\0';

    if (ago_error_occurred(c)) {
        ago_error_print(ago_error_get(c));
        result = -1;
    }
    ago_ctx_free(c);
    return result;
}

/* ================================================================
 *  RED PHASE: These tests define the expected behavior of the
 *  interpreter. They will fail until the interpreter is implemented.
 * ================================================================ */

/* ---- Hello World ---- */

AGO_TEST(test_hello_world) {
    int r = run_and_capture("print(\"hello world\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "hello world\n");
}

/* ---- Print integer ---- */

AGO_TEST(test_print_integer) {
    int r = run_and_capture("print(42)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

/* ---- Print boolean ---- */

AGO_TEST(test_print_bool) {
    int r = run_and_capture("print(true)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

/* ---- Arithmetic ---- */

AGO_TEST(test_arithmetic) {
    int r = run_and_capture("print(2 + 3 * 4)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "14\n");
}

AGO_TEST(test_subtraction) {
    int r = run_and_capture("print(10 - 3)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "7\n");
}

AGO_TEST(test_division) {
    int r = run_and_capture("print(10 / 3)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "3\n");
}

AGO_TEST(test_modulo) {
    int r = run_and_capture("print(10 % 3)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "1\n");
}

AGO_TEST(test_unary_negate) {
    int r = run_and_capture("print(-5)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "-5\n");
}

/* ---- Let bindings ---- */

AGO_TEST(test_let_binding) {
    int r = run_and_capture("let x = 42\nprint(x)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

AGO_TEST(test_let_expression) {
    int r = run_and_capture("let x = 2 + 3\nprint(x)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "5\n");
}

/* ---- Multiple statements ---- */

AGO_TEST(test_multiple_prints) {
    int r = run_and_capture("print(1)\nprint(2)\nprint(3)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "1\n2\n3\n");
}

/* ---- Comparison ---- */

AGO_TEST(test_comparison) {
    int r = run_and_capture("print(3 > 2)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGO_TEST(test_equality) {
    int r = run_and_capture("print(1 == 1)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

/* ---- Main ---- */

int main(void) {
    AgoTestCtx ctx = {0, 0};

    printf("=== Interpreter Tests ===\n");

    AGO_RUN_TEST(&ctx, test_hello_world);
    AGO_RUN_TEST(&ctx, test_print_integer);
    AGO_RUN_TEST(&ctx, test_print_bool);
    AGO_RUN_TEST(&ctx, test_arithmetic);
    AGO_RUN_TEST(&ctx, test_subtraction);
    AGO_RUN_TEST(&ctx, test_division);
    AGO_RUN_TEST(&ctx, test_modulo);
    AGO_RUN_TEST(&ctx, test_unary_negate);
    AGO_RUN_TEST(&ctx, test_let_binding);
    AGO_RUN_TEST(&ctx, test_let_expression);
    AGO_RUN_TEST(&ctx, test_multiple_prints);
    AGO_RUN_TEST(&ctx, test_comparison);
    AGO_RUN_TEST(&ctx, test_equality);

    AGO_SUMMARY(&ctx);
}
