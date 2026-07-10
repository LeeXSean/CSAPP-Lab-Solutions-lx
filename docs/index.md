---
title: Home
hide:
  - navigation
  - toc
---

<div class="hero" markdown>
<div class="eyebrow">CMU 15-213 · Computer Systems</div>

# CS:APP Lab Notes

<p class="lede">From a single bit flip to a concurrent web proxy. Source-backed notes on the implementations, design choices, and the <em>why</em> behind them.</p>

<div class="hero-actions" markdown>
[Start with Data Lab →](labs/data-lab.md){ .md-button .md-button--primary }
[Source on GitHub](https://github.com/LeeXSean/CSAPP-Lab-Solutions-lx){ .md-button }
</div>

</div>

## The Labs

<div class="grid cards" markdown>

-   <span class="lab-tag">01 · Data</span> <span class="pill pill--done">btest 36/36</span>

    ### [Data Lab](labs/data-lab.md)

    ---

    Integer and floating-point operations built from bitwise primitives only, under strict operator-count limits.

-   <span class="lab-tag">02 · Reverse Eng.</span> <span class="pill pill--done">defused</span>

    ### [Bomb Lab](labs/bomb-lab.md)

    ---

    Defusing a binary bomb phase by phase in GDB, reading x86-64 assembly, and cracking a hidden stage backed by a binary tree.

-   <span class="lab-tag">03 · Exploits</span> <span class="pill pill--done">5/5 pass</span>

    ### [Attack Lab](labs/attack-lab.md)

    ---

    Hijacking control flow through stack buffer overflows — from code injection to a ROP chain that defeats NX and ASLR.

-   <span class="lab-tag">04 · Memory</span> <span class="pill pill--done">53/61</span>

    ### [Cache Lab](labs/cache-lab.md)

    ---

    An LRU cache simulator written from scratch, then a blocked matrix transpose tuned to drive misses to the floor.

-   <span class="lab-tag">05 · Processes</span> <span class="pill pill--done">16/16 traces</span>

    ### [Shell Lab](labs/shell-lab.md)

    ---

    A job-control Unix shell, with signal masking around the `fork`/`addjob` race.

-   <span class="lab-tag">06 · Allocation</span> <span class="pill pill--done">97/100</span>

    ### [Malloc Lab](labs/malloc-lab.md)

    ---

    `malloc`/`free`/`realloc` from the ground up, six designs deep — from one implicit list to twelve segregated rings.

-   <span class="lab-tag">07 · Networking</span> <span class="pill pill--wip">In progress</span>

    ### [Proxy Lab](labs/proxy-lab.md)

    ---

    A caching, concurrent HTTP proxy that ties together sockets, concurrency, and synchronization.

-   <span class="lab-tag">08 · File Systems</span> <span class="pill pill--dev">Developing</span>

    ### [SFS Lab](labs/sfs-lab.md)

    ---

    A small on-disk file system, made thread-safe and scalable with fine-grained locking.

</div>

---

!!! quote "About these notes"
    The first seven entries follow CMU's *Computer Systems: A Programmer's Perspective* (3rd ed.); SFS Lab is a separate file-system extension. Every completed solution was run against its grader, with the result shown at the top of the writeup. Proxy Lab and SFS Lab are still in progress.
