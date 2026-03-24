---
layout: default
title: Standard Library
nav_order: 4
---

# AGL Standard Library Reference

## 1. I/O

### print

```
print(args...) -> nil
```

Prints each argument to stdout, separated by nothing, followed by a newline. Accepts any number of arguments of any type. Arrays are printed as `[a, b, c]`, structs as `<struct TypeName>`, results as `ok(...)` or `err(...)`.

```ago
print("hello")          // hello
print(42)               // 42
print("x =", 10)        // x =10
print([1, 2, 3])        // [1, 2, 3]
```

**Errors:** None.

---

## 2. Type Inspection

### type

```
type(val: any) -> string
```

Returns the type name of `val` as a string.

```ago
type(42)            // "int"
type(3.14)          // "float"
type(true)          // "bool"
type("hello")       // "string"
type([1, 2])        // "array"
type(fn() {})       // "fn"
type(ok(1))         // "result"
type(nil)           // "nil"
type({})            // "map"
```

**Errors:** Requires exactly 1 argument.

### len

```
len(x: array | string | map) -> int
```

Returns the number of elements in an array or map, or the length of a string.

```ago
len([1, 2, 3])      // 3
len("hello")         // 5
len([])              // 0
```

```ago
// Practical: validate input length before processing
let password = "abc"
if len(password) < 8 {
    print("Password must be at least 8 characters")
}

let items = ["a", "b", "c"]
print(f"Processing {len(items)} items")  // Processing 3 items
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not an array, string, or map.

---

## 3. Type Conversion

### str

```
str(val: any) -> string
```

Converts any value to its string representation. If the value is already a string, returns it unchanged.

```ago
str(42)             // "42"
str(3.14)           // "3.14"
str(true)           // "true"
str(nil)            // "nil"
str([1, 2])         // "<array[2]>"
```

```ago
// Practical: build log messages from mixed types
let count = 42
let msg = "Found " + str(count) + " results"
print(msg)          // Found 42 results
```

**Errors:** Requires exactly 1 argument.

### int

```
int(x: string | float | int) -> int
```

Converts a value to an integer. Strings are parsed as base-10 integers. Floats are truncated toward zero. Integers are returned unchanged.

```ago
int("123")          // 123
int(3.7)            // 3
int(42)             // 42
```

```ago
// Practical: parse numeric user input
let port_str = "8080"
let port = int(port_str)
print(f"Listening on port {port}")  // Listening on port 8080
```

**Errors:**
- Requires exactly 1 argument.
- RuntimeError if string is not a valid integer (`"abc"`, `""`).
- TypeError if argument is not a string, float, or int.

### float

```
float(x: string | int | float) -> float
```

Converts a value to a float. Strings are parsed as floating-point numbers. Integers are widened. Floats are returned unchanged.

```ago
float("2.5")        // 2.5
float(42)           // 42.0
float(3.14)         // 3.14
```

**Errors:**
- Requires exactly 1 argument.
- RuntimeError if string is not a valid number.
- TypeError if argument is not a string, int, or float.

---

## 4. Arrays

### push

```
push(arr: array, val: any) -> array
```

Returns a new array with `val` appended to the end of `arr`. The original array is not modified.

```ago
let a = [1, 2, 3]
let b = push(a, 4)     // [1, 2, 3, 4]
print(a)                // [1, 2, 3]  (unchanged)
```

```ago
// Practical: accumulate results in a loop
let results = []
for i in [1, 2, 3, 4, 5] {
    if i % 2 == 0 {
        let results = push(results, i * 10)
    }
}
print(results)          // [20, 40]
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not an array.
- RuntimeError if array size would exceed 1024 elements.

### map

```
map(arr: array, fn: fn) -> array
```

Returns a new array where each element is the result of calling `fn` on the corresponding element of `arr`.

```ago
let nums = [1, 2, 3]
let doubled = map(nums, fn(x: int) -> int { return x * 2 })
print(doubled)          // [2, 4, 6]
```

```ago
// Practical: transform string data
let names = ["alice", "bob", "charlie"]
let upper = map(names, fn(n: string) -> string { return to_upper(n) })
print(upper)            // ["ALICE", "BOB", "CHARLIE"]

// Practical: extract fields from structured data
let prices = [100, 250, 50]
let with_tax = map(prices, fn(p: int) -> float {
    return float(p) * 1.1
})
print(with_tax)         // [110.0, 275.0, 55.0]
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not an array or second is not a function.
- Propagates any error raised by `fn`.

### filter

```
filter(arr: array, fn: fn) -> array
```

Returns a new array containing only the elements of `arr` for which `fn` returns a truthy value.

```ago
let nums = [1, 2, 3, 4, 5]
let evens = filter(nums, fn(x: int) -> bool { return x % 2 == 0 })
print(evens)            // [2, 4]
```

```ago
// Practical: filter strings by condition
let names = ["alice", "bob", "charlie", "dan"]
let long_names = filter(names, fn(n: string) -> bool { return len(n) > 3 })
print(long_names)       // ["alice", "charlie"]

// Practical: chain map and filter
let scores = [85, 42, 91, 67, 73, 55]
let passing = filter(scores, fn(s: int) -> bool { return s >= 70 })
let grades = map(passing, fn(s: int) -> string {
    if s >= 90 { return "A" }
    if s >= 80 { return "B" }
    return "C"
})
print(grades)           // ["B", "A", "C"]
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not an array or second is not a function.
- Propagates any error raised by `fn`.

---

## 5. Strings

### split

```
split(s: string, sep: string) -> array
```

Splits a string into an array of substrings at each occurrence of `sep`.

```ago
split("a,b,c", ",")    // ["a", "b", "c"]
split("hello world", " ")  // ["hello", "world"]
```

```ago
// Practical: parse CSV-style data
let csv = "name,age,city"
let headers = split(csv, ",")
print(headers)          // ["name", "age", "city"]
print(len(headers))     // 3

// Practical: parse key=value pairs
let line = "host=localhost"
let parts = split(line, "=")
print(parts[0])         // host
print(parts[1])         // localhost
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if either argument is not a string.
- RuntimeError if separator is empty.

### trim

```
trim(s: string) -> string
```

Removes leading and trailing whitespace (spaces, tabs, newlines, carriage returns) from a string.

```ago
trim("  hello  ")       // "hello"
trim("\thello\n")        // "hello"
```

```ago
// Practical: clean user input or command output
let text = "  Hello, World!  "
print(trim(text))       // Hello, World!

let raw = "\n  some data \r\n"
let clean = trim(raw)
print(clean)            // some data
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a string.

### contains

```
contains(s: string, sub: string) -> bool
```

Returns `true` if `s` contains the substring `sub`, `false` otherwise. An empty substring always returns `true`.

```ago
contains("hello world", "world")   // true
contains("hello world", "xyz")     // false
contains("hello", "")              // true
```

```ago
// Practical: search and branch on content
let log_line = "2026-03-25 ERROR: connection refused"
if contains(log_line, "ERROR") {
    print("Error detected in log")
}
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if either argument is not a string.

### replace

```
replace(s: string, old: string, new: string) -> string
```

Returns a new string with all occurrences of `old` replaced by `new`. If `old` is empty, returns the original string unchanged.

```ago
replace("hello world", "world", "ago")   // "hello ago"
replace("aaa", "a", "bb")                // "bbbbbb"
```

```ago
// Practical: URL and path manipulation
let url = "https://example.com/path"
print(replace(url, "https", "http"))  // http://example.com/path

// Practical: template substitution
let template = "Hello, {name}! Welcome to {place}."
let msg = replace(replace(template, "{name}", "Alice"), "{place}", "Ago")
print(msg)  // Hello, Alice! Welcome to Ago.
```

**Errors:**
- Requires exactly 3 arguments.
- TypeError if any argument is not a string.

### starts_with

```
starts_with(s: string, prefix: string) -> bool
```

Returns `true` if `s` begins with `prefix`.

```ago
starts_with("hello", "hel")    // true
starts_with("hello", "xyz")    // false
```

```ago
// Practical: protocol detection
let url = "https://example.com/api"
if starts_with(url, "https") {
    print("Secure connection")  // Secure connection
}
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if either argument is not a string.

### ends_with

```
ends_with(s: string, suffix: string) -> bool
```

Returns `true` if `s` ends with `suffix`.

```ago
ends_with("hello.txt", ".txt")   // true
ends_with("hello.txt", ".csv")   // false
```

```ago
// Practical: file type checking
let filename = "report.json"
if ends_with(filename, ".json") {
    print("JSON file detected")  // JSON file detected
}
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if either argument is not a string.

### to_upper

```
to_upper(s: string) -> string
```

Returns a new string with all ASCII lowercase letters converted to uppercase.

```ago
to_upper("hello")       // "HELLO"
to_upper("Hello World") // "HELLO WORLD"
to_upper("abc123")      // "ABC123"
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a string.

### to_lower

```
to_lower(s: string) -> string
```

Returns a new string with all ASCII uppercase letters converted to lowercase.

```ago
to_lower("HELLO")       // "hello"
to_lower("Hello World") // "hello world"
to_lower("ABC123")      // "abc123"
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a string.

### join

```
join(arr: array, sep: string) -> string
```

Joins an array of strings into a single string with `sep` between each element.

```ago
join(["a", "b", "c"], ",")      // "a,b,c"
join(["hello", "world"], " ")   // "hello world"
join(["one"], "-")               // "one"
```

```ago
// Practical: build a CSV line
let fields = ["Alice", "30", "NYC"]
let csv_line = join(fields, ",")
print(csv_line)         // Alice,30,NYC

// Practical: build a file path
let parts = ["home", "user", "docs", "file.txt"]
let path = join(parts, "/")
print(path)             // home/user/docs/file.txt
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not an array or second is not a string.
- TypeError if any array element is not a string.

### substr

```
substr(s: string, start: int, length: int) -> string
```

Returns a substring of `s` starting at index `start` with the given `length`. Out-of-range values are clamped to valid bounds.

```ago
substr("hello world", 0, 5)   // "hello"
substr("hello world", 6, 5)   // "world"
substr("hello", 1, 3)         // "ell"
```

```ago
// Practical: extract parts of a formatted string
let timestamp = "2026-03-25T14:30:00"
let date = substr(timestamp, 0, 10)
let time = substr(timestamp, 11, 8)
print(date)             // 2026-03-25
print(time)             // 14:30:00
```

**Errors:**
- Requires exactly 3 arguments.
- TypeError if first argument is not a string or second/third are not integers.

---

## 6. Math

### abs

```
abs(n: int | float) -> int | float
```

Returns the absolute value of a number. The return type matches the input type.

```ago
abs(-42)            // 42
abs(3.14)           // 3.14
abs(-2.5)           // 2.5
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a number.

### min

```
min(a: int | float, b: int | float) -> int | float
```

Returns the smaller of two numbers. Both arguments must be the same numeric type.

```ago
min(10, 3)          // 3
min(2.5, 1.0)       // 1.0
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if arguments are not two numbers of the same type (no int/float mixing).

### max

```
max(a: int | float, b: int | float) -> int | float
```

Returns the larger of two numbers. Both arguments must be the same numeric type.

```ago
max(10, 3)          // 10
max(2.5, 1.0)       // 2.5
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if arguments are not two numbers of the same type (no int/float mixing).

---

## 7. Result Constructors

`ok` and `err` are syntax forms, not functions. They wrap a value into a `result` type for structured error handling.

### ok

```
ok(expr) -> result
```

Wraps a value as a successful result.

### err

```
err(expr) -> result
```

Wraps a value as an error result.

### match

Results are unwrapped with the `match` expression:

```ago
fn safe_div(a: int, b: int) -> result {
    if b == 0 {
        return err("division by zero")
    }
    return ok(a / b)
}

match safe_div(10, 3) {
    ok(v) -> print(v)       // 3
    err(e) -> print(e)
}

match safe_div(10, 0) {
    ok(v) -> print(v)
    err(e) -> print(e)      // division by zero
}
```

**Errors:**
- `match` requires the subject to be a `result` value (TypeError otherwise).

---

## 8. Maps

### map_get

```
map_get(m: map, key: string) -> any
```

Returns the value associated with `key` in map `m`, or `nil` if the key does not exist.

```ago
let m = map_set(map_set({}, "name", "Alice"), "age", 30)
print(map_get(m, "name"))     // Alice
print(map_get(m, "age"))      // 30
print(map_get(m, "missing"))  // nil
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not a map or second is not a string.

### map_set

```
map_set(m: map, key: string, val: any) -> map
```

Returns a new map with `key` set to `val`. If the key already exists, its value is updated. The original map is not modified.

```ago
let m = {}
let m = map_set(m, "host", "localhost")
let m = map_set(m, "port", 8080)
print(map_get(m, "host"))     // localhost
print(map_get(m, "port"))     // 8080
```

```ago
// Practical: build a configuration map step by step
let config = {}
let config = map_set(config, "host", "localhost")
let config = map_set(config, "port", 8080)
let config = map_set(config, "debug", true)
print(map_keys(config))       // ["host", "port", "debug"]

// Updating an existing key returns a new map
let config = map_set(config, "port", 9090)
print(map_get(config, "port"))  // 9090
```

**Errors:**
- Requires exactly 3 arguments.
- TypeError if first argument is not a map or second is not a string.

### map_has

```
map_has(m: map, key: string) -> bool
```

Returns `true` if `key` exists in map `m`, `false` otherwise.

```ago
let m = map_set({}, "name", "Alice")
print(map_has(m, "name"))     // true
print(map_has(m, "email"))    // false
```

```ago
// Practical: conditional logic based on map contents
let headers = map_set({}, "Content-Type", "application/json")
if map_has(headers, "Authorization") {
    print("Authenticated request")
} else {
    print("Anonymous request")  // Anonymous request
}
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not a map or second is not a string.

### map_keys

```
map_keys(m: map) -> array
```

Returns an array of all keys in the map as strings.

```ago
let m = map_set(map_set({}, "a", 1), "b", 2)
print(map_keys(m))            // ["a", "b"]
print(map_keys({}))           // []
```

```ago
// Practical: iterate over map entries
let scores = map_set(map_set(map_set({}, "Alice", 95), "Bob", 87), "Carol", 92)
let names = map_keys(scores)
for name in names {
    print(f"{name}: {map_get(scores, name)}")
}
// Alice: 95
// Bob: 87
// Carol: 92
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a map.

### map_del

```
map_del(m: map, key: string) -> map
```

Returns a new map with `key` removed. If the key does not exist, returns a copy of the original map. The original map is not modified.

```ago
let m = map_set(map_set({}, "a", 1), "b", 2)
let m2 = map_del(m, "a")
print(map_keys(m2))           // ["b"]
print(map_keys(m))            // ["a", "b"]  (unchanged)
```

```ago
// Practical: remove sensitive data before logging
let request = map_set(map_set({}, "user", "alice"), "token", "secret123")
let safe = map_del(request, "token")
print(safe)                   // {"user": "alice"}
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not a map or second is not a string.

---

## 9. File I/O

### read_file

```
read_file(path: string) -> result
```

Reads the entire contents of a file. Returns `ok(content)` on success or `err(message)` on failure.

```ago
match read_file("data.txt") {
    ok(content) -> print(content)
    err(e) -> print("Error: " + e)
}
```

```ago
// Practical: read, parse, and use JSON config
write_file("/tmp/config.json", "{\"key\": \"value\", \"count\": 42}")?
let content = read_file("/tmp/config.json")?
let config = json_parse(content)?
print(map_get(config, "key"))      // value
print(map_get(config, "count"))    // 42
```

```ago
// Practical: handle missing file gracefully
let result = read_file("/nonexistent")
let msg = match result {
    ok(data) -> data
    err(e) -> f"File error: {e}"
}
print(msg)  // File error: cannot read file
```

**Errors (returned as `err`):**
- `"cannot read file"` -- file does not exist or is not readable.
- `"file too large"` -- file exceeds 10 MB.

**Errors (raised as runtime errors):**
- TypeError if argument is not a string.
- Requires exactly 1 argument.

### write_file

```
write_file(path: string, content: string) -> result
```

Writes `content` to a file, overwriting any existing content. Returns `ok(true)` on success or `err(message)` on failure.

```ago
match write_file("out.txt", "hello world") {
    ok(_) -> print("written")
    err(e) -> print("Error: " + e)
}
```

```ago
// Practical: write structured data to a file
let data = map_set(map_set({}, "name", "Alice"), "score", 95)
let json = json_stringify(data)
write_file("/tmp/result.json", json)?
print("Saved: " + json)  // Saved: {"name":"Alice","score":95}
```

**Errors (returned as `err`):**
- `"cannot write file"` -- path is not writable.

**Errors (raised as runtime errors):**
- TypeError if either argument is not a string.
- Requires exactly 2 arguments.

### file_exists

```
file_exists(path: string) -> bool
```

Returns `true` if the file at `path` exists and is readable, `false` otherwise.

```ago
if file_exists("config.agl") {
    print("found config")
}
```

```ago
// Practical: load config with a fallback
let config_path = "settings.json"
if file_exists(config_path) {
    let raw = read_file(config_path)?
    let config = json_parse(raw)?
    print("Loaded config")
} else {
    print("Using defaults")
}
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a string.

---

## 10. JSON

### json_parse

```
json_parse(s: string) -> result
```

Parses a JSON string into an AGL value. Objects become maps, arrays become arrays, strings/numbers/booleans/null map to their AGL equivalents. Returns a `result`.

```ago
let data = json_parse("{\"name\": \"Alice\", \"age\": 30}")?
print(map_get(data, "name"))   // Alice
print(map_get(data, "age"))    // 30
```

```ago
// Practical: parse nested JSON structures
let json = "{\"users\": [{\"name\": \"Alice\", \"age\": 30}, {\"name\": \"Bob\", \"age\": 25}]}"
let data = json_parse(json)?
let users = map_get(data, "users")
print(len(users))             // 2
print(map_get(users[0], "name"))  // Alice
print(map_get(users[1], "age"))   // 25

// Practical: parse a JSON array
let arr = json_parse("[1, 2, 3, 4, 5]")?
print(len(arr))               // 5
print(arr[0])                 // 1
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a string.

### json_stringify

```
json_stringify(val: any) -> string
```

Converts an AGL value to a JSON string. Maps become objects, arrays become arrays, strings/numbers/booleans/nil map to their JSON equivalents.

```ago
let obj = map_set(map_set({}, "status", "ok"), "count", 42)
print(json_stringify(obj))    // {"status":"ok","count":42}
```

```ago
// Practical: serialize data for file storage or API calls
let payload = map_set(map_set({}, "action", "greet"), "message", "hello")
let json = json_stringify(payload)
print(json)                   // {"action":"greet","message":"hello"}

// Practical: pretty-print arrays
let items = [1, "two", true, nil]
print(json_stringify(items))  // [1,"two",true,null]
```

**Errors:**
- Requires exactly 1 argument.

---

## 11. Environment

### env

```
env(name: string) -> result
```

Reads an environment variable. Returns `ok(value)` if the variable is set, or `err("not set")` if it is not.

```ago
match env("HOME") {
    ok(home) -> print("Home: " + home)
    err(e) -> print("HOME not set")
}
```

```ago
// Practical: require an environment variable
let api_key = match env("API_KEY") {
    ok(key) -> key
    err(_) -> {
        print("ERROR: API_KEY must be set")
        return
    }
}
print("Using API key: " + substr(api_key, 0, 4) + "...")
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a string.

### env_default

```
env_default(name: string, fallback: string) -> string
```

Reads an environment variable, returning `fallback` if the variable is not set. Unlike `env()`, this returns a plain string (not a result).

```ago
let host = env_default("HOST", "localhost")
let port = env_default("PORT", "8080")
print(f"Server: {host}:{port}")  // Server: localhost:8080
```

```ago
// Practical: application configuration from environment
let db_url = env_default("DATABASE_URL", "postgres://localhost/dev")
let debug = env_default("DEBUG", "false")
let log_level = env_default("LOG_LEVEL", "info")
print(f"DB: {db_url}")         // DB: postgres://localhost/dev
print(f"Debug: {debug}")       // Debug: false
print(f"Log level: {log_level}")  // Log level: info
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if either argument is not a string.

---

## 12. HTTP

### http_get

```
http_get(url: string, headers: map) -> result
```

Sends an HTTP GET request. Returns `ok(response_map)` on success or `err(message)` on failure. The response map contains:
- `"status"` -- HTTP status code (int)
- `"body"` -- response body (string)
- `"headers"` -- response headers (map)

```ago
let resp = http_get("https://httpbin.org/get", {})?
print(map_get(resp, "status"))  // 200
print(map_get(resp, "body"))    // {"url": "https://httpbin.org/get", ...}
```

```ago
// Practical: API call with authentication
let headers = map_set({}, "Authorization", "Bearer token123")
let headers = map_set(headers, "Accept", "application/json")
let resp = http_get("https://api.example.com/users", headers)?

let status = map_get(resp, "status")
if status == 200 {
    let users = json_parse(map_get(resp, "body"))?
    print(f"Found {len(users)} users")
} else {
    print(f"Request failed with status {status}")
}
```

**Errors (returned as `err`):**
- Network errors, DNS resolution failures, timeouts (30s), TLS errors.

**Errors (raised as runtime errors):**
- Requires exactly 2 arguments.
- TypeError if first argument is not a string or second is not a map.

**Note:** Requires libcurl. If not available, returns `err("HTTP not available: libcurl required")`.

### http_post

```
http_post(url: string, headers: map, body: string) -> result
```

Sends an HTTP POST request. Returns `ok(response_map)` on success or `err(message)` on failure. The response map has the same structure as `http_get`.

```ago
let headers = map_set({}, "Content-Type", "application/json")
let body = json_stringify(map_set({}, "name", "Alice"))
let resp = http_post("https://httpbin.org/post", headers, body)?
print(map_get(resp, "status"))  // 200
```

```ago
// Practical: send data to an API
let headers = map_set({}, "Content-Type", "application/json")
let headers = map_set(headers, "Authorization", "Bearer mytoken")
let payload = map_set(map_set({}, "title", "New Task"), "done", false)
let body = json_stringify(payload)

let resp = http_post("https://api.example.com/tasks", headers, body)?
let status = map_get(resp, "status")
if status == 201 {
    let created = json_parse(map_get(resp, "body"))?
    print(f"Created task with ID: {map_get(created, \"id\")}")
} else {
    print(f"Failed: HTTP {status}")
}
```

**Errors (returned as `err`):**
- Same as `http_get`.

**Errors (raised as runtime errors):**
- Requires exactly 3 arguments.
- TypeError if first argument is not a string, second is not a map, or third is not a string.

**Note:** Requires libcurl. If not available, returns `err("HTTP not available: libcurl required")`.

---

## 13. Process Execution

### exec

```
exec(cmd: string, args: array) -> result
```

Executes an external command with the given arguments. Returns `ok(result_map)` on success or `err(message)` on failure. The result map contains:
- `"stdout"` -- captured standard output (string)
- `"stderr"` -- captured standard error (string)
- `"status"` -- exit code (int, 0 = success)

```ago
let result = exec("echo", ["hello"])?
print(map_get(result, "stdout"))   // hello\n
print(map_get(result, "status"))   // 0
```

```ago
// Practical: capture and use command output
let result = exec("date", ["+%Y-%m-%d"])?
let today = trim(map_get(result, "stdout"))
print(f"Today is {today}")        // Today is 2026-03-25
```

```ago
// Practical: check git status
let result = exec("git", ["status", "--porcelain"])?
let stdout = trim(map_get(result, "stdout"))
if map_get(result, "status") == 0 {
    if len(stdout) == 0 {
        print("Git repo is clean")
    } else {
        print("Uncommitted changes detected")
    }
}
```

```ago
// Practical: handle command failure
let result = exec("ls", ["/nonexistent"])
match result {
    ok(r) -> {
        if map_get(r, "status") != 0 {
            print("Command failed: " + trim(map_get(r, "stderr")))
        }
    }
    err(e) -> print("Exec error: " + e)
}
```

**Errors (returned as `err`):**
- `"fork failed"` -- system could not create a child process.
- `"failed to create pipes"` -- pipe creation failure.
- `"out of memory"` -- memory allocation failure.

**Errors (raised as runtime errors):**
- Requires exactly 2 arguments.
- TypeError if first argument is not a string or second is not an array.

---

## 14. Time

### now

```
now() -> int
```

Returns the current time as Unix epoch milliseconds.

```ago
let start = now()
// ... do some work ...
let elapsed = now() - start
print(f"Took {elapsed} ms")
```

**Errors:** Requires exactly 0 arguments.

### sleep

```
sleep(ms: int) -> nil
```

Pauses execution for `ms` milliseconds.

```ago
print("Starting...")
sleep(1000)
print("1 second later")
```

```ago
// Practical: retry with backoff
fn fetch_with_retry(url: string, max_retries: int) -> result {
    for i in [0, 1, 2] {
        let resp = http_get(url, {})
        let val = match resp {
            ok(r) -> {
                if map_get(r, "status") == 200 {
                    return ok(r)
                }
                nil
            }
            err(_) -> nil
        }
        if i < max_retries - 1 {
            sleep(1000 * (i + 1))
        }
    }
    return err("max retries exceeded")
}
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not an integer.
