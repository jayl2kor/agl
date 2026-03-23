#include "common.h"
#include "error.h"
#include "interpreter.h"

/* ---- Interactive REPL ---- */

static int run_repl(void) {
    printf("ago %s — interactive REPL\n", AGO_VERSION);
    printf("Type expressions or statements. Ctrl+D to exit.\n\n");

    AgoRepl *repl = ago_repl_new();
    if (!repl) {
        fprintf(stderr, "ago: failed to initialize REPL\n");
        return 1;
    }

    char line[4096];
    char buf[16384];
    int buf_len = 0;
    int brace_depth = 0;

    for (;;) {
        /* Print prompt */
        printf("%s", brace_depth > 0 ? "...> " : "ago> ");
        fflush(stdout);

        if (!fgets(line, (int)sizeof(line), stdin)) {
            /* EOF (Ctrl+D) */
            if (buf_len > 0) {
                /* Execute any pending input */
                buf[buf_len] = '\0';
                ago_repl_exec(repl, buf);
            }
            printf("\n");
            break;
        }

        /* Track brace depth for multi-line input */
        for (const char *p = line; *p; p++) {
            if (*p == '{') brace_depth++;
            else if (*p == '}') { if (brace_depth > 0) brace_depth--; }
        }

        /* Append line to buffer */
        int line_len = (int)strlen(line);
        if (buf_len + line_len < (int)sizeof(buf) - 1) {
            memcpy(buf + buf_len, line, (size_t)line_len);
            buf_len += line_len;
        }

        /* Execute when braces are balanced */
        if (brace_depth == 0 && buf_len > 0) {
            buf[buf_len] = '\0';
            ago_repl_exec(repl, buf);
            buf_len = 0;
        }
    }

    ago_repl_free(repl);
    return 0;
}

/* ---- File execution ---- */

static int run_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ago: cannot open '%s'\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) {
        fprintf(stderr, "ago: cannot read '%s'\n", path);
        fclose(f);
        return 1;
    }
    fseek(f, 0, SEEK_SET);
    char *source = malloc((size_t)len + 1);
    if (!source) { fclose(f); return 1; }
    size_t read_len = fread(source, 1, (size_t)len, f);
    source[read_len] = '\0';
    fclose(f);

    AgoCtx *ctx = ago_ctx_new();
    if (!ctx) { free(source); return 1; }

    int result = ago_run(source, path, ctx);

    if (ago_error_occurred(ctx)) {
        ago_error_print(ago_error_get(ctx));
        result = 1;
    }

    ago_ctx_free(ctx);
    free(source);
    return result;
}

/* ---- Entry point ---- */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        return run_repl();
    }
    return run_file(argv[1]);
}
