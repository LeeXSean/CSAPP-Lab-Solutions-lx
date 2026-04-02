<div align="center">

# CS:APP Labs

*Computer Systems: A Programmer's Perspective* (3rd ed.) Lab Solutions

[English](#english) | [中文](#中文)

</div>

---

## English

### Progress

> All completed labs pass the official grading scripts with full marks.

- ✅ [Data Lab](Data_Lab/) — Bit manipulation, integer/float encoding
- ✅ [Bomb Lab](Bomb_Lab/) — Reverse engineering, GDB, x86-64 assembly
- ✅ [Attack Lab](Attack_Lab/) — Buffer overflow, ROP chain, ASLR bypass
- ✅ [Cache Lab](Cache_Lab/) — Cache simulation, matrix transpose optimization
- ✅ [Shell Lab](Shell_Lab/) — Processes, signals, job control
- 🔄 Malloc Lab — In Progress
- 🔄 Proxy Lab — In Progress

### Lab Details

<details>
<summary><b>Data Lab</b></summary>

> Implement integer and floating-point operations using only bitwise operators,
> under strict constraints on allowed operators and operation count.

</details>

<details>
<summary><b>Bomb Lab</b></summary>

> Defuse a binary bomb by reverse engineering its phases with GDB.
> Includes cracking a hidden phase backed by a binary tree recursive search.

</details>

<details>
<summary><b>Attack Lab</b></summary>

> Exploit stack buffer overflows to hijack control flow.
> - Phase 1–3: code injection
> - Phase 4–5: ROP chain to bypass NX and ASLR

</details>

<details>
<summary><b>Cache Lab</b></summary>

> **Part A** — Implement an LRU cache simulator in C,
> supporting arbitrary `(S, E, b)` parameters.
>
> **Part B** — Optimize matrix transpose for cache performance.
> The 64x64 case splits each 8x8 block into four 4x4 sub-blocks,
> repurposing unused regions of B as a staging buffer to minimize conflict misses.

</details>

<details>
<summary><b>Shell Lab</b></summary>

> Implement a Unix shell (`tsh`) with:
> - Foreground / background job control
> - Signal handling (`SIGINT`, `SIGTSTP`, `SIGCHLD`)
> - Built-in commands: `jobs`, `fg`, `bg`, `quit`
>
> Signal masking eliminates race conditions between `fork` and `addjob`.

</details>

---

## 中文

### 实验进度

> 所有已完成实验均通过官方评分脚本，满分通过。

- ✅ [Data Lab](Data_Lab/) — 位运算、整数/浮点数编码
- ✅ [Bomb Lab](Bomb_Lab/) — 逆向工程、GDB、x86-64 汇编
- ✅ [Attack Lab](Attack_Lab/) — 栈溢出、ROP 链、ASLR 绕过
- ✅ [Cache Lab](Cache_Lab/) — 缓存模拟器、矩阵转置优化
- ✅ [Shell Lab](Shell_Lab/) — 进程、信号、作业控制
- 🔄 Malloc Lab — 进行中
- 🔄 Proxy Lab — 进行中

### 各实验说明

<details>
<summary><b>Data Lab</b></summary>

> 在禁用控制流语句、仅允许位运算符且限制运算符数量的约束下，
> 实现整数和浮点数的底层操作。

</details>

<details>
<summary><b>Bomb Lab</b></summary>

> 使用 GDB 对二进制炸弹进行逆向调试，逐一破解每个阶段。
> 包含一个基于二叉树递归搜索的隐藏关卡（Secret Phase）。

</details>

<details>
<summary><b>Attack Lab</b></summary>

> 利用栈缓冲区溢出劫持程序控制流。
> - 第 1–3 阶段：代码注入
> - 第 4–5 阶段：在开启 NX 和 ASLR 的情况下构造 ROP 链完成攻击

</details>

<details>
<summary><b>Cache Lab</b></summary>

> **Part A** — 用 C 实现支持任意 `(S, E, b)` 参数的 LRU 缓存模拟器。
>
> **Part B** — 优化矩阵转置的缓存命中率。
> 64x64 情况下将每个 8x8 块拆分为四个 4x4 子块，
> 借用 B 矩阵的空闲区域作为临时缓冲，最小化冲突缺失。

</details>

<details>
<summary><b>Shell Lab</b></summary>

> 实现支持前台/后台作业控制的 Unix Shell（`tsh`）：
> - 信号处理：`SIGINT`、`SIGTSTP`、`SIGCHLD`
> - 内置命令：`jobs`、`fg`、`bg`、`quit`
>
> 通过信号屏蔽消除 `fork` 与 `addjob` 之间的竞争条件。

</details>

---

## Acknowledgment / 致谢

Special thanks to [virgiling](https://github.com/virgiling) for the helpful [blog](https://virgiling.wiki/) that provided great guidance throughout these labs.

特别感谢 [virgiling](https://github.com/virgiling) 大佬，感谢大佬写的[博客](https://virgiling.wiki/)，在做实验期间提供了很大的帮助。
