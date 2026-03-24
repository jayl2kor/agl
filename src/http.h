#pragma once

#include "runtime.h"

/* Perform an HTTP request. method must be "GET" or "POST".
 * Returns a Result<map, string> where the map has keys:
 *   "status" (int), "body" (string), "headers" (map).
 * On error, returns err("message"). */
AgoVal ago_http_request(const char *method,
                        const char *url, int url_len,
                        AgoMapVal *headers,
                        const char *body, int body_len,
                        AgoArena *arena, AgoGc *gc);
