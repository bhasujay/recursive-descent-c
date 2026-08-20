# Recursive Descent Compiler — Zero-Dependency C Implementation

A handwritten compiler front-end built entirely in a C-like language with **zero external dependencies** — no Flex, no Bison, no ANTLR, no runtime scaffolding. Every component, from lexer to the hash-chained symbol table, was engineered from scratch: raw pointer arithmetic, manual memory management, and deliberate architectural decisions at every layer. This is a fully operational language processor capable of lexical analysis, syntactic validation, semantic type-checking, and direct expression evaluation across integer, float, and string domains.

---

## Core Architecture

### 1. DFA Lexer — Future-Proofed for Turing-Complete Expansion

The tokenizer in [`parser.c`](./parser.c) is implemented as a **handwritten Deterministic Finite Automaton (DFA)** — not a regex engine, not a library call. A single-pass state machine drives character classification, operator disambiguation, and literal boundary detection simultaneously.

**The lexer is deliberately over-engineered relative to the current grammar.** The full token vocabulary is defined and recognized at the lexical layer regardless of whether the parser currently consumes it:

| Category | Tokens |
|---|---|
| Control Flow Keywords | `if`, `else`, `while`, `for`, `return` |
| Type Specifiers | `int`, `float`, `string` |
| Compound Assignment | `+=`, `-=`, `*=`, `/=` |
| Increment / Decrement | `++`, `--` |
| Relational Operators | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| Structural Delimiters | `{}`, `[]`, `()`, `,`, `;` |
| Literals | `INT_LITERAL`, `FLOAT_LITERAL`, `STRING_LITERAL` |

This decouples tokenization from grammar scope. Extending the parser to support `while` loops, `for` constructs, function declarations, or array indexing (all formally specified in the proposed grammar) requires **zero modifications to the lexer** — the tokens are already being emitted.

The DFA handles pathological inputs in-line:
- **Malformed identifiers** (`var.1`, `foo.bar.baz`) — detected by lookahead on `.` sequences; error emitted, token skipped.
- **Malformed numeric literals** (`12.34.56`, `3.`) — dot-count tracking within the numeric scan state; error emitted on violation.
- **Unclosed string literals** — EOF sentinel check terminates the string scan state; error emitted with original open-quote coordinates.

The token stream is backed by a **dynamically growing array** initialized at 128 slots and doubled on overflow via `realloc`, keeping amortized allocation cost at O(1) per token.

---

### 2. Recursive Descent Parser with Integrated Semantic Analysis

The parser is a **pure recursive descent implementation** where every non-terminal in the Context-Free Grammar maps 1:1 to a C function. There is no intermediate AST construction phase — semantic validation and expression evaluation are woven directly into the descent, eliminating a full tree-traversal pass.

```
program()
  └─ stmt_list()
       └─ stmt()
            ├─ decl()         → types() + insert_to_table() + expr()
            ├─ assign_stmt()  → symbol_exists() + expr() + add_value()
            ├─ print_stmt()   → expr() + type-dispatched stdout write
            └─ expr()
                 └─ term()
                      └─ factor()
```

**Right-Associative Arithmetic:** The expression grammar is intentionally right-recursive:

```
Expr  → Term PLUS  Expr  |  Term MINUS Expr  |  Term
Term  → Factor STAR Term  |  Factor SLASH Term  |  Factor
```

`expr()` recurses into itself on the right operand before `solve()` is called, delegating operand ordering to the C call stack. This is an explicit architectural choice — it simplifies recursive descent logic and eliminates the need for a separate precedence-climbing loop.

**Semantic validation occurs at point-of-parse:**
- `decl()` inserts the symbol into the table before consuming the `=`, then validates the expression result type against the declared type via `add_value()`.
- `assign_stmt()` calls `symbol_exists()` before consuming the identifier — assignment to an undeclared variable is a hard semantic error.
- `factor()` resolves identifier references through `get_value()` and propagates `SYM_UNKNOWN` upward on failure, preventing cascading evaluation on already-broken operands.
- `solve()` enforces type-compatibility: `int`/`float` operands are subject to implicit float promotion; string operands only permit `+` (concatenation); cross-type operations between string and numeric types are rejected with a precise error.

---

### 3. O(1) Symbol Table — djb2 Hash Map with Load-Adaptive Resizing

The symbol table is a **custom open-addressing hash map with linked-list chaining** for collision resolution. No stdlib hash structures, no tree maps, no sorted arrays.

**Hash function — djb2:**
```c
unsigned int hash(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    return hash;
}
```
djb2 was selected for its exceptional distribution across short alphanumeric strings (the dominant identifier profile in compiled languages) and its branchless inner loop.

**Collision handling:** Each bucket in the `Symbol*` array is the head of an intrusive singly-linked list. On hash collision, a new `Symbol` node is heap-allocated and chained to the bucket's tail. Lookup, insertion, and update all walk this chain with `strcmp` identity confirmation.

**Dynamic resizing at 0.75 load factor:**
```c
#define HASH_THRESHOLD 0.75
#define INITIAL_SYMBOL_TABLE_SIZE 32

if ((float)symbol_count / symbol_table_size > HASH_THRESHOLD)
    resize_symbol_table();
```
`resize_symbol_table()` allocates a new table at `2 × old_size`, rehashes all live entries (including chained nodes), and frees the old allocation. This maintains the average O(1) lookup guarantee across table growth events by keeping the load factor bounded.

**Each symbol stores:**
```c
typedef struct Symbol {
    char*      identifier;     // heap-allocated identifier string
    SymbolType type;           // SYM_INT | SYM_FLOAT | SYM_STRING | SYM_EMPTY
    Value      val;            // tagged union: int_value | float_value | string_value
    int        declared_line;  // source line for redeclaration diagnostics
    struct Symbol* next;       // collision chain pointer
} Symbol;
```

---

## Language Specification

### Implemented Grammar

```
Program      → StmtList
StmtList     → Stmt StmtList  |  ε
Stmt         → Decl  |  assign_stmt  |  PrintStmt  |  Expr SEMICOLON
Decl         → types IDENTIFIER ASSIGN Expr SEMICOLON
             | types IDENTIFIER SEMICOLON
assign_stmt  → IDENTIFIER ASSIGN Expr SEMICOLON
PrintStmt    → print LPAREN Expr RPAREN SEMICOLON
Expr         → Term PLUS Expr  |  Term MINUS Expr  |  Term
Term         → Factor STAR Term  |  Factor SLASH Term  |  Factor
Factor       → LPAREN Expr RPAREN  |  IDENTIFIER  |  INT_LITERAL
             | FLOAT_LITERAL  |  STRING_LITERAL
types        → int  |  float  |  string
```

### Supported Types

| Type | Behavior |
|---|---|
| `int` | 32-bit signed integer arithmetic; division truncates toward zero |
| `float` | IEEE 754 single-precision; `int` operands are implicitly promoted in mixed expressions |
| `string` | Heap-allocated C string; `+` operator performs concatenation via `malloc` + `strcpy` + `strcat` |

### Type Rules
- **No implicit string coercion.** Arithmetic between `string` and `int`/`float` is a hard semantic error.
- **Strict variable declaration.** Use of any identifier before `Decl` is a semantic error, caught at `factor()` descent.
- **Redeclaration is an error.** `insert_to_table()` checks the bucket before inserting; a duplicate identifier at any hash index triggers a redeclaration error with the original declaration line cited.
- **Division by zero** is caught at runtime within `solve()` for both `int` and `float` domains.

---

## Build & Execution

**Requirements:** GCC (any modern version). No other dependencies.

### Compile
```bash
gcc -o parser parser.c -Wall -Wextra -std=c99
```

### Run
```bash
# Compile and validate a source file
./parser input.txt

# Compile with token stream dump (lexer debug output)
./parser input.txt --tokens
```

### Example Input (`input.txt`)
```c
int x = 10;
float y = 3.14;
string greeting = "Hello, ";
string name = "World";
int result = x * 2 + 5;
print(result);
print(greeting + name);
y = y + 1.0;
print(y);
```

### Example Output
```
25
Hello, World
4.140000
```

---

## Advanced Error Handling

The error system is a **multi-stage, non-terminating aggregator**. Errors at the lexical phase and errors at the syntax/semantic phase are collected into a persistent linked list and reported in full — the compiler does not abort on the first fault.

```c
typedef struct Error {
    char*         message;   // heap-allocated, formatted error string
    int           line;
    int           column;
    struct Error* next;      // intrusive linked list for O(1) append
} Error;
```

`add_error()` appends to a tail-tracked linked list via a function-local `static Error* errors_tail` pointer, ensuring O(1) insertion regardless of error count.

### Error Categories

**Lexical Errors** (detected during `tokenize()`):
```
error: Malformed identifier 'var.1'
  --> line 2, column 5

error: Malformed numeric literal '12.34.56'
  --> line 3, column 9

error: Unclosed string literal
  --> line 5, column 1
```

**Syntax Errors** (detected during recursive descent):
```
error: Expected ';' after declaration of 'x'
  --> line 4, column 12

error: Missing operator between 'x' and 'y'
  --> line 7, column 8

error: Expected ')' after expression in print statement, found 'EOF'
  --> line 9, column 1
```

**Semantic Errors** (detected during symbol resolution and type checking):
```
error: Redeclaration of variable 'x'. Previously declared at line 2.
  --> line 6, column 0

error: Undeclared variable 'z' used in expression
  --> line 8, column 5

error: Type mismatch for variable 'x'. Expected 'int' but got 'float'.
  --> line 10, column 7

error: Only concatenation (+) is supported for string types
  --> line 11, column 9
```

All output is routed to `stderr` with **ANSI escape code formatting**: errors are rendered in bold red (`\x1b[1;31m`), and source location pointers in cyan (`\x1b[36m`), giving a terminal output format consistent with production compilers (GCC, Clang).

---

## Project Structure

```
.
├── parser.c    # Lexer (DFA), parser (recursive descent), symbol table, semantic analysis, main
└── parser.h    # Token types, Symbol/Value/Error structs, all function prototypes, constants
```

All logic is contained within two files. The header enforces a clean interface boundary between the data model (`parser.h`) and all executable logic (`parser.c`).
