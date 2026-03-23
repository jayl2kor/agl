#include "runtime.h"

/* ---- Built-in function dispatch ---- */

bool try_builtin_call(AgoInterp *interp, const char *name, int name_len,
                      AgoNode *call_node, AgoVal *out) {
    int argc = call_node->as.call.arg_count;
    int line = call_node->line;
    int col  = call_node->column;

    /* print(args...) */
    if (ago_str_eq(name, name_len, "print", 5)) {
        for (int i = 0; i < argc; i++) {
            AgoVal arg = eval_expr(interp, call_node->as.call.args[i]);
            if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
            builtin_print(arg);
        }
        *out = val_nil();
        return true;
    }

    /* len(x) */
    if (ago_str_eq(name, name_len, "len", 3)) {
        if (argc != 1) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "len() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AgoVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind == VAL_ARRAY) { *out = val_int(arg.as.array->count); return true; }
        if (arg.kind == VAL_STRING) {
            int slen; str_content(arg, &slen);
            *out = val_int(slen); return true;
        }
        ago_error_set(interp->ctx, AGO_ERR_TYPE,
                      ago_loc(NULL, line, col), "len() requires an array or string");
        *out = val_nil(); return true;
    }

    /* type(val) -> string */
    if (ago_str_eq(name, name_len, "type", 4)) {
        if (argc != 1) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "type() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AgoVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        const char *tname;
        switch (arg.kind) {
        case VAL_INT:    tname = "int"; break;
        case VAL_FLOAT:  tname = "float"; break;
        case VAL_BOOL:   tname = "bool"; break;
        case VAL_STRING: tname = "string"; break;
        case VAL_FN:     tname = "fn"; break;
        case VAL_ARRAY:  tname = "array"; break;
        case VAL_STRUCT: tname = "struct"; break;
        case VAL_RESULT: tname = "result"; break;
        case VAL_NIL:    tname = "nil"; break;
        default:         tname = "unknown"; break;
        }
        *out = val_string(tname, (int)strlen(tname));
        return true;
    }

    /* str(val) -> string */
    if (ago_str_eq(name, name_len, "str", 3)) {
        if (argc != 1) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "str() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AgoVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        char buf[256];
        int n = 0;
        switch (arg.kind) {
        case VAL_INT:    n = snprintf(buf, sizeof(buf), "%lld", (long long)arg.as.integer); break;
        case VAL_FLOAT:  n = snprintf(buf, sizeof(buf), "%g", arg.as.floating); break;
        case VAL_BOOL:   n = snprintf(buf, sizeof(buf), "%s", arg.as.boolean ? "true" : "false"); break;
        case VAL_NIL:    n = snprintf(buf, sizeof(buf), "nil"); break;
        case VAL_FN:     n = snprintf(buf, sizeof(buf), "<fn>"); break;
        case VAL_STRUCT: n = snprintf(buf, sizeof(buf), "<struct %.*s>", arg.as.strct->type_name_length, arg.as.strct->type_name); break;
        case VAL_RESULT: n = snprintf(buf, sizeof(buf), "%s(...)", arg.as.result->is_ok ? "ok" : "err"); break;
        case VAL_ARRAY:  n = snprintf(buf, sizeof(buf), "<array[%d]>", arg.as.array->count); break;
        case VAL_STRING: {
            int slen; const char *sd = str_content(arg, &slen);
            char *copy = ago_arena_alloc(interp->arena, (size_t)slen);
            if (copy) memcpy(copy, sd, (size_t)slen);
            *out = val_string(copy ? copy : "", copy ? slen : 0);
            return true;
        }
        }
        if (n < 0) n = 0;
        if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
        char *s = ago_arena_alloc(interp->arena, (size_t)n);
        if (s) memcpy(s, buf, (size_t)n);
        *out = val_string(s ? s : "", s ? n : 0);
        return true;
    }

    /* int(x) -> int */
    if (ago_str_eq(name, name_len, "int", 3)) {
        if (argc != 1) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "int() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AgoVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind == VAL_STRING) {
            int slen; const char *sd = str_content(arg, &slen);
            char tmp[64];
            if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
            memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
            char *end;
            int64_t v = strtoll(tmp, &end, 10);
            if (end == tmp) {
                ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                              ago_loc(NULL, line, col), "int() invalid integer string");
                *out = val_nil(); return true;
            }
            *out = val_int(v); return true;
        }
        if (arg.kind == VAL_FLOAT) { *out = val_int((int64_t)arg.as.floating); return true; }
        if (arg.kind == VAL_INT) { *out = arg; return true; }
        ago_error_set(interp->ctx, AGO_ERR_TYPE,
                      ago_loc(NULL, line, col), "int() cannot convert this type");
        *out = val_nil(); return true;
    }

    /* float(x) -> float */
    if (ago_str_eq(name, name_len, "float", 5)) {
        if (argc != 1) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "float() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AgoVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind == VAL_STRING) {
            int slen; const char *sd = str_content(arg, &slen);
            char tmp[64];
            if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
            memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
            char *end;
            double v = strtod(tmp, &end);
            if (end == tmp) {
                ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                              ago_loc(NULL, line, col), "float() invalid number string");
                *out = val_nil(); return true;
            }
            *out = val_float(v); return true;
        }
        if (arg.kind == VAL_INT) { *out = val_float((double)arg.as.integer); return true; }
        if (arg.kind == VAL_FLOAT) { *out = arg; return true; }
        ago_error_set(interp->ctx, AGO_ERR_TYPE,
                      ago_loc(NULL, line, col), "float() cannot convert this type");
        *out = val_nil(); return true;
    }

    /* push(arr, val) -> new array */
    if (ago_str_eq(name, name_len, "push", 4)) {
        if (argc != 2) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "push() takes exactly 2 arguments");
            *out = val_nil(); return true;
        }
        AgoVal arr_val = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AgoVal elem = eval_expr(interp, call_node->as.call.args[1]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arr_val.kind != VAL_ARRAY) {
            ago_error_set(interp->ctx, AGO_ERR_TYPE,
                          ago_loc(NULL, line, col), "push() first argument must be an array");
            *out = val_nil(); return true;
        }
        AgoArrayVal *old = arr_val.as.array;
        if (old->count >= MAX_ARRAY_SIZE) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col),
                          "array size limit exceeded (max %d)", MAX_ARRAY_SIZE);
            *out = val_nil(); return true;
        }
        int new_count = old->count + 1;
        AgoArrayVal *arr = ago_gc_alloc(interp->gc, sizeof(AgoArrayVal), array_cleanup);
        if (!arr) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "out of memory");
            *out = val_nil(); return true;
        }
        arr->count = new_count;
        arr->elements = malloc(sizeof(AgoVal) * (size_t)new_count);
        if (!arr->elements) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "out of memory");
            *out = val_nil(); return true;
        }
        memcpy(arr->elements, old->elements, sizeof(AgoVal) * (size_t)old->count);
        arr->elements[old->count] = elem;
        AgoVal v; v.kind = VAL_ARRAY; v.as.array = arr;
        *out = v; return true;
    }

    /* map(arr, fn) -> new array */
    if (ago_str_eq(name, name_len, "map", 3) && argc == 2) {
        AgoVal arr_val = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AgoVal fn_val = eval_expr(interp, call_node->as.call.args[1]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arr_val.kind != VAL_ARRAY || fn_val.kind != VAL_FN) {
            ago_error_set(interp->ctx, AGO_ERR_TYPE,
                          ago_loc(NULL, line, col), "map() requires (array, fn)");
            *out = val_nil(); return true;
        }
        AgoArrayVal *src = arr_val.as.array;
        AgoArrayVal *dst = ago_gc_alloc(interp->gc, sizeof(AgoArrayVal), array_cleanup);
        if (!dst) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "out of memory");
            *out = val_nil(); return true;
        }
        dst->count = src->count;
        dst->elements = src->count > 0 ? malloc(sizeof(AgoVal) * (size_t)src->count) : NULL;
        for (int i = 0; i < src->count; i++) {
            dst->elements[i] = call_fn_direct(interp, fn_val.as.fn,
                                              &src->elements[i], 1, line, col);
            if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        }
        AgoVal v; v.kind = VAL_ARRAY; v.as.array = dst;
        *out = v; return true;
    }

    /* filter(arr, fn) -> new array */
    if (ago_str_eq(name, name_len, "filter", 6) && argc == 2) {
        AgoVal arr_val = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AgoVal fn_val = eval_expr(interp, call_node->as.call.args[1]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arr_val.kind != VAL_ARRAY || fn_val.kind != VAL_FN) {
            ago_error_set(interp->ctx, AGO_ERR_TYPE,
                          ago_loc(NULL, line, col), "filter() requires (array, fn)");
            *out = val_nil(); return true;
        }
        AgoArrayVal *src = arr_val.as.array;
        AgoVal *temp = src->count > 0 ? malloc(sizeof(AgoVal) * (size_t)src->count) : NULL;
        int kept = 0;
        for (int i = 0; i < src->count; i++) {
            AgoVal pred = call_fn_direct(interp, fn_val.as.fn,
                                         &src->elements[i], 1, line, col);
            if (ago_error_occurred(interp->ctx)) { free(temp); *out = val_nil(); return true; }
            if (is_truthy(pred)) temp[kept++] = src->elements[i];
        }
        AgoArrayVal *dst = ago_gc_alloc(interp->gc, sizeof(AgoArrayVal), array_cleanup);
        if (!dst) {
            free(temp);
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "out of memory");
            *out = val_nil(); return true;
        }
        dst->count = kept;
        dst->elements = kept > 0 ? malloc(sizeof(AgoVal) * (size_t)kept) : NULL;
        if (kept > 0) memcpy(dst->elements, temp, sizeof(AgoVal) * (size_t)kept);
        free(temp);
        AgoVal v; v.kind = VAL_ARRAY; v.as.array = dst;
        *out = v; return true;
    }

    /* abs(n) */
    if (ago_str_eq(name, name_len, "abs", 3)) {
        if (argc != 1) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "abs() takes exactly 1 argument");
            *out = val_nil(); return true;
        }
        AgoVal arg = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (arg.kind == VAL_INT) {
            *out = val_int(arg.as.integer < 0 ? -arg.as.integer : arg.as.integer);
            return true;
        }
        if (arg.kind == VAL_FLOAT) {
            *out = val_float(arg.as.floating < 0 ? -arg.as.floating : arg.as.floating);
            return true;
        }
        ago_error_set(interp->ctx, AGO_ERR_TYPE,
                      ago_loc(NULL, line, col), "abs() requires a number");
        *out = val_nil(); return true;
    }

    /* min(a, b) */
    if (ago_str_eq(name, name_len, "min", 3)) {
        if (argc != 2) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "min() takes exactly 2 arguments");
            *out = val_nil(); return true;
        }
        AgoVal a = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AgoVal b = eval_expr(interp, call_node->as.call.args[1]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (a.kind == VAL_INT && b.kind == VAL_INT) {
            *out = a.as.integer <= b.as.integer ? a : b; return true;
        }
        if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) {
            *out = a.as.floating <= b.as.floating ? a : b; return true;
        }
        ago_error_set(interp->ctx, AGO_ERR_TYPE,
                      ago_loc(NULL, line, col),
                      "min() requires two numbers of the same type");
        *out = val_nil(); return true;
    }

    /* max(a, b) */
    if (ago_str_eq(name, name_len, "max", 3)) {
        if (argc != 2) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, col), "max() takes exactly 2 arguments");
            *out = val_nil(); return true;
        }
        AgoVal a = eval_expr(interp, call_node->as.call.args[0]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        AgoVal b = eval_expr(interp, call_node->as.call.args[1]);
        if (ago_error_occurred(interp->ctx)) { *out = val_nil(); return true; }
        if (a.kind == VAL_INT && b.kind == VAL_INT) {
            *out = a.as.integer >= b.as.integer ? a : b; return true;
        }
        if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) {
            *out = a.as.floating >= b.as.floating ? a : b; return true;
        }
        ago_error_set(interp->ctx, AGO_ERR_TYPE,
                      ago_loc(NULL, line, col),
                      "max() requires two numbers of the same type");
        *out = val_nil(); return true;
    }

    return false;  /* not a builtin */
}
