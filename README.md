# csapp-labs

Solutions and implementation notes for CMU 15-213 and
*Computer Systems: A Programmer’s Perspective*, third edition.

[Read the notes](https://leexsean.github.io/csapp-labs/)

## Contents

| Lab | Focus | Result |
| --- | --- | --- |
| [Data Lab](https://leexsean.github.io/csapp-labs/labs/data-lab/) | Bitwise integer and floating-point operations | 36/36 |
| [Bomb Lab](https://leexsean.github.io/csapp-labs/labs/bomb-lab/) | GDB and x86-64 reverse engineering | 6 + secret |
| [Attack Lab](https://leexsean.github.io/csapp-labs/labs/attack-lab/) | Buffer overflows, code injection, ROP | 5/5 |
| [Cache Lab](https://leexsean.github.io/csapp-labs/labs/cache-lab/) | LRU simulation and blocked transpose | 53/61 |
| [Shell Lab](https://leexsean.github.io/csapp-labs/labs/shell-lab/) | Processes, signals, job control | 16/16 traces |
| [Malloc Lab](https://leexsean.github.io/csapp-labs/labs/malloc-lab/) | Segregated free lists and in-place reallocation | 97/100 |
| [Proxy Lab](https://leexsean.github.io/csapp-labs/labs/proxy-lab/) | Concurrent HTTP proxy with caching | 70/70 |
| [SFS Lab](https://leexsean.github.io/csapp-labs/labs/sfs-lab/) | File operations and synchronization | 12/12 correctness |

Results are from the lab graders; each writeup includes implementation details
and verification commands. SFS is a separate, AI-assisted extension. Its basic
exercise ends at correctness with one mutex; finer-grained locking and filesystem
extensions are optional.

## Use

To preview the notes locally:

```sh
python -m venv .venv
. .venv/bin/activate
pip install mkdocs-material
mkdocs serve
```

The site is built from [docs/](docs/) with MkDocs Material and published through
[GitHub Actions](.github/workflows/deploy.yml).

Thanks to [virgiling](https://github.com/virgiling) for the
[lab notes](https://virgiling.wiki/) that helped with these solutions.
