#pragma once

#include "runtime.h"

/* Parse a JSON string into an AgoVal.
 * Returns a VAL_RESULT: ok(value) on success, err("message") on failure.
 * Strings are allocated from arena, containers from gc. */
AgoVal ago_json_parse(const char *input, int length, AgoArena *arena, AgoGc *gc);

/* Stringify an AgoVal into compact JSON.
 * Returns an arena-allocated string. *out_len receives the byte length. */
const char *ago_json_stringify(AgoVal val, int *out_len, AgoArena *arena);
