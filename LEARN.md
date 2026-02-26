# LEARN.md — Understanding how CHN works

This document explains the internals. Not what you write, but how the language processes what you write and runs it. Good if you want to understand what is actually happening under the hood.

---

## How CHN runs your code

When you run `./chn file.chn`, four things happen in sequence:

**Lexer** reads the raw text and breaks it into tokens. A token is the smallest meaningful unit — a number, a string, a keyword, an operator. It does not understand grammar, it just slices the characters.

**Parser** reads the tokens and builds an AST (abstract syntax tree). The AST is a tree of nodes that represent the structure of your program. A binary expression like `a + b` becomes a node with two children.

**Compiler** walks the AST and emits bytecode. Bytecode is a compact list of instructions for the VM. The compiler also resolves variable names and checks visibility rules.

**VM** runs the bytecode. It is a stack machine — instructions push and pop values on a stack. It calls native C functions for things like file I/O and networking.

---

## What is stdo

`stdo` is the print statement. It prints all its arguments to standard output, separated by spaces, followed by a newline.

```chn
stdo("hello")           -- prints: hello
stdo("x =", 42)         -- prints: x = 42
stdo(1, 2, 3)           -- prints: 1 2 3
```

It is a keyword, not a function. The parser recognizes it directly and compiles it to an `OP_PRINT` instruction with an argument count. The VM then pops that many values off the stack and prints them.

You can pass any value — numbers, strings, booleans, nil, arrays. Arrays print as `[1, 2, 3]`. Booleans print as `true` or `false`. Nil prints as `nil`.

---

## What is stdi

`stdi` reads input from the user and stores it in a variable. The optional second argument is a prompt string shown before the cursor.

```chn
var name = ""
stdi(name, "Enter your name: ")
stdo("Hello", name)
```

The VM reads a line from stdin. If the input looks like a number, it stores a number value. Otherwise it stores a string. This means `stdi(x)` auto-detects the type — `"42"` becomes the number `42`, but `"hello"` stays a string.

---

## What is while

`while` is a loop that keeps running its body as long as the condition is true.

```chn
var i = 0
while (i < 5) {
    stdo(i)
    i = i + 1
}
```

How the compiler turns this into bytecode:

1. Marks the current bytecode offset as `loop_start`
2. Compiles the condition expression — this pushes a true/false value onto the stack
3. Emits `JUMP_IF_FALSE → exit_label` — the condition value stays on the stack, the VM peeks at it
4. Emits `POP` — if we're on the true path, pop the condition value
5. Compiles the body
6. Emits `GC_SAFEPOINT` — gives the garbage collector a chance to run
7. Emits `JUMP → loop_start` — go back to the top
8. At `exit_label`: emits `POP` — on the false path, pop the condition value

`break` compiles to a `JUMP` whose destination is patched later to point just after the exit `POP`. `continue` compiles to a `JUMP` whose destination is patched to `loop_start`.

---

## What is for

`for` is a C-style loop with optional init, condition, and post expression.

```chn
for (var i = 0; i < 5; i = i + 1) {
    stdo(i)
}
```

All three parts are optional. `for (;;)` is an infinite loop. The `continue` statement in a for loop jumps to the post expression, not to the condition check.

---

## What is switch

`switch` compares a subject against a list of values. No fallthrough — each case is independent.

```chn
switch (x) {
    case 1:
        stdo("one")
    case 2:
        stdo("two")
    default:
        stdo("other")
}
```

How it compiles: the subject is evaluated once and left on the stack. For each case, the compiler emits a DUP (to copy the subject), pushes the case value, runs EQ, and jumps past the body if false. When a case matches, the subject copy and the comparison result are both popped before the body runs. After the body an implicit break jumps to the end. If no case matches and there is a default, the subject is still on the stack when the default body runs.

---

## What is var and let

`var` declares a mutable variable. `let` declares an immutable one — you cannot reassign it after the declaration.

```chn
var count = 0
count = count + 1    -- ok

let max = 100
max = 200            -- compile error: cannot reassign 'let' variable 'max'
```

At the global level, variables are stored in a globals array indexed by slot number. Inside a function, variables are stored on the stack at a known slot offset from the frame base — these are called locals.

The compiler resolves names at compile time. If you use a name that was not declared, it gives a compile error with a suggestion if a similar name exists.

---

## What is func

`func` declares a function. It can be prefixed with a visibility keyword.

```chn
public func add(a, b) {
    return a + b
}

private func helper(x) {
    return x * 2
}
```

`public` — callable from anywhere, including from other files after import.
`private` — callable only within the same source file.
`protected` — callable only from within the declaring function and its nested functions.
`func` with no prefix — same as private.

Functions can be nested inside other functions. A nested function is visible only to the function that declares it (and its own nested functions if it uses protected).

Functions in CHN are not closures. They do not capture variables from the outer scope. If you need to pass data into a nested function, use parameters.

---

## What is array

Arrays are ordered lists of any values. Elements can be different types.

```chn
array items = []
items.add("hello")
items.add(42)
items.add(true)
stdo(items.length())    -- 3
stdo(items[1])          -- 42
items[0] = "world"      -- assign by index
```

You can also declare with initial values:

```chn
array primes = [2, 3, 5, 7, 11]
```

The built-in array methods are: `add(val)`, `insert(idx, val)`, `cut(idx)`, `remove(val)`, `rall(val)`, `length()`. These compile to a special `OP_METHOD_CALL` instruction rather than a function call.

Negative indexing works: `a[-1]` is the last element.

---

## What is map in CHN

Map is not a built-in type — it is a library in `chn-libs/map.chn` that simulates a hash map using a CHN array under the hood.

```chn
imp map

var m = map_new()
map_set(m, "name", "Alice")
map_set(m, "age",  "30")
stdo(map_get(m, "name"))      -- Alice
stdo(map_has(m, "age"))       -- true
stdo(map_size(m))             -- 2
```

Internally the map is a flat array that alternates keys and values: `[key0, val0, key1, val1, ...]`. Functions like `map_get` scan this array linearly. For small maps this is fine. For very large maps (thousands of entries) use a different strategy.

Because map is just a library, `map_new()` returns a regular array. You can pass it to functions, store it in variables, and put it in other arrays.

---

## What is imp

`imp` imports a library file. Bare names search the `chn-libs/` directories. Quoted paths are relative.

```chn
imp os               -- loads chn-libs/os.chn
imp map              -- loads chn-libs/map.chn
imp "utils/helper"   -- loads ./utils/helper.chn
```

When you import a file, all its `export public func` declarations become available in your file. Private functions in the imported file are not accessible.

The import system walks up the directory tree looking for `chn-libs/` folders, so it works regardless of where you run the binary from.

---

## How the AST works

When the parser reads your source, it builds a tree of `ASTNode` structs. Each node has a `kind` field that says what it is (a number literal, a binary expression, a while loop, etc.) and fields specific to that kind.

For example, the expression `a + b` parses into:

```
NODE_BINARY
  op: TK_PLUS
  left:  NODE_IDENT  (name="a")
  right: NODE_IDENT  (name="b")
```

The statement `if (x > 0) { stdo(x) }` parses into:

```
NODE_IF
  condition: NODE_BINARY
               op: TK_GT
               left:  NODE_IDENT (name="x")
               right: NODE_NUMBER (value=0)
  then_branch: NODE_BLOCK
                 [NODE_PRINT
                    args: [NODE_IDENT (name="x")]]
  else_branch: null
```

The compiler then does a single pass over this tree and emits bytecode for each node. There is no optimization pass — what you write is what gets compiled.

---

## How opcodes work

Bytecode is stored as a flat array of bytes. Each instruction is one byte for the opcode, followed by zero, one, or two bytes of operand depending on the instruction. Operands that are 16-bit values are stored little-endian (low byte first).

The VM reads instructions with three macros:

```c
READ_BYTE()   // read one byte, advance ip
READ_U16()    // read two bytes as uint16, advance ip by 2
```

Here are the main opcodes and what they do:

| Opcode | Operand | What it does |
|--------|---------|--------------|
| `CONST` | u16 index | Push `constants[index]` onto the stack |
| `NIL` | — | Push nil |
| `TRUE` | — | Push true |
| `FALSE` | — | Push false |
| `POP` | — | Pop and discard the top of the stack |
| `DUP` | — | Push a copy of the top value |
| `GET_VAR` | u16 slot | Push the global variable at that slot |
| `SET_VAR` | u16 slot | Write the top of stack into that global slot (does not pop) |
| `DEF_VAR` | u16 slot | Pop the stack and store it in that global slot |
| `GET_LOCAL` | u16 slot | Push the local variable at frame_base + slot |
| `SET_LOCAL` | u16 slot | Write top of stack into frame_base + slot (does not pop) |
| `ADD` | — | Pop two values, push their sum (or string concat) |
| `SUB` | — | Pop two numbers, push difference |
| `MUL` | — | Pop two numbers, push product |
| `DIV` | — | Pop two numbers, push quotient |
| `MOD` | — | Pop two numbers, push remainder |
| `NEG` | — | Pop one number, push its negation |
| `EQ` | — | Pop two values, push true if equal |
| `NEQ` | — | Pop two values, push true if not equal |
| `LT` | — | Pop two numbers, push true if left < right |
| `GT` | — | Pop two numbers, push true if left > right |
| `LE` | — | Pop two numbers, push true if left ≤ right |
| `GE` | — | Pop two numbers, push true if left ≥ right |
| `NOT` | — | Pop one value, push its boolean negation |
| `JUMP` | u16 target | Set ip to target (absolute offset in bytecode) |
| `JUMP_IF_FALSE` | u16 target | Peek at top; if falsy, jump. Does NOT pop the value. |
| `JUMP_IF_TRUE` | u16 target | Peek at top; if truthy, jump. Does NOT pop the value. |
| `CALL` | u16 argc | Call the function at stack[top - argc - 1] with argc arguments |
| `RETURN` | — | Pop return value, unwind frame, push return value into caller's frame |
| `ARRAY_NEW` | — | Push a new empty array |
| `ARRAY_PUSH` | — | Pop value, push it into the array at top of stack |
| `ARRAY_INDEX` | — | Pop index and array, push array[index] |
| `ARRAY_SET` | — | Pop value, index, array; set array[index] = value; push value |
| `ARRAY_LEN` | — | Pop array or string, push its length |
| `METHOD_CALL` | u8 method_id, u8 argc | Call a built-in array method |
| `GC_SAFEPOINT` | — | Run one incremental GC step |
| `PRINT` | u16 n | Pop n values and print them space-separated with a newline |
| `PROMPT` | — | Pop one value and print it (no newline, used by stdi) |
| `INPUT` | u16 slot, u8 is_local | Read a line from stdin, auto-detect type, store in variable |
| `NATIVE` | u16 call_id, u8 argc | Call a built-in C function |
| `HALT` | — | Stop the VM |

The constants pool is a separate array per chunk (top-level or function). String literals, number literals, and function values are stored there. `CONST` with index 0 pushes `constants[0]` onto the stack.

---

## How the stack works

The stack is a flat array of `Value` structs. `stack_top` points to the next free slot. Every value is the same size — a type tag plus a union for the actual data.

When you call a function, the compiler emits `CONST` to push the function value, then pushes each argument. The `CALL` instruction creates a new call frame. The frame records where the arguments start (`base_idx`) and saves the return address. The function's local variables live at frame_base + local_slot, right on the same stack array.

When a function returns, `RETURN` pops the return value, unwinds the stack back to where the function value was, removes the function value itself, and pushes the return value in its place.

---

## How the GC works

The garbage collector is a tri-color incremental mark-and-sweep with two generations.

Every GC-managed object (strings and arrays) has a color: white (not yet visited), gray (found, children not yet traced), or black (fully traced).

When a collection runs:

1. All roots are marked gray. Roots are values on the stack, values in global variables, and constants in active call frames.
2. Gray objects are traced — each gray object is turned black, and its children (items inside an array, or nothing for a string) are pushed gray.
3. After all gray objects are traced, any remaining white objects are not reachable and are freed.

The incremental part: instead of doing a full collection at once, `GC_SAFEPOINT` instructions (emitted at loop back-edges and function entries) process a small batch of gray objects each time. A full stop-the-world collection is triggered when allocated memory crosses a threshold.

Strings are interned — two strings with the same content share the same object in memory. This means string equality is a pointer comparison after a hash check.

`FunctionObject` structs are not GC-managed. They are allocated with `calloc` at compile time and live for the entire process lifetime.

---

## How native calls work

Native functions are C functions exposed to CHN via the `OP_NATIVE` instruction. Each native function has a 16-bit ID.

When the compiler encounters a call like `os_time()`, it recognizes it as a native call (because `os.chn` defines it with `__native__(0x0000, 0)` syntax), emits `OP_NATIVE` with the call ID and argument count.

The VM pops the arguments, calls `native_dispatch(vm, id, argc)` which is a giant switch statement, and the C code pushes the result back onto the stack.

Native calls can do things CHN cannot express natively: syscalls, sockets, TLS, file operations, time queries. The native layer is where CHN connects to the operating system.

---

## How functions resolve across files

When you import a file, all functions from that file are added to the compiler's import list. When the compiler sees a call to `os_time()`, it first looks in the current-file function list. If not found, it searches the import list. If found there, it checks visibility — private functions from imported files are rejected with an error.

This resolution happens at compile time. There is no dynamic dispatch — every call is resolved to a specific `FunctionObject` before any bytecode is emitted.

Forward calls within a single file work because the compiler does a pre-scan pass over all top-level function declarations before compiling bodies. This lets you call a function that is defined later in the file.

---

## How errors are reported

Every bytecode instruction stores the source line number it came from (in the `line_info` array parallel to `code`). When a runtime error occurs, the VM looks up the current instruction's offset to find the line number, then `error_runtime()` opens the source file and extracts that line to show in the error message.

Compile errors are reported during the AST walk, using the line stored in each AST node (which comes from the token that started the node).

The "did you mean?" suggestions use Levenshtein distance. If you mistype `os_tyme`, the compiler computes the edit distance to every known function name and suggests the one with the smallest distance, if it is within the threshold.

---

## Truthiness

Every value has a truthy/falsy interpretation used in conditions:

- `nil` is falsy
- `false` is falsy
- `0` (the number) is falsy
- `""` (empty string) is falsy
- `[]` (empty array) is falsy
- Everything else is truthy — non-zero numbers, non-empty strings, non-empty arrays, `true`, functions

This means `if (some_array)` is a valid way to check whether an array is non-empty.

---

## String concatenation and types

The `+` operator with a string on either side converts the other operand to a string automatically.

```chn
stdo("count: " + 42)        -- count: 42
stdo("ok: " + true)         -- ok: true
stdo("nil: " + nil)         -- nil: nil
stdo("arr: " + [1, 2, 3])   -- arr: [1, 2, 3]
```

Numbers print without trailing `.0` when they are whole numbers. Floats use up to 14 significant digits.

---

## Limits

These are hard-coded limits in `common.h`:

| Constant | Value | What it limits |
|----------|-------|----------------|
| `MAX_CONSTANTS` | 512 | Unique literal values per function |
| `MAX_VARIABLES` | 512 | Global variables |
| `MAX_LOCALS` | 256 | Local variables per function |
| `MAX_STACK` | 4096 | Stack depth |
| `MAX_CODE` | 65536 | Bytecode bytes per function |
| `MAX_FUNCS` | 512 | Functions per compilation unit |
| `MAX_CALL_DEPTH` | 256 | Recursion depth |
| `MAX_PARAMS` | 32 | Parameters per function |
| `MAX_STRING_LEN` | 4096 | String literal length |

