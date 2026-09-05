# CS:APP Labs

Lab solutions and notes for *Computer Systems: A Programmer’s Perspective*
(3rd edition), CMU 15-213.

[Read the notes](https://leexsean.github.io/csapp-labs/) · [Browse the source](https://github.com/LeeXSean/csapp-labs)

Every completed solution below is checked against the lab's actual grader; the result is
shown in the table and at the top of each writeup.

| # | Lab | Focus | Result | Writeup |
|:-:|-----|-------|--------|---------|
| 01 | **Data Lab** | Bit manipulation, integer & float encoding | **36/36** | [Notes →](https://leexsean.github.io/csapp-labs/labs/data-lab/) |
| 02 | **Bomb Lab** | Reverse engineering · GDB · x86-64 | **6 + secret** | [Notes →](https://leexsean.github.io/csapp-labs/labs/bomb-lab/) |
| 03 | **Attack Lab** | Buffer overflow · code injection · ROP | **5/5** | [Notes →](https://leexsean.github.io/csapp-labs/labs/attack-lab/) |
| 04 | **Cache Lab** | LRU cache simulator · blocked transpose | **53/61** | [Notes →](https://leexsean.github.io/csapp-labs/labs/cache-lab/) |
| 05 | **Shell Lab** | Processes · signals · job control | **16/16** | [Notes →](https://leexsean.github.io/csapp-labs/labs/shell-lab/) |
| 06 | **Malloc Lab** | Allocator · segregated free lists | **97/100** | [Notes →](https://leexsean.github.io/csapp-labs/labs/malloc-lab/) |
| 07 | **Proxy Lab** | Concurrent, caching HTTP proxy | **70/70** | [Notes →](https://leexsean.github.io/csapp-labs/labs/proxy-lab/) |
| 08 | **SFS Lab** *(AI-assisted)* | On-disk file system · fine-grained locking | **12/12 correctness** | [Notes →](https://leexsean.github.io/csapp-labs/labs/sfs-lab/) |

<details>
<summary><b>A few design highlights</b></summary>

> - **Data Lab** — every integer puzzle solved within its operator budget; floats manipulated purely at the bit level.
> - **Bomb Lab** — all six phases plus the hidden phase (a recursive binary-tree search).
> - **Attack Lab** — code injection for `ctarget`; a ROP chain that defeats NX and ASLR for `rtarget`.
> - **Cache Lab** — an `O(E)` LRU simulator; the 64×64 transpose splits each 8×8 block into four 4×4 sub-blocks, staging data in `B` to dodge conflict misses.
> - **Shell Lab** — signal masking erases the `fork`/`addjob` race; `sigsuspend` replaces the busy-wait in `waitfg`.
> - **Malloc Lab** — segregated explicit free lists, footer-less allocated blocks via a `prev_alloc` bit, and a `realloc` that grows in place before copying.
> - **Proxy Lab** — one detached worker per connection; binary-safe response relay and a reader–writer-locked approximate-LRU cache.
> - **SFS Lab** — explicitly AI-assisted; atomic rename and synchronization pass all 12 correctness traces; finer-grained locks and filesystem extensions are optional.

</details>

## The notes site

The writeups live in [`docs/`](docs/) as an [MkDocs Material](https://squidfunk.github.io/mkdocs-material/) site and are published to GitHub Pages automatically on every push (see [`.github/workflows/deploy.yml`](.github/workflows/deploy.yml)). To run it locally:

```bash
python -m venv .venv && source .venv/bin/activate
pip install mkdocs-material
mkdocs serve            # then open http://127.0.0.1:8000
```

## Acknowledgment

Thanks to [virgiling](https://github.com/virgiling) for the helpful [blog](https://virgiling.wiki/) that guided me through these labs.
