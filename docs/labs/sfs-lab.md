---
title: SFS Lab
description: A small on-disk file system you implement and then make thread-safe and scalable.
---

# SFS Lab · A Multithreaded File System

<p class="article-meta">File systems &amp; concurrency <span class="dot">·</span> <span class="pill pill--dev">Developing</span> <span class="dot">·</span> <a href="https://github.com/LeeXSean/sfslab-local-handout">Handout</a></p>

!!! warning "Status: writeup in progress"
    I packaged the lab for offline self-study and I'm working through it — the full writeup, with the on-disk walkthrough and the locking design, lands after Proxy Lab. For now, here's the shape of the lab and its on-disk format.

SFS — the Shark File System — is a small but real on-disk file system: files, a directory, and free-space management, all persisted to a disk image. You implement it in a single file, `sfs-disk.c`, against a fixed API. What makes it more than a data-structure exercise is the second half: the file system has to be **thread-safe *and* scalable**, not merely correct.

!!! abstract "The assignment"
    Implement `sfs-disk.c` against the interface in `sfs-api.h`, in two parts:

    - **Correctness** — finish the three operations left unimplemented in the handout: `sfs_getpos`, `sfs_seek`, and `sfs_rename`. `rename` has to be **atomic**: if the new name already exists, it is replaced with no window in which a concurrent thread sees it missing.
    - **Concurrency** — add locking so the file system is thread-safe *and* actually scales, so that operations on independent files proceed in parallel. Grading compares you against a one-global-mutex baseline: coarse locking passes correctness but forfeits the performance score.

## The disk image

An SFS disk is just a byte array — an `mmap`'d file — divided into fixed **512-byte blocks**. Block 0 is the **super block**; every block is found by number, so block *N* lives at byte offset `512 * N`.

``` text
   an SFS disk = an mmap'd file, split into 512-byte blocks
   +---------+---------+---------+---------+- - -+
   |  block  |  block  |  block  |  block  |     |
   |    0    |    1    |    2    |    3    | ... |
   | (super) |         |         |         |     |
   +---------+---------+---------+---------+- - -+
   block N starts at byte offset  512 * N
```

Every block except the super block opens with a 12-byte header — a 4-byte **type** tag (`FREE` / `FILE` / `DIR`) plus 4-byte **prev** and **next** block numbers. A `FILE` block uses all 500 remaining bytes for data; a `DIR` block uses 20 bytes of padding followed by fifteen 32-byte directory entries. Those `prev`/`next` fields are the whole trick: blocks are threaded into linked lists.

``` text
   FILE block (512 B)
   +--------------------------------+
   | type[4] | prev[4] | next[4]    |
   +--------------------------------+
   | 500 B file data                |
   +--------------------------------+

   DIR block (512 B)
   +--------------------------------+
   | type[4] | prev[4] | next[4]    |
   +--------------------------------+
   | 20 B unused / padding          |
   +--------------------------------+
   | 15 x 32 B directory entries    |
   +--------------------------------+

   super block (block 0, 512 B)
   +--------------------------------+
   | magic[8] + n_blocks[4]         |
   | freelist[4] + next_rootdir[4]  |
   +--------------------------------+
   | 12 B unused / padding          |
   +--------------------------------+
   | 15 x 32 B root-dir entries     |
   +--------------------------------+

   type[4] = FREE | FILE | DIR
```

So the entire file system is a handful of linked lists of blocks:

``` text
   a file = a chain of FILE blocks, linked by each header's prev/next:

     dir_entry.first_block
              |
              v
            [FILE] <-> [FILE] <-> [FILE] ---> 0     (0 = end)

   root directory = 15 entries in the super block, then a chain of
                    DIR blocks containing 32-byte entries
                    { first_block, size, name[24] }
   free space     = unused blocks threaded onto a free list
```

A block number of `0` in any field means "none" — the on-disk equivalent of a null pointer (block 0 is the super block, so it is never a valid link target).

## The operations

The API is a small POSIX-flavored set. Positions are per-descriptor; the file system is a single flat directory.

| Group | Operations | Notes |
|-------|-----------|-------|
| Image | `sfs_format`, `sfs_mount`, `sfs_unmount` | create / attach / detach a disk image |
| Files | `sfs_open`, `sfs_close` | open-or-create; returns an SFS "descriptor" |
| I/O | `sfs_read`, `sfs_write` | short counts allowed; `write` grows the file |
| Position | `sfs_getpos`, `sfs_seek` | **to implement**; `seek` clamps to `[0, size]` |
| Namespace | `sfs_remove`, `sfs_rename` | **`rename` to implement**, and it must be atomic |
| Listing | `sfs_list` | one name per call via an opaque cookie |

Two carry non-obvious contracts. **`rename`** must atomically replace an existing target. And **`sfs_list`** hands back one filename per call through a cookie you must not skip — it has to stay coherent even as other threads create, delete, or rename entries mid-iteration (which is also why you must never mutate the directory from inside a list loop).

## Part 1 · The missing pieces { data-toc-label="Part 1" }

`sfs_getpos` and `sfs_seek` are the gentle warm-up: report and adjust a descriptor's position, clamping a seek into `[0, file size]` (deliberately friendlier than `lseek`). `sfs_rename` is the first real one — walking the directory chain, moving an entry, and collapsing an existing target without ever leaving the namespace in a half-updated state.

## Part 2 · Locking, for correctness and scale { data-toc-label="Part 2" }

This is the actual lab. The provided baseline wraps every call in one global mutex: correct, but every operation serializes, so nothing scales. The goal is **fine-grained locking** — think per-file and per-block rather than one big lock — so that two threads writing different files don't wait on each other, while the tricky invariants still hold:

``` text
   baseline:  [ one global mutex ]   correct, but every op serializes

   goal:      per-file / per-block locks
              - ops on independent files run in parallel
              - rename stays atomic; list stays coherent
              - a fixed lock order keeps it deadlock-free
```

The interesting tension is that the shared structures — the directory chain, the free list — are exactly the ones every operation touches, so naive fine-grained locking either reintroduces a global bottleneck or opens the door to deadlock and torn updates. Finding a locking discipline that is provably safe *and* measurably faster than the baseline is the whole point.
