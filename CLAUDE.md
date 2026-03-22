# Ago Programming Language

A medium-level programming language designed for AI agents.

## Build

```bash
make        # build
make test   # run tests
make clean  # clean build artifacts
```

## Project Structure

```
src/       — compiler/interpreter source (C11)
tests/     — test files
examples/  — .ago example programs
```

## Conventions

### Build
- C11 standard: `-std=c11 -Wall -Wextra -Werror -pedantic`
- Debug builds include `-fsanitize=address,undefined` (AddressSanitizer + UBSan)
- No external dependencies beyond libc for core compiler

### Naming
- **Types**: PascalCase with Ago prefix — `AgoLexer`, `AgoToken`, `AgoValue`
- **Functions**: snake_case with `ago_` prefix — `ago_lexer_next_token()`
- **Enums**: SCREAMING_SNAKE with `AGO_` prefix — `AGO_TOKEN_INT`, `AGO_TOKEN_EOF`
- **Macros**: SCREAMING_SNAKE — `AGO_VERSION`, `AGO_ASSERT`

### Code Patterns
- `static` for all functions NOT exposed in a `.h` header
- `const` on pointer parameters that are not mutated
- Error return: set error on `AgoCtx *`, return NULL/false — `ago_error_set(ctx, code, fmt, ...)`
- String ownership: arena-owned or interned. Never return a `char *` that the caller must free.
- Compiler memory: arena allocator for AST/tokens. `ago_arena_free()` at end of compilation.
- Runtime memory: `ago_alloc()` for GC-tracked heap objects.

### Tests
- One test file per module (`tests/test_lexer.c`, `tests/test_parser.c`)
- `tests/` directory owned by @test-engineer
- Test harness: `AgoTestCtx` struct, no global state

## Agents

| Agent | Purpose | When to use |
|-------|---------|-------------|
| `@lang-architect` | 언어 설계 및 컴파일러/인터프리터 구현 | 핵심 구현 작업 (lexer, parser, VM 등) |
| `@test-engineer` | 테스트 작성 및 검증 | 모듈 테스트, 엣지케이스, 테스트 하네스 |
| `@stdlib-designer` | 표준 라이브러리 설계/구현 | stdlib 모듈 API 설계 및 C 구현 |
| `@docs-writer` | 문서/스펙 작성 | 언어 명세, 튜토리얼, 에러 카탈로그 |
| `@example-author` | 예제 프로그램 작성 | 데모, 통합 테스트, 쇼케이스 |

## Skills

| Skill | Purpose |
|-------|---------|
| `/run [file]` | Ago 프로그램 빌드 & 실행 |
| `/test [module]` | 테스트 실행 + 결과 요약 |
| `/check` | 커밋 전 전체 검증 (빌드→테스트→예제→메모리검사) |
| `/status` | 프로젝트 현황 개요 |
