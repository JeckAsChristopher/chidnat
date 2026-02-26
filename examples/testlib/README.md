# test

An example CHN library package demonstrating the `chn-publish` / `chn-install` workflow.

## Install

```
chn-install test
```

## Usage

```chn
imp test

stdo(test_greet("World"))
stdo(test_reverse("hello"))
stdo(test_is_palindrome("racecar"))
stdo(test_max(10, 20))
stdo(test_sum([1, 2, 3, 4, 5]))
stdo(test_version())
```

## Functions

| Function | Args | Returns | Description |
|---|---|---|---|
| `test_greet` | name | string | Returns a greeting |
| `test_reverse` | s | string | Reverses a string |
| `test_is_palindrome` | s | bool | True if string reads same both ways |
| `test_abs` | n | number | Absolute value |
| `test_max` | a, b | number | Larger of two values |
| `test_min` | a, b | number | Smaller of two values |
| `test_clamp` | n, lo, hi | number | Clamp to range |
| `test_sum` | arr | number | Sum of array |
| `test_scale` | arr, f | array | Multiply each element by f |
| `test_includes` | arr, val | bool | True if val exists in array |
| `test_unique` | arr | array | Remove duplicates |
| `test_version` | — | string | Library version string |
| `test_about` | — | string | Library description |

## License

MIT
