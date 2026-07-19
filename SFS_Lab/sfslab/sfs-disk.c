//
// SFS Disk - Shark File System Implementation
//
// The Shark File System is a simple, FAT-style file system that is
//   designed to illustrate the basic principles of a file system for
//   students and be extensible.  The file system relies on the "disk"
//   being memory mapped so updates to memory can be sent to durable
//   storage by the operating system.  The file system only supports
//   the bare minimum set of features - files can be accessed by name,
//   created, deleted, renamed, read from, written to.  A "production"
//   filesystem includes many other features, such as permissions,
//   access time, nested directories, long file names, etc.
//
// The disk is "formatted" into 512-byte blocks.  Each block is linked
//   to the other blocks, similar to a free list in malloc.
//
// The file system relies on the first block being the "superblock".
//   This special block contains the information about the disk being
//   used and can locate the root directory.  Under the current
//   implementation, the root directory starts in the superblock and
//   expands into a chain of directory blocks when needed.
//
// A file consists of zero or more "disk blocks" which are chunks of 512
//   bytes.  Each block links to the one before and after it in the file,
//   which provide 500 bytes of space per allocated block.  The end of
//   the file is known by both the size of the file and the last block
//   links to block 0, which is "NULL".  A modern file system might
//   instead use a B-tree or other structure to manage the allocated
//   blocks.
//
// Open files are tracked using a two-level structure.  One level is the
//   open file descriptor tracking the position in the file for that
//   descriptor.  It links to a separate table that has a single entry
//   per file, which provides the current size of the file and the
//   reference count.  An unlinked file stays here until its final
//   descriptor is closed.
//
// @author Brian Railing (bpr@cs.cmu.edu)
//

#include "sfs-disk.h"
#include "sfs-api.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/** Maximum number of simultaneously open descriptors.  The expanded
    directory may contain more files than this. */
#define OPEN_FILE_LIMIT 32

static_assert(sizeof(sfs_block_file_t) == SFS_BLOCK_SIZE,
              "SFS_BLOCK_SIZE and sfs_block_file_t are out of sync");
static_assert(sizeof(sfs_block_dir_t) == SFS_BLOCK_SIZE,
              "SFS_BLOCK_SIZE and sfs_block_dir_t are out of sync");
static_assert(sizeof(sfs_filesystem_t) == SFS_BLOCK_SIZE,
              "SFS_BLOCK_SIZE and sfs_filesystem_t are out of sync");

/** This struct corresponds to what CS:APP calls a "v-node table" entry. */
typedef struct sfs_mem_file_t
{
    uint32_t refCount;
    int tableIndex;
    int unlinked;
    pthread_mutex_t lock;
    sfs_dir_entry_t *diskFile;
    sfs_dir_entry_t unlinkedFile;
} sfs_mem_file_t;

/** This struct corresponds to what CS:APP calls an "open file table" entry.
    The "descriptor table" is the openFileDescTable array itself. */
typedef struct sfs_mem_filedesc_t
{
    sfs_mem_file_t *fileEntry;
    size_t currPos;
} sfs_mem_filedesc_t;

static_assert(sizeof SFS_DISK_MAGIC == offsetof(sfs_filesystem_t, n_blocks),
              "'type' field of sfs_filesystem_t does not match SFS_DISK_MAGIC");

static sfs_mem_file_t *openFileTable[OPEN_FILE_LIMIT];
static sfs_mem_filedesc_t *openFileDescTable[OPEN_FILE_LIMIT];

/* Directory and descriptor-table changes are short and infrequent.  File data
   operations lock only their permanent descriptor slot and their file, so
   independent files can proceed in parallel. */
static pthread_rwlock_t directoryLock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_rwlock_t openTableLock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_mutex_t allocationLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t descriptorLocksOnce = PTHREAD_ONCE_INIT;
static pthread_mutex_t descriptorLocks[OPEN_FILE_LIMIT];

static void initializeDescriptorLocks(void)
{
    for (int i = 0; i < OPEN_FILE_LIMIT; i++)
    {
        int err = pthread_mutex_init(&descriptorLocks[i], NULL);
        assert(err == 0);
        (void)err;
    }
}

/** Lock FD's permanent table slot and return its live descriptor, if any. */
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

//
// Internal subroutines
//

/** Round up the size_t value SIZE to the nearest multiple of N.
    Special case: for all nonzero k, roundUp(k*N, N) returns k*N,
    but roundUp(0, N) returns N.  */
static size_t roundUp(size_t size, size_t n)
{
    if (size == 0)
        size = 1;
    return n * ((size + (n - 1)) / n);
}

/** Return the smaller of two size_t quantities. */
static size_t sizeMin(size_t a, size_t b)
{
    return a < b ? a : b;
}

/** Allocate N_BLOCKS free blocks from the free list.  Set each newly
    allocated block's type to TYPE, and chain them all together.
    Return the block ID of the first block in the chain.

    If N_BLOCKS blocks are not currently available for allocation,
    leaves the free list unchanged and returns 0.  Also returns 0
    if N_BLOCKS is zero.  */
static block_id allocateBlocks(uint32_t n_blocks, const char *type)
{
    pthread_mutex_lock(&allocationLock);
    sfs_filesystem_t *super = accessSuperBlock();
    if (super->freelist == 0 || n_blocks == 0)
    {
        pthread_mutex_unlock(&allocationLock);
        return 0;
    }

    block_id first_alloc_id = super->freelist;
    sfs_block_hdr_t *first_alloc_blk = accessFreeBlock(first_alloc_id);

    sfs_block_hdr_t *last_alloc_blk = first_alloc_blk;

    for (uint32_t i = 1; i < n_blocks; i++)
    {
        if (last_alloc_blk->next_block == 0)
        {
            pthread_mutex_unlock(&allocationLock);
            return 0; // not enough free blocks available
        }
        last_alloc_blk = accessFreeBlock(last_alloc_blk->next_block);
    }

    // At this point we know that we have n_blocks free blocks available.
    // 'first_alloc_blk' points to the first block being allocated and
    // 'last_alloc_blk' points to the last block being allocated (*not*
    // one beyond).
    block_id next_free_id = last_alloc_blk->next_block;
    if (next_free_id != 0)
    {
        sfs_block_hdr_t *next_free_blk = accessFreeBlock(next_free_id);
        next_free_blk->prev_block = 0;
        last_alloc_blk->next_block = 0;
    }
    super->freelist = next_free_id;

    for (sfs_block_hdr_t *b = first_alloc_blk; b;
         b = accessFreeBlock(b->next_block))
        setBlockType(b, type);

    pthread_mutex_unlock(&allocationLock);
    return first_alloc_id;
}

/** Deallocate all of the blocks in the allocation chain starting at
    'first_block'; that is, move them to the free list and change their type
    to SFS_BLOCK_TYPE_FREE.  'first_block' does not have to be the very
    first block in an allocation chain.  */
static void freeBlocks(block_id first_block)
{
    pthread_mutex_lock(&allocationLock);
    sfs_filesystem_t *superBlock = accessSuperBlock();
    sfs_block_hdr_t *b = accessBlock(first_block);
    if (b->prev_block != 0)
    {
        sfs_block_hdr_t *p = accessBlock(b->prev_block);
        p->next_block = 0;
        b->prev_block = 0;
    }

    for (;;)
    {
        assert(memcmp(b->type, SFS_BLOCK_TYPE_FREE, sizeof b->type) != 0);
        setBlockType(b, SFS_BLOCK_TYPE_FREE);
        if (b->next_block == 0)
            break;
        b = accessBlock(b->next_block);
    }
    b->next_block = superBlock->freelist;
    if (superBlock->freelist != 0)
    {
        // The free list is doubly linked (sfs-fsck checks this), so the
        // old head's prev pointer must be re-aimed at the chain we are
        // prepending; otherwise the list is left inconsistent.
        sfs_block_hdr_t *oldHead = accessFreeBlock(superBlock->freelist);
        oldHead->prev_block = idOfBlock(b);
    }
    superBlock->freelist = first_block;
    pthread_mutex_unlock(&allocationLock);
}

static uint32_t blocksForFileSize(size_t size)
{
    if (size == 0)
        return 0;
    return (uint32_t)(roundUp(size, BLOCK_DATA_SIZE) / BLOCK_DATA_SIZE);
}

/** Count the blocks already owned by FILE.  Legacy SFS images represent an
    empty file with one block; developer images use SFS_EMPTY_FILE_BLOCK. */
static uint32_t allocatedBlocksForFile(const sfs_dir_entry_t *file)
{
    if (file->first_block == SFS_EMPTY_FILE_BLOCK)
        return 0;
    assert(file->first_block != 0);
    return file->size == 0 ? 1 : blocksForFileSize(file->size);
}

static sfs_block_file_t *fileBlockAt(block_id first, uint32_t index)
{
    sfs_block_file_t *block = accessFileBlock(first);
    while (index-- > 0)
    {
        block = accessFileBlock(block->h.next_block);
        assert(block != NULL);
    }
    return block;
}

static void zeroFileRange(block_id first, size_t begin, size_t end)
{
    if (begin == end)
        return;

    uint32_t index = (uint32_t)(begin / BLOCK_DATA_SIZE);
    sfs_block_file_t *block = fileBlockAt(first, index);
    size_t offset = begin % BLOCK_DATA_SIZE;
    while (begin < end)
    {
        size_t chunk = sizeMin(BLOCK_DATA_SIZE - offset, end - begin);
        memset(block->data + offset, 0, chunk);
        begin += chunk;
        if (begin < end)
        {
            block = accessFileBlock(block->h.next_block);
            assert(block != NULL);
            offset = 0;
        }
    }
}

static block_id blockForPosition(block_id first, size_t position)
{
    if (first == 0)
        return 0;
    uint32_t index = (uint32_t)(position / BLOCK_DATA_SIZE);
    if (position != 0 && position % BLOCK_DATA_SIZE == 0)
        index--;
    return idOfBlock(&fileBlockAt(first, index)->h);
}

static sfs_mem_file_t *findOpenFile(const sfs_dir_entry_t *entry)
{
    for (int i = 0; i < OPEN_FILE_LIMIT; i++)
        if (openFileTable[i] != NULL && openFileTable[i]->diskFile == entry)
            return openFileTable[i];
    return NULL;
}

/** Allocate an open-file-table entry and descriptor for ENTRY. */
static int addOpenFileEntry(sfs_dir_entry_t *entry)
{
    pthread_once(&descriptorLocksOnce, initializeDescriptorLocks);
    sfs_mem_filedesc_t *memDescFile = NULL;
    int fd = -1;
    for (int idx = 0; idx < OPEN_FILE_LIMIT; idx++)
    {
        if (openFileDescTable[idx] == NULL)
        {
            memDescFile = malloc(sizeof(sfs_mem_filedesc_t));
            if (memDescFile == NULL)
            {
                return -ENOMEM;
            }
            fd = idx;
            break;
        }
    }
    if (memDescFile == NULL)
    {
        // No fd slots left.
        return -EMFILE;
    }

    sfs_mem_file_t *fileEntry = findOpenFile(entry);
    if (fileEntry == NULL)
    {
        int table_index = -1;
        for (int i = 0; i < OPEN_FILE_LIMIT; i++)
        {
            if (openFileTable[i] == NULL)
            {
                table_index = i;
                break;
            }
        }
        assert(table_index >= 0);
        fileEntry = malloc(sizeof(sfs_mem_file_t));
        if (fileEntry == NULL)
        {
            free(memDescFile);
            return -ENOMEM;
        }

        fileEntry->diskFile = entry;
        fileEntry->tableIndex = table_index;
        fileEntry->unlinked = 0;
        fileEntry->refCount = 0;
        int err = pthread_mutex_init(&fileEntry->lock, NULL);
        if (err != 0)
        {
            free(fileEntry);
            free(memDescFile);
            return -err;
        }
        openFileTable[table_index] = fileEntry;
    }

    fileEntry->refCount += 1;
    memDescFile->fileEntry = fileEntry;
    memDescFile->currPos = 0;
    pthread_mutex_lock(&descriptorLocks[fd]);
    openFileDescTable[fd] = memDescFile;
    pthread_mutex_unlock(&descriptorLocks[fd]);
    return fd;
}

int sfs_has_open_files(void)
{
    pthread_rwlock_rdlock(&openTableLock);
    for (int idx = 0; idx < OPEN_FILE_LIMIT; idx++)
    {
        if (openFileDescTable[idx] != NULL)
        {
            pthread_rwlock_unlock(&openTableLock);
            return 1;
        }
    }

    // With no live descriptor entries, no per-file entries should remain.
    for (int idx = 0; idx < OPEN_FILE_LIMIT; idx++)
    {
        assert(openFileTable[idx] == NULL);
    }

    pthread_rwlock_unlock(&openTableLock);
    return 0;
}

static sfs_block_dir_t *accessDirectoryBlock(block_id id)
{
    sfs_block_hdr_t *header = accessBlock(id);
    assert(memcmp(header->type, SFS_BLOCK_TYPE_DIR, sizeof header->type) == 0);
    return (sfs_block_dir_t *)header;
}

static sfs_dir_entry_t *findDirectoryEntry(const char *name,
                                           sfs_dir_entry_t **empty_out,
                                           block_id *last_dir_out)
{
    sfs_filesystem_t *super = accessSuperBlock();
    sfs_dir_entry_t *empty = NULL;
    for (size_t i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
    {
        sfs_dir_entry_t *entry = &super->files[i];
        sfs_mem_file_t *open = findOpenFile(entry);
        if (open != NULL)
            pthread_mutex_lock(&open->lock);
        int occupied = entry->first_block != 0;
        int matches = occupied && strcmp(entry->name, name) == 0;
        if (open != NULL)
            pthread_mutex_unlock(&open->lock);
        if (matches)
            return entry;
        if (empty == NULL && !occupied)
            empty = entry;
    }

    block_id last = 0;
    for (block_id id = super->next_rootdir; id != 0;)
    {
        sfs_block_dir_t *dir = accessDirectoryBlock(id);
        last = id;
        for (size_t i = 0; i < DIR_ENTRIES_PER_BLOCK; i++)
        {
            sfs_dir_entry_t *entry = &dir->files[i];
            sfs_mem_file_t *open = findOpenFile(entry);
            if (open != NULL)
                pthread_mutex_lock(&open->lock);
            int occupied = entry->first_block != 0;
            int matches = occupied && strcmp(entry->name, name) == 0;
            if (open != NULL)
                pthread_mutex_unlock(&open->lock);
            if (matches)
                return entry;
            if (empty == NULL && !occupied)
                empty = entry;
        }
        id = dir->h.next_block;
    }
    if (empty_out != NULL)
        *empty_out = empty;
    if (last_dir_out != NULL)
        *last_dir_out = last;
    return NULL;
}

static sfs_dir_entry_t *expandDirectory(block_id last_dir)
{
    block_id id = allocateBlocks(1, SFS_BLOCK_TYPE_DIR);
    if (id == 0)
        return NULL;
    sfs_block_dir_t *dir = (sfs_block_dir_t *)accessBlock(id);
    memset((char *)dir + sizeof dir->h, 0, SFS_BLOCK_SIZE - sizeof dir->h);
    dir->h.prev_block = last_dir;
    dir->h.next_block = 0;
    if (last_dir == 0)
        accessSuperBlock()->next_rootdir = id;
    else
        accessDirectoryBlock(last_dir)->h.next_block = id;
    return &dir->files[0];
}

/** Create a zero-block empty file in ENTRY and return an open descriptor. */
static int createFile(const char *fileName, sfs_dir_entry_t *entry)
{
    entry->first_block = SFS_EMPTY_FILE_BLOCK;
    entry->size = 0;

    size_t len = strlen(fileName);
    assert(len + 1 <= SFS_FILE_NAME_SIZE_LIMIT);
    memcpy(entry->name, fileName, len);
    memset(entry->name + len, '\0', SFS_FILE_NAME_SIZE_LIMIT - len);

    int fd = addOpenFileEntry(entry);
    if (fd < 0)
        memset(entry, 0, sizeof *entry);
    return fd;
}

/** Remove ENTRY while the directory and open-file tables are write-locked. */
static void unlinkDirectoryEntry(sfs_dir_entry_t *entry)
{
    sfs_mem_file_t *open = findOpenFile(entry);
    if (open != NULL)
    {
        pthread_mutex_lock(&open->lock);
        open->unlinkedFile = *entry;
        open->diskFile = &open->unlinkedFile;
        open->unlinked = 1;
        pthread_mutex_unlock(&open->lock);
    }
    else if (entry->first_block != SFS_EMPTY_FILE_BLOCK)
    {
        freeBlocks(entry->first_block);
    }
    memset(entry, 0, sizeof *entry);
}

//
// SFS API functions begin here
// see sfs-api.h for documentation comments for these functions
//

int sfs_open(const char *fileName)
{
    if (fileName[0] == '\0')
        return -EINVAL;

    // Can only have 23 characters, because the string on disk is NUL
    // terminated.
    if (strnlen(fileName, SFS_FILE_NAME_SIZE_LIMIT + 1) + 1 >
        SFS_FILE_NAME_SIZE_LIMIT)
        return -ENAMETOOLONG;

    // Is a disk image available?
    if (getSFSStatus() < 0)
        return -ENOMEDIUM;

    pthread_rwlock_wrlock(&directoryLock);
    pthread_rwlock_wrlock(&openTableLock);
    int result;
    sfs_dir_entry_t *empty = NULL;
    block_id last_dir = 0;
    sfs_dir_entry_t *entry = findDirectoryEntry(fileName, &empty, &last_dir);
    if (entry != NULL)
    {
        result = addOpenFileEntry(entry);
        goto out;
    }

    /* Do not change the directory when the descriptor table is already full. */
    int have_fd = 0;
    for (int i = 0; i < OPEN_FILE_LIMIT; i++)
        if (openFileDescTable[i] == NULL)
        {
            have_fd = 1;
            break;
        }
    if (!have_fd)
    {
        result = -EMFILE;
        goto out;
    }

    if (empty == NULL)
    {
        empty = expandDirectory(last_dir);
        if (empty == NULL)
        {
            result = -ENOSPC;
            goto out;
        }
    }
    result = createFile(fileName, empty);
out:
    pthread_rwlock_unlock(&openTableLock);
    pthread_rwlock_unlock(&directoryLock);
    return result;
}

void sfs_close(int fd)
{
    pthread_rwlock_wrlock(&openTableLock);
    if (fd < 0 || fd >= OPEN_FILE_LIMIT)
    {
        pthread_rwlock_unlock(&openTableLock);
        return;
    }
    pthread_once(&descriptorLocksOnce, initializeDescriptorLocks);
    pthread_mutex_lock(&descriptorLocks[fd]);
    sfs_mem_filedesc_t *tFile = openFileDescTable[fd];
    if (!tFile)
    {
        pthread_mutex_unlock(&descriptorLocks[fd]);
        pthread_rwlock_unlock(&openTableLock);
        return;
    }
    sfs_mem_file_t *fileEntry = tFile->fileEntry;
    openFileDescTable[fd] = NULL;
    free(tFile);

    fileEntry->refCount--;
    if (fileEntry->refCount > 0)
    {
        pthread_mutex_unlock(&descriptorLocks[fd]);
        pthread_rwlock_unlock(&openTableLock);
        return;
    }

    if (fileEntry->unlinked &&
        fileEntry->diskFile->first_block != SFS_EMPTY_FILE_BLOCK)
        freeBlocks(fileEntry->diskFile->first_block);
    int idx = fileEntry->tableIndex;
    pthread_mutex_destroy(&fileEntry->lock);
    free(fileEntry);
    openFileTable[idx] = NULL;
    pthread_mutex_unlock(&descriptorLocks[fd]);
    pthread_rwlock_unlock(&openTableLock);
}

ssize_t sfs_read(int fd, char *buf, size_t len)
{
    sfs_mem_filedesc_t *tFile = lockDescriptor(fd);
    if (tFile == NULL)
        return -EBADF;
    sfs_mem_file_t *file = tFile->fileEntry;
    pthread_mutex_lock(&file->lock);

    // We are going to read 'len' bytes, or the amount of data remaining
    // in the file, whichever is smaller.
    // This subtraction cannot produce a value larger than SSIZE_MAX
    // because it's impossible for a file in SFS to be that large.
    size_t fileSize = file->diskFile->size;
    size_t currPos = tFile->currPos;

    assert(currPos <= fileSize);
    size_t totalToRead = sizeMin(fileSize - currPos, len);

    size_t toRead = totalToRead;
    if (toRead == 0)
    {
        pthread_mutex_unlock(&file->lock);
        pthread_mutex_unlock(&descriptorLocks[fd]);
        return 0;
    }

    // Copy chunks of data from the mapped disk image to the caller's buffer.
    //
    // Each chunk is the smaller of:
    //  - the amount of data still to be read
    //  - the amount of data between currPos and the end of the current block
    // This number can be different from BLOCK_DATA_SIZE only for the
    // very first and the very last chunk of a read operation.
    //
    // Each chunk starts at the beginning of a disk block's data area,
    // except the very first chunk, which will begin in the middle of a
    // data area if the previous read or seek operation left the file
    // position not a multiple of BLOCK_DATA_SIZE.
    block_id first = file->diskFile->first_block;
    assert(first != 0 && first != SFS_EMPTY_FILE_BLOCK);
    sfs_block_file_t *diskBlock =
        accessFileBlock(blockForPosition(first, currPos));
    size_t blockPos = currPos % BLOCK_DATA_SIZE;
    size_t chunkSize =
        sizeMin(roundUp(currPos, BLOCK_DATA_SIZE) - currPos, toRead);
    for (;;)
    {
        // The chunk size can be zero on the first iteration, if the
        // starting position was exactly at a block boundary.
        if (chunkSize > 0)
        {
            memcpy(buf, &diskBlock->data[blockPos], chunkSize);
            buf += chunkSize;
            toRead -= chunkSize;
        }
        if (toRead == 0)
            break;

        blockPos = 0;
        chunkSize = sizeMin(BLOCK_DATA_SIZE, toRead);
        diskBlock = accessFileBlock(diskBlock->h.next_block);
        // This could only happen legitimately if we were reading to the end
        // of a file whose size was an exact multiple of BLOCK_DATA_SIZE, but
        // then we would already have exited the loop.
        assert(diskBlock != NULL);
    }

    tFile->currPos = currPos + totalToRead;

    pthread_mutex_unlock(&file->lock);
    pthread_mutex_unlock(&descriptorLocks[fd]);
    return (ssize_t)totalToRead;
}

ssize_t sfs_write(int fd, const char *buf, size_t len)
{
    sfs_mem_filedesc_t *tFile = lockDescriptor(fd);
    if (tFile == NULL)
        return -EBADF;
    sfs_mem_file_t *file = tFile->fileEntry;
    pthread_mutex_lock(&file->lock);

    size_t fileSize = file->diskFile->size;
    size_t currPos = tFile->currPos;
    assert(currPos <= fileSize);

    // This implementation does not do a partial write if there is
    // insufficient space on disk for the complete write; it always
    // either writes all 'len' bytes, or none.
    if (len > SFS_MAX_FILE_SIZE - currPos)
    {
        pthread_mutex_unlock(&file->lock);
        pthread_mutex_unlock(&descriptorLocks[fd]);
        return -EFBIG;
    }
    if (len == 0)
    {
        pthread_mutex_unlock(&file->lock);
        pthread_mutex_unlock(&descriptorLocks[fd]);
        return 0;
    }

    size_t fileAllocSize =
        (size_t)allocatedBlocksForFile(file->diskFile) * BLOCK_DATA_SIZE;
    size_t endPos = len + currPos;
    size_t toWrite = len;

    // If we need to enlarge the file, do so now, and if we can't make
    // it big enough, fail the whole operation.
    block_id firstNewId = 0;
    if (endPos > fileAllocSize)
    {
        size_t fileNewAllocSize = roundUp(endPos, BLOCK_DATA_SIZE);
        uint32_t addlBlocks =
            (uint32_t)((fileNewAllocSize - fileAllocSize) / BLOCK_DATA_SIZE);
        assert(addlBlocks >= 1);

        firstNewId = allocateBlocks(addlBlocks, SFS_BLOCK_TYPE_FILE);
        if (firstNewId == 0)
        {
            pthread_mutex_unlock(&file->lock);
            pthread_mutex_unlock(&descriptorLocks[fd]);
            return -ENOSPC;
        }
    }

    // Copy chunks of data from the caller's buffer to the mapped disk image.
    // See comments above the very similar loop in sfs_read() for more detail.
    sfs_block_file_t *diskBlock;
    block_id first = file->diskFile->first_block;
    if (first == SFS_EMPTY_FILE_BLOCK)
    {
        assert(fileSize == 0 && currPos == 0 && firstNewId != 0);
        diskBlock = accessFileBlock(firstNewId);
        file->diskFile->first_block = firstNewId;
        firstNewId = 0;
    }
    else
    {
        assert(first != 0);
        diskBlock = accessFileBlock(blockForPosition(first, currPos));
    }
    size_t blockPos = currPos % BLOCK_DATA_SIZE;
    size_t chunkSize =
        sizeMin(roundUp(currPos, BLOCK_DATA_SIZE) - currPos, toWrite);
    for (;;)
    {
        // The chunk size can be zero on the first iteration, if the
        // starting position was exactly at a block boundary.
        if (chunkSize > 0)
        {
            memcpy(&diskBlock->data[blockPos], buf, chunkSize);
            buf += chunkSize;
            toWrite -= chunkSize;
        }
        if (toWrite == 0)
            break;

        blockPos = 0;
        chunkSize = sizeMin(BLOCK_DATA_SIZE, toWrite);
        sfs_block_file_t *nextBlock = accessFileBlock(diskBlock->h.next_block);
        if (nextBlock == NULL)
        {
            // We should only get here once, at most, per write call.
            assert(firstNewId != 0);
            // We have just advanced the file position to the end of the
            // original allocation for the file.  Attach the additional
            // blocks beginning at 'firstNewId' to the end of the file,
            // and continue.
            nextBlock = accessFileBlock(firstNewId);
            diskBlock->h.next_block = firstNewId;
            nextBlock->h.prev_block = idOfBlock(&diskBlock->h);
            firstNewId = 0;
        }
        diskBlock = nextBlock;
    }

    tFile->currPos = endPos;
    if (endPos > fileSize)
    {
        assert(endPos <= SFS_MAX_FILE_SIZE);
        file->diskFile->size = (uint32_t)endPos;
    }
    pthread_mutex_unlock(&file->lock);
    pthread_mutex_unlock(&descriptorLocks[fd]);
    return (ssize_t)len;
}

ssize_t sfs_fstat(int fd)
{
    sfs_mem_filedesc_t *descriptor = lockDescriptor(fd);
    if (descriptor == NULL)
        return -EBADF;
    sfs_mem_file_t *file = descriptor->fileEntry;
    pthread_mutex_lock(&file->lock);
    ssize_t size = (ssize_t)file->diskFile->size;
    pthread_mutex_unlock(&file->lock);
    pthread_mutex_unlock(&descriptorLocks[fd]);
    return size;
}

int sfs_ftruncate(int fd, size_t length)
{
    if (fd < 0 || fd >= OPEN_FILE_LIMIT)
        return -EBADF;

    pthread_once(&descriptorLocksOnce, initializeDescriptorLocks);
    pthread_rwlock_wrlock(&openTableLock);
    sfs_mem_filedesc_t *descriptor = openFileDescTable[fd];
    if (descriptor == NULL)
    {
        pthread_rwlock_unlock(&openTableLock);
        return -EBADF;
    }
    if (length > SFS_MAX_FILE_SIZE)
    {
        pthread_rwlock_unlock(&openTableLock);
        return -EFBIG;
    }

    sfs_mem_file_t *file = descriptor->fileEntry;
    int slots[OPEN_FILE_LIMIT];
    int slot_count = 0;
    for (int i = 0; i < OPEN_FILE_LIMIT; i++)
    {
        sfs_mem_filedesc_t *open = openFileDescTable[i];
        if (open != NULL && open->fileEntry == file)
        {
            pthread_mutex_lock(&descriptorLocks[i]);
            slots[slot_count++] = i;
        }
    }

    pthread_mutex_lock(&file->lock);
    int result = 0;
    size_t old_size = file->diskFile->size;
    if (length == old_size)
        goto out;

    uint32_t old_blocks = allocatedBlocksForFile(file->diskFile);
    uint32_t new_blocks = blocksForFileSize(length);
    block_id first = file->diskFile->first_block == SFS_EMPTY_FILE_BLOCK
                         ? 0
                         : file->diskFile->first_block;

    if (new_blocks > old_blocks)
    {
        uint32_t extra = new_blocks - old_blocks;
        block_id first_new = allocateBlocks(extra, SFS_BLOCK_TYPE_FILE);
        if (first_new == 0)
        {
            result = -ENOSPC;
            goto out;
        }
        if (old_blocks == 0)
        {
            first = first_new;
            file->diskFile->first_block = first_new;
        }
        else
        {
            sfs_block_file_t *tail = fileBlockAt(first, old_blocks - 1);
            sfs_block_file_t *new_head = accessFileBlock(first_new);
            tail->h.next_block = first_new;
            new_head->h.prev_block = idOfBlock(&tail->h);
        }
    }
    else if (new_blocks < old_blocks)
    {
        if (new_blocks == 0)
        {
            freeBlocks(first);
            first = 0;
            file->diskFile->first_block = SFS_EMPTY_FILE_BLOCK;
        }
        else
        {
            sfs_block_file_t *tail = fileBlockAt(first, new_blocks - 1);
            block_id first_free = tail->h.next_block;
            assert(first_free != 0);
            freeBlocks(first_free);
        }
    }

    if (length > old_size)
        zeroFileRange(first, old_size, length);
    else
        zeroFileRange(first, length, (size_t)new_blocks * BLOCK_DATA_SIZE);
    file->diskFile->size = (uint32_t)length;

    if (length < old_size)
    {
        for (int i = 0; i < OPEN_FILE_LIMIT; i++)
        {
            sfs_mem_filedesc_t *open = openFileDescTable[i];
            if (open != NULL && open->fileEntry == file &&
                open->currPos >= length)
            {
                if (open->currPos > length)
                    open->currPos = length;
            }
        }
    }
out:
    pthread_mutex_unlock(&file->lock);
    while (slot_count > 0)
        pthread_mutex_unlock(&descriptorLocks[slots[--slot_count]]);
    pthread_rwlock_unlock(&openTableLock);
    return result;
}

ssize_t sfs_getpos(int fd)
{
    sfs_mem_filedesc_t *descriptor = lockDescriptor(fd);
    if (descriptor == NULL)
        return -EBADF;
    ssize_t position = (ssize_t)descriptor->currPos;
    pthread_mutex_unlock(&descriptorLocks[fd]);
    return position;
}

ssize_t sfs_seek(int fd, ssize_t delta)
{
    sfs_mem_filedesc_t *descriptor = lockDescriptor(fd);
    if (descriptor == NULL)
        return -EBADF;
    sfs_mem_file_t *file = descriptor->fileEntry;
    pthread_mutex_lock(&file->lock);
    size_t position = descriptor->currPos;
    size_t file_size = file->diskFile->size;
    if (delta < 0)
    {
        size_t distance = (size_t)(-(delta + 1)) + 1;
        position = distance > position ? 0 : position - distance;
    }
    else
    {
        size_t distance = (size_t)delta;
        position =
            distance > file_size - position ? file_size : position + distance;
    }
    descriptor->currPos = position;
    pthread_mutex_unlock(&file->lock);
    pthread_mutex_unlock(&descriptorLocks[fd]);
    return (ssize_t)position;
}

int sfs_remove(const char *name)
{
    if (name[0] == '\0')
        return -EINVAL;

    // Can only have 23 characters, because the string on disk is NUL
    // terminated.
    if (strnlen(name, SFS_FILE_NAME_SIZE_LIMIT + 1) + 1 >
        SFS_FILE_NAME_SIZE_LIMIT)
        return -ENAMETOOLONG;

    // Is a disk image available?
    if (getSFSStatus() < 0)
        return -ENOMEDIUM;

    pthread_rwlock_wrlock(&directoryLock);
    pthread_rwlock_wrlock(&openTableLock);
    sfs_dir_entry_t *entry = findDirectoryEntry(name, NULL, NULL);
    if (entry == NULL)
    {
        pthread_rwlock_unlock(&openTableLock);
        pthread_rwlock_unlock(&directoryLock);
        return -ENOENT;
    }

    unlinkDirectoryEntry(entry);
    pthread_rwlock_unlock(&openTableLock);
    pthread_rwlock_unlock(&directoryLock);
    return 0;
}

int sfs_rename(const char *old_name, const char *new_name)
{
    if (old_name[0] == '\0' || new_name[0] == '\0')
        return -EINVAL;

    if (strnlen(old_name, SFS_FILE_NAME_SIZE_LIMIT + 1) + 1 >
            SFS_FILE_NAME_SIZE_LIMIT ||
        strnlen(new_name, SFS_FILE_NAME_SIZE_LIMIT + 1) + 1 >
            SFS_FILE_NAME_SIZE_LIMIT)
        return -ENAMETOOLONG;

    if (getSFSStatus() < 0)
        return -ENOMEDIUM;

    pthread_rwlock_wrlock(&directoryLock);
    pthread_rwlock_wrlock(&openTableLock);
    sfs_dir_entry_t *old_entry = findDirectoryEntry(old_name, NULL, NULL);
    if (old_entry == NULL)
    {
        pthread_rwlock_unlock(&openTableLock);
        pthread_rwlock_unlock(&directoryLock);
        return -ENOENT;
    }

    if (strcmp(old_name, new_name) == 0)
    {
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
    return 0;
}

int sfs_list(sfs_list_cookie *cookie, char filename_out[],
             size_t filename_space)
{
    // The API promises that whenever this function returns a nonzero
    // status -- errors included -- the cookie is reset to NULL, so a
    // caller can always start a fresh loop afterward.

    // Corner case: If filename_space is zero, we cannot produce
    // an empty string into it on error.
    if (filename_space == 0)
    {
        *cookie = NULL;
        return -EINVAL;
    }

    if (getSFSStatus() < 0)
    {
        *cookie = NULL;
        return -ENOMEDIUM;
    }

    pthread_rwlock_rdlock(&directoryLock);
    pthread_rwlock_rdlock(&openTableLock);
    // The cookie is the next physical directory slot across the superblock
    // and every extension block.
    uintptr_t next_file_slot = (uintptr_t)*cookie;
    uintptr_t slot = 0;
    sfs_filesystem_t *super = accessSuperBlock();
    for (size_t i = 0; i < DIR_ENTRIES_PER_BLOCK; i++, slot++)
    {
        sfs_dir_entry_t *entry = &super->files[i];
        sfs_mem_file_t *open = findOpenFile(entry);
        if (open != NULL)
            pthread_mutex_lock(&open->lock);
        int occupied = entry->first_block != 0;
        if (slot >= next_file_slot && occupied)
        {
            size_t len = strlen(entry->name);
            if (len + 1 > filename_space)
            {
                *cookie = NULL;
                if (open != NULL)
                    pthread_mutex_unlock(&open->lock);
                pthread_rwlock_unlock(&openTableLock);
                pthread_rwlock_unlock(&directoryLock);
                return -ENAMETOOLONG;
            }
            memcpy(filename_out, entry->name, len + 1);
            *cookie = (void *)(slot + 1);
            if (open != NULL)
                pthread_mutex_unlock(&open->lock);
            pthread_rwlock_unlock(&openTableLock);
            pthread_rwlock_unlock(&directoryLock);
            return 0;
        }
        if (open != NULL)
            pthread_mutex_unlock(&open->lock);
    }
    for (block_id id = super->next_rootdir; id != 0;)
    {
        sfs_block_dir_t *dir = accessDirectoryBlock(id);
        for (size_t i = 0; i < DIR_ENTRIES_PER_BLOCK; i++, slot++)
        {
            sfs_dir_entry_t *entry = &dir->files[i];
            sfs_mem_file_t *open = findOpenFile(entry);
            if (open != NULL)
                pthread_mutex_lock(&open->lock);
            int occupied = entry->first_block != 0;
            if (slot >= next_file_slot && occupied)
            {
                size_t len = strlen(entry->name);
                if (len + 1 > filename_space)
                {
                    *cookie = NULL;
                    if (open != NULL)
                        pthread_mutex_unlock(&open->lock);
                    pthread_rwlock_unlock(&openTableLock);
                    pthread_rwlock_unlock(&directoryLock);
                    return -ENAMETOOLONG;
                }
                memcpy(filename_out, entry->name, len + 1);
                *cookie = (void *)(slot + 1);
                if (open != NULL)
                    pthread_mutex_unlock(&open->lock);
                pthread_rwlock_unlock(&openTableLock);
                pthread_rwlock_unlock(&directoryLock);
                return 0;
            }
            if (open != NULL)
                pthread_mutex_unlock(&open->lock);
        }
        id = dir->h.next_block;
    }

    // No more files to report.
    *cookie = NULL;
    pthread_rwlock_unlock(&openTableLock);
    pthread_rwlock_unlock(&directoryLock);
    return 1;
}
