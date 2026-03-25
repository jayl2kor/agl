# AGL — Agent Language

A general-purpose programming language designed for AI agents.
Built from scratch in C11 with a bytecode compiler and stack-based virtual machine.

---

## What is AGL?

AGL is a **fully implemented programming language** — not a DSL, not a parser prototype, not a rule engine. It has:

- A **lexer** that tokenizes source with Go-style auto-semicolon insertion
- A **recursive-descent parser** that produces a typed AST (27 node kinds)
- A **semantic analyzer** with 2-pass scope resolution and forward reference support
- A **bytecode compiler** that emits 46 distinct opcodes into chunk objects
- A **stack-based virtual machine** that executes bytecode with a mark-and-sweep garbage collector
- A **standard library** of 40 built-in functions (HTTP, JSON, file I/O, process execution, string manipulation)
- **671+ tests** running under AddressSanitizer and UBSan, across 6 test suites

The entire implementation is ~8,500 lines of C11 with zero external dependencies for the core language (libcurl is optional for HTTP).

---

## Compilation Pipeline

AGL is not interpreted by walking an AST. Source code goes through a full compilation pipeline:

```
Source (.agl)
    │
    ▼
  Lexer ─────────── Tokenize with auto-semicolons (34 token types)
    │
    ▼
  Parser ────────── Recursive-descent + Pratt parsing → AST (27 node kinds)
    │
    ▼
  Sema ──────────── 2-pass semantic analysis (scope, immutability, arity)
    │
    ▼
  Compiler ──────── AST → Bytecode IR (46 opcodes, constant pool)
    │
    ▼
  VM ────────────── Stack-based execution, mark-and-sweep GC
```

### Bytecode IR

The compiler produces `AglChunk` objects — the intermediate representation:

```c
typedef struct AglChunk {
    uint8_t *code;              // bytecode array
    int code_count;
    struct AglVal *constants;   // constant pool (literals, function objects)
    int const_count;
} AglChunk;
```

Each function compiles to its own chunk. The 46 opcodes cover:

| Category | Opcodes |
|----------|---------|
| Constants | `CONST`, `NIL`, `TRUE`, `FALSE` |
| Arithmetic | `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `NEGATE` |
| Comparison | `EQ`, `NEQ`, `LT`, `GT`, `LE`, `GE` |
| Logic | `NOT` |
| Variables | `DEFINE_LET`, `DEFINE_VAR`, `GET_VAR`, `SET_VAR` |
| Stack | `POP`, `POP_SCOPE` |
| Control flow | `JUMP`, `JUMP_BACK`, `JUMP_IF_FALSE`, `JUMP_IF_TRUE` |
| Functions | `CLOSURE`, `CALL`, `RETURN`, `RETURN_NIL`, `CALL_BUILTIN` |
| Data structures | `ARRAY`, `INDEX`, `STRUCT`, `GET_FIELD`, `MAP` |
| Error handling | `RESULT_OK`, `RESULT_ERR`, `MATCH`, `TRY` |
| Iteration | `ITER_SETUP`, `ITER_NEXT`, `ITER_CLEANUP` |
| Modules | `IMPORT` |
| Debug | `LINE` |

### Garbage Collector

The VM uses a mark-and-sweep GC with an intrusive linked list:

```c
typedef struct AglObj {
    struct AglObj *next;        // intrusive linked list of all objects
    size_t size;                // allocation size for GC accounting
    void (*cleanup)(void *obj); // free internal buffers before sweep
    bool marked;
} AglObj;
```

GC roots: value stack + environment + call frame closures.
Collection triggers at statement boundaries when `bytes_allocated > threshold`.
Threshold grows by 2x after each sweep (minimum 1 MB).

---

## Quick Start

```bash
git clone https://github.com/jayl2kor/agl.git
cd agl
make
./agl examples/hello.agl    # run a program
./agl                       # interactive REPL
```

---

## Language Features

### Variables and State

AGL has mutable and immutable bindings. It is **not stateless**.

```agl
let name = "AGL"           // immutable
var count = 0              // mutable
count = count + 1
print(f"{name}: {count}")  // AGL: 1
```

### Control Flow

Full control flow: `if`/`else`, `while`, `for-in`, `break`, `continue`.

```agl
for i in [1, 2, 3, 4, 5] {
    if i % 2 == 0 { continue }
    if i > 3 { break }
    print(i)
}
// Output: 1, 3
```

### Functions and Closures

First-class functions, closures with captured state, higher-order functions.

```agl
fn make_adder(n: int) -> fn {
    return fn(x: int) -> int { return n + x }
}

let add5 = make_adder(5)
print(add5(10))             // 15

let doubled = map([1, 2, 3], fn(x: int) -> int { return x * 2 })
print(doubled)              // [2, 4, 6]
```

Forward references work — functions can call each other regardless of definition order:

```agl
fn is_even(n: int) -> bool {
    if n == 0 { return true }
    return is_odd(n - 1)
}
fn is_odd(n: int) -> bool {
    if n == 0 { return false }
    return is_even(n - 1)
}
print(is_even(10))          // true
```

### Result Type and Error Propagation

Built-in `ok`/`err` constructors, `match` expressions, and `?` operator for concise error propagation.

```agl
fn safe_div(a: int, b: int) -> result {
    if b == 0 { return err("division by zero") }
    return ok(a / b)
}

// Pattern matching
match safe_div(10, 0) {
    ok(v) -> print(v)
    err(e) -> print(e)      // division by zero
}

// ? operator: unwraps ok, propagates err
fn pipeline() -> result {
    let a = safe_div(10, 2)?    // ok(5) → 5
    let b = safe_div(a, 0)?     // err → returns err immediately
    return ok(b)
}
```

### Maps and JSON

Map literals, bracket access, and built-in JSON serialization.

```agl
let config = {"host": "localhost", "port": 8080, "debug": true}
print(config["host"])           // localhost
print(map_keys(config))        // ["host", "port", "debug"]

let json = json_stringify(config)
print(json)                     // {"host":"localhost","port":8080,"debug":true}

let parsed = json_parse(json)?
print(parsed["port"])           // 8080
```

### HTTP and External Interaction

HTTP client, process execution, environment variables, file I/O.

```agl
// HTTP
let resp = http_get("https://api.example.com/data", {})?
let data = json_parse(resp["body"])?

// Process execution
let result = exec("git", ["status", "--porcelain"])?
print(trim(result["stdout"]))

// Environment variables
let api_key = env("API_KEY")?

// File I/O
write_file("/tmp/out.json", json_stringify(data))?
let content = read_file("/tmp/out.json")?
```

### String Interpolation

```agl
let name = "world"
let x = 42
print(f"Hello, {name}! The answer is {x + 1}.")
// Hello, world! The answer is 43.
```

### Modules

File-based import system with path traversal protection and cycle detection.

```agl
// math.agl
fn square(n: int) -> int { return n * n }

// main.agl
import "math"
print(square(7))    // 49
```

### Structs

```agl
struct Point {
    x: int
    y: int
}
let p = Point { x: 10, y: 20 }
print(p.x + p.y)               // 30
```

---

## AI Agent Example

Call the Claude API, parse the JSON response, and print the result:

```agl
let api_key = env("ANTHROPIC_API_KEY")?

let body = json_stringify({
    "model": "claude-sonnet-4-20250514",
    "max_tokens": 256,
    "messages": [
        {"role": "user", "content": "Say hello in one sentence."}
    ]
})

var headers = map_set({}, "content-type", "application/json")
let headers = map_set(headers, "x-api-key", api_key)
let headers = map_set(headers, "anthropic-version", "2023-06-01")

let resp = http_post("https://api.anthropic.com/v1/messages", headers, body)?
let data = json_parse(resp["body"])?
print(data["content"][0]["text"])
```

---

## Standard Library (40 functions)

| Category | Functions |
|----------|----------|
| **I/O** | `print` |
| **Types** | `type`, `len`, `str`, `int`, `float` |
| **Arrays** | `push`, `map`, `filter` |
| **Math** | `abs`, `min`, `max` |
| **Strings** | `split`, `trim`, `contains`, `replace`, `starts_with`, `ends_with`, `to_upper`, `to_lower`, `join`, `substr` |
| **Maps** | `map_get`, `map_set`, `map_keys`, `map_has`, `map_del` |
| **JSON** | `json_parse`, `json_stringify` |
| **HTTP** | `http_get`, `http_post` |
| **File I/O** | `read_file`, `write_file`, `file_exists` |
| **Process** | `exec` |
| **Environment** | `env`, `env_default` |
| **Time** | `now`, `sleep` |
| **Result** | `ok`, `err` |

---

## Project Stats

| Metric | Value |
|--------|-------|
| Language | C11 (no external deps for core) |
| Source files | 38 (.c and .h) |
| Lines of code | ~8,500 |
| Test assertions | 671+ |
| Test suites | 6 (Lexer, Parser, Sema, Interpreter, GC, VM) |
| Bytecode opcodes | 46 |
| Built-in functions | 40 |
| AST node types | 27 |
| Token types | 34 |
| Memory safety | ASan + UBSan enabled in all builds |

---

## Building

### Requirements

- **C11 compiler** (Clang or GCC)
- **POSIX environment** (macOS or Linux)
- **libcurl** (optional, for HTTP support)

### Make Targets

```bash
make            # build the agl binary (debug, ASan + UBSan)
make test       # run all 671+ tests
make clean      # remove build artifacts
```

---

## Testing

```bash
$ make test

--- 139 passed, 0 failed ---    # Lexer
--- 117 passed, 0 failed ---    # Parser
---  23 passed, 0 failed ---    # Sema
--- 232 passed, 0 failed ---    # Interpreter
---  19 passed, 0 failed ---    # GC
--- 141 passed, 0 failed ---    # VM
```

All tests run under AddressSanitizer and UndefinedBehaviorSanitizer.

---

## Documentation

| Document | URL |
|----------|-----|
| **Documentation Site** | [jayl2kor.github.io/agl](https://jayl2kor.github.io/agl/) |
| Language Specification | [docs/spec.md](docs/spec.md) |
| Getting Started | [docs/tutorial.md](docs/tutorial.md) |
| Standard Library | [docs/stdlib.md](docs/stdlib.md) |
| Error Catalog | [docs/errors.md](docs/errors.md) |

---

## License

MIT License. See [LICENSE](LICENSE) for details.
