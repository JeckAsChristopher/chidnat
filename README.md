# CHN

CHN is a small scripting language that compiles to bytecode and runs on a stack-based VM written in C. It has a clean syntax, a real garbage collector, and a standard library that covers file I/O, networking, binary data, color output, and more.

## Building

```
make          # release build → ./chn
make debug    # with AddressSanitizer
make clean
```

Requires gcc and make. No external dependencies.

## Running

```
./chn file.chn            # run a file
./chn                     # start the REPL
./chn --ast file.chn      # print the AST
./chn --disasm file.chn   # print the bytecode
```

## Language basics

```chn
-- this is a comment

var x = 10          -- mutable variable
let name = "Alice"  -- immutable (let cannot be reassigned)

stdo(x, name)       -- print to stdout, space-separated, newline at end
```

Variables and arrays:

```chn
var score = 0
array items = []
items.add("first")
items.add("second")
stdo(items.length())   -- 2
stdo(items[0])         -- first
```

Control flow:

```chn
if (score > 90) {
    stdo("great")
} else if (score > 60) {
    stdo("ok")
} else {
    stdo("try again")
}

var i = 0
while (i < 5) {
    stdo(i)
    i = i + 1
}

for (var j = 0; j < 3; j = j + 1) {
    stdo(j)
}

switch (score) {
    case 100: stdo("perfect")
    case 90:  stdo("nearly")
    default:  stdo("other")
}
```

Functions:

```chn
public func greet(name) {
    return "Hello " + name
}

stdo(greet("World"))
```

Visibility: `public` means callable from anywhere, `private` means only within the same file, `protected` means only callable from within the declaring function and its nested functions. The default when you write `func` with no keyword is private.

## Importing libraries

```chn
imp os
imp network
imp color
imp map
imp vector
imp bin
imp math
```

Libraries live in `chn-libs/`. After importing you call the functions directly.

## Standard libraries

**os.chn** — time, file I/O, directories, env, logging, JSON, CSV, shell capture

**network.chn** — TCP/UDP/TLS sockets, HTTP client, HTTP server helpers, WebSocket, DNS, URL encoding

**color.chn** — ANSI terminal colors, styles, 256-color, true-color RGB/hex, gradients, box drawing

**map.chn** — key-value map (hash map built from arrays), freq counter, merge, copy

**vector.chn** — dynamic array with sort, search, set operations, numeric aggregates

**bin.chn** — bitwise ops, byte packing, checksums, hashing, base64/hex encoding, binary file I/O, BMP/WAV/chndata writers

**math.chn** — NumPy-style math functions: trig, log, exp, rounding, statistics, matrix ops, FFT

## Examples

```
./chn examples/server.chn          # HTTP server on :8080
./chn examples/os_test.chn         # OS library demo
./chn examples/color_test.chn      # terminal color demo
./chn examples/network_test.chn    # TCP/HTTP loopback test
./chn examples/mapping_test.chn    # map library demo
./chn examples/vector_test.chn     # vector library demo
./chn examples/bin_test.chn        # binary/bitwise demo
```

## Array methods

Arrays have a small set of built-in methods callable with dot syntax:

```chn
array a = [1, 2, 3]
a.add(4)            -- append
a.insert(0, 0)      -- insert at index
a.cut(2)            -- remove by index
a.remove(3)         -- remove first occurrence of value
a.rall(3)           -- remove all occurrences of value
stdo(a.length())    -- length
stdo(a[0])          -- index
a[0] = 99           -- index assign
```

## Types

| Type     | Example              |
|----------|----------------------|
| number   | `42`, `3.14`, `0xFF` |
| string   | `"hello\nworld"`     |
| bool     | `true`, `false`      |
| nil      | `nil`                |
| array    | `[1, "two", true]`   |
| function | passed as values     |

Numbers are always 64-bit floats. Integers print without a decimal point as long as they are whole numbers.

## Error messages

CHN gives source context on every error:

```
error: error on line 5 while compiling

flashback:
  on file examples/demo.chn
       line 5
       > stdo(undef_var)
       error:    undefined variable 'undef_var' — did you mean 'def_var'?
```

Undefined variable and function names suggest the closest match using Levenshtein distance.

## NOTE

chn-libs must have the same parent directory of **chn** binary file.
