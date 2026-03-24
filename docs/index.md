---
layout: default
title: Home
nav_order: 1
---

# AGL Documentation

**AGL (Agent Language)** is a programming language designed for AI agents. It compiles to bytecode and runs on a stack-based virtual machine, with built-in support for JSON, HTTP, process execution, and environment variables.

---

## Quick Start

```bash
git clone https://github.com/jayl2kor/ago.git
cd ago
make
./agl examples/hello.agl
```

---

## Hello World

```
print("Hello, World!")
```

---

## Agent Example

```
let api_key = env("ANTHROPIC_API_KEY")?

let body = json_stringify({
    "model": "claude-sonnet-4-20250514",
    "max_tokens": 256,
    "messages": [{"role": "user", "content": "Hello!"}]
})

let headers = map_set({}, "content-type", "application/json")
let headers = map_set(headers, "x-api-key", api_key)
let headers = map_set(headers, "anthropic-version", "2023-06-01")

let resp = http_post("https://api.anthropic.com/v1/messages", headers, body)?
let data = json_parse(resp["body"])?
print(data["content"][0]["text"])
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [Getting Started](tutorial) | Build, run, and write your first programs |
| [Language Specification](spec) | Complete grammar, types, and semantics |
| [Standard Library](stdlib) | All 40 built-in functions |
| [Error Catalog](errors) | Error codes and troubleshooting |

---

## Key Features

- **Bytecode VM** with 46 opcodes and mark-and-sweep GC
- **Maps** with `{}` literals and bracket access
- **JSON** parse/stringify built-in
- **HTTP** GET/POST via libcurl
- **Result type** with `?` error propagation
- **String interpolation** `f"Hello, {name}!"`
- **Closures** and higher-order functions
- **Module system** with file-based imports
- **671+ tests**, ASan + UBSan clean

---

## Error Handling

AGL uses a `result` type for safe error handling — no exceptions, no crashes.

```
// Result type
fn safe_divide(a: int, b: int) -> result {
    if b == 0 { return err("division by zero") }
    return ok(a / b)
}

// Pattern matching
let msg = match safe_divide(10, 0) {
    ok(v) -> str(v)
    err(e) -> f"Error: {e}"
}
print(msg)  // Error: division by zero

// ? operator for concise error propagation
fn process() -> result {
    let a = safe_divide(10, 2)?   // unwraps ok(5)
    let b = safe_divide(a, 0)?    // returns err("division by zero")
    return ok(b)
}
```

---

## Data Processing

Maps, JSON, and string operations make data manipulation straightforward.

```
// Build a request
let request = {
    "model": "claude-sonnet-4-20250514",
    "messages": [{"role": "user", "content": "Hello"}]
}
let json = json_stringify(request)

// Parse response
let data = json_parse(json)?
let model = data["model"]
print(f"Using {model}")

// String operations
let words = split("hello world foo", " ")
let upper = map(words, fn(w: string) -> string {
    return to_upper(substr(w, 0, 1)) + substr(w, 1, len(w) - 1)
})
print(join(upper, " "))  // Hello World Foo
```

---

## System Interaction

AGL can interact with the operating system — environment variables, processes, files, and HTTP.

```
// Environment variables
let home = env_default("HOME", "/tmp")

// Execute commands
let result = exec("ls", ["-la"])?
print(result["stdout"])

// File I/O
write_file("/tmp/hello.txt", "Hello from AGL!")?
let content = read_file("/tmp/hello.txt")?
print(content)

// HTTP requests
let resp = http_get("https://api.example.com/data", {})?
print(f"Status: {resp[\"status\"]}")
```

---

## Functions and Closures

First-class functions, closures, and higher-order functions.

```
// Higher-order functions
let numbers = [1, 2, 3, 4, 5]
let evens = filter(numbers, fn(x: int) -> bool { return x % 2 == 0 })
let doubled = map(evens, fn(x: int) -> int { return x * 2 })
print(doubled)  // [4, 8]

// Closures
fn make_counter() -> fn {
    var count = 0
    return fn() -> int {
        count = count + 1
        return count
    }
}
let counter = make_counter()
print(counter())  // 1
print(counter())  // 2
```
