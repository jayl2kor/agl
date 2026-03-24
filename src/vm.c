#include "runtime.h"
#include "chunk.h"
#include "vm.h"
#include "compiler.h"
#include "parser.h"
#include "sema.h"
#include "json.h"

/* ---- VM state ---- */

#define VM_STACK_MAX 1024
#define VM_FRAMES_MAX 512

typedef struct {
    AgoFnVal *fn;
    uint8_t *ip;
    int stack_base;
    int env_saved_count;
    bool is_closure;
    AgoEnv *saved_env;      /* arena-allocated, for closures */
} VmFrame;

typedef struct {
    AgoVal stack[VM_STACK_MAX];
    int stack_top;

    VmFrame frames[VM_FRAMES_MAX];
    int frame_count;

    AgoEnv env;
    AgoCtx *ctx;
    AgoArena *arena;
    AgoGc *gc;
    const char *file;

    int current_line;

    AgoModule modules[MAX_MODULES];
    int module_count;
} Vm;

/* ---- Stack operations ---- */

static void vm_push(Vm *vm, AgoVal val) {
    if (vm->stack_top >= VM_STACK_MAX) {
        ago_error_set(vm->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, vm->current_line, 0), "stack overflow");
        return;
    }
    vm->stack[vm->stack_top++] = val;
}

static AgoVal vm_pop(Vm *vm) {
    return vm->stack[--vm->stack_top];
}

static AgoVal vm_peek(Vm *vm, int distance) {
    return vm->stack[vm->stack_top - 1 - distance];
}

/* ---- Helpers ---- */

static inline uint16_t read_u16(uint8_t *ip) {
    return (uint16_t)(ip[0] | (ip[1] << 8));
}

/* ---- GC root marking for VM ---- */

static void vm_gc_collect(Vm *vm) {
    /* Mark value stack */
    for (int i = 0; i < vm->stack_top; i++) {
        mark_val(vm->stack[i]);
    }
    /* Mark environment */
    for (int i = 0; i < vm->env.count; i++) {
        mark_val(vm->env.values[i]);
    }
    /* Mark call frame functions and saved envs */
    for (int i = 0; i < vm->frame_count; i++) {
        if (vm->frames[i].fn) {
            ago_gc_mark(&vm->frames[i].fn->obj);
        }
        if (vm->frames[i].saved_env) {
            for (int j = 0; j < vm->frames[i].saved_env->count; j++) {
                mark_val(vm->frames[i].saved_env->values[j]);
            }
        }
    }
    ago_gc_sweep(vm->gc);
}

/* Forward declarations */
static AgoVal call_fn_direct_vm(Vm *vm, AgoFnVal *fn, AgoVal *args, int argc,
                                int line, int col);
static int vm_execute(Vm *vm, AgoChunk *chunk);

/* ---- Trace capture for VM errors ---- */

static void vm_capture_trace(void *data, AgoError *err) {
    Vm *vm = data;
    int n = vm->frame_count;
    if (n > AGO_MAX_TRACE) n = AGO_MAX_TRACE;
    err->trace_count = n;
    for (int i = 0; i < n; i++) {
        int src_idx = vm->frame_count - 1 - i;
        VmFrame *f = &vm->frames[src_idx];
        AgoTraceFrame *t = &err->trace[i];
        t->line = vm->current_line;
        t->column = 0;
        t->name[0] = '\0';
        if (f->fn && f->fn->decl) {
            const char *fname = f->fn->decl->as.fn_decl.name;
            int flen = f->fn->decl->as.fn_decl.name_length;
            if (fname && flen > 0) {
                if (flen >= (int)sizeof(t->name)) flen = (int)sizeof(t->name) - 1;
                memcpy(t->name, fname, (size_t)flen);
                t->name[flen] = '\0';
            }
        }
    }
}

/* ---- Builtin dispatch (pre-evaluated args) ---- */

static AgoVal vm_call_builtin(Vm *vm, int builtin_id, AgoVal *args, int argc) {
    int line = vm->current_line;
    int col = 0;

    switch (builtin_id) {
    case 0: /* print */
        for (int i = 0; i < argc; i++) {
            builtin_print(args[i]);
        }
        return val_nil();

    case 1: /* len */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "len() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind == VAL_ARRAY) return val_int(args[0].as.array->count);
        if (args[0].kind == VAL_STRING) { int slen; str_content(args[0], &slen); return val_int(slen); }
        if (args[0].kind == VAL_MAP) return val_int(args[0].as.map->count);
        ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "len() requires an array, string, or map");
        return val_nil();

    case 2: /* type */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "type() takes exactly 1 argument"); return val_nil(); }
        { const char *tname;
          switch (args[0].kind) {
          case VAL_INT: tname = "int"; break; case VAL_FLOAT: tname = "float"; break;
          case VAL_BOOL: tname = "bool"; break; case VAL_STRING: tname = "string"; break;
          case VAL_FN: tname = "fn"; break; case VAL_ARRAY: tname = "array"; break;
          case VAL_STRUCT: tname = "struct"; break; case VAL_RESULT: tname = "result"; break;
          case VAL_MAP: tname = "map"; break;
          case VAL_NIL: tname = "nil"; break; default: tname = "unknown"; break;
          }
          return val_string(tname, (int)strlen(tname));
        }

    case 3: /* str */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "str() takes exactly 1 argument"); return val_nil(); }
        { AgoVal arg = args[0];
          char buf[256]; int n = 0;
          switch (arg.kind) {
          case VAL_INT: n = snprintf(buf, sizeof(buf), "%lld", (long long)arg.as.integer); break;
          case VAL_FLOAT: n = snprintf(buf, sizeof(buf), "%g", arg.as.floating); break;
          case VAL_BOOL: n = snprintf(buf, sizeof(buf), "%s", arg.as.boolean ? "true" : "false"); break;
          case VAL_NIL: n = snprintf(buf, sizeof(buf), "nil"); break;
          case VAL_FN: n = snprintf(buf, sizeof(buf), "<fn>"); break;
          case VAL_STRUCT: n = snprintf(buf, sizeof(buf), "<struct %.*s>", arg.as.strct->type_name_length, arg.as.strct->type_name); break;
          case VAL_RESULT: n = snprintf(buf, sizeof(buf), "%s(...)", arg.as.result->is_ok ? "ok" : "err"); break;
          case VAL_ARRAY: n = snprintf(buf, sizeof(buf), "<array[%d]>", arg.as.array->count); break;
          case VAL_MAP: n = snprintf(buf, sizeof(buf), "<map[%d]>", arg.as.map->count); break;
          case VAL_STRING: { int slen; const char *sd = str_content(arg, &slen);
              char *copy = ago_arena_alloc(vm->arena, (size_t)slen);
              if (copy) memcpy(copy, sd, (size_t)slen);
              return val_string(copy ? copy : "", copy ? slen : 0); }
          }
          if (n < 0) n = 0; if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
          char *s = ago_arena_alloc(vm->arena, (size_t)n);
          if (s) memcpy(s, buf, (size_t)n);
          return val_string(s ? s : "", s ? n : 0);
        }

    case 4: /* int */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "int() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind == VAL_STRING) {
            int slen; const char *sd = str_content(args[0], &slen);
            char tmp[64]; if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
            memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
            char *end; int64_t v = strtoll(tmp, &end, 10);
            if (end == tmp) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "int() invalid integer string"); return val_nil(); }
            return val_int(v);
        }
        if (args[0].kind == VAL_FLOAT) return val_int((int64_t)args[0].as.floating);
        if (args[0].kind == VAL_INT) return args[0];
        ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "int() cannot convert this type");
        return val_nil();

    case 5: /* float */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "float() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind == VAL_STRING) {
            int slen; const char *sd = str_content(args[0], &slen);
            char tmp[64]; if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
            memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
            char *end; double v = strtod(tmp, &end);
            if (end == tmp) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "float() invalid number string"); return val_nil(); }
            return val_float(v);
        }
        if (args[0].kind == VAL_INT) return val_float((double)args[0].as.integer);
        if (args[0].kind == VAL_FLOAT) return args[0];
        ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "float() cannot convert this type");
        return val_nil();

    case 6: /* push */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "push() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_ARRAY) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "push() first argument must be an array"); return val_nil(); }
        { AgoArrayVal *old = args[0].as.array;
          if (old->count >= MAX_ARRAY_SIZE) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "array size limit exceeded (max %d)", MAX_ARRAY_SIZE); return val_nil(); }
          int nc = old->count + 1;
          AgoArrayVal *arr = ago_gc_alloc(vm->gc, sizeof(AgoArrayVal), array_cleanup);
          if (!arr) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          arr->count = nc; arr->elements = malloc(sizeof(AgoVal) * (size_t)nc);
          if (!arr->elements) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          memcpy(arr->elements, old->elements, sizeof(AgoVal) * (size_t)old->count);
          arr->elements[old->count] = args[1];
          AgoVal v; v.kind = VAL_ARRAY; v.as.array = arr; return v;
        }

    case 7: /* map */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map() requires (array, fn)"); return val_nil(); }
        if (args[0].kind != VAL_ARRAY || args[1].kind != VAL_FN) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map() requires (array, fn)"); return val_nil(); }
        { AgoArrayVal *src = args[0].as.array;
          AgoArrayVal *dst = ago_gc_alloc(vm->gc, sizeof(AgoArrayVal), array_cleanup);
          if (!dst) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          dst->count = src->count;
          dst->elements = src->count > 0 ? malloc(sizeof(AgoVal) * (size_t)src->count) : NULL;
          for (int i = 0; i < src->count; i++) {
              dst->elements[i] = call_fn_direct_vm(vm, args[1].as.fn, &src->elements[i], 1, line, col);
              if (ago_error_occurred(vm->ctx)) return val_nil();
          }
          AgoVal v; v.kind = VAL_ARRAY; v.as.array = dst; return v;
        }

    case 8: /* filter */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "filter() requires (array, fn)"); return val_nil(); }
        if (args[0].kind != VAL_ARRAY || args[1].kind != VAL_FN) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "filter() requires (array, fn)"); return val_nil(); }
        { AgoArrayVal *src = args[0].as.array;
          AgoVal *temp = src->count > 0 ? malloc(sizeof(AgoVal) * (size_t)src->count) : NULL;
          int kept = 0;
          for (int i = 0; i < src->count; i++) {
              AgoVal pred = call_fn_direct_vm(vm, args[1].as.fn, &src->elements[i], 1, line, col);
              if (ago_error_occurred(vm->ctx)) { free(temp); return val_nil(); }
              if (is_truthy(pred)) temp[kept++] = src->elements[i];
          }
          AgoArrayVal *dst = ago_gc_alloc(vm->gc, sizeof(AgoArrayVal), array_cleanup);
          if (!dst) { free(temp); ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          dst->count = kept;
          dst->elements = kept > 0 ? malloc(sizeof(AgoVal) * (size_t)kept) : NULL;
          if (kept > 0) memcpy(dst->elements, temp, sizeof(AgoVal) * (size_t)kept);
          free(temp);
          AgoVal v; v.kind = VAL_ARRAY; v.as.array = dst; return v;
        }

    case 9: /* abs */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "abs() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind == VAL_INT) return val_int(args[0].as.integer < 0 ? -args[0].as.integer : args[0].as.integer);
        if (args[0].kind == VAL_FLOAT) return val_float(args[0].as.floating < 0 ? -args[0].as.floating : args[0].as.floating);
        ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "abs() requires a number"); return val_nil();

    case 10: /* min */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "min() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind == VAL_INT && args[1].kind == VAL_INT) return args[0].as.integer <= args[1].as.integer ? args[0] : args[1];
        if (args[0].kind == VAL_FLOAT && args[1].kind == VAL_FLOAT) return args[0].as.floating <= args[1].as.floating ? args[0] : args[1];
        ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "min() requires two numbers of the same type"); return val_nil();

    case 11: /* max */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "max() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind == VAL_INT && args[1].kind == VAL_INT) return args[0].as.integer >= args[1].as.integer ? args[0] : args[1];
        if (args[0].kind == VAL_FLOAT && args[1].kind == VAL_FLOAT) return args[0].as.floating >= args[1].as.floating ? args[0] : args[1];
        ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "max() requires two numbers of the same type"); return val_nil();

    case 12: /* read_file */
        if (argc != 1 || args[0].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "read_file() requires a string path"); return val_nil(); }
        { int slen; const char *sd = str_content(args[0], &slen);
          char tmp[512]; if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
          memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
          FILE *f = fopen(tmp, "rb");
          AgoResultVal *rv = ago_gc_alloc(vm->gc, sizeof(AgoResultVal), NULL);
          if (!rv) { if (f) fclose(f); ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          if (!f) { rv->is_ok = false; rv->value = val_string("cannot read file", 16); }
          else {
              fseek(f, 0, SEEK_END); long flen = ftell(f); fseek(f, 0, SEEK_SET);
              if (flen < 0 || flen > 10*1024*1024) { fclose(f); rv->is_ok = false; rv->value = val_string("file too large", 14); }
              else {
                  char *buf = ago_arena_alloc(vm->arena, (size_t)flen);
                  if (!buf) { fclose(f); ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
                  size_t nr = fread(buf, 1, (size_t)flen, f); fclose(f);
                  rv->is_ok = true; rv->value = val_string(buf, (int)nr);
              }
          }
          AgoVal v; v.kind = VAL_RESULT; v.as.result = rv; return v;
        }

    case 13: /* write_file */
        if (argc != 2 || args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "write_file() requires (string, string)"); return val_nil(); }
        { int plen; const char *pd = str_content(args[0], &plen);
          int clen; const char *cd = str_content(args[1], &clen);
          char tmp[512]; if (plen >= (int)sizeof(tmp)) plen = (int)sizeof(tmp) - 1;
          memcpy(tmp, pd, (size_t)plen); tmp[plen] = '\0';
          FILE *f = fopen(tmp, "wb");
          AgoResultVal *rv = ago_gc_alloc(vm->gc, sizeof(AgoResultVal), NULL);
          if (!rv) { if (f) fclose(f); ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          if (!f) { rv->is_ok = false; rv->value = val_string("cannot write file", 17); }
          else { fwrite(cd, 1, (size_t)clen, f); fclose(f); rv->is_ok = true; rv->value = val_bool(true); }
          AgoVal v; v.kind = VAL_RESULT; v.as.result = rv; return v;
        }

    case 14: /* file_exists */
        if (argc != 1 || args[0].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "file_exists() requires a string path"); return val_nil(); }
        { int slen; const char *sd = str_content(args[0], &slen);
          char tmp[512]; if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
          memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
          FILE *f = fopen(tmp, "r");
          if (f) { fclose(f); return val_bool(true); }
          return val_bool(false);
        }

    case 15: /* map_get */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "map_get() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_MAP) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map_get() first argument must be a map"); return val_nil(); }
        if (args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map_get() key must be a string"); return val_nil(); }
        { AgoMapVal *m = args[0].as.map;
          int klen; const char *kdata = str_content(args[1], &klen);
          for (int i = 0; i < m->count; i++) {
              if (m->key_lengths[i] == klen && memcmp(m->keys[i], kdata, (size_t)klen) == 0) return m->values[i];
          }
          return val_nil();
        }

    case 16: /* map_set */
        if (argc != 3) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "map_set() takes exactly 3 arguments"); return val_nil(); }
        if (args[0].kind != VAL_MAP) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map_set() first argument must be a map"); return val_nil(); }
        if (args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map_set() key must be a string"); return val_nil(); }
        { AgoMapVal *old = args[0].as.map;
          int klen; const char *kdata = str_content(args[1], &klen);
          /* Check if key exists */
          int existing = -1;
          for (int i = 0; i < old->count; i++) {
              if (old->key_lengths[i] == klen && memcmp(old->keys[i], kdata, (size_t)klen) == 0) { existing = i; break; }
          }
          int new_count = existing >= 0 ? old->count : old->count + 1;
          if (new_count > MAX_MAP_SIZE) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "map size limit exceeded (max %d)", MAX_MAP_SIZE); return val_nil(); }
          AgoMapVal *nm = ago_gc_alloc(vm->gc, sizeof(AgoMapVal), map_cleanup);
          if (!nm) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          nm->count = new_count; nm->capacity = new_count;
          nm->keys = malloc(sizeof(char *) * (size_t)new_count);
          nm->key_lengths = malloc(sizeof(int) * (size_t)new_count);
          nm->values = malloc(sizeof(AgoVal) * (size_t)new_count);
          if (!nm->keys || !nm->key_lengths || !nm->values) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          /* Copy old entries */
          for (int i = 0; i < old->count; i++) {
              nm->keys[i] = old->keys[i];
              nm->key_lengths[i] = old->key_lengths[i];
              nm->values[i] = old->values[i];
          }
          if (existing >= 0) {
              nm->values[existing] = args[2];
          } else {
              nm->keys[old->count] = kdata;
              nm->key_lengths[old->count] = klen;
              nm->values[old->count] = args[2];
          }
          AgoVal v; v.kind = VAL_MAP; v.as.map = nm; return v;
        }

    case 17: /* map_keys */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "map_keys() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind != VAL_MAP) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map_keys() requires a map"); return val_nil(); }
        { AgoMapVal *m = args[0].as.map;
          AgoArrayVal *arr = ago_gc_alloc(vm->gc, sizeof(AgoArrayVal), array_cleanup);
          if (!arr) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          arr->count = m->count;
          arr->elements = m->count > 0 ? malloc(sizeof(AgoVal) * (size_t)m->count) : NULL;
          for (int i = 0; i < m->count; i++) {
              arr->elements[i] = val_string(m->keys[i], m->key_lengths[i]);
          }
          AgoVal v; v.kind = VAL_ARRAY; v.as.array = arr; return v;
        }

    case 18: /* map_has */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "map_has() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_MAP) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map_has() first argument must be a map"); return val_nil(); }
        if (args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map_has() key must be a string"); return val_nil(); }
        { AgoMapVal *m = args[0].as.map;
          int klen; const char *kdata = str_content(args[1], &klen);
          for (int i = 0; i < m->count; i++) {
              if (m->key_lengths[i] == klen && memcmp(m->keys[i], kdata, (size_t)klen) == 0) return val_bool(true);
          }
          return val_bool(false);
        }

    case 19: /* map_del */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "map_del() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_MAP) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map_del() first argument must be a map"); return val_nil(); }
        if (args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "map_del() key must be a string"); return val_nil(); }
        { AgoMapVal *old = args[0].as.map;
          int klen; const char *kdata = str_content(args[1], &klen);
          AgoMapVal *nm = ago_gc_alloc(vm->gc, sizeof(AgoMapVal), map_cleanup);
          if (!nm) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          int new_count = 0;
          nm->keys = old->count > 0 ? malloc(sizeof(char *) * (size_t)old->count) : NULL;
          nm->key_lengths = old->count > 0 ? malloc(sizeof(int) * (size_t)old->count) : NULL;
          nm->values = old->count > 0 ? malloc(sizeof(AgoVal) * (size_t)old->count) : NULL;
          for (int i = 0; i < old->count; i++) {
              if (old->key_lengths[i] == klen && memcmp(old->keys[i], kdata, (size_t)klen) == 0) continue;
              nm->keys[new_count] = old->keys[i];
              nm->key_lengths[new_count] = old->key_lengths[i];
              nm->values[new_count] = old->values[i];
              new_count++;
          }
          nm->count = new_count; nm->capacity = old->count;
          AgoVal v; v.kind = VAL_MAP; v.as.map = nm; return v;
        }

    case 20: /* split */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "split() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "split() requires (string, string)"); return val_nil(); }
        { int slen, seplen;
          const char *sdata = str_content(args[0], &slen);
          const char *sepdata = str_content(args[1], &seplen);
          /* Count parts and build array */
          AgoVal parts[256]; int pcount = 0;
          int start = 0;
          for (int i = 0; i <= slen - seplen; i++) {
              if (memcmp(sdata + i, sepdata, (size_t)seplen) == 0) {
                  int plen = i - start;
                  char *p = ago_arena_alloc(vm->arena, (size_t)(plen > 0 ? plen : 1));
                  if (p && plen > 0) memcpy(p, sdata + start, (size_t)plen);
                  parts[pcount++] = val_string(p ? p : "", plen);
                  start = i + seplen;
                  i = start - 1;
              }
          }
          /* Last part */
          { int plen = slen - start;
            char *p = ago_arena_alloc(vm->arena, (size_t)(plen > 0 ? plen : 1));
            if (p && plen > 0) memcpy(p, sdata + start, (size_t)plen);
            parts[pcount++] = val_string(p ? p : "", plen);
          }
          AgoArrayVal *arr = ago_gc_alloc(vm->gc, sizeof(AgoArrayVal), array_cleanup);
          if (!arr) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          arr->count = pcount;
          arr->elements = malloc(sizeof(AgoVal) * (size_t)pcount);
          memcpy(arr->elements, parts, sizeof(AgoVal) * (size_t)pcount);
          AgoVal v; v.kind = VAL_ARRAY; v.as.array = arr; return v;
        }

    case 21: /* trim */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "trim() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "trim() requires a string"); return val_nil(); }
        { int slen; const char *sdata = str_content(args[0], &slen);
          int start = 0, end = slen;
          while (start < end && (sdata[start] == ' ' || sdata[start] == '\t' || sdata[start] == '\n' || sdata[start] == '\r')) start++;
          while (end > start && (sdata[end-1] == ' ' || sdata[end-1] == '\t' || sdata[end-1] == '\n' || sdata[end-1] == '\r')) end--;
          int rlen = end - start;
          char *buf = ago_arena_alloc(vm->arena, (size_t)(rlen > 0 ? rlen : 1));
          if (buf && rlen > 0) memcpy(buf, sdata + start, (size_t)rlen);
          return val_string(buf ? buf : "", rlen);
        }

    case 22: /* contains */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "contains() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "contains() requires (string, string)"); return val_nil(); }
        { int slen, sublen;
          const char *sdata = str_content(args[0], &slen);
          const char *subdata = str_content(args[1], &sublen);
          if (sublen > slen) return val_bool(false);
          if (sublen == 0) return val_bool(true);
          for (int i = 0; i <= slen - sublen; i++) {
              if (memcmp(sdata + i, subdata, (size_t)sublen) == 0) return val_bool(true);
          }
          return val_bool(false);
        }

    case 23: /* replace */
        if (argc != 3) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "replace() takes exactly 3 arguments"); return val_nil(); }
        if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING || args[2].kind != VAL_STRING) {
            ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "replace() requires (string, string, string)"); return val_nil();
        }
        { int slen, oldlen, newlen;
          const char *sdata = str_content(args[0], &slen);
          const char *olddata = str_content(args[1], &oldlen);
          const char *newdata = str_content(args[2], &newlen);
          if (oldlen == 0) { /* No replacement possible */
              char *buf = ago_arena_alloc(vm->arena, (size_t)slen);
              if (buf) memcpy(buf, sdata, (size_t)slen);
              return val_string(buf ? buf : sdata, slen);
          }
          /* Count occurrences */
          int occ = 0;
          for (int i = 0; i <= slen - oldlen; i++) {
              if (memcmp(sdata + i, olddata, (size_t)oldlen) == 0) { occ++; i += oldlen - 1; }
          }
          int rlen = slen + occ * (newlen - oldlen);
          char *buf = ago_arena_alloc(vm->arena, (size_t)(rlen > 0 ? rlen : 1));
          if (!buf) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          int wi = 0;
          for (int i = 0; i < slen; ) {
              if (i <= slen - oldlen && memcmp(sdata + i, olddata, (size_t)oldlen) == 0) {
                  memcpy(buf + wi, newdata, (size_t)newlen); wi += newlen; i += oldlen;
              } else { buf[wi++] = sdata[i++]; }
          }
          return val_string(buf, rlen);
        }

    case 24: /* starts_with */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "starts_with() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "starts_with() requires (string, string)"); return val_nil(); }
        { int slen, plen;
          const char *sdata = str_content(args[0], &slen);
          const char *pdata = str_content(args[1], &plen);
          return val_bool(slen >= plen && memcmp(sdata, pdata, (size_t)plen) == 0);
        }

    case 25: /* ends_with */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "ends_with() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "ends_with() requires (string, string)"); return val_nil(); }
        { int slen, plen;
          const char *sdata = str_content(args[0], &slen);
          const char *pdata = str_content(args[1], &plen);
          return val_bool(slen >= plen && memcmp(sdata + slen - plen, pdata, (size_t)plen) == 0);
        }

    case 26: /* to_upper */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "to_upper() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "to_upper() requires a string"); return val_nil(); }
        { int slen; const char *sdata = str_content(args[0], &slen);
          char *buf = ago_arena_alloc(vm->arena, (size_t)(slen > 0 ? slen : 1));
          if (!buf) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          for (int i = 0; i < slen; i++) buf[i] = (char)(sdata[i] >= 'a' && sdata[i] <= 'z' ? sdata[i] - 32 : sdata[i]);
          return val_string(buf, slen);
        }

    case 27: /* to_lower */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "to_lower() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "to_lower() requires a string"); return val_nil(); }
        { int slen; const char *sdata = str_content(args[0], &slen);
          char *buf = ago_arena_alloc(vm->arena, (size_t)(slen > 0 ? slen : 1));
          if (!buf) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          for (int i = 0; i < slen; i++) buf[i] = (char)(sdata[i] >= 'A' && sdata[i] <= 'Z' ? sdata[i] + 32 : sdata[i]);
          return val_string(buf, slen);
        }

    case 28: /* join */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "join() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_ARRAY || args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "join() requires (array, string)"); return val_nil(); }
        { AgoArrayVal *arr = args[0].as.array;
          int seplen; const char *sepdata = str_content(args[1], &seplen);
          /* Calculate total length */
          int total = 0;
          for (int i = 0; i < arr->count; i++) {
              if (arr->elements[i].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "join() array elements must be strings"); return val_nil(); }
              int elen; str_content(arr->elements[i], &elen);
              total += elen;
              if (i > 0) total += seplen;
          }
          char *buf = ago_arena_alloc(vm->arena, (size_t)(total > 0 ? total : 1));
          if (!buf && total > 0) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          int wi = 0;
          for (int i = 0; i < arr->count; i++) {
              if (i > 0 && seplen > 0) { memcpy(buf + wi, sepdata, (size_t)seplen); wi += seplen; }
              int elen; const char *edata = str_content(arr->elements[i], &elen);
              if (elen > 0) { memcpy(buf + wi, edata, (size_t)elen); wi += elen; }
          }
          return val_string(buf ? buf : "", total);
        }

    case 29: /* substr */
        if (argc != 3) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "substr() takes exactly 3 arguments"); return val_nil(); }
        if (args[0].kind != VAL_STRING || args[1].kind != VAL_INT || args[2].kind != VAL_INT) {
            ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "substr() requires (string, int, int)"); return val_nil();
        }
        { int slen; const char *sdata = str_content(args[0], &slen);
          int start = (int)args[1].as.integer;
          int rlen = (int)args[2].as.integer;
          if (start < 0) start = 0;
          if (start > slen) start = slen;
          if (rlen < 0) rlen = 0;
          if (start + rlen > slen) rlen = slen - start;
          char *buf = ago_arena_alloc(vm->arena, (size_t)(rlen > 0 ? rlen : 1));
          if (buf && rlen > 0) memcpy(buf, sdata + start, (size_t)rlen);
          return val_string(buf ? buf : "", rlen);
        }

    case 31: /* json_parse */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "json_parse() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "json_parse() requires a string"); return val_nil(); }
        { int slen; const char *sd = str_content(args[0], &slen);
          return ago_json_parse(sd, slen, vm->arena, vm->gc);
        }

    case 32: /* json_stringify */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "json_stringify() takes exactly 1 argument"); return val_nil(); }
        { int out_len;
          const char *s = ago_json_stringify(args[0], &out_len, vm->arena);
          return val_string(s, out_len);
        }

    case 33: /* env */
        if (argc != 1) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "env() takes exactly 1 argument"); return val_nil(); }
        if (args[0].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "env() requires a string"); return val_nil(); }
        { int slen; const char *sd = str_content(args[0], &slen);
          char tmp[256]; if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
          memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
          const char *val = getenv(tmp);
          AgoResultVal *rv = ago_gc_alloc(vm->gc, sizeof(AgoResultVal), NULL);
          if (!rv) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory"); return val_nil(); }
          if (val) {
              int vlen = (int)strlen(val);
              char *copy = ago_arena_alloc(vm->arena, (size_t)vlen);
              if (copy) memcpy(copy, val, (size_t)vlen);
              rv->is_ok = true;
              rv->value = val_string(copy ? copy : "", copy ? vlen : 0);
          } else {
              rv->is_ok = false;
              rv->value = val_string("not set", 7);
          }
          AgoVal v; v.kind = VAL_RESULT; v.as.result = rv; return v;
        }

    case 34: /* env_default */
        if (argc != 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "env_default() takes exactly 2 arguments"); return val_nil(); }
        if (args[0].kind != VAL_STRING || args[1].kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, line, col), "env_default() requires (string, string)"); return val_nil(); }
        { int slen; const char *sd = str_content(args[0], &slen);
          char tmp[256]; if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
          memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
          const char *val = getenv(tmp);
          if (val) {
              int vlen = (int)strlen(val);
              char *copy = ago_arena_alloc(vm->arena, (size_t)vlen);
              if (copy) memcpy(copy, val, (size_t)vlen);
              return val_string(copy ? copy : "", copy ? vlen : 0);
          } else {
              int flen; const char *fdata = str_content(args[1], &flen);
              char *copy = ago_arena_alloc(vm->arena, (size_t)flen);
              if (copy) memcpy(copy, fdata, (size_t)flen);
              return val_string(copy ? copy : "", copy ? flen : 0);
          }
        }

    default:
        ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "unknown builtin %d", builtin_id);
        return val_nil();
    }
}

/* ---- VM function call (for map/filter callbacks) ---- */

static AgoVal call_fn_direct_vm(Vm *vm, AgoFnVal *fn, AgoVal *args, int argc,
                                int line, int col) {
    /* For now, delegate to the tree-walk call_fn_direct through a thin wrapper.
     * This requires constructing a temporary AgoInterp. We'll use the VM's env. */
    if (!fn->chunk) {
        /* Tree-walk fallback — should not happen in full VM mode */
        ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col),
                      "cannot call non-compiled function in VM");
        return val_nil();
    }

    /* VM-native function call */
    if (fn->arity != argc) {
        ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col),
                      "expected %d arguments, got %d", fn->arity, argc);
        return val_nil();
    }

    if (vm->frame_count >= VM_FRAMES_MAX) {
        ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col),
                      "maximum call depth exceeded (limit %d)", VM_FRAMES_MAX);
        return val_nil();
    }

    /* Save current env for closure */
    AgoEnv *saved_env = NULL;
    int saved_count = vm->env.count;
    if (fn->captured_count > 0) {
        saved_env = ago_arena_alloc(vm->arena, sizeof(AgoEnv));
        if (!saved_env) {
            ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, line, col), "out of memory");
            return val_nil();
        }
        *saved_env = vm->env;
        env_init(&vm->env);
        for (int i = 0; i < fn->captured_count; i++) {
            env_define(&vm->env, fn->captured_names[i], fn->captured_name_lengths[i],
                       fn->captured_values[i], fn->captured_immutable[i]);
        }
    }

    /* Define parameters */
    for (int i = 0; i < argc; i++) {
        env_define(&vm->env, fn->decl->as.fn_decl.param_names[i],
                   fn->decl->as.fn_decl.param_name_lengths[i],
                   args[i], true);
    }

    /* Push frame */
    VmFrame *frame = &vm->frames[vm->frame_count++];
    frame->fn = fn;
    frame->ip = fn->chunk->code;
    frame->stack_base = vm->stack_top;
    frame->env_saved_count = saved_count;
    frame->is_closure = (saved_env != NULL);
    frame->saved_env = saved_env;

    /* Execute the function's bytecode using full dispatch */
    int base_frame = vm->frame_count;
    int rc = vm_execute(vm, fn->chunk);

    /* Get return value (pushed by OP_RETURN or default nil) */
    AgoVal result = val_nil();
    if (rc == 0 && vm->stack_top > frame->stack_base) {
        result = vm_pop(vm);
    }

    /* Restore state (in case OP_RETURN didn't clean up properly) */
    if (vm->frame_count >= base_frame) {
        vm->frame_count = base_frame - 1;
    }
    vm->stack_top = frame->stack_base;
    if (saved_env) {
        vm->env = *saved_env;
    } else {
        vm->env.count = saved_count;
    }

    if (rc != 0 && !ago_error_occurred(vm->ctx)) {
        return val_nil();
    }

    return result;
}

/* ---- Main execution loop ---- */

static int vm_execute(Vm *vm, AgoChunk *chunk) {
    uint8_t *ip = chunk->code;
    uint8_t *end = chunk->code + chunk->code_count;

    while (ip < end) {
        if (ago_error_occurred(vm->ctx)) return -1;

        uint8_t op = *ip++;

        switch (op) {

        case AGO_OP_CONST: {
            uint16_t idx = read_u16(ip); ip += 2;
            vm_push(vm, chunk->constants[idx]);
            break;
        }

        case AGO_OP_NIL:   vm_push(vm, val_nil()); break;
        case AGO_OP_TRUE:  vm_push(vm, val_bool(true)); break;
        case AGO_OP_FALSE: vm_push(vm, val_bool(false)); break;

        case AGO_OP_ADD: {
            AgoVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) { vm_push(vm, val_int(a.as.integer + b.as.integer)); break; }
            if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) { vm_push(vm, val_float(a.as.floating + b.as.floating)); break; }
            if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double la = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double lb = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                vm_push(vm, val_float(la + lb)); break;
            }
            if (a.kind == VAL_STRING && b.kind == VAL_STRING) {
                int llen, rlen; const char *ld = str_content(a, &llen); const char *rd = str_content(b, &rlen);
                int total = llen + rlen;
                char *buf = ago_arena_alloc(vm->arena, (size_t)total);
                if (!buf) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
                memcpy(buf, ld, (size_t)llen); memcpy(buf + llen, rd, (size_t)rlen);
                vm_push(vm, val_string(buf, total)); break;
            }
            ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "invalid binary operation");
            return -1;
        }

        case AGO_OP_SUB: {
            AgoVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) { vm_push(vm, val_int(a.as.integer - b.as.integer)); break; }
            if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) { vm_push(vm, val_float(a.as.floating - b.as.floating)); break; }
            if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double la = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double lb = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                vm_push(vm, val_float(la - lb)); break;
            }
            ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
        }

        case AGO_OP_MUL: {
            AgoVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) { vm_push(vm, val_int(a.as.integer * b.as.integer)); break; }
            if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) { vm_push(vm, val_float(a.as.floating * b.as.floating)); break; }
            if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double la = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double lb = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                vm_push(vm, val_float(la * lb)); break;
            }
            ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
        }

        case AGO_OP_DIV: {
            AgoVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) {
                if (b.as.integer == 0) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "division by zero"); return -1; }
                vm_push(vm, val_int(a.as.integer / b.as.integer)); break;
            }
            if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) { vm_push(vm, val_float(a.as.floating / b.as.floating)); break; }
            if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double la = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double lb = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                vm_push(vm, val_float(la / lb)); break;
            }
            ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
        }

        case AGO_OP_MOD: {
            AgoVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) {
                if (b.as.integer == 0) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "division by zero"); return -1; }
                vm_push(vm, val_int(a.as.integer % b.as.integer)); break;
            }
            ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
        }

        case AGO_OP_NEGATE: {
            AgoVal a = vm_pop(vm);
            if (a.kind == VAL_INT) { vm_push(vm, val_int(-a.as.integer)); break; }
            if (a.kind == VAL_FLOAT) { vm_push(vm, val_float(-a.as.floating)); break; }
            ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "invalid unary operator"); return -1;
        }

        case AGO_OP_NOT: {
            AgoVal a = vm_pop(vm);
            if (a.kind == VAL_BOOL) { vm_push(vm, val_bool(!a.as.boolean)); break; }
            ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "invalid unary operator"); return -1;
        }

        /* Comparison ops */
        case AGO_OP_EQ: case AGO_OP_NEQ:
        case AGO_OP_LT: case AGO_OP_GT:
        case AGO_OP_LE: case AGO_OP_GE: {
            AgoVal b = vm_pop(vm), a = vm_pop(vm);
            bool result = false;
            if (a.kind == VAL_INT && b.kind == VAL_INT) {
                int64_t l = a.as.integer, r = b.as.integer;
                switch (op) {
                case AGO_OP_EQ: result = l == r; break; case AGO_OP_NEQ: result = l != r; break;
                case AGO_OP_LT: result = l < r; break; case AGO_OP_GT: result = l > r; break;
                case AGO_OP_LE: result = l <= r; break; case AGO_OP_GE: result = l >= r; break;
                default: break;
                }
            } else if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) {
                double l = a.as.floating, r = b.as.floating;
                switch (op) {
                case AGO_OP_EQ: result = l == r; break; case AGO_OP_NEQ: result = l != r; break;
                case AGO_OP_LT: result = l < r; break; case AGO_OP_GT: result = l > r; break;
                case AGO_OP_LE: result = l <= r; break; case AGO_OP_GE: result = l >= r; break;
                default: break;
                }
            } else if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double l = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double r = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                switch (op) {
                case AGO_OP_EQ: result = l == r; break; case AGO_OP_NEQ: result = l != r; break;
                case AGO_OP_LT: result = l < r; break; case AGO_OP_GT: result = l > r; break;
                case AGO_OP_LE: result = l <= r; break; case AGO_OP_GE: result = l >= r; break;
                default: break;
                }
            } else if (a.kind == VAL_BOOL && b.kind == VAL_BOOL) {
                switch (op) {
                case AGO_OP_EQ: result = a.as.boolean == b.as.boolean; break;
                case AGO_OP_NEQ: result = a.as.boolean != b.as.boolean; break;
                default: ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
                }
            } else if (a.kind == VAL_STRING && b.kind == VAL_STRING) {
                int llen, rlen; const char *ld = str_content(a, &llen); const char *rd = str_content(b, &rlen);
                switch (op) {
                case AGO_OP_EQ: result = llen == rlen && memcmp(ld, rd, (size_t)llen) == 0; break;
                case AGO_OP_NEQ: result = llen != rlen || memcmp(ld, rd, (size_t)llen) != 0; break;
                case AGO_OP_LT: case AGO_OP_GT: case AGO_OP_LE: case AGO_OP_GE: {
                    int minlen = llen < rlen ? llen : rlen;
                    int cmp = memcmp(ld, rd, (size_t)minlen);
                    if (cmp == 0) cmp = (llen > rlen) - (llen < rlen);
                    if (op == AGO_OP_LT) result = cmp < 0;
                    else if (op == AGO_OP_GT) result = cmp > 0;
                    else if (op == AGO_OP_LE) result = cmp <= 0;
                    else result = cmp >= 0;
                    break;
                }
                default: break;
                }
            } else {
                ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
            }
            vm_push(vm, val_bool(result));
            break;
        }

        /* Variables */
        case AGO_OP_DEFINE_LET: case AGO_OP_DEFINE_VAR: {
            uint16_t idx = read_u16(ip); ip += 2;
            AgoVal val = vm_pop(vm);
            AgoVal name = chunk->constants[idx];
            bool immut = (op == AGO_OP_DEFINE_LET);
            if (!env_define(&vm->env, name.as.string.data, name.as.string.length, val, immut)) {
                ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "too many variables (max %d)", MAX_VARS);
                return -1;
            }
            break;
        }

        case AGO_OP_GET_VAR: {
            uint16_t idx = read_u16(ip); ip += 2;
            AgoVal name = chunk->constants[idx];
            AgoVal *v = env_get(&vm->env, name.as.string.data, name.as.string.length);
            if (!v) {
                ago_error_set(vm->ctx, AGO_ERR_NAME, ago_loc(NULL, vm->current_line, 0),
                              "undefined variable '%.*s'", name.as.string.length, name.as.string.data);
                return -1;
            }
            vm_push(vm, *v);
            break;
        }

        case AGO_OP_SET_VAR: {
            uint16_t idx = read_u16(ip); ip += 2;
            AgoVal val = vm_pop(vm);
            AgoVal name = chunk->constants[idx];
            int rc = env_assign(&vm->env, name.as.string.data, name.as.string.length, val);
            if (rc == 1) { ago_error_set(vm->ctx, AGO_ERR_NAME, ago_loc(NULL, vm->current_line, 0), "undefined variable '%.*s'", name.as.string.length, name.as.string.data); return -1; }
            if (rc == 2) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "cannot assign to immutable variable '%.*s'", name.as.string.length, name.as.string.data); return -1; }
            break;
        }

        case AGO_OP_POP: vm_pop(vm); break;

        case AGO_OP_POP_SCOPE: {
            uint8_t count = *ip++;
            vm->env.count -= count;
            break;
        }

        /* Control flow */
        case AGO_OP_JUMP: { uint16_t offset = read_u16(ip); ip += 2; ip += offset; break; }
        case AGO_OP_JUMP_BACK: { uint16_t offset = read_u16(ip); ip += 2; ip -= offset; break; }
        case AGO_OP_JUMP_IF_FALSE: {
            uint16_t offset = read_u16(ip); ip += 2;
            AgoVal cond = vm_pop(vm);
            if (!is_truthy(cond)) ip += offset;
            break;
        }
        case AGO_OP_JUMP_IF_TRUE: {
            uint16_t offset = read_u16(ip); ip += 2;
            AgoVal cond = vm_pop(vm);
            if (is_truthy(cond)) ip += offset;
            break;
        }

        /* Functions */
        case AGO_OP_CLOSURE: {
            uint16_t idx = read_u16(ip); ip += 2;
            AgoVal fn_template = chunk->constants[idx];
            /* Create a closure: copy the function and capture current env */
            AgoFnVal *orig = fn_template.as.fn;
            AgoFnVal *fn = ago_gc_alloc(vm->gc, sizeof(AgoFnVal), fn_cleanup);
            if (!fn) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            fn->decl = orig->decl;
            fn->chunk = orig->chunk;
            fn->arity = orig->arity;
            /* Capture current environment */
            fn->captured_count = vm->env.count;
            fn->captured_names = NULL; fn->captured_name_lengths = NULL;
            fn->captured_values = NULL; fn->captured_immutable = NULL;
            if (fn->captured_count > 0) {
                size_t n = (size_t)fn->captured_count;
                fn->captured_names = malloc(sizeof(char *) * n);
                fn->captured_name_lengths = malloc(sizeof(int) * n);
                fn->captured_values = malloc(sizeof(AgoVal) * n);
                fn->captured_immutable = malloc(sizeof(bool) * n);
                if (!fn->captured_names || !fn->captured_name_lengths ||
                    !fn->captured_values || !fn->captured_immutable) {
                    ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "out of memory");
                    return -1;
                }
                memcpy(fn->captured_names, vm->env.names, sizeof(char *) * n);
                memcpy(fn->captured_name_lengths, vm->env.name_lengths, sizeof(int) * n);
                memcpy(fn->captured_values, vm->env.values, sizeof(AgoVal) * n);
                memcpy(fn->captured_immutable, vm->env.immutable, sizeof(bool) * n);
            }
            AgoVal fv; fv.kind = VAL_FN; fv.as.fn = fn;
            vm_push(vm, fv);
            break;
        }

        case AGO_OP_CALL: {
            uint8_t argc = *ip++;
            AgoVal callee = vm->stack[vm->stack_top - 1 - argc];
            if (callee.kind != VAL_FN) {
                if (callee.kind == VAL_NIL || callee.kind == VAL_INT || callee.kind == VAL_STRING) {
                    ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "expression is not callable");
                } else {
                    ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "expression is not callable");
                }
                return -1;
            }
            AgoFnVal *fn = callee.as.fn;
            if (!fn->chunk) {
                ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "cannot call non-compiled function");
                return -1;
            }
            if (fn->arity != argc) {
                ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0),
                              "expected %d arguments, got %d", fn->arity, argc);
                return -1;
            }
            /* Collect args from stack */
            AgoVal args[64];
            for (int i = 0; i < argc; i++) {
                args[i] = vm->stack[vm->stack_top - argc + i];
            }
            /* Pop callee + args */
            vm->stack_top -= (argc + 1);

            AgoVal result = call_fn_direct_vm(vm, fn, args, argc, vm->current_line, 0);
            if (ago_error_occurred(vm->ctx)) return -1;
            vm_push(vm, result);
            break;
        }

        case AGO_OP_RETURN: {
            /* Return value is already on stack (TOS) — leave it there for caller */
            return 0;
        }
        case AGO_OP_RETURN_NIL:
            vm_push(vm, val_nil());
            return 0;

        /* Builtins */
        case AGO_OP_CALL_BUILTIN: {
            uint16_t bid = read_u16(ip); ip += 2;
            uint8_t argc = *ip++;
            AgoVal args[64];
            for (int i = argc - 1; i >= 0; i--) {
                args[i] = vm_pop(vm);
            }
            AgoVal result = vm_call_builtin(vm, bid, args, argc);
            if (ago_error_occurred(vm->ctx)) return -1;
            vm_push(vm, result);
            break;
        }

        /* Compound types */
        case AGO_OP_ARRAY: {
            uint16_t count = read_u16(ip); ip += 2;
            AgoArrayVal *arr = ago_gc_alloc(vm->gc, sizeof(AgoArrayVal), array_cleanup);
            if (!arr) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            arr->count = count;
            arr->elements = count > 0 ? malloc(sizeof(AgoVal) * count) : NULL;
            for (int i = count - 1; i >= 0; i--) { arr->elements[i] = vm_pop(vm); }
            AgoVal v; v.kind = VAL_ARRAY; v.as.array = arr;
            vm_push(vm, v);
            break;
        }

        case AGO_OP_INDEX: {
            AgoVal idx_val = vm_pop(vm);
            AgoVal obj = vm_pop(vm);
            if (obj.kind == VAL_MAP) {
                if (idx_val.kind != VAL_STRING) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "map key must be a string"); return -1; }
                int klen; const char *kdata = str_content(idx_val, &klen);
                AgoMapVal *m = obj.as.map;
                bool found = false;
                for (int i = 0; i < m->count; i++) {
                    if (m->key_lengths[i] == klen && memcmp(m->keys[i], kdata, (size_t)klen) == 0) {
                        vm_push(vm, m->values[i]);
                        found = true;
                        break;
                    }
                }
                if (!found) { vm_push(vm, val_nil()); }
                break;
            }
            if (obj.kind != VAL_ARRAY) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "cannot index non-array value"); return -1; }
            if (idx_val.kind != VAL_INT) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "array index must be an integer"); return -1; }
            int i = (int)idx_val.as.integer;
            if (i < 0 || i >= obj.as.array->count) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "index %d out of bounds (length %d)", i, obj.as.array->count); return -1; }
            vm_push(vm, obj.as.array->elements[i]);
            break;
        }

        case AGO_OP_STRUCT: {
            uint16_t type_idx = read_u16(ip); ip += 2;
            uint8_t field_count = *ip++;
            AgoStructVal *s = ago_gc_alloc(vm->gc, sizeof(AgoStructVal), NULL);
            if (!s) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            AgoVal type_name = chunk->constants[type_idx];
            s->type_name = type_name.as.string.data;
            s->type_name_length = type_name.as.string.length;
            s->field_count = field_count;
            /* Pop name+value pairs (pushed as: name0, val0, name1, val1, ...) */
            for (int i = field_count - 1; i >= 0; i--) {
                s->field_values[i] = vm_pop(vm);
                AgoVal fname = vm_pop(vm);
                s->field_names[i] = fname.as.string.data;
                s->field_name_lengths[i] = fname.as.string.length;
            }
            AgoVal v; v.kind = VAL_STRUCT; v.as.strct = s;
            vm_push(vm, v);
            break;
        }

        case AGO_OP_GET_FIELD: {
            uint16_t idx = read_u16(ip); ip += 2;
            AgoVal obj = vm_pop(vm);
            if (obj.kind != VAL_STRUCT) { ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "cannot access field on non-struct value"); return -1; }
            AgoVal fname = chunk->constants[idx];
            AgoStructVal *s = obj.as.strct;
            bool found = false;
            for (int i = 0; i < s->field_count; i++) {
                if (ago_str_eq(s->field_names[i], s->field_name_lengths[i],
                               fname.as.string.data, fname.as.string.length)) {
                    vm_push(vm, s->field_values[i]);
                    found = true;
                    break;
                }
            }
            if (!found) {
                ago_error_set(vm->ctx, AGO_ERR_NAME, ago_loc(NULL, vm->current_line, 0),
                              "no field '%.*s'", fname.as.string.length, fname.as.string.data);
                return -1;
            }
            break;
        }

        case AGO_OP_MAP: {
            uint16_t count = read_u16(ip); ip += 2;
            AgoMapVal *m = ago_gc_alloc(vm->gc, sizeof(AgoMapVal), map_cleanup);
            if (!m) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            m->count = count;
            m->capacity = count > 0 ? count : 0;
            m->keys = count > 0 ? malloc(sizeof(char *) * count) : NULL;
            m->key_lengths = count > 0 ? malloc(sizeof(int) * count) : NULL;
            m->values = count > 0 ? malloc(sizeof(AgoVal) * count) : NULL;
            /* Stack has: key0, val0, key1, val1, ... (pushed in order) */
            /* Pop in reverse: valN-1, keyN-1, ..., val0, key0 */
            for (int i = count - 1; i >= 0; i--) {
                m->values[i] = vm_pop(vm);
                AgoVal key = vm_pop(vm);
                int klen; const char *kdata = str_content(key, &klen);
                m->keys[i] = kdata;
                m->key_lengths[i] = klen;
            }
            AgoVal v; v.kind = VAL_MAP; v.as.map = m;
            vm_push(vm, v);
            break;
        }

        case AGO_OP_RESULT_OK: {
            AgoVal inner = vm_pop(vm);
            AgoResultVal *rv = ago_gc_alloc(vm->gc, sizeof(AgoResultVal), NULL);
            if (!rv) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            rv->is_ok = true; rv->value = inner;
            AgoVal v; v.kind = VAL_RESULT; v.as.result = rv;
            vm_push(vm, v);
            break;
        }

        case AGO_OP_RESULT_ERR: {
            AgoVal inner = vm_pop(vm);
            AgoResultVal *rv = ago_gc_alloc(vm->gc, sizeof(AgoResultVal), NULL);
            if (!rv) { ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            rv->is_ok = false; rv->value = inner;
            AgoVal v; v.kind = VAL_RESULT; v.as.result = rv;
            vm_push(vm, v);
            break;
        }

        case AGO_OP_MATCH: {
            uint16_t ok_name_idx = read_u16(ip); ip += 2;
            uint16_t err_name_idx = read_u16(ip); ip += 2;
            uint16_t ok_offset = read_u16(ip); ip += 2;
            uint16_t err_offset = read_u16(ip); ip += 2;
            AgoVal subject = vm_pop(vm);
            if (subject.kind != VAL_RESULT) {
                ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "match requires a result value");
                return -1;
            }
            uint8_t *base = ip;
            if (subject.as.result->is_ok) {
                AgoVal name = chunk->constants[ok_name_idx];
                env_define(&vm->env, name.as.string.data, name.as.string.length,
                           subject.as.result->value, true);
                ip = base + ok_offset;
            } else {
                AgoVal name = chunk->constants[err_name_idx];
                env_define(&vm->env, name.as.string.data, name.as.string.length,
                           subject.as.result->value, true);
                ip = base + err_offset;
            }
            /* Note: the match arm body will execute and leave a value on stack.
             * The ok arm ends with JUMP past err arm. Env is restored after. */
            /* We need to restore env after the match body executes.
             * For now, we let the match body run, and POP_SCOPE handles cleanup.
             * Actually, match is an expression in the compiler — the body just pushes
             * a value. We'll restore env count after both arms converge. */
            /* Restore happens naturally when the enclosing scope ends. For match as
             * an expression, we need explicit cleanup. Let's emit POP_SCOPE 1 in
             * the compiler after each arm. For now, just continue. */
            break;
        }

        case AGO_OP_ITER_SETUP: {
            AgoVal arr = vm_pop(vm);
            if (arr.kind != VAL_ARRAY) {
                ago_error_set(vm->ctx, AGO_ERR_TYPE, ago_loc(NULL, vm->current_line, 0), "for-in requires an array");
                return -1;
            }
            /* Push iteration state: array, length, index */
            vm_push(vm, arr);
            vm_push(vm, val_int(arr.as.array->count));
            vm_push(vm, val_int(0)); /* current index */
            break;
        }

        case AGO_OP_ITER_NEXT: {
            uint16_t end_offset = read_u16(ip); ip += 2;
            /* Stack: [..., array, len, index] */
            AgoVal idx = vm_peek(vm, 0);
            AgoVal len = vm_peek(vm, 1);
            if (idx.as.integer >= len.as.integer) {
                ip += end_offset;
                break;
            }
            AgoVal arr = vm_peek(vm, 2);
            vm_push(vm, arr.as.array->elements[(int)idx.as.integer]);
            /* Increment index */
            vm->stack[vm->stack_top - 2].as.integer++; /* update index in-place */
            break;
        }

        case AGO_OP_ITER_CLEANUP: {
            /* Pop array, len, index */
            vm_pop(vm); vm_pop(vm); vm_pop(vm);
            break;
        }

        case AGO_OP_IMPORT: {
            uint16_t idx = read_u16(ip); ip += 2;
            AgoVal path_val = chunk->constants[idx];
            const char *import_path = path_val.as.string.data;
            int import_len = path_val.as.string.length;

            /* Resolve path */
            char resolved[512];
            if (!resolve_import_path(vm->file, import_path, import_len,
                                     resolved, sizeof(resolved))) {
                ago_error_set(vm->ctx, AGO_ERR_IO, ago_loc(NULL, vm->current_line, 0),
                              "invalid import path '%.*s'", import_len, import_path);
                return -1;
            }

            /* Check module cache */
            bool already_loaded = false;
            for (int i = 0; i < vm->module_count; i++) {
                if (strcmp(vm->modules[i].path, resolved) == 0) {
                    already_loaded = true;
                    break;
                }
            }
            if (already_loaded) break;

            /* Read module file */
            char *mod_source = ago_read_file(resolved);
            if (!mod_source) {
                ago_error_set(vm->ctx, AGO_ERR_IO, ago_loc(NULL, vm->current_line, 0),
                              "cannot open module '%.*s'", import_len, import_path);
                return -1;
            }

            /* Parse */
            AgoArena *mod_arena = ago_arena_new();
            if (!mod_arena) { free(mod_source); return -1; }
            AgoParser mod_parser;
            ago_parser_init(&mod_parser, mod_source, resolved, mod_arena, vm->ctx);
            AgoNode *mod_prog = ago_parser_parse(&mod_parser);
            if (!mod_prog || ago_error_occurred(vm->ctx)) {
                ago_arena_free(mod_arena); free(mod_source); return -1;
            }

            /* Register module before execution (prevents circular imports) */
            if (vm->module_count < MAX_MODULES) {
                AgoModule *m = &vm->modules[vm->module_count++];
                m->path = strdup(resolved);
                m->source = mod_source;
                m->arena = mod_arena;
            } else {
                ago_arena_free(mod_arena); free(mod_source);
                ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0),
                              "too many modules (max %d)", MAX_MODULES);
                return -1;
            }

            /* Compile + execute module */
            AgoChunk *mod_chunk = ago_compile(mod_prog, vm->ctx, mod_arena, vm->gc);
            if (!mod_chunk || ago_error_occurred(vm->ctx)) return -1;

            const char *saved_file = vm->file;
            vm->file = resolved;
            int rc = vm_execute(vm, mod_chunk);
            vm->file = saved_file;
            ago_chunk_free(mod_chunk);
            if (rc != 0) return -1;
            break;
        }

        case AGO_OP_LINE: {
            uint16_t line = read_u16(ip); ip += 2;
            vm->current_line = line;
            /* GC check at statement boundaries */
            if (ago_gc_should_collect(vm->gc)) {
                vm_gc_collect(vm);
            }
            break;
        }

        default:
            ago_error_set(vm->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, vm->current_line, 0),
                          "unknown opcode %d", op);
            return -1;
        }
    }

    return 0;
}

/* ---- Public API ---- */

int ago_vm_run(AgoChunk *chunk, const char *filename, AgoCtx *ctx) {
    if (!chunk || !ctx) return -1;

    AgoArena *arena = ago_arena_new();
    if (!arena) return -1;

    AgoGc *gc = ago_gc_new();
    if (!gc) { ago_arena_free(arena); return -1; }

    Vm vm;
    memset(&vm, 0, sizeof(vm));
    env_init(&vm.env);
    vm.ctx = ctx;
    vm.arena = arena;
    vm.gc = gc;
    vm.file = filename ? filename : "<stdin>";
    vm.current_line = 1;

    ctx->trace_cb = vm_capture_trace;
    ctx->trace_data = &vm;

    int result = vm_execute(&vm, chunk);

    ctx->trace_cb = NULL;
    ctx->trace_data = NULL;
    ago_gc_free(gc);
    /* Free module cache */
    for (int i = 0; i < vm.module_count; i++) {
        free(vm.modules[i].path);
        free(vm.modules[i].source);
        ago_arena_free(vm.modules[i].arena);
    }

    ago_gc_free(gc);
    ago_arena_free(arena);
    return result;
}

/* ---- Compile + execute AST via VM ---- */

int ago_vm_interpret(AgoNode *program, const char *filename, AgoCtx *ctx) {
    if (!program || program->kind != AGO_NODE_PROGRAM) return -1;

    AgoArena *arena = ago_arena_new();
    if (!arena) return -1;

    AgoGc *gc = ago_gc_new();
    if (!gc) { ago_arena_free(arena); return -1; }

    AgoChunk *chunk = ago_compile(program, ctx, arena, gc);
    if (!chunk || ago_error_occurred(ctx)) {
        ago_gc_free(gc);
        ago_arena_free(arena);
        return -1;
    }

    Vm vm;
    memset(&vm, 0, sizeof(vm));
    env_init(&vm.env);
    vm.ctx = ctx;
    vm.arena = arena;
    vm.gc = gc;
    vm.file = filename ? filename : "<stdin>";
    vm.current_line = 1;

    ctx->trace_cb = vm_capture_trace;
    ctx->trace_data = &vm;

    int result = vm_execute(&vm, chunk);

    ctx->trace_cb = NULL;
    ctx->trace_data = NULL;
    ago_chunk_free(chunk);
    for (int i = 0; i < vm.module_count; i++) {
        free(vm.modules[i].path);
        free(vm.modules[i].source);
        ago_arena_free(vm.modules[i].arena);
    }
    ago_gc_free(gc);
    ago_arena_free(arena);
    return result;
}

/* ---- VM-based REPL ---- */

struct AgoVmRepl {
    Vm vm;
    AgoCtx *ctx;
};

AgoVmRepl *ago_vm_repl_new(void) {
    AgoVmRepl *repl = calloc(1, sizeof(AgoVmRepl));
    if (!repl) return NULL;

    repl->ctx = ago_ctx_new();
    if (!repl->ctx) { free(repl); return NULL; }

    AgoArena *arena = ago_arena_new();
    if (!arena) { ago_ctx_free(repl->ctx); free(repl); return NULL; }

    AgoGc *gc = ago_gc_new();
    if (!gc) { ago_arena_free(arena); ago_ctx_free(repl->ctx); free(repl); return NULL; }

    env_init(&repl->vm.env);
    repl->vm.ctx = repl->ctx;
    repl->vm.arena = arena;
    repl->vm.gc = gc;
    repl->vm.file = "<repl>";
    repl->vm.current_line = 1;

    repl->ctx->trace_cb = vm_capture_trace;
    repl->ctx->trace_data = &repl->vm;

    return repl;
}

void ago_vm_repl_free(AgoVmRepl *repl) {
    if (!repl) return;
    for (int i = 0; i < repl->vm.module_count; i++) {
        free(repl->vm.modules[i].path);
        free(repl->vm.modules[i].source);
        ago_arena_free(repl->vm.modules[i].arena);
    }
    ago_gc_free(repl->vm.gc);
    ago_arena_free(repl->vm.arena);
    repl->ctx->trace_cb = NULL;
    ago_ctx_free(repl->ctx);
    free(repl);
}

int ago_vm_repl_exec(AgoVmRepl *repl, const char *source) {
    if (!repl || !source) return -1;

    ago_error_clear(repl->ctx);

    /* Copy source into arena for AST pointer safety */
    AgoArena *parse_arena = ago_arena_new();
    if (!parse_arena) return -1;

    size_t src_len = strlen(source);
    char *src_copy = ago_arena_alloc(parse_arena, src_len + 1);
    if (!src_copy) { ago_arena_free(parse_arena); return -1; }
    memcpy(src_copy, source, src_len + 1);

    AgoParser parser;
    ago_parser_init(&parser, src_copy, "<repl>", parse_arena, repl->ctx);
    AgoNode *program = ago_parser_parse(&parser);

    if (!program || ago_error_occurred(repl->ctx)) {
        if (ago_error_occurred(repl->ctx)) {
            ago_error_print(ago_error_get(repl->ctx));
        }
        ago_arena_free(parse_arena);
        return -1;
    }

    AgoChunk *chunk = ago_compile(program, repl->ctx, parse_arena, repl->vm.gc);
    if (!chunk || ago_error_occurred(repl->ctx)) {
        if (ago_error_occurred(repl->ctx)) {
            ago_error_print(ago_error_get(repl->ctx));
        }
        ago_arena_free(parse_arena);
        return -1;
    }

    int result = vm_execute(&repl->vm, chunk);
    if (ago_error_occurred(repl->ctx)) {
        ago_error_print(ago_error_get(repl->ctx));
        ago_chunk_free(chunk);
        /* Keep parse arena alive for fn decls that reference AST */
        return -1;
    }

    ago_chunk_free(chunk);
    /* Keep parse arena alive — fn decl AST nodes may be referenced */
    return result;
}
