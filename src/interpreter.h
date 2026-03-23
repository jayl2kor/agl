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
