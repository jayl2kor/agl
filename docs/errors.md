---
layout: default
title: Error Catalog
nav_order: 5
---

# AGL Error Catalog

## Error Output Format

Errors are printed to stderr in the following format:

```
file:line:col: error: message
```

When no file context is available (e.g., runtime errors):

```
error: message
```

### Stack Traces

Errors occurring inside function calls include a stack trace, printed immediately after the error line. Frames are listed innermost-first (most recent call at the top):

```
error: division by zero
  in inner() (line 5)
  in outer() (line 10)
```

Anonymous functions (lambdas) appear as `<lambda>`:

```
error: index 5 out of bounds (length 3)
  in <lambda> (line 8)
  in process() (line 12)
```

Maximum trace depth is 16 frames.

#### Stack Trace Example

Nested function calls produce a full call stack when an error occurs:

```
// Nested function error shows call stack:
fn divide(a: int, b: int) -> int {
    return a / b
}

fn average(a: int, b: int) -> int {
    return divide(a + b, 0)
}

average(10, 20)
```

```
// Output:
// error: division by zero
//   in divide() (line 3)
//   in average() (line 7)
```

Lambdas passed to higher-order functions also appear in the trace:

```
let nums = [1, 2, 0, 4]
let inverted = map(nums, fn(x: int) -> int {
    return 100 / x
})
```

```
// Output:
// error: division by zero
//   in <lambda> (line 3)
```

---

## Error Categories

### AGL_ERR_SYNTAX -- Syntax Errors

Produced by the lexer and parser during source analysis.

#### Lexer Errors

| Message | Cause | Fix |
|---------|-------|-----|
| `unterminated string literal` | String opened with `"` but never closed, or newline inside string. | Close the string with `"` on the same line. |
| `unexpected character` | Unrecognized character in source. | Remove the character or replace with valid syntax. |
| `unexpected character '&', did you mean '&&'?` | Single `&` used instead of logical AND. | Use `&&` for logical AND. |
| `unexpected character '\|', did you mean '\|\|'?` | Single `\|` used instead of logical OR. | Use `\|\|` for logical OR. |

#### Parser Errors

| Message | Cause | Fix |
|---------|-------|-----|
| `expected TYPE, got TOKEN` | A required token was not found. Examples: `expected ')'`, `expected '{'`, `expected ':'`. | Add the missing token at the indicated location. |
| `expected type name, got TOKEN` | A type annotation position has a non-type token. | Provide a valid type name (`int`, `string`, `fn`, etc.). |
| `expected parameter name` | Non-identifier in function parameter list. | Use an identifier for the parameter name. |
| `expected variable name` | `let` or `var` not followed by an identifier. | Provide a variable name after `let`/`var`. |
| `expected function name` | `fn` at top level not followed by an identifier. | Provide a function name after `fn`. |
| `expected struct name` | `struct` not followed by an identifier. | Provide a struct name after `struct`. |
| `expected field name` | Non-identifier inside struct literal or declaration. | Use an identifier for field names. |
| `expected field name after '.'` | Dot operator not followed by an identifier. | Provide a field name after `.`. |
| `expected module path after 'import'` | `import` not followed by a string literal. | Use `import "path"` syntax. |
| `expected binding name` | Match arm `ok(...)` or `err(...)` missing an identifier. | Provide a binding name: `ok(v) ->`. |
| `expected 'ok' or 'err' arm in match` | Match expression body contains something other than ok/err arms. | Ensure match has exactly one `ok` and one `err` arm. |
| `duplicate 'ok' arm in match` | Two `ok` arms in the same match expression. | Remove the duplicate arm. |
| `duplicate 'err' arm in match` | Two `err` arms in the same match expression. | Remove the duplicate arm. |
| `invalid assignment target` | Left side of `=` is not a simple identifier. | Assign only to variable names (not expressions). |
| `unexpected token 'TOKEN'` | Token found where an expression was expected. | Check for missing operands or misplaced keywords. |
| `too many parameters (max 64)` | Function declaration exceeds parameter limit. | Reduce the number of parameters. |
| `too many arguments (max 128)` | Function call exceeds argument limit. | Reduce the number of arguments. |
| `too many array elements (max 128)` | Array literal exceeds element limit. | Split into multiple arrays. |
| `too many struct fields (max 64)` | Struct declaration or literal exceeds field limit. | Reduce the number of fields. |
| `too many statements in block (max 256)` | Block contains too many statements. | Extract code into separate functions. |
| `too many top-level declarations (max 512)` | Source file exceeds declaration limit. | Split into multiple modules. |

---

### AGL_ERR_NAME -- Name Errors

Produced by semantic analysis and the interpreter for undefined or duplicate names.

| Message | Cause | Fix |
|---------|-------|-----|
| `undefined variable 'x'` | Variable `x` used but never declared. | Declare with `let` or `var` before use, or fix the spelling. |
| `unknown function 'foo'` | Called `foo()` but no function with that name exists. | Define the function or check the name spelling. |
| `no field 'f'` | Accessed `.f` on a struct that has no field named `f`. | Check the struct definition for valid field names. |

#### Example: Undefined Variable

```
// This produces an error:
print(x)  // error: undefined variable 'x'
```

```
// Fix: declare the variable before use
let x = 42
print(x)
```

#### Example: Unknown Function

```
// This produces an error:
let result = compute(10)  // error: unknown function 'compute'
```

```
// Fix: define the function before calling it
fn compute(n: int) -> int {
    return n * 2
}
let result = compute(10)
```

#### Example: No Such Field

```
// This produces an error:
struct Point {
    x: int
    y: int
}
let p = Point { x: 10, y: 20 }
print(p.z)  // error: no field 'z'
```

```
// Fix: use a field name that exists on the struct
struct Point {
    x: int
    y: int
}
let p = Point { x: 10, y: 20 }
print(p.x)
```

---

### AGL_ERR_TYPE -- Type Errors

Produced by semantic analysis and the interpreter for type mismatches.

| Message | Cause | Fix |
|---------|-------|-----|
| `cannot assign to immutable variable 'x'` | Assignment to a `let`-bound variable. | Change the declaration to `var` if mutation is needed. |
| `invalid unary operator` | Unary `-` on non-number, or `!` on non-bool. | Apply `-` to `int`/`float` and `!` to `bool` only. |
| `invalid binary operation` | Binary operator applied to incompatible types. | Ensure both operands have compatible types for the operator. |
| `cannot access field on non-struct value` | Dot access on a value that is not a struct. | Only use `.field` on struct values. |
| `cannot index non-array value` | Index `[n]` on a value that is not an array. | Only use `[index]` on array values. |
| `array index must be an integer` | Non-integer used as array index. | Use an `int` value as the index. |
| `match requires a result value` | `match` applied to a non-result value. | Only match on values produced by `ok()` or `err()`. |
| `'?' operator requires a result value` | `?` applied to a non-result value. | Only use `?` on values produced by `ok()` or `err()`. |
| `expression is not callable` | Attempted to call a non-function value with `()`. | Ensure the callee is a function. |
| `for-in requires an array` | `for x in val` where `val` is not an array. | Only iterate over array values. |
| `expected N arguments, got M` | Function call arity mismatch (detected by sema). | Pass the correct number of arguments. |
| `len() requires an array or string` | `len()` called on an incompatible type. | Pass an array or string to `len()`. |
| `int() cannot convert this type` | `int()` called on a non-convertible type. | Pass a `string`, `float`, or `int`. |
| `float() cannot convert this type` | `float()` called on a non-convertible type. | Pass a `string`, `int`, or `float`. |
| `abs() requires a number` | `abs()` called on a non-numeric value. | Pass an `int` or `float`. |
| `min() requires two numbers of the same type` | `min()` called with mismatched or non-numeric types. | Pass two `int` values or two `float` values. |
| `max() requires two numbers of the same type` | `max()` called with mismatched or non-numeric types. | Pass two `int` values or two `float` values. |
| `push() first argument must be an array` | `push()` called with non-array first argument. | Pass an array as the first argument. |
| `map() requires (array, fn)` | `map()` called with wrong argument types. | Pass `(array, fn)`. |
| `filter() requires (array, fn)` | `filter()` called with wrong argument types. | Pass `(array, fn)`. |
| `read_file() requires a string path` | `read_file()` called with non-string argument. | Pass a string path. |
| `file_exists() requires a string path` | `file_exists()` called with non-string argument. | Pass a string path. |
| `write_file() requires (string, string)` | `write_file()` called with wrong argument types. | Pass `(path_string, content_string)`. |

#### Example: Immutable Assignment

```
// This produces an error:
let x = 10
x = 20  // error: cannot assign to immutable variable 'x'
```

```
// Fix: use var instead of let for mutable variables
var x = 10
x = 20
```

#### Example: Type Mismatch in Operations

```
// This produces an error:
let result = 42 + "hello"  // error: invalid binary operation
```

```
// Fix: convert to the same type before operating
let result = str(42) + " hello"
```

```
// Also an error -- mixing int and float:
let sum = 10 + 2.5  // error: invalid binary operation
```

```
// Fix: use the same numeric type
let sum = 10.0 + 2.5
```

#### Example: Match on Non-Result

```
// This produces an error:
let x = 42
match x {               // error: match requires a result value
    ok(v) -> print(v)
    err(e) -> print(e)
}
```

```
// Fix: match only on result values
fn safe_div(a: int, b: int) -> result {
    if b == 0 {
        return err("division by zero")
    }
    return ok(a / b)
}
match safe_div(10, 3) {
    ok(v) -> print(v)
    err(e) -> print(e)
}
```

#### Example: ? on Non-Result

```
// This produces an error:
let x = 42
let y = x?  // error: '?' operator requires a result value
```

```
// Fix: use ? only on result values returned by ok() or err()
fn parse_number(s: string) -> result {
    let n = int(s)
    return ok(n)
}
fn process() -> result {
    let val = parse_number("42")?  // unwraps ok, propagates err
    return ok(val + 1)
}
```

#### Example: For-In on Non-Array

```
// This produces an error:
let name = "hello"
for ch in name {        // error: for-in requires an array
    print(ch)
}
```

```
// Fix: iterate only over arrays
let letters = ["h", "e", "l", "l", "o"]
for ch in letters {
    print(ch)
}
```

#### Example: Wrong Argument Count

```
// This produces an error:
fn add(a: int, b: int) -> int {
    return a + b
}
print(add(1))        // error: expected 2 arguments, got 1
```

```
// Fix: pass the correct number of arguments
fn add(a: int, b: int) -> int {
    return a + b
}
print(add(1, 2))
```

---

### AGL_ERR_RUNTIME -- Runtime Errors

Produced by the interpreter during execution.

| Message | Cause | Fix |
|---------|-------|-----|
| `division by zero` | Integer division or modulo by zero. | Check the divisor before dividing. |
| `index N out of bounds (length M)` | Array index `N` is negative or >= `M`. | Ensure `0 <= index < len(array)`. |
| `cannot assign to immutable variable 'x'` | Runtime assignment to a `let` binding. | Change to `var` if mutation is needed. |
| `expected N arguments, got M` | Function called with wrong number of arguments. | Match the function's parameter count. |
| `maximum call depth exceeded (limit 512)` | Infinite recursion or deep call chain. | Add a base case to recursive functions. |
| `too many variables (max 256)` | Environment exceeded variable slot limit. | Reduce variable count or restructure code. |
| `too many modules (max 64)` | Import limit exceeded. | Reduce the number of imported modules. |
| `string too large` | String concatenation result exceeds size limit. | Reduce string sizes. |
| `out of memory` | Arena or heap allocation failed. | Reduce program memory usage. |
| `array size limit exceeded (max 1024)` | `push()` would exceed maximum array size. | Use smaller arrays or restructure data. |
| `int() invalid integer string` | `int("abc")` -- string cannot be parsed as integer. | Pass a valid numeric string. |
| `float() invalid number string` | `float("abc")` -- string cannot be parsed as float. | Pass a valid numeric string. |
| `unsupported expression type` | Internal: AST node not handled by evaluator. | Report as a bug. |
| `unsupported statement type` | Internal: AST node not handled by executor. | Report as a bug. |

#### Example: Division by Zero

```
// This produces an error:
let ratio = 100 / 0  // error: division by zero
```

```
// Fix: guard against zero before dividing
fn safe_div(a: int, b: int) -> result {
    if b == 0 {
        return err("cannot divide by zero")
    }
    return ok(a / b)
}
match safe_div(100, 0) {
    ok(v) -> print(v)
    err(e) -> print(e)    // prints: cannot divide by zero
}
```

#### Example: Index Out of Bounds

```
// This produces an error:
let items = [10, 20, 30]
print(items[5])  // error: index 5 out of bounds (length 3)
```

```
// Fix: check the index against the array length
let items = [10, 20, 30]
let i = 2
if i < len(items) {
    print(items[i])  // prints: 30
}
```

```
// Negative indices are also out of bounds:
let items = [10, 20, 30]
print(items[-1])  // error: index -1 out of bounds (length 3)
```

```
// Fix: use a valid non-negative index
let items = [10, 20, 30]
print(items[len(items) - 1])  // prints: 30
```

#### Example: Maximum Call Depth Exceeded

```
// This produces an error:
fn loop(n: int) -> int {
    return loop(n + 1)  // error: maximum call depth exceeded (limit 512)
}
loop(0)
```

```
// Fix: add a base case to stop recursion
fn countdown(n: int) {
    if n <= 0 {
        print("done")
        return
    }
    print(n)
    countdown(n - 1)
}
countdown(5)
```

#### Built-in Arity Errors

Each built-in function validates its argument count:

| Message | Fix |
|---------|-----|
| `len() takes exactly 1 argument` | Pass exactly 1 argument. |
| `type() takes exactly 1 argument` | Pass exactly 1 argument. |
| `str() takes exactly 1 argument` | Pass exactly 1 argument. |
| `int() takes exactly 1 argument` | Pass exactly 1 argument. |
| `float() takes exactly 1 argument` | Pass exactly 1 argument. |
| `push() takes exactly 2 arguments` | Pass exactly 2 arguments. |
| `abs() takes exactly 1 argument` | Pass exactly 1 argument. |
| `min() takes exactly 2 arguments` | Pass exactly 2 arguments. |
| `max() takes exactly 2 arguments` | Pass exactly 2 arguments. |
| `read_file() takes exactly 1 argument` | Pass exactly 1 argument. |
| `write_file() takes exactly 2 arguments` | Pass exactly 2 arguments. |
| `file_exists() takes exactly 1 argument` | Pass exactly 1 argument. |

---

### AGL_ERR_IO -- I/O Errors

Produced by the module system during import resolution.

| Message | Cause | Fix |
|---------|-------|-----|
| `invalid import path 'path'` | Path contains `..`, resolves outside base directory, or is too long. | Use a relative path without `..` that stays within the project. |
| `cannot open module 'path'` | Module file does not exist or cannot be read. | Verify the file exists at the expected location (`.agl` extension is added automatically). |

#### Example: Invalid Import Path

```
// This produces an error:
import "../secret/config"  // error: invalid import path '../secret/config'
```

```
// Fix: use a relative path without '..' that stays within the project
import "lib/config"
```

```
// Also invalid -- escaping the project directory:
import "../../etc/passwd"  // error: invalid import path '../../etc/passwd'
```

```
// Fix: only import modules within your project
import "utils"       // imports utils.agl from the same directory
import "lib/math"    // imports lib/math.agl relative to current file
```

---

## Error Context

### Error Object Structure

Each error contains:

- **Code**: One of `AGL_ERR_SYNTAX`, `AGL_ERR_TYPE`, `AGL_ERR_NAME`, `AGL_ERR_RUNTIME`, `AGL_ERR_IO`.
- **Location**: File path, line number, column number.
- **Message**: Human-readable description (max 256 characters).
- **Stack trace**: Up to 16 call frames (populated for runtime errors inside functions).

### Error Propagation

Agl uses single-error semantics. When an error occurs:

1. The error is set on the shared `AglCtx` context.
2. All subsequent evaluation and execution short-circuits (checks `agl_error_occurred`).
3. The error is printed to stderr and the program exits with a nonzero status.

Only one error is reported per run. The first error encountered halts execution.
