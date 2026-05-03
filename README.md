# 8051 LCD1602 Calculator

[中文说明 / Chinese version](README.zh-CN.md)

Fixed-point calculator for STC89C52RC (Puzhong 51 A2 board). LCD1602 display,
4x4 matrix keyboard + 4 independent keys. Single-file C, zero stdlib
dependencies, ~6 KB Flash.

## Hardware

### LCD1602

| Signal | Pin |
| --- | --- |
| DB0-DB7 | P0 |
| RS / RW / EN | P2.6 / P2.5 / P2.7 |

### Matrix Keyboard (P1)

```text
S1  S2  S3  S4      7  8  9  +
S5  S6  S7  S8      4  5  6  -
S9  S10 S11 S12     1  2  3  *
S13 S14 S15 S16    +/- 0  .  /
```

### Independent Keys

| Key | Function | Pin |
| --- | --- | --- |
| R1 | Backspace | P3.1 |
| R2 | AC | P3.0 |
| R3 | Percent | P3.2 |
| R4 | Equals | P3.3 |

## Build

### Toolchain (Arch Linux)

```sh
sudo pacman -S sdcc make
pip install stcgal
sudo usermod -aG uucp $USER   # serial port access, re-login to take effect
```

### Compile and Flash

```sh
make            # -> build/main.hex
make flash      # default: /dev/ttyUSB0, 115200 baud, stc89 protocol
```

### SDCC Flags

```sh
sdcc -mmcs51 --model-small --iram-size 256 --idata-loc 0x80
```

| Flag | Effect |
| --- | --- |
| `-mmcs51` | Target MCS-51 architecture |
| `--model-small` | All variables default to internal RAM with direct addressing |
| `--iram-size 256` | Enable 8052-style 256-byte internal RAM (upper 128 via indirect) |
| `--idata-loc 0x80` | Place `__idata` objects at 0x80, keeping lower RAM for direct-access globals |

## Architecture

Single-file design (~900 lines). No headers, no libraries, no link-time
dependencies. The code is layered by calling convention — each layer only
calls downward:

```text
main loop
  ├─ scan_key_action              hardware input
  │    ├─ scan_independent_action   R1-R4 debounce
  │    └─ scan_matrix_raw           4x4 column/row detect
  ├─ handle_action                calculator state machine
  │    ├─ input_digit / input_dot / input_sign
  │    ├─ input_operator / input_equal
  │    ├─ input_percent / input_backspace
  │    └─ compute
  │         ├─ fixed_add
  │         ├─ fixed_mul            4-term decomposition
  │         └─ fixed_div
  └─ render                       LCD output
       ├─ build_live_expr           format "left op right"
       ├─ format_scaled             fixed-point -> ASCII
       └─ lcd_line_tail / lcd_line_right
```

- Editing state (`g_edit_abs`, `g_edit_neg`, `g_dot`, `g_frac_count`) is
  fully separated from committed state (`g_left`, `g_op`). Backspace removes
  the last typed character at the input level, not the value level.
- Hardware scanning and calculator logic communicate through a single `action`
  byte. Changing key wiring only touches the scan layer.
- All arithmetic checks overflow before committing. No silent wraparound.

## Fixed-Point Representation

All values are stored as `i32` scaled by 100 (`SCALE = 100L`):

| Display | Internal | Display | Internal |
| --- | --- | --- | --- |
| `1` | `100` | `-5.5` | `-550` |
| `1.23` | `123` | `200000` | `20000000` |

Two decimal places. Maximum relative error from truncation is 1/SCALE = 1%
(e.g. 1 / 3 = 0.33 instead of 0.333...).

### Why CALC_MAX = 200000.00

The binding constraint is `fixed_div`, which computes:

```c
result = (ua * SCALE) / ub;
```

The intermediate `ua * SCALE` must fit in `u32`:

```text
20000000 * 100 = 2 * 10^9 < 2^32 - 1 = 4.29 * 10^9   OK
```

A higher CALC_MAX would make division safe but overflow the multiplication
sub-products. 20000000 is the largest round value that keeps all four
operations safe within `u32`.

## Arithmetic

### Multiplication

Naive fixed-point multiply `(a * b) / SCALE` overflows `u32` for any operand
above ~655 (since 65536^2 > 2^32). The implementation decomposes each operand
into integer and fractional parts:

```text
a = ai * SCALE + af
b = bi * SCALE + bf

a * b / SCALE = ai * bi * SCALE     term 1
              + ai * bf              term 2
              + bi * af              term 3
              + af * bf / SCALE      term 4
```

Each term is overflow-checked via `add_abs_checked()` before accumulation.
Term 4 cannot overflow since af, bf < 100, so af * bf < 10000.

Worst case: 200000.00 * 200000.00. Term 1 alone = 200000 * 200000 * 100 =
4 * 10^12, which overflows `u32`. The guard `ai > CALC_MAX / (bi * SCALE)`
catches this and returns overflow before the multiply executes.

### Division

```c
result = (abs(a) * SCALE) / abs(b);
```

Integer truncation gives a maximum absolute error of 1 unit (0.01 in display).
Division by zero sets error state 1 (`Div by zero`). Result exceeding
CALC_MAX sets error state 2 (`Overflow`).

### Addition

Both operands share the same scale, so addition is direct. Overflow is checked
before the add: `a > CALC_MAX - b` (positive) or `a < CALC_MIN - b`
(negative). Subtraction reuses `fixed_add` by negating the second operand.

## Input State Machine

| Variable | Type | Role |
| --- | --- | --- |
| `g_left` | `i32` | Committed left operand / result |
| `g_edit_abs` | `u32` | Magnitude of number being typed |
| `g_edit_neg` | `__bit` | Sign of number being typed |
| `g_dot` | `__bit` | Decimal point entered |
| `g_frac_count` | `u8` | Fractional digits entered (0-2) |
| `g_has_digit` | `__bit` | At least one digit entered |
| `g_op` | `u8` | Pending operator (`+`/`-`/`*`/`/` or 0) |
| `g_result_shown` | `__bit` | Display showing a computed result |
| `g_error` | `u8` | Error state (0 = none, 1 = div0, 2 = overflow) |

Transitions:

- **Digit**: pre-decimal `edit_abs = edit_abs * 10 + d * SCALE`;
  post-decimal `edit_abs += d * frac_place[frac_count++]`.
- **Dot**: sets `g_dot`, further digits go to fractional positions.
- **Sign (+/-)**: toggles `g_edit_neg`. On a displayed result, negates `g_left`.
- **Operator**: commits edit to `g_left` (or chains: computes `g_left op edit`,
  stores in `g_left`), clears edit state for next operand.
- **Equals**: computes `g_left op edit`, stores result, clears operator.
- **Backspace**: removes last input element in reverse order: fractional digit
  -> dot -> integer digit -> sign. Pending operator with no edit input: removes
  the operator.
- **Percent**: divides current value by 100 (shifts decimal left by 2).
- **AC**: resets all state.

After a result, typing a digit starts fresh; typing an operator chains.

## Memory Budget

### Internal RAM Map

From SDCC `main.mem` (annotated):

```text
      0 1 2 3 4 5 6 7 8 9 A B C D E F
0x00:|0|0|0|0|0|0|0|0|Q|Q|Q|Q|Q|Q|Q| |  register bank 0 + overlay
0x10:| | | | | | | | | | | | | | | | |  free (17 bytes)
0x20:|B|a|a|a|a|a|a|a|a|a|a|a|a|a|a|a|  bit area + direct data
 ... |a|a|a|a|a|a|a|a|a|a|a|a|a|a|a| |
0x80:|I|I|I|I|I|I|I|I|I|I|I|I|I|I|I|I|  idata (indirect access only)
 ... |I|I|I|I|I|I|S|S|S|S|S|S|S|S|S|S|
0xF0:|S|S|S|S|S|S|S|S|S|S|S|S|S|S|S|S|  stack (grows upward)
```

### Allocation Breakdown

| Region | Address | Bytes | Contents |
| --- | --- | --- | --- |
| Register bank 0 | 0x00-0x07 | 8 | R0-R7 |
| Overlay (Q) | 0x08-0x0E | 7 | Shared locals for non-overlapping call paths |
| Bit area (B) | 0x20 | 1 | 4 `__bit` flags |
| Direct data (a) | 0x21-0x7E | 85 | `g_left`, `g_edit_abs`, `g_op`, `g_rev[8]`, etc. |
| IDATA (I) | 0x80-0xD5 | 86 | `g_expr[24]`, `g_num[14]`, `fixed_mul` locals |
| Stack (S) | 0xD6-0xFF | 42 | Call stack (return addresses + temporaries) |
| Flash | 0x0000-0x1770 | 6001 | Code + `__code` constants |

### Stack Depth

Deepest call chain:

```text
main -> handle_action -> input_operator -> compute -> fixed_mul
```

`fixed_mul` declares 10 local variables as `__idata`, so they occupy
pre-allocated IDATA slots instead of stack frames. The 42-byte stack budget
covers return addresses (2 bytes * ~5 levels = 10 bytes) plus non-`__idata`
locals and compiler temporaries.

### Optimization Techniques

| Technique | Mechanism | Effect |
| --- | --- | --- |
| `__code` constants | `g_key_map`, `g_frac_place` in Flash | -20 B RAM |
| `__bit` flags | 4 booleans in bit-addressable area | -3 B RAM, single-cycle `SETB`/`CLR`/`JB` |
| `__idata` buffers | `g_expr[24]`, `g_num[14]` in upper RAM | -38 B direct RAM |
| `__idata` locals | `fixed_mul` temporaries in upper RAM | -40 B stack |
| SDCC overlay | Non-overlapping functions share 7 B | -23 B vs. separate allocation |
| No printf/float | Manual formatting + fixed-point | -3~6 KB Flash |
| O(n) string build | `append_text`/`append_u32` track length | Avoids O(n^2) rescan per char |

## Timing

| Operation | Duration | Notes |
| --- | --- | --- |
| LCD EN pulse | 2 ms | 1 ms high + 1 ms low |
| Full LCD refresh | ~64 ms | 32 characters * 2 ms |
| Matrix debounce | 12 ms | Second read after initial detect |
| Matrix release wait | 10 ms/poll | Up to 200 polls (2 s timeout) |
| Independent key debounce | 12 ms | Same pattern as matrix |
| Idle scan cycle | ~2 ms | Matrix probe + independent checks |

Main loop runs at ~15 Hz during active input (LCD-bound). Idle polling is
~500 Hz.

## Limits

| Property | Value |
| --- | --- |
| Range | -200000.00 to 200000.00 |
| Decimal places | 2 (fixed) |
| Max relative error | 1% (integer truncation in division) |
| Error states | `Div by zero` (div by 0), `Overflow` (out of range) |

Trailing zeros are trimmed in results (`1.50` -> `1.5`) but preserved during
active decimal input.

## Modification Guide

| Change | Location |
| --- | --- |
| LCD pins | `LCD_RS`, `LCD_RW`, `LCD_EN` `__sbit` definitions |
| R1-R4 pins | `KEY_R1`-`KEY_R4` `__sbit` definitions |
| Key layout | `g_key_map[16]` array |
| Decimal precision | `SCALE`, `SCALE_DIGITS`, `g_frac_place[]` — verify RAM/Flash after |
| Serial port | `PORT`, `BAUD`, `PROTOCOL` in Makefile |
