---
title: Bomb Lab
description: Defusing a binary bomb phase by phase — reading x86-64 assembly in GDB.
---

# Bomb Lab · Defusing the Bomb

<p class="article-meta">Reverse engineering <span class="dot">·</span> Keywords: GDB, x86-64, jump tables, recursion, BST <span class="dot">·</span> <a href="https://github.com/LeeXSean/csapp-labs/tree/main/Bomb_Lab">Source</a></p>

!!! success "Verified locally"
    `./bomb psol.txt` defuses all **6 phases plus the secret stage** — ending on *"Congratulations! You've defused the bomb!"*

A binary that reads a line, checks it against something hidden, and calls `explode_bomb` if you're wrong. Six phases, plus a seventh you have to *discover*. The only tool that matters is GDB, and the only skill is reading intent out of assembly.

!!! abstract "The method"
    The bomb ships with no source for the phases — only the executable. Every phase follows the same anatomy, and so does the way in:

    1. `break phase_n`, `run`, then `disas` to see the phase's code.
    2. Find the conditional branch that guards the call to `explode_bomb`. The condition needed to **skip** that call is your constraint.
    3. Read the operands being compared — dump memory with `x/s`, `x/d`, print registers — and work **backward** to an input that satisfies them.

    A useful reflex: every `je/jne/jle/ja/...` that jumps *toward* `explode_bomb` is a wrong turn, so its negation points straight at the solution.

---

## Phase 1 · a plain string compare { data-toc-label="Phase 1" }

The gentlest phase. It hands your input and a fixed pointer to `strings_not_equal` (which, like `strcmp`, returns `0` when the two strings are equal) and explodes unless the result is zero:

``` asm
mov    $0x402400,%esi          ; arg2 = a fixed string in .rodata
callq  strings_not_equal       ; %eax = 0 iff input == that string
test   %eax,%eax               ; set flags from %eax
je     400ef7                  ; %eax == 0 -> jump past the bomb
callq  explode_bomb            ; ...otherwise, boom
```

So survival means `strings_not_equal` returns `0`, i.e. your line **equals the string at `0x402400`**. There's nothing to compute — just read that address in GDB:

``` gdb
(gdb) x/s 0x402400
0x402400: "Border relations with Canada have never been better."
```

> **`Border relations with Canada have never been better.`**

## Phase 2 · a loop over six numbers { data-toc-label="Phase 2" }

`read_six_numbers` parses six integers onto the stack. Two checks follow. First, the very first number is pinned to `1`:

``` asm
callq  read_six_numbers
cmpl   $0x1,(%rsp)             ; numbers[0] must be 1
je     400f30                  ; ok -> enter the loop setup
callq  explode_bomb
```

Then a loop walks the array with `%rbx` (a moving pointer) up to `%rbp` (one past the end), checking that each element is **twice** its predecessor — `add %eax,%eax` is just `%eax * 2`:

``` asm
mov    -0x4(%rbx),%eax         ; eax = previous element
add    %eax,%eax               ; eax = previous * 2
cmp    %eax,(%rbx)             ; current == previous * 2 ?
jne    ... explode
add    $0x4,%rbx               ; advance to the next int
cmp    %rbp,%rbx               ; reached the end?
jne    ... (loop)
```

Starting from `1` and doubling five times gives a geometric sequence:

> **`1 2 4 8 16 32`**

## Phase 3 · a switch / jump table { data-toc-label="Phase 3" }

Here `sscanf` reads **two** integers (its format string lives at `0x4025cf` — inspect it and you'll see `"%d %d"`). The return value, the count of items parsed, must be at least `2`:

``` asm
lea    0x8(%rsp),%rdx          ; &first
lea    0xc(%rsp),%rcx          ; &second
mov    $0x4025cf,%esi          ; "%d %d"
callq  sscanf
cmp    $0x1,%eax
jg     400f6a                  ; parsed > 1 value -> continue
callq  explode_bomb
```

The first number selects a branch through a **jump table** — the shape a `switch` takes once compiled. It is bounded to `0–7`, then used to index a table of code addresses at `0x402470`:

``` asm
cmpl   $0x7,0x8(%rsp)          ; first must be <= 7 (unsigned compare)
ja     ... explode
mov    0x8(%rsp),%eax
jmpq   *0x402470(,%rax,8)      ; goto table[first]
```

Each case loads a constant into `%eax`, and the final check demands the **second** number equal it. (Dump the table itself with `x/8a 0x402470` to read every case's target address.) Following the entry for `first = 1` lands on:

``` asm
mov    $0x137,%eax             ; case 1 -> 0x137 (= 311)
cmp    0xc(%rsp),%eax          ; second == 311 ?
je     ... defused
```

Any of the eight cases yields a valid answer; taking case `1`:

> **`1 311`**

## Phase 4 · a recursive function { data-toc-label="Phase 4" }

`sscanf` again reads two integers; this time *exactly* two, and the first is capped at `14`:

``` asm
cmp    $0x2,%eax               ; exactly two values parsed
jne    ... explode
cmpl   $0xe,0x8(%rsp)          ; first <= 14
jbe    ... continue
```

The first number is passed to `func4(first, 0, 14)`, whose result must be `0`, and the second number must also be `0`:

``` asm
mov    $0xe,%edx               ; hi = 14
mov    $0x0,%esi               ; lo = 0
mov    0x8(%rsp),%edi          ; x = first
callq  func4
test   %eax,%eax               ; func4 must return 0
jne    ... explode
cmpl   $0x0,0xc(%rsp)          ; second == 0
je     ... defused
```

`func4` is a **binary search** over `[lo, hi]`. It computes the midpoint (the `shr`/`sar` pair is just a signed divide-by-two that rounds toward zero), then recurses into one half — doubling the running result on the way back:

``` asm
mov    %edx,%eax
sub    %esi,%eax               ; hi - lo
...
sar    %eax                    ; (hi-lo)/2
lea    (%rax,%rsi,1),%ecx      ; mid = lo + (hi-lo)/2
cmp    %edi,%ecx
jle    400ff2                  ; mid <= x ? ...
lea    -0x1(%rcx),%edx         ; x < mid -> recurse on [lo, mid-1], result = 2*r
...
lea    0x1(%rcx),%esi          ; x > mid -> recurse on [mid+1, hi], result = 2*r+1
```

The result stays `0` only along a path that never takes the `2*r + 1` (go-right) branch. So `func4` returns `0` exactly for the midpoints reached by going left from `[0, 14]`:

``` text
   func4(x, 0, 14),  mid = (lo + hi) / 2 at each step:
       x == mid  ->  return 0
       x <  mid  ->  go left,  result = 2*r        (0 stays 0)
       x >  mid  ->  go right, result = 2*r + 1     (never 0 again)

   Follow the all-left spine; the matching x at each level returns 0:

       [0,14]  mid = 7   --  x = 7  -->  0
         |
         +--  [0,6]  mid = 3   --  x = 3  -->  0
                |
                +--  [0,2]  mid = 1   --  x = 1  -->  0
                       |
                       +--  [0,0]  mid = 0   --  x = 0  -->  0

   => func4 returns 0 for x in {0, 1, 3, 7}
```

Take `1`, and pair it with the required second `0`:

> **`1 0`** — and note what appending `DrEvil` here unlocks in the [secret phase](#the-secret-phase).

## Phase 5 · a table-lookup cipher { data-toc-label="Phase 5" }

The input must be **6 characters** long. Then each character is transformed and the six results must spell a target word. The transform: mask a character down to its **low 4 bits**, and use that as an index into a 16-byte table at `0x4024b0` — the string `"maduiersnfotvbyl"` (peek with `x/s 0x4024b0`):

``` asm
movzbl (%rbx,%rax,1),%ecx      ; ecx = input[i]
and    $0xf,%edx               ; keep the low nibble
movzbl 0x4024b0(%rdx),%edx     ; edx = table[nibble]
mov    %dl,0x10(%rsp,%rax,1)    ; append to the result buffer
...
mov    $0x40245e,%esi          ; target = "flyers"
callq  strings_not_equal
```

So the puzzle is: pick six bytes whose low nibbles index the letters of `"flyers"`. Reading the table, those letters sit at indices `9, 15, 14, 5, 6, 7`. Any characters with those low nibbles work — a printable set is one per column:

| target letter | `f` | `l` | `y` | `e` | `r` | `s` |
|---------------|-----|-----|-----|-----|-----|-----|
| table index (= low nibble needed) | 9 | 15 | 14 | 5 | 6 | 7 |
| a printable char with that low nibble | `)` `0x29` | `/` `0x2f` | `.` `0x2e` | `%` `0x25` | `&` `0x26` | `'` `0x27` |

> **`)/.%&'`**

## Phase 6 · reordering a linked list { data-toc-label="Phase 6" }

The hardest of the six, and worth taking slowly. It reads six numbers and enforces two properties: each is in `1–6`, and all are **distinct** (a nested loop compares every pair):

``` asm
mov    0x0(%r13),%eax
sub    $0x1,%eax
cmp    $0x5,%eax               ; (value - 1) <= 5  -> value in 1..6
jbe    ...
...
cmp    %eax,0x0(%rbp)          ; compare against every other value
jne    ...                     ; must differ -> distinct
```

Next it maps every value `x` to `7 - x`, and uses those to index into a six-node **linked list** in `.data` (each node is `{ int value; int pad; node *next; }`). It threads the nodes into the order your numbers specify, then verifies that order is **descending by node value**:

``` asm
mov    $0x7,%ecx
sub    (%rax),%edx             ; 7 - each input
...
mov    0x8(%rdx),%rdx          ; follow ->next to the chosen node
...
mov    0x8(%rbx),%rax
mov    (%rax),%eax
cmp    %eax,(%rbx)
jge    ...                     ; node.value >= next.value -> descending
```

So the recipe is: read the node values, sort them **descending**, and emit `7 - node` for each. Dumping the list (`x/12xw 0x6032d0` shows each node's value and `next`):

``` text
   node    1      2      3      4      5      6
   value   0x14c  0xa8   0x39c  0x2b3  0x1dd  0x1bb

   sort the values high -> low; that fixes the node order,
   and the input is 7 - node:

   value   0x39c  0x2b3  0x1dd  0x1bb  0x14c  0xa8
   node    3      4      5      6      1      2
   input   4      3      2      1      6      5
```

> **`4 3 2 1 6 5`**

---

## The secret phase

There's a seventh phase, and finding it is half the puzzle. After all six are defused, `phase_defused` quietly re-parses the line you gave **phase 4** — this time as `"%d %d %s"` — and checks whether the trailing word is `"DrEvil"`:

``` asm
mov    $0x402622,%esi          ; "DrEvil"
lea    0x10(%rsp),%rdi         ; the %s captured from phase 4's line
callq  strings_not_equal       ; match -> call secret_phase
```

So phase 4's answer grows a third token: **`1 0 DrEvil`**. The secret phase then reads one integer (`1 <= n <= 1001`, from a `(n-1) <= 0x3e8` check) and calls `fun7` on a binary search tree rooted at `0x6030f0`; the return value must be exactly `2`:

``` asm
mov    (%rdi),%edx             ; node value
cmp    %esi,%edx               ; compare node value with input n
jle    ...                     ; node <= n -> handle match or descend right
                               ; fall through when n < node -> left child (0x8)
                               ; n > node -> right child (0x10); n == node -> return 0
```

`fun7` **encodes the path it walks** into its return value: each step left multiplies by `2`, each step right does `2·r + 1`, and a match returns `0`. Read backward, a result of `2` factors uniquely as `2 = 2·(2·0 + 1)` — that is, *left, then right, then a match*:

``` text
   fun7 walks a BST; each step folds into the return value:
       n <  node  ->  go left,   return 2*r
       n >  node  ->  go right,  return 2*r + 1
       n == node  ->  match,     return 0

   Want return = 2.  Factor it to read the path back off:

       2  =  2 * ( 2*0 + 1 )
       |        |      `-- match          -> 0
       |        `--------- step RIGHT     -> 2*0 + 1
       `------------------ step LEFT      -> 2*1

   So the path is  left, then right, then match:

          root (0x6030f0)
          /
       node               (n < root  -> left)
          \
         [ 22 ]           (n > node  -> right, and n == 22 -> match)

   =>  n = the value at  root -> left -> right  =  22
```

So `n` must equal the value stored at *root -> left -> right*, which is `22` — the one number that threads exactly the `left, right, match` path:

> **`22`**
