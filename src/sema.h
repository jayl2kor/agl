#pragma once

#include "common.h"
#include "ast.h"
#include "error.h"

/* Semantic analysis: name resolution, immutability, arity checks.
 * Run after parsing, before interpretation.
 * Errors are reported via AgoCtx. */

typedef struct AgoSema AgoSema;

/* Create / destroy */
AgoSema *ago_sema_new(AgoCtx *ctx, AgoArena *arena);
void ago_sema_free(AgoSema *sema);

/* Analyze a program AST. Returns true if no errors found. */
bool ago_sema_check(AgoSema *sema, AgoNode *program);
