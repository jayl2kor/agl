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
