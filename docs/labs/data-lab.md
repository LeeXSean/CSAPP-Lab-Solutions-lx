---
title: Data Lab
description: Integer and floating-point operations from bitwise primitives only — a walkthrough of Data Lab.
---

# Data Lab · Fighting With Bits

<p class="article-meta">Data representation <span class="dot">·</span> Keywords: bitwise ops, two's complement, IEEE 754 <span class="dot">·</span> <a href="https://github.com/LeeXSean/csapp-labs/blob/main/Data_Lab/datalab-handout/bits.c">bits.c</a></p>

!!! success "Verified locally"
    `./btest` → **36 / 36** correct · `./dlc bits.c` reports the solution legal (`-m32`).

Data Lab is the course's first assignment, and a deliberately constrained one: under a strict set of rules, you re-implement everyday operations using nothing but raw bit manipulation.

!!! note "The rules"
    **Integer problems** may use only `! ~ & ^ | + << >>`, each with an operator budget; no `if` / loops / `==` / `*` / casts / constants larger than `0xFF`. **Floating-point problems** relax to allow loops and conditionals, but still forbid any float type or operation — you treat a `float` as a 32-bit `unsigned` and manhandle its bits directly.

The challenge is never "get it right" — it's "get it right *within budget*." Below are the most instructive problems in detail; the rest follow the same playbook.

---

## Integer Problems

### isTmax — recognizing the maximum without comparing { data-toc-label="isTmax" }

Decide whether `x` is the two's-complement max, `0x7FFFFFFF`. With no `==`, translate "equal" into "XOR to zero."

``` c
int isTmax(int x) {
  return !!(x ^ ~0) & !((x + 1) ^ ~x); // (1)
}
```

1.  Key observation: `Tmax + 1` overflows to `Tmin (0x80000000)`, and `~Tmax` is *also* `0x80000000`. So **`x` is Tmax iff `x + 1 == ~x`**, expressed as `(x+1) ^ ~x == 0`.
    <br>One trap: `x = -1 (0xFFFFFFFF)` also satisfies `x+1 == ~x` (both are 0). So `!!(x ^ ~0)` rules `-1` out — `x ^ ~0` is just `~x`, which is 0 when `x = -1`, making `!!` false.

In short: **turn equality into "XOR to zero," then plug the `-1` false positive.** This pattern recurs throughout Data Lab.

### isAsciiDigit — range checks via the sign bit { data-toc-label="isAsciiDigit" }

Test whether `0x30 ≤ x ≤ 0x39`. With no `<=`, split the range into "are the high bits right?" plus "did the low nibble overflow?"

``` c
int isAsciiDigit(int x) {
  int hi = !(x >> 4 ^ 3);          // (1)
  int lo = !((9 + ~(x ^ 48) + 1) >> 31); // (2)
  return hi & lo;
}
```

1.  Every digit `'0'..'9'` has high bits equal to `0x3` (i.e. `x >> 4 == 3`). Zero it with `x >> 4 ^ 3`, then `!` to pin `x` into `0x30..0x3F`.
2.  `~(x ^ 48) + 1` is `-(x ^ 0x30)`; since the high nibble is already `0x3`, `x ^ 0x30` extracts exactly the low digit `d`. So `9 + (-d) = 9 - d`: if `d ≤ 9` the result is non-negative (sign bit 0); if `d ≥ 10` it's negative (sign bit 1). `>> 31` takes the sign bit, and `!` turns it into "is it ≤ 9?"

!!! tip "The recurring trick: arithmetic `>> 31` = extract the sign"
    On a 32-bit two's-complement value, `x >> 31` smears the sign bit across the whole word: `0x00000000` for non-negative, `0xFFFFFFFF` for negative. It doubles as both a **sign test** and an **all-zeros/all-ones mask generator** — the master key of this lab.

### conditional — forging a mask from a boolean { data-toc-label="conditional" }

Implement `x ? y : z` without `?:`. The idea: turn "is `x` truthy?" into an all-ones-or-all-zeros mask, then use it to pick `y` or `z`.

``` c
int conditional(int x, int y, int z) {
  int mask = ~!x + 1;          // (1)
  return ((mask ^ y) & y) ^ (mask & z); // (2)
}
```

1.  `!x` collapses any nonzero to `0` and `0` to `1`. Then `~(..) + 1` negates:
    <br>`x != 0` → `mask = 0x00000000`; `x == 0` → `mask = 0xFFFFFFFF`.
2.  Just substitute to verify:
    <br>`x` truthy (`mask=0`): `(0^y)&y ^ (0&z) = y`;
    <br>`x` falsy (`mask=~0`): `(~y & y) ^ z = 0 ^ z = z`.

"Boolean → all-ones/all-zeros mask → pick one of two" is the universal recipe for branch-like problems — `isLessOrEqual` below rests on the same idea.

### isLessOrEqual — comparison that dodges overflow { data-toc-label="isLessOrEqual" }

The naive `x <= y` checks `y - x >= 0`, but subtracting operands of opposite sign can overflow. The fix is to **split into same-sign and opposite-sign cases**.

``` c
int isLessOrEqual(int x, int y) {
  int diff_sign = (y + (~x + 1)) >> 31 & 1; // (1)
  int sx = x >> 31 & 1, sy = y >> 31 & 1;
  int diff_signbit = sx ^ sy;               // (2)
  return (!diff_sign & !diff_signbit)       // (3)
       | (sx & diff_signbit);
}
```

1.  When the signs match, `y - x` can't overflow, and its sign bit is the answer: `≥ 0` means `x ≤ y`.
2.  `sx ^ sy` detects opposite signs.
3.  Merge the two branches:
    <br>**same sign** (`diff_signbit = 0`): the result is the sign of `y - x`, i.e. `!diff_sign`;
    <br>**opposite sign** (`diff_signbit = 1`): the negative one is smaller, so just check whether `x` is negative (`sx`) — `x < 0 ≤ y` guarantees `x ≤ y`.

### logicalNeg — implementing `!` without `!` { data-toc-label="logicalNeg" }

`!x` asks "is `x` zero?" The key insight: **for any number except 0, either it or its negation has its sign bit set**; only `0` and its negation are both non-negative.

``` c
int logicalNeg(int x) {
  int sign  = x >> 31 & 1;          // (1)
  int nsign = (~x + 1) >> 31 & 1;   // sign bit of -x
  return ~(sign | nsign) << 31 >> 31 & 1; // (2)
}
```

1.  Take the sign bits of `x` and `-x`. When `x = 0` both are 0; when `x != 0` at least one is 1 (including the `Tmin` edge case, which stays negative under negation).
2.  `sign | nsign` is 0 exactly when `x == 0`, else 1. Invert it, smear the low bit across the word with `<< 31 >> 31`, and `& 1` — logical negation, achieved.

### howManyBits — binary-searching the most significant bit { data-toc-label="howManyBits" }

Find the minimum number of bits to represent `x` in two's complement. This is Data Lab's finale, and its logic builds in layers.

``` c
int howManyBits(int x) {
  int v = (x >> 31) ^ x;   // (1)
  int b16, b8, b4, b2, b1;
  b16 = !!(v >> 16) << 4; v >>= b16; // (2)
  b8  = !!(v >>  8) << 3; v >>= b8;
  b4  = !!(v >>  4) << 2; v >>= b4;
  b2  = !!(v >>  2) << 1; v >>= b2;
  b1  = !!(v >>  1) << 0; v >>= b1;
  return b16 + b8 + b4 + b2 + b1 + v + 1; // (3)
}
```

1.  Normalize first: for `x ≥ 0`, `x >> 31 = 0` so `v = x`; for `x < 0`, `v = ~x`. A negative number's width is set by its highest `0` bit, and inverting turns that into the highest `1` bit — unifying it with the positive case.
2.  Binary-search the top set bit: first ask "anything in the high 16 bits?", and if so record weight 16 and shift them away; then repeat for 8, 4, 2, 1. Each step uses `!!` to squash "nonzero" into `0/1`.
3.  Summing the five weights gives the index of the top significant bit; `v` has been reduced to 0 or 1 by now; add `+1` for the sign bit to get the total width.

!!! example "Why the `+1`"
    Two's complement always spends one bit on the sign. Take `howManyBits(12) = 5`: `12 = 0b01100`, whose top significant bit is 4 bits of magnitude — plus 1 sign bit = 5. Meanwhile `howManyBits(-1) = 1`, because `~(-1) = 0` needs only a single sign bit.

---

## Floating-Point Problems

Here the game isn't operator budgets but your grasp of IEEE 754's three fields — **sign `s`, exponent `exp`, fraction `frac`**. Single precision lays them out as `1 · 8 · 23` bits.

### floatScale2 — multiply a float by 2 { data-toc-label="floatScale2" }

In float-land, ×2 is usually just "exponent plus one" — but denormals and special values need care.

``` c
unsigned floatScale2(unsigned uf) {
  unsigned exp  = uf & (0xFF << 23);   // (1)
  unsigned frac = uf & 0x7FFFFF;
  unsigned sign = uf & (0x1 << 31);

  if (exp == (0xFFu << 23)) return uf;         // (2)
  if (exp == 0) return sign | (frac << 1);      // (3)
  exp += (1 << 23);                             // (4)
  return (exp == (0xFFu << 23)) ? (sign | exp) : (sign | exp | frac);
}
```

1.  Three lines carve out the exponent, fraction, and sign fields. (The original code does a `uf << 1 & ... >> 1` dance that's equivalent to masking directly.)
2.  `exp` all ones → `±∞` or `NaN`; `2 * x` is still itself, so return unchanged.
3.  `exp == 0` → denormal; just shift the fraction left by one to double it. The elegance: if the top fraction bit carries into the exponent field, `frac << 1` **automatically** produces the smallest normal number — no special case needed.
4.  Normal number: bump the exponent. If that overflows to all ones, return the corresponding infinity (dropping the fraction); otherwise reassemble `sign | exp | frac`.

### floatFloat2Int — float to integer { data-toc-label="floatFloat2Int" }

Equivalent to C's `(int) f`: compute the integer part of `1.frac × 2^E` bit by bit.

``` c
int floatFloat2Int(unsigned uf) {
  int exp  = (uf >> 23) & 0xFF;
  int frac = uf & 0x7FFFFF;
  int sign = (uf >> 31) & 1;
  int E    = exp - 127;            // (1)

  if (E < 0)  return 0;            // (2)
  if (E > 30) return 0x80000000;   // (3)

  frac |= (1 << 23);               // (4)
  frac = (E <= 23) ? (frac >> (23 - E)) : (frac << (E - 23)); // (5)
  return sign ? -frac : frac;
}
```

1.  Remove the bias to recover the true exponent `E`.
2.  `E < 0` means `|f| < 1`, so the integer part truncates to 0.
3.  `E > 30` exceeds `int`'s range (including `∞`, `NaN`); by convention, return `0x80000000`.
4.  Restore the implicit leading 1 that IEEE 754 omits, yielding the full 24-bit mantissa `1.frac`.
5.  The mantissa currently carries 23 fractional bits. To get the integer value, "move the binary point" into place: for `E ≤ 23`, right-shift away the excess fraction (truncation); for `E > 23`, left-shift to scale up. Finally apply the sign.

### floatPower2 — compute 2.0^x { data-toc-label="floatPower2" }

Construct the bit pattern of `2^x` directly, branching on whether `x` lands in the normal or denormal range.

``` c
unsigned floatPower2(int x) {
  if (x > 127)   return 0xFF << 23;        // (1)
  if (x < -149)  return 0;                 // (2)
  if (x >= -126) return (x + 127) << 23;   // (3)
  return 1 << (149 + x);                   // (4)
}
```

1.  `x > 127` exceeds the largest normal exponent — overflow to `+∞`.
2.  `x < -149` is smaller than the tiniest denormal, so return `0`.
3.  Normal range `[-126, 127]`: `2^x` has a zero fraction and exponent field `x + 127`, shifted into place.
4.  Denormal range `[-149, -127]`: the smallest denormal is `2^-149` (fraction's lowest bit set). So `2^x` just places that single 1 at bit `149 + x`.
