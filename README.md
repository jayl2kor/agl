# AGL — Agent Language

A programming language designed for AI agents. Built in C11 with a bytecode VM.

## Features

- **Bytecode VM** with 47 opcodes and mark-and-sweep GC
- **Map/Dictionary** with `{}` literals and JSON-like access
- **JSON** parse/stringify built-in (no external dependencies)
- **HTTP** client (GET/POST) via libcurl
- **Result type** with `?` error propagation operator
- **String interpolation** `f"hello {name}"`
- **Closures** and higher-order functions (map, filter)
- **Module system** with file-based imports
- **Process execution** and environment variable access
- **671+ tests**, AddressSanitizer + UBSan clean

## Quick Start

```bash
make
./agl examples/hello.agl
./agl                      # interactive REPL
```

## Hello World

```agl
print("Hello, World!")
```

## AI Agent Example

```agl
let api_key = env("ANTHROPIC_API_KEY")?

let body = json_stringify({
    "model": "claude-sonnet-4-20250514",
    "max_tokens": 1024,
    "messages": [{"role": "user", "content": "Hello!"}]
})

let headers = {
    "x-api-key": api_key,
    "content-type": "application/json",
    "anthropic-version": "2023-06-01"
}

let resp = http_post("https://api.anthropic.com/v1/messages", headers, body)?
let data = json_parse(resp["body"])?
print(data["content"][0]["text"])
```

## Language Overview

```agl
// Variables
let x = 42              // immutable
var count = 0           // mutable

// Functions
fn add(a: int, b: int) -> int {
    return a + b
}

// Control flow
if x > 0 { print("positive") }
while count < 10 { count = count + 1 }
for item in [1, 2, 3] { print(item) }

// Result type and error handling
let data = json_parse(text)?           // ? propagates errors
let result = match http_get(url, {}) {
    ok(resp) -> resp["body"]
    err(e) -> f"failed: {e}"
}

// Maps
let config = {"model": "claude-sonnet-4-20250514", "max_tokens": 1024}
let model = config["model"]
let updated = map_set(config, "temperature", 0)

// Lambdas and higher-order functions
let doubled = map([1, 2, 3], fn(x: int) -> int { return x * 2 })
let evens = filter([1, 2, 3, 4], fn(x: int) -> bool { return x % 2 == 0 })

// String interpolation
let name = "World"
print(f"Hello {name}!")

// Structs
struct Point { x: int; y: int }
let p = Point { x: 10, y: 20 }
print(p.x)
```

## Standard Library

| Category | Functions |
|----------|----------|
| I/O | `print` |
| Types | `type`, `len`, `str`, `int`, `float` |
| Arrays | `push`, `map`, `filter` |
| Math | `abs`, `min`, `max` |
| Maps | `map_get`, `map_set`, `map_keys`, `map_has`, `map_del` |
| Strings | `split`, `trim`, `contains`, `replace`, `starts_with`, `ends_with`, `to_upper`, `to_lower`, `join`, `substr` |
| JSON | `json_parse`, `json_stringify` |
| HTTP | `http_get`, `http_post` |
| File I/O | `read_file`, `write_file`, `file_exists` |
| Process | `exec` |
| Config | `env`, `env_default` |
| Time | `now`, `sleep` |

## Architecture

```
Source → Lexer → Parser → Sema → Compiler → VM
                                    |          |
                                AglChunk    Stack-based
                               (bytecode)   execution
```

## Building

**Requirements:**
- C11 compiler (clang or gcc)
- Optional: libcurl for HTTP support

```bash
make            # build (debug with ASan+UBSan)
make test       # run 671+ tests
make clean      # clean build artifacts
```

## Documentation

- [Language Specification](docs/spec.md)
- [Getting Started Tutorial](docs/tutorial.md)
- [Standard Library Reference](docs/stdlib.md)
- [Error Catalog](docs/errors.md)

## License

MIT
