#pragma once

#include "common.h"
#include "ast.h"
#include "error.h"

/* Run an Ago program (AST) and capture stdout output.
 * Returns 0 on success, non-zero on error.
 * If output is non-NULL, stdout is captured into the buffer. */
int ago_interpret(AgoNode *program, const char *filename, AgoCtx *ctx);

/* Convenience: parse and run source code directly.
 * Returns 0 on success. */
int ago_run(const char *source, const char *filename, AgoCtx *ctx);

/* ---- REPL (persistent interpreter state) ---- */

typedef struct AgoRepl AgoRepl;

AgoRepl *ago_repl_new(void);
void ago_repl_free(AgoRepl *repl);

/* Execute one chunk of source in the persistent REPL state.
 * Variables and functions persist across calls.
 * Returns 0 on success, -1 on error. Errors are printed to stderr.
 * The AgoCtx is reset between calls (errors don't persist). */
int ago_repl_exec(AgoRepl *repl, const char *source);
