---
title: Home
hide:
  - toc
---

# CS:APP Lab Notes

CMU 15-213 · Computer Systems

From a single bit flip to a concurrent web proxy. Source-backed notes on the implementations, design choices, and the *why* behind them.

[Start with Data Lab →](labs/data-lab.md) · [Source on GitHub](https://github.com/LeeXSean/csapp-labs)

## The Labs

### [Data Lab](labs/data-lab.md)

<span class="lab-meta">01 · Data · btest 36/36</span>

Integer and floating-point operations built from bitwise primitives only, under strict operator-count limits.

### [Bomb Lab](labs/bomb-lab.md)

<span class="lab-meta">02 · Reverse Eng. · defused</span>

Defusing a binary bomb phase by phase in GDB, reading x86-64 assembly, and cracking a hidden stage backed by a binary tree.

### [Attack Lab](labs/attack-lab.md)

<span class="lab-meta">03 · Exploits · 5/5 pass</span>

Hijacking control flow through stack buffer overflows — from code injection to a ROP chain that defeats NX and ASLR.

### [Cache Lab](labs/cache-lab.md)

<span class="lab-meta">04 · Memory · 53/61</span>

An LRU cache simulator written from scratch, then a blocked matrix transpose tuned to drive misses to the floor.

### [Shell Lab](labs/shell-lab.md)

<span class="lab-meta">05 · Processes · 16/16 traces</span>

A job-control Unix shell, with signal masking around the `fork`/`addjob` race.

### [Malloc Lab](labs/malloc-lab.md)

<span class="lab-meta">06 · Allocation · 97/100</span>

`malloc`/`free`/`realloc` from the ground up, six designs deep — from one implicit list to twelve segregated rings.

### [Proxy Lab](labs/proxy-lab.md)

<span class="lab-meta">07 · Networking · 70/70</span>

An HTTP proxy that parses absolute URIs, rewrites hop-by-hop headers, and caches full responses under an approximate LRU policy.

### [SFS Lab](labs/sfs-lab.md)

<span class="lab-meta">08 · File Systems · 12/12 · AI-assisted</span>

A small mmap-backed file system with atomic rename, open-file lifetime tracking, and synchronization, with finer-grained locking as an optional refinement.

---

## About these notes

The first seven entries follow CMU's *Computer Systems: A Programmer's Perspective* (3rd ed.); SFS Lab is a separate file-system extension and is explicitly marked as AI-assisted. Every completed solution was run against its grader, with the result shown at the top of the writeup.
