#pragma once

#include "runtime.h"

/* Execute a command with arguments.
 * Returns a Result<map, string> where the map has keys:
 *   "stdout" (string), "stderr" (string), "status" (int).
 * On error, returns err("message"). */
AgoVal ago_exec(const char *cmd, int cmd_len,
                AgoArrayVal *args,
                AgoArena *arena, AgoGc *gc);
