# CS:APP Labs

[English](#csapp-labs-1) | [中文](#csapp-labs-2)

---

## CS:APP Labs <a name="csapp-labs-1"></a>

Solutions to the labs from *Computer Systems: A Programmer's Perspective* (CS:APP, 3rd ed.).

### Progress

| Lab | Status | Key Topics |
|-----|--------|------------|
| Data Lab | ✅ Full Score | Bit manipulation, integer/float encoding |
| Bomb Lab | ✅ Full Score | Reverse engineering, GDB, x86-64 assembly |
| Attack Lab | ✅ Full Score | Buffer overflow, ROP chain, ASLR bypass |
| Cache Lab | ✅ Full Score | Cache simulation, matrix transpose optimization |
| Shell Lab | ✅ Full Score | Processes, signals, job control |
| Malloc Lab | 🔄 In Progress | — |
| Proxy Lab | 🔄 In Progress | — |

### Labs

#### Data Lab [`Data_Lab/`](Data_Lab/)
Implement integer and floating-point operations using only bitwise operators, under strict constraints on allowed operators and operation count.

#### Bomb Lab [`Bomb_Lab/`](Bomb_Lab/)
Defuse a binary bomb by reverse engineering its phases with GDB. Includes cracking a hidden phase backed by a binary tree recursive search.

#### Attack Lab [`Attack_Lab/`](Attack_Lab/)
Exploit stack buffer overflows to hijack control flow. Phase 1–3 use code injection; Phase 4–5 construct a ROP chain to bypass NX and ASLR.

#### Cache Lab [`Cache_Lab/`](Cache_Lab/)
- **Part A**: Implement an LRU cache simulator in C, supporting arbitrary `(S, E, b)` parameters.
- **Part B**: Optimize matrix transpose for cache performance. The 64×64 case splits each 8×8 block into four 4×4 sub-blocks, repurposing unused regions of B as a staging buffer to minimize conflict misses.

#### Shell Lab [`Shell_Lab/`](Shell_Lab/)
Implement a Unix shell (`tsh`) supporting foreground/background job control, signal handling (SIGINT, SIGTSTP, SIGCHLD), and built-in commands (`jobs`, `fg`, `bg`, `quit`). Signal masking is used to eliminate race conditions between `fork` and `addjob`.

---

## CS:APP 实验 <a name="csapp-labs-2"></a>

《深入理解计算机系统》（CS:APP 第3版）核心实验的代码实现。

### 实验进度

| 实验 | 状态 | 核心知识点 |
|------|------|------------|
| Data Lab | ✅ 满分 | 位运算、整数/浮点数编码 |
| Bomb Lab | ✅ 满分 | 逆向工程、GDB、x86-64 汇编 |
| Attack Lab | ✅ 满分 | 栈溢出、ROP 链、ASLR 绕过 |
| Cache Lab | ✅ 满分 | 缓存模拟器、矩阵转置优化 |
| Shell Lab | ✅ 满分 | 进程、信号、作业控制 |
| Malloc Lab | 🔄 进行中 | — |
| Proxy Lab | 🔄 进行中 | — |

### 各实验说明

#### Data Lab [`Data_Lab/`](Data_Lab/)
在禁用控制流语句、仅允许位运算符且限制运算符数量的约束下，实现整数和浮点数的底层操作。

#### Bomb Lab [`Bomb_Lab/`](Bomb_Lab/)
使用 GDB 对二进制炸弹进行逆向调试，逐一破解每个阶段。包含一个基于二叉树递归搜索的隐藏关卡（Secret Phase）。

#### Attack Lab [`Attack_Lab/`](Attack_Lab/)
利用栈缓冲区溢出劫持程序控制流。第 1–3 阶段使用代码注入；第 4–5 阶段在开启 NX 和 ASLR 的情况下，构造 ROP 链完成攻击。

#### Cache Lab [`Cache_Lab/`](Cache_Lab/)
- **Part A**：用 C 实现支持任意 `(S, E, b)` 参数的 LRU 缓存模拟器。
- **Part B**：优化矩阵转置的缓存命中率。64×64 情况下将每个 8×8 块拆分为四个 4×4 子块，借用 B 矩阵的空闲区域作为临时缓冲，最小化冲突缺失。

#### Shell Lab [`Shell_Lab/`](Shell_Lab/)
实现支持前台/后台作业控制的 Unix Shell（`tsh`），包含 SIGINT、SIGTSTP、SIGCHLD 信号处理及内置命令（`jobs`、`fg`、`bg`、`quit`）。通过信号屏蔽消除 `fork` 与 `addjob` 之间的竞争条件。

---

*所有已完成实验均通过官方评分脚本，满分通过。*
