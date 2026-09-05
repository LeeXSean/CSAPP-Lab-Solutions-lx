---
title: SFS Lab
description: An AI-assisted SFS solution, with core correctness, optional extensions, and notes on locking.
---

# SFS Lab · Building a Small File System

<p class="article-meta">File systems &amp; concurrency <span class="dot">·</span> Correctness 12/12 <span class="dot">·</span> <a href="https://github.com/LeeXSean/csapp-labs/blob/main/SFS_Lab/sfslab/sfs-disk.c">sfs-disk.c</a></p>

!!! info "AI-assisted solution"
    This implementation and writeup were completed with AI assistance.

!!! success "Verified locally"
    `make && make test` → **12/12 correctness**, with ThreadSanitizer clean across three fuzzed schedules. Performance is a separate, optional experiment.

SFS is a fixed-layout file system stored in an `mmap`'d disk image. The API is small — open, close, read, write, seek, list, remove, rename — but the implementation must keep three things consistent at once:

1. the **directory namespace** (`name -> file`),
2. the **block graph** on disk, and
3. the **per-descriptor position** each opener sees.

The concurrency part is mostly about ownership: each piece of shared state gets one lock domain.

!!! abstract "The assignment"
    The basic exercise has two steps:

    - **Correctness** — finish the missing operations `sfs_getpos`, `sfs_seek`, and `sfs_rename`, with `rename` required to replace an existing target atomically.
    - **Concurrency** — make the file system thread-safe. Start with one mutex; fine-grained locking is a follow-up optimization.

    The code in this repository is an **extended solution**, based on the handout's `developer` branch. File sizing, zero-block empty files, directory growth, and Unix-style unlink are optional. They are described below to explain the code, not as prerequisites for finishing the lab.

    For the basic starter, use [sfslab on main](https://github.com/LeeXSean/sfslab). The `SFS_Lab/sfslab/` directory here contains completed functions; its adjacent archive contains the extension starter.

## The on-disk model

An SFS image is divided into fixed **512-byte blocks**. Block 0 is the **super block**; every other block starts with a 12-byte header:

``` text
super block (block 0)
+--------------------------------+
| magic[8] + n_blocks[4]         |
| freelist[4] + next_rootdir[4]  |
+--------------------------------+
| 12 bytes unused                |
+--------------------------------+
| 15 x 32-byte root entries      |
+--------------------------------+

ordinary block
+--------------------------------+
| type[4] | prev[4] | next[4]    |
+--------------------------------+
| payload depends on block type  |
+--------------------------------+
```

The block `type` is one of three values read from `sfs-disk.h`:

- `FREE` — block is on the free list,
- `FILE` — block contributes data to one file,
- `DIR` — block stores directory entries.

A `FILE` block spends 12 bytes on its header and leaves **500 bytes** for data:

``` text
FILE block (512 B)
+--------------------------------+
| type[4] | prev[4] | next[4]    |
+--------------------------------+
| 500 B file data                |
+--------------------------------+
```

A `DIR` block uses that same 12-byte header, then 20 bytes of padding, then **15 directory entries** of 32 bytes each:

``` text
DIR block (512 B)
+--------------------------------+
| type[4] | prev[4] | next[4]    |
+--------------------------------+
| 20 B unused                    |
+--------------------------------+
| 15 x 32-byte directory entries |
+--------------------------------+
```

Each directory entry is just:

``` c
typedef struct sfs_dir_entry_t {
    block_id first_block;
    uint32_t size;
    char name[24];
} sfs_dir_entry_t;
```

So a file is represented by one directory entry plus a linked chain of `FILE` blocks:

``` text
{name, size, first_block} --> [FILE] <-> [FILE] <-> [FILE] --> 0
```

The root directory begins inside the super block and grows by chaining `DIR` blocks through `next_rootdir` when those embedded 15 entries fill up.

### Optional extension: empty files without data blocks

The optional extension format adds one extra encoding in `sfs-disk.h`:

``` c
#define SFS_EMPTY_FILE_BLOCK UINT32_MAX
```

That creates a three-way distinction for `first_block`:

| `first_block` value | Meaning |
|---|---|
| `0` | unused directory slot |
| `SFS_EMPTY_FILE_BLOCK` | live empty file |
| any other block id | first data block of a live file |

This keeps empty files live without burning a 512-byte data block, while still making occupied directory slots unambiguous.

---

## One open file becomes three objects

The most important design choice is not on disk at all. SFS separates **the name in the directory**, **the shared state of one file**, and **the per-descriptor position**:

``` text
openFileDescTable[fd]
        |
        v
  sfs_mem_filedesc_t          one per open descriptor
  +------------------+
  | currPos          |
  | fileEntry -------+---+
  +------------------+   |
                         v
                   sfs_mem_file_t       one per open file
                   +----------------+
                   | refCount       |
                   | unlinked       |
                   | pthread lock   |
                   | diskFile ------+---+
                   +----------------+   |
                                        v
                                  sfs_dir_entry_t
                                  { name, size, first_block }
```

The two in-memory structs are:

``` c
typedef struct sfs_mem_file_t {
    uint32_t refCount;
    int tableIndex;
    int unlinked;
    pthread_mutex_t lock;
    sfs_dir_entry_t *diskFile;
    sfs_dir_entry_t unlinkedFile;
} sfs_mem_file_t;

typedef struct sfs_mem_filedesc_t {
    sfs_mem_file_t *fileEntry;
    size_t currPos;
} sfs_mem_filedesc_t;
```

This split gives two immediate properties:

- Opening the same file twice gives two descriptors with **independent** `currPos` values.
- Every opener still shares one `sfs_mem_file_t`, so file size, block chain, and unlink state have one mutex and one reference count.

### Optional extension: Unix-style unlink

The helper `unlinkDirectoryEntry` is the core of both `sfs_remove` and overwrite-style `sfs_rename`:

``` c
if (open != NULL) {
    pthread_mutex_lock(&open->lock);
    open->unlinkedFile = *entry;
    open->diskFile = &open->unlinkedFile;
    open->unlinked = 1;
    pthread_mutex_unlock(&open->lock);
} else if (entry->first_block != SFS_EMPTY_FILE_BLOCK) {
    freeBlocks(entry->first_block);
}
memset(entry, 0, sizeof *entry);
```

If the file is closed, its blocks can go back to the free list immediately. If it is open, the directory entry is copied into `unlinkedFile`, `diskFile` is redirected to that private copy, and the visible directory slot is cleared.

That gives SFS the Unix rule in compact form: removing a file destroys its **name** now, but destroys its **storage** only after the last open descriptor disappears.

`sfs_close` finishes the job. When `refCount` drops to zero, it checks `unlinked` and frees the saved block chain.

---

## Part 1 · Completing the API

### `getpos` and `seek` are descriptor-local

`sfs_getpos` is the simple one: validate `fd`, return `currPos`, unlock the descriptor slot.

`sfs_seek` is slightly more careful because it has to move backward without invoking signed-overflow undefined behavior:

``` c
if (delta < 0) {
    size_t distance = (size_t)(-(delta + 1)) + 1;
    position = distance > position ? 0 : position - distance;
} else {
    size_t distance = (size_t)delta;
    position =
        distance > file_size - position ? file_size : position + distance;
}
```

The rule matches the API comment in `sfs-api.h`: the result is always clamped into `[0, file_size]`. SFS deliberately does **not** allow a seek position past EOF.

The `-(delta + 1) + 1` pattern matters because directly negating the most negative `ssize_t` value is undefined.

### `rename` is one namespace transaction

The required behavior is stronger than “change the string in the directory entry.” If `new_name` already exists, SFS has to replace it **atomically**: no other thread should ever observe a gap where `new_name` does not exist.

The implementation keeps the directory and open-file tables write-locked for the entire operation:

``` c
pthread_rwlock_wrlock(&directoryLock);
pthread_rwlock_wrlock(&openTableLock);

sfs_dir_entry_t *old_entry = findDirectoryEntry(old_name, NULL, NULL);
if (old_entry == NULL) {
    pthread_rwlock_unlock(&openTableLock);
    pthread_rwlock_unlock(&directoryLock);
    return -ENOENT;
}

if (strcmp(old_name, new_name) == 0) {
    pthread_rwlock_unlock(&openTableLock);
    pthread_rwlock_unlock(&directoryLock);
    return 0;
}

sfs_dir_entry_t *new_entry = findDirectoryEntry(new_name, NULL, NULL);
if (new_entry != NULL)
    unlinkDirectoryEntry(new_entry);

size_t len = strlen(new_name);
memcpy(old_entry->name, new_name, len);
memset(old_entry->name + len, '\0', SFS_FILE_NAME_SIZE_LIMIT - len);

pthread_rwlock_unlock(&openTableLock);
pthread_rwlock_unlock(&directoryLock);
```

The key is that `unlinkDirectoryEntry(new_entry)` and the rename of `old_entry` happen under the same lock interval. Other threads see either the old namespace or the finished new one, never the half-updated state in between.

Because overwrite-rename uses the same unlink path as `sfs_remove`, an already-open target file keeps working after replacement. Its descriptors now point at `unlinkedFile`, and its blocks are reclaimed only on final close.

### Writing grows the file atomically on allocation failure

The graded API permits short writes, but this implementation takes an all-or-nothing path when a write needs more blocks: if the final size cannot be allocated, it returns `-ENOSPC` before touching the file.

The first step is to reserve all additional blocks up front:

``` c
if (endPos > fileAllocSize) {
    size_t fileNewAllocSize = roundUp(endPos, BLOCK_DATA_SIZE);
    uint32_t addlBlocks =
        (uint32_t)((fileNewAllocSize - fileAllocSize) / BLOCK_DATA_SIZE);

    firstNewId = allocateBlocks(addlBlocks, SFS_BLOCK_TYPE_FILE);
    if (firstNewId == 0) {
        pthread_mutex_unlock(&file->lock);
        pthread_mutex_unlock(&descriptorLocks[fd]);
        return -ENOSPC;
    }
}
```

Only after allocation succeeds does the write path start copying bytes into file blocks. If the file used to be empty, the first successful write swaps the empty-file sentinel for the new chain head:

``` c
if (first == SFS_EMPTY_FILE_BLOCK) {
    diskBlock = accessFileBlock(firstNewId);
    file->diskFile->first_block = firstNewId;
    firstNewId = 0;
}
```

If the file already had data, the new chain is attached only when the copy loop reaches the old tail:

``` c
sfs_block_file_t *nextBlock = accessFileBlock(diskBlock->h.next_block);
if (nextBlock == NULL) {
    nextBlock = accessFileBlock(firstNewId);
    diskBlock->h.next_block = firstNewId;
    nextBlock->h.prev_block = idOfBlock(&diskBlock->h);
    firstNewId = 0;
}
```

So allocation failure leaves the old file untouched, and successful growth preserves a valid block chain throughout.

### Reads and writes advance in 500-byte chunks

Once the current block is known, both `sfs_read` and `sfs_write` iterate one chunk at a time, never crossing a block boundary in a single copy:

``` c
size_t blockPos = currPos % BLOCK_DATA_SIZE;
size_t chunkSize =
    sizeMin(roundUp(currPos, BLOCK_DATA_SIZE) - currPos, toRead);

for (;;) {
    if (chunkSize > 0) {
        memcpy(buf, &diskBlock->data[blockPos], chunkSize);
        buf += chunkSize;
        toRead -= chunkSize;
    }
    if (toRead == 0)
        break;

    blockPos = 0;
    chunkSize = sizeMin(BLOCK_DATA_SIZE, toRead);
    diskBlock = accessFileBlock(diskBlock->h.next_block);
}
```

At an exact block boundary, the first `chunkSize` is zero, so the loop advances once before copying. Everywhere else, the first chunk consumes only the remainder of the current block. That keeps the logic uniform for both boundary and non-boundary positions.

---

## Part 2 · Optional refinement: finer-grained locks

The code uses five lock domains:

| Lock | Protects |
|---|---|
| `directoryLock` (`rwlock`) | directory slots, root-directory growth, namespace changes |
| `openTableLock` (`rwlock`) | table membership and lifetime, `refCount`, `unlinked`, table scans |
| `descriptorLocks[fd]` (`mutex`) | one descriptor slot and its `currPos` |
| `fileEntry->lock` (`mutex`) | `diskFile` target, one file's size, block chain, and contents |
| `allocationLock` (`mutex`) | free-list allocation and reclamation |

The lock order is fixed:

``` text
directory -> open table -> descriptor slot -> file -> allocator
```

Not every function uses every lock, but no function reverses that order.

### The hot path avoids the open-file table

The scalability pivot is `lockDescriptor(fd)`:

``` c
static sfs_mem_filedesc_t *lockDescriptor(int fd)
{
    if (fd < 0 || fd >= OPEN_FILE_LIMIT)
        return NULL;
    pthread_once(&descriptorLocksOnce, initializeDescriptorLocks);
    pthread_mutex_lock(&descriptorLocks[fd]);
    sfs_mem_filedesc_t *descriptor = openFileDescTable[fd];
    if (descriptor == NULL)
        pthread_mutex_unlock(&descriptorLocks[fd]);
    return descriptor;
}
```

A `getpos` call needs only its descriptor slot; operations that inspect or change file state add the per-file lock:

``` text
getpos:              fd slot
read / write / seek: fd slot -> file
```

and a write that grows the file adds the allocator lock at the end:

``` text
fd slot -> file -> allocator
```

That means two threads operating on different files do **not** need the directory lock, and they do **not** need the open-table lock either. They contend only if they actually share a descriptor, a file, or the free list.

The open-table lock remains necessary for `open`, `close`, `remove`, `rename`, and `ftruncate`, because those paths create or destroy global relationships. But it is no longer in the middle of millions of timed I/O calls.

### Descriptor positions are simple; block locations are derived

The starter design this article grew from cached more block-related state in each descriptor. This implementation keeps only `currPos`:

``` c
typedef struct sfs_mem_filedesc_t {
    sfs_mem_file_t *fileEntry;
    size_t currPos;
} sfs_mem_filedesc_t;
```

Whenever the code needs the current block, it derives it from the file's head block and the byte position:

``` c
static block_id blockForPosition(block_id first, size_t position)
{
    if (first == 0)
        return 0;
    uint32_t index = (uint32_t)(position / BLOCK_DATA_SIZE);
    if (position != 0 && position % BLOCK_DATA_SIZE == 0)
        index--;
    return idOfBlock(&fileBlockAt(first, index)->h);
}
```

That removes one class of synchronization work. When an empty file receives its first block, or `ftruncate` frees tail blocks, there are no cached block IDs scattered across live descriptors that now need repair. The only persistent per-descriptor state is the byte position.

Locating a block requires walking from the file head. This is simple for small files but repeated I/O near the end of a large file can become expensive; caching block positions would be a separate optimization.

### Directory readers sometimes need the file lock too

Directory operations cannot reason only about names.

`findDirectoryEntry` and `sfs_list` both do this before deciding whether a slot is occupied:

``` c
sfs_mem_file_t *open = findOpenFile(entry);
if (open != NULL)
    pthread_mutex_lock(&open->lock);
int occupied = entry->first_block != 0;
int matches = occupied && strcmp(entry->name, name) == 0;
if (open != NULL)
    pthread_mutex_unlock(&open->lock);
```

Why? Because `first_block` lives inside the directory entry, but it changes under the file lock when:

- an empty file receives its first data block,
- `ftruncate` grows or shrinks the allocation.

So a directory scan sometimes depends on file metadata that is mutable elsewhere. The fix is to follow that data dependency to the lock that already owns it.

### `sfs_list` uses a physical slot cookie

The listing API in `sfs-api.h` returns one name per call through an opaque cookie. In this implementation the cookie is just the next physical directory slot, encoded as a `void *` and counted across:

1. the 15 root entries inside the super block, then
2. every chained `DIR` block after that.

That is why `sfs_list` can resume exactly where it left off without materializing a separate iterator object. The API still requires callers to iterate until a nonzero status, but this implementation acquires and releases both read locks within each call; the cookie stores only the slot position.

---

## Verification

Run from `SFS_Lab/sfslab/`:

```sh
make
make test
make developer-test x-traces
```

Verified on September 5, 2026, using the updated local driver:

| Check | Result |
|---|---|
| Category A · feature tests | 5/5 |
| Category B · sequential correctness | 4/4 |
| Category C · actual concurrent calls | 3/3 |
| ThreadSanitizer fuzzed schedules | 3 clean runs |
| Correctness | 12/12 |
| Optional file-size trace | 1/1 |
| Optional directory/unlink/empty-file traces | 3/3 |

Unlike the old driver, the normal C traces no longer serialize calls on behalf
of the implementation. A passing result tests the locks in the solution itself.

To explore performance separately:

```sh
make grade
# Or, after calibration:
./test-sfs --benchmark
```

The optional 22-point scale is local feedback, not an official CMU grade.
Throughput depends on the machine and workload; the earlier 22/22 result is
not the completion criterion for the current basic exercise.
