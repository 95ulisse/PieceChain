#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <libgen.h>
#include <assert.h>

#define MEM_BLOCK_SIZE ((size_t) (1024 * 1024)) /* 1MiB */

#include "PieceChain/PieceChain.h"
#include "list.h"
#include "util.h"

typedef struct {
    unsigned char* data;
    size_t size;
    size_t len;
    enum {
        BLOCK_MMAP,
        BLOCK_MALLOC
    } type;
    struct list_head list;
} Block;

typedef struct {
    unsigned char* data;
    size_t size;
    struct list_head global_list;
    struct list_head list; // This is not used as a list at all, so maybe we should not use a `list_head`.
} Piece;

typedef struct {
    Piece* start;
    Piece* end;
    size_t len;
} Span;

typedef struct {
    Span original;
    Span replacement;
    size_t pos;
    struct list_head list;
} Change;

typedef struct {
    struct list_head changes;
    struct list_head list;
} Revision;

struct PieceChain_t {
    size_t size;
    bool dirty;

    struct list_head all_blocks; // List of all the blocks (for freeing)
    struct list_head all_pieces; // List of all the pieces (for freeing)
    struct list_head all_revisions; // File history

    struct list_head pieces; // Current active pieces
    Piece* cache; // Last modified piece for caching

    Revision* current_revision; // Pointer to the current active revision
    struct list_head pending_changes; // Changes not yet attached to a revision

    PieceChainError_t last_error;
};

struct PieceChainIterator_t {
    PieceChain_t* file;
    size_t max_off; // Maximum offset requested by the user
    size_t current_off; // Offset we have iterated to until now
    Piece* current_piece;
};


// Functions to manage blocks
static Block* block_alloc(PieceChain_t*, size_t);
static Block* block_alloc_mmap(PieceChain_t*, int fd, size_t size);
static void block_free(PieceChain_t*, Block*);
static bool block_can_fit(Block*, size_t len);
static unsigned char* block_append(Block*, const unsigned char* data, size_t len);

// Functions to manage pieces
static Piece* piece_alloc(PieceChain_t*);
static void piece_free(Piece*);
static bool piece_find(PieceChain_t*, size_t abs, Piece** piece, size_t* offset);

// Functions to manage the piece cache
static void cache_put(PieceChain_t*, Piece*);
static bool cache_insert(PieceChain_t*, Piece*, size_t piece_offset, const unsigned char* data, size_t len);
static bool cache_delete(PieceChain_t*, Piece*, size_t piece_offset, size_t len);

// Functions to manage spans and changes
static void span_init(Span*, Piece* start, Piece* end);
static void span_swap(PieceChain_t*, Span* original, Span* replacement);
static Change* change_alloc(PieceChain_t*, size_t pos);
static void change_free(Change*, bool free_pieces);

// Functions to manage revisions
static Revision* revision_alloc(PieceChain_t*);
static void revision_free(Revision*, bool free_pieces);
static bool revision_purge(PieceChain_t*);


inline static void set_error(PieceChain_t* file, const char* message, int err) {
    file->last_error.message = message;
    file->last_error.err = err;
}

static Block* block_alloc(PieceChain_t* file, size_t size) {
    
    Block* block = malloc(sizeof(Block));
    if (block == NULL) {
        set_error(file, "Out of memory", ENOMEM);
        return NULL;
    }
    block->size = MAX(size, MEM_BLOCK_SIZE);
    block->len = 0;
    block->type = BLOCK_MALLOC;
    list_init(&block->list);

    block->data = malloc(sizeof(char) * block->size);
    if (block->data == NULL) {
        set_error(file, "Out of memory", ENOMEM);
        free(block);
        return NULL;
    }

    // Add the created block to the list of all blocks for tracking
    list_add_tail(&file->all_blocks, &block->list);

    return block;

}

static Block* block_alloc_mmap(PieceChain_t* file, int fd, size_t size) {
    
    Block* block = malloc(sizeof(Block));
    if (block == NULL) {
        set_error(file, "Out of memory", ENOMEM);
        return NULL;
    }
    block->size = size;
    block->len = size;
    block->type = BLOCK_MMAP;
    list_init(&block->list);

    block->data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (block->data == MAP_FAILED) {
        set_error(file, "Cannot mmap", errno);
        return NULL;
    }

    // Add the created block to the list of all blocks for tracking
    list_add_tail(&file->all_blocks, &block->list);

    return block;
   
}

static void block_free(PieceChain_t* file, Block* block) {
    (void) file;

    switch (block->type) {
        case BLOCK_MALLOC:
            free(block->data);
            break;
        case BLOCK_MMAP:
            munmap(block->data, block->size);
            break;
        default:
            abort();
    }
    list_del(&block->list);
    free(block);
}

static bool block_can_fit(Block* block, size_t n) {
    return block->size - block->len >= n;
}

static unsigned char* block_append(Block* block, const unsigned char* data, size_t len) {
    if (!block_can_fit(block, len)) {
        return NULL;
    }
    unsigned char* ptr = block->data + block->len;
    memmove(ptr, data, len);
    block->len += len;
    return ptr;
}

static Piece* piece_alloc(PieceChain_t* file) {
    Piece* piece = malloc(sizeof(Piece));
    if (piece == NULL) {
        set_error(file, "Out of memory", ENOMEM);
        return NULL;
    }
    piece->data = NULL;
    piece->size = 0;
    list_init(&piece->list);
    list_init(&piece->global_list);

    list_add_tail(&file->all_pieces, &piece->global_list);

    return piece;
}

static void piece_free(Piece* piece) {
    list_del(&piece->global_list);
    free(piece);
}

static bool piece_find(PieceChain_t* file, size_t abs, Piece** piece, size_t* offset) {
    if (abs > file->size) {
        return false;
    }

    size_t abspos = 0;
    list_for_each_member(p, &file->pieces, Piece, list) {
        if (abs < abspos + p->size) {
            *piece = p;
            *offset = abs - abspos;
            return true;
        } else {
            abspos += p->size;
        }
    }

    return false;
}

static void cache_put(PieceChain_t* file, Piece* piece) {
    file->cache = piece;

#ifdef DEBUG
    if (piece != NULL) {
        // The piece we use as cache must use as a backing block the last one
        // and must end exactly where the last block ends
        Block* blk = list_last(&file->all_blocks, Block, list);
        assert(piece->data + piece->size == blk->data + blk->len);
    }
#endif
}

static bool cache_insert(PieceChain_t* file, Piece* piece, size_t piece_offset, const unsigned char* data, size_t len) {
    if (file->cache == NULL || file->cache != piece) {
        return false;
    }

    // The cached piece must always be the last one created
    Block* blk = list_last(&file->all_blocks, Block, list);
    assert(piece->data + piece->size == blk->data + blk->len);

    // Insertion can happen only if the block can fit the new data
    if (!block_can_fit(blk, len)) {
        return false;
    }

    // Insert the data in the block
    unsigned char* blk_insertion = blk->data + blk->len - (piece->size - piece_offset);
    assert(blk_insertion >= blk->data);
    if (blk_insertion == blk->data + blk->len) {
        block_append(blk, data, len);
    } else {
        memmove(blk_insertion + len, blk_insertion, piece->size - piece_offset);
        memmove(blk_insertion, data, len);
        blk->len += len;
    }

    // Update the counters
    piece->size += len;
    file->size += len;
    Change* change = list_last(&file->pending_changes, Change, list);
    change->replacement.len += len;

    return true;
}

static bool cache_delete(PieceChain_t* file, Piece* piece, size_t piece_offset, size_t len) {
    if (file->cache == NULL || file->cache != piece) {
        return false;
    }
    
    // The cached piece must always be the last one created
    Block* blk = list_last(&file->all_blocks, Block, list);
    assert(piece->data + piece->size == blk->data + blk->len);

    // Deletion can happen only if the whole deletion range is in the cached piece
    if (piece->size - piece_offset < len) {
        return false;
    }

    // Delete the data from the block
    unsigned char* blk_del = blk->data + blk->len - (piece->size - piece_offset);
    assert(blk_del >= blk->data);
    if (blk_del < blk->data + blk->len) {
        memmove(blk_del, blk_del + len, piece->size - piece_offset - len);
    }
    blk->len -= len;

    // Update the counters
    piece->size -= len;
    file->size -= len;
    Change* change = list_last(&file->pending_changes, Change, list);
    change->replacement.len -= len;

    return true;
}

static void span_init(Span* span, Piece* start, Piece* end) {
    span->start = start;
    span->end = end;
    if (start == NULL && end == NULL) {
        span->len = 0;
        return;
    }
    assert(start != NULL && end != NULL);

    list_for_each_interval(p, start, end, Piece, list) {
        span->len += p->size;
    }
}

static void span_swap(PieceChain_t* file, Span* original, Span* replacement) {
    if (original->len == 0 && replacement->len == 0) {
        return;
    } else if (original->len == 0) {
        // An insertion
        replacement->start->list.prev->next = &replacement->start->list;
        replacement->end->list.next->prev = &replacement->end->list;
    } else if (replacement->len == 0) {
        // A deletion
        original->start->list.prev->next = original->end->list.next;
        original->end->list.next->prev = original->start->list.prev;
    } else {
        original->start->list.prev->next = &replacement->start->list;
        original->end->list.next->prev = &replacement->end->list;
    }
    file->size -= original->len;
    file->size += replacement->len;
}

static Change* change_alloc(PieceChain_t* file, size_t pos) {
    Change* change = malloc(sizeof(Change));
    if (change == NULL) {
        set_error(file, "Out of memory", ENOMEM);
        return NULL;
    }
    change->pos = pos;
    span_init(&change->original, NULL, NULL);
    span_init(&change->replacement, NULL, NULL);

    list_add_tail(&file->pending_changes, &change->list);
    return change;
}

static void change_free(Change* change, bool free_pieces) {
    // We don't need to free the pieces of the original span, since they will be referenced by a previous change
    if (free_pieces && change->replacement.start != NULL) {
        list_for_each_interval(p, change->replacement.start, change->replacement.end, Piece, list) {
            piece_free(p);
        }
    }
    free(change);
}

static Revision* revision_alloc(PieceChain_t* file) {
    Revision* rev = malloc(sizeof(Revision));
    if (rev == NULL) {
        set_error(file, "Out of memory", ENOMEM);
        return NULL;
    }
    list_init(&rev->changes);
    list_init(&rev->list);

    list_add_tail(&file->all_revisions, &rev->list);

    return rev;
}

static void revision_free(Revision* rev, bool free_pieces) {
    list_for_each_rev_member(change, &rev->changes, Change, list) {
        change_free(change, free_pieces);
    }
    free(rev);
}

static bool revision_purge(PieceChain_t* file) {

    // The history of revisions is linear:
    // If you undo a change, than perform a new operation, you cannot redo to the original state.
    // This function purges any revision after the current active one:
    // in other words, discards redo history.

    if (list_empty(&file->all_revisions)) {
        // No revision committed yet
        return false;
    }

    if (list_last(&file->all_revisions, Revision, list) == file->current_revision) {
        // We are already at the last revision
        return false;
    }

    list_for_each_rev_interval(rev, list_next(file->current_revision, Revision, list), list_last(&file->all_revisions, Revision, list), Revision, list) {
        list_del(&rev->list);
        revision_free(rev, true);
    }

    assert(file->current_revision == list_last(&file->all_revisions, Revision, list));

    return true;
}

PieceChain_t* piece_chain_open(const char* path) {

    // Initialize a new File structure
    PieceChain_t* file = calloc(1, sizeof(PieceChain_t));
    if (file == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    list_init(&file->all_blocks);
    list_init(&file->all_revisions);
    list_init(&file->all_pieces);
    list_init(&file->pieces);
    list_init(&file->pending_changes);

    if (path == NULL) {

        // Allocate an initial empty revision
        Revision* initial_rev = revision_alloc(file);
        if (initial_rev == NULL) {
            free(file);
            return NULL;
        }
        file->current_revision = initial_rev;

        return file;
    }

    // Open the file r/w, if we fail try r/o
    int fd;
    errno = 0;
    while ((fd = open(path, O_RDONLY)) == -1 && errno == EINTR);
    if (errno != 0) {
        piece_chain_destroy(file);
        return NULL;
    }

    // Stat the file to get some info about it
    struct stat s;
    if (fstat(fd, &s) < 0) {
        close(fd);
        piece_chain_destroy(file);
        return NULL;
    }

    // Get file size.
    // Block devices need a specific ioctl.
    size_t size = 0;
    if (S_ISBLK(s.st_mode)) {
        if (ioctl(fd, BLKGETSIZE64, &size) < 0) {
            close(fd);
            piece_chain_destroy(file);
            return NULL;
        }
    } else if (S_ISREG(s.st_mode)) {
        size = s.st_size;
    } else {
        close(fd);
        piece_chain_destroy(file);
        return NULL;
    }

    // mmap the file to memory and create the initial piece
    Piece* p = NULL;
    if (size > 0) {
        Block* b = block_alloc_mmap(file, fd, size);
        if (b == NULL) {
            close(fd);
            piece_chain_destroy(file);
            return NULL;
        }
        p = piece_alloc(file);
        if (p == NULL) {
            close(fd);
            piece_chain_destroy(file);
            return NULL;
        }
        p->data = b->data;
        p->size = b->size;
        p->list.prev = &file->pieces;
        p->list.next = &file->pieces;
    }

    // Prepare the initial change
    Change* change = change_alloc(file, 0);
    if (change == NULL) {
        piece_chain_destroy(file);
        return NULL;
    }
    span_init(&change->original, NULL, NULL);
    span_init(&change->replacement, p, p);
    span_swap(file, &change->original, &change->replacement);

    // Commit the change to a revision
    if (!piece_chain_commit(file)) {
        piece_chain_destroy(file);
        return NULL;
    }

    // Now that we have the file mapped in memory, we can safely close the fd
    close(fd);

    return file;

}

void piece_chain_destroy(PieceChain_t* file) {
    if (file == NULL) {
        return;
    }

    piece_chain_commit(file); // Commits any pending change

    list_for_each_rev_member(rev, &file->all_revisions, Revision, list) {
        // Note: here we are freeing revisions and changes, but not pieces.
        // This is because we are going to free all the pieces later,
        // and if we free some pieces without a proper undo, we end up with
        // a screwed chain.
        revision_free(rev, false);
    }

    list_for_each_rev_member(p, &file->all_pieces, Piece, global_list) {
        piece_free(p);
    }

    list_for_each_rev_member(b, &file->all_blocks, Block, list) {
        block_free(file, b);
    }

    free(file);
}

static bool write_to_fd_visitor(PieceChain_t* file, size_t unused, const unsigned char* data, size_t len, void* user) {
    (void) unused;

    int fd = (int)(long) user;

    const size_t blocksize = 64 * 1024;
    size_t offset = 0;
    while (offset < len) {
        ssize_t written;
        while ((written = write(fd, data + offset, MIN(blocksize, len - offset))) == -1 && errno == EINTR);
        if (written < 0) {
            set_error(file, "Cannot write", errno);
            return false;
        }
        offset += written;
    }

    return true;
}

static bool write_to_fd(PieceChain_t* file, int fd) {
    return piece_chain_visit(file, 0, file->size, write_to_fd_visitor, (void*)(long) fd);
}

static bool piece_chain_save_atomic(PieceChain_t* file, const char* path) {

    // File is first saved to a temp directory,
    // then moved to the target destination with a rename.
    // This will fail of the original file is a synbolic or hard link,
    // and if there are problems in restoring the original file permissions.

    int oldfd = -1;
    int dirfd = -1;
    int tmpfd = -1;
    char* tmpname = NULL;
    char* pathdup = strdup(path);

    if (pathdup == NULL) {
        set_error(file, "Out of memory", ENOMEM);
        goto error;
    }

    // Open the original file and get some info about it
    while ((oldfd = open(path, O_RDONLY)) == -1 && errno == EINTR);
    if (oldfd < 0 && errno != ENOENT) {
        set_error(file, "Cannot open file", errno);
        goto error;
    }
    struct stat oldstat = { 0 };
    if (oldfd != -1) {
        if (lstat(path, &oldstat) < 0) {
            set_error(file, "Cannot stat file", errno);
            goto error;
        }

        // The rename method does not work if the target is not a regular file or if it is a hard link
        if (!S_ISREG(oldstat.st_mode) || oldstat.st_nlink > 1) {
			goto error;
        }
    }

    size_t tmpnamelen = strlen(path) + 6 /* ~~save */ + 1 /* '\0' */;
    tmpname = malloc(sizeof(char) * tmpnamelen);
    if (tmpname == NULL) {
        set_error(file, "Out of memory", ENOMEM);
        goto error;
    }
    snprintf(tmpname, tmpnamelen, "%s~~save", path);

    // Create the temp file
    while ((tmpfd = open(tmpname, O_WRONLY | O_CREAT | O_TRUNC, oldfd == -1 ? 0666 : oldstat.st_mode)) == -1 && errno == EINTR);
    if (tmpfd < 0) {
        set_error(file, "Cannot open temp file", errno);
        goto error;
    }

    // If the old file existed, try to copy the owner to the temp file
    int res;
    if (oldfd != -1) {
        if (oldstat.st_uid != getuid()) {
            while ((res = fchown(tmpfd, oldstat.st_uid, (uid_t) -1)) == -1 && errno == EINTR);
            if (res < 0) {
                goto error;
            }
        }
        if (oldstat.st_gid != getgid()) {
            while ((res = fchown(tmpfd, (uid_t) -1, oldstat.st_gid)) == -1 && errno == EINTR);
            if (res < 0) {
                goto error;
            }
        }
    }

    // We don't need the old file anymore
    if (oldfd != -1 ) {
        close(oldfd);
        oldfd = -1;
    }

    // Write to the temp file
    if (!write_to_fd(file, tmpfd)) {
        goto error;
    }
    
    while ((res = fsync(tmpfd)) == -1 && errno == EINTR);
    if (res < 0) {
        set_error(file, "Cannot fsync temp file", errno);
        goto error;
    }

    while ((res = close(tmpfd)) == -1 && errno == EINTR);
    if (res < 0) {
        set_error(file, "Cannot close temp file", errno);
        goto error;
    }

    // Move the temp file over the original one
    while ((res = rename(tmpname, path)) == -1 && errno == EINTR);
    if (res < 0) {
        set_error(file, "Cannot rename temp flie to destination", errno);
        goto error;
    }

    // Open the parent directory and sync it to be sure that the rename has been committed to disk
    while ((dirfd = open(dirname(pathdup), O_DIRECTORY | O_RDONLY)) == -1 && errno == EINTR);
    if (dirfd < 0) {
        set_error(file, "Cannot open destination directory", errno);
        goto error;
    }
    while ((res = fsync(dirfd)) == -1 && errno == EINTR);
    if (res < 0) {
        set_error(file, "Cannot fsync destination directory", errno);
        goto error;
    }
    close(dirfd);

    free(tmpname);
    free(pathdup);
    return true;

error:
    if (oldfd != -1) {
        close(oldfd);
    }
    if (tmpfd != -1) {
        close(tmpfd);
    }
    if (dirfd != -1) {
        close(dirfd);
    }
    if (tmpname != NULL) {
        unlink(tmpname);
        free(tmpname);
    }
    if (pathdup != NULL) {
        free(pathdup);
    }
    return false;

}

static bool piece_chain_save_inplace(PieceChain_t* file, const char* path) {
    
    int fd;
    while ((fd = open(path, O_WRONLY | O_CREAT, 0666)) == -1 && errno == EINTR);
    if (fd < 0) {
        set_error(file, "Cannot open file", errno);
        return false;
    }

    if (!write_to_fd(file, fd)) {
        close(fd);
        return false;
    }

    int res;
    while ((res = fsync(fd)) == -1 && errno == EINTR);
    if (res < 0) {
        set_error(file, "Cannot fsync file", errno);
        close(fd);
        return false;
    }

    close(fd);

    return true;

}

bool piece_chain_save(PieceChain_t* file, const char* path, enum PieceChainSaveMode savemode) {    

    bool success = false;
    switch (savemode) {
        
        case SAVE_MODE_ATOMIC:
            success = piece_chain_save_atomic(file, path);
            break;
        
        case SAVE_MODE_INPLACE:
            success = piece_chain_save_inplace(file, path);
            break;
        
        case SAVE_MODE_AUTO:
            success = piece_chain_save_atomic(file, path);
            if (!success) {
                success = piece_chain_save_inplace(file, path);
            }
            break;

        default:
            abort();
    }

    if (success) {
        file->dirty = false;
    }
    return success;

}

size_t piece_chain_size(PieceChain_t* file) {
    return file->size;
}

bool piece_chain_dirty(PieceChain_t* file) {
    return file->dirty;
}

PieceChainError_t* piece_chain_last_error(PieceChain_t* file) {
    return &file->last_error;
}

bool piece_chain_insert(PieceChain_t* file, size_t offset, const unsigned char* data, size_t len) {

    if (len == 0) {
        return true;
    }
    if (offset > file->size) {
        return false;
    }

    // Find the piece at offset `offset`
    Piece* piece;
    size_t piece_offset;
    if (!piece_find(file, offset, &piece, &piece_offset)) {
        if (list_empty(&file->pieces)) {
            piece = NULL;
            piece_offset = 0;
        } else if (offset == file->size) {
            piece = list_last(&file->pieces, Piece, list);
            piece_offset = piece->size;
        } else {
            return false;
        }
    }

    // Discard any redo history
    revision_purge(file);

    // First try with the cached piece.
    // If we are inserting at the beginning of a piece, check if the previous one was cached and try using it
    if (piece != NULL) {
        if (cache_insert(file, piece, piece_offset, data, len)) {
            goto success;
        }
        if (piece_offset == 0 && list_first(&file->pieces, Piece, list) != piece) {
            Piece* prev = list_prev(piece, Piece, list);
            if (cache_insert(file, prev, prev->size, data, len)) {
                goto success;
            }
        }
    }

    // Let's see if we can reuse the last block to store the new data
    Block* b;
    if (!list_empty(&file->all_blocks) && block_can_fit(list_last(&file->all_blocks, Block, list), len)) {
        b = list_last(&file->all_blocks, Block, list);
    } else {
        b = block_alloc(file, len);
        if (b == NULL) {
            return false;
        }
    }

    unsigned char* ptr = block_append(b, data, len);
    if (ptr == NULL) {
        return false;
    }

    // There might be two cases for insertion, depending on the offset:
    // - At the boundary of an already existing piece
    // - In the middle of an existing piece
    // In the first case, we can just allocate a new piece and insert it,
    // while in the other we have to replace the existing piece with three new ones.

    Change* change = change_alloc(file, offset);
    if (change == NULL) {
        return false;
    }

    Piece* new;

    if (piece == NULL) {
        // We have no piece to attach to because this is the first insertion to an empty file

        new = piece_alloc(file);
        if (new == NULL) {
            return false;
        }
        new->data = ptr;
        new->size = len;

        // Insert as the first piece
        new->list.prev = new->list.next = &file->pieces;

        span_init(&change->original, NULL, NULL);
        span_init(&change->replacement, new, new);       

    } else if (piece_offset == 0 || piece_offset == piece->size) {
        // For how we counted offsets, the only way that the `piece_offset == piece->size` condition
        // can be true is when we are inserting at the end of the file.

        new = piece_alloc(file);
        if (new == NULL) {
            return false;
        }
        new->data = ptr;
        new->size = len;

        // Insert before or after the piece
        if (piece_offset == 0) {
            new->list.prev = piece->list.prev;
            new->list.next = &piece->list;
        } else {
            new->list.prev = &piece->list;
            new->list.next = piece->list.next;
        }
        
        span_init(&change->original, NULL, NULL);
        span_init(&change->replacement, new, new);

    } else {

        Piece* before = piece_alloc(file);
        Piece* middle = piece_alloc(file);
        Piece* after = piece_alloc(file);
        if (before == NULL || middle == NULL || after == NULL) {
            return false;
        }

        // Split the data among the three pieces
        before->data = piece->data;
        before->size = piece_offset;
        middle->data = ptr;
        middle->size = len;
        after->data = piece->data + piece_offset;
        after->size = piece->size - piece_offset;

        // Join the three pieces together
        before->list.prev = piece->list.prev;
        before->list.next = &middle->list;
        middle->list.prev = &before->list;
        middle->list.next = &after->list;
        after->list.prev = &middle->list;
        after->list.next = piece->list.next;

        span_init(&change->original, piece, piece);
        span_init(&change->replacement, before, after);
    
        new = middle;

    }

    // Apply the prepared change
    cache_put(file, new);
    span_swap(file, &change->original, &change->replacement);

success:

    // Mark the file as dirty
    file->dirty = true;
    
    return true;

}

bool piece_chain_delete(PieceChain_t* file, size_t offset, size_t len) {

    if (len == 0) {
        return true;
    }
    if (offset > file->size) {
        return false;
    }

    // Find the piece at the begin of the range to delete
    Piece* start_piece;
    size_t start_piece_offset;
    Piece* end_piece;
    size_t end_piece_offset;
    if (!piece_find(file, offset, &start_piece, &start_piece_offset)) {
        return false;
    }
    if (!piece_find(file, offset + len, &end_piece, &end_piece_offset)) {
        // If the end of the delete range fell out of the file, delete up to the last char
        end_piece = list_last(&file->pieces, Piece, list);
        end_piece_offset = end_piece->size;
    }  
    assert(start_piece != NULL);
    assert(end_piece != NULL);

    // Discard any redo history
    revision_purge(file);

    // First try with the cached piece
    if (cache_delete(file, start_piece, start_piece_offset, len)) {
        goto success;
    }

    // Deletion range can both start and end up in the middle of a piece.
    // This means that we might have to create new pieces to account for the split pieces.

    Change* change = change_alloc(file, offset);
    if (change == NULL) {
        return false;
    }

    bool split_start = start_piece_offset != 0;
    bool split_end = end_piece_offset != end_piece->size;
    
    struct list_head* before = start_piece->list.prev;
    struct list_head* after = end_piece->list.next;

    Piece* new_start = NULL;
    Piece* new_end = NULL;

    if (split_start) {
        new_start = piece_alloc(file);
        if (new_start == NULL) {
            return false;
        }
        new_start->data = start_piece->data;
        new_start->size = start_piece_offset;
        new_start->list.prev = before;
        new_start->list.next = after;
    }

    if (split_end) {
        new_end = piece_alloc(file);
        if (new_end == NULL) {
            return false;
        }
        new_end->data = end_piece->data + end_piece_offset;
        new_end->size = end_piece->size - end_piece_offset;
        new_end->list.prev = before;
        new_end->list.next = after;
        if (split_start) {
            new_end->list.prev = &new_start->list;
            new_start->list.next = &new_end->list;
        }
    }

    if (new_start == NULL && new_end != NULL) {
        new_start = new_end;
    } else if (new_start != NULL && new_end == NULL) {
        new_end = new_start;
    }

    span_init(&change->original, start_piece, end_piece);
    span_init(&change->replacement, new_start, new_end);
    span_swap(file, &change->original, &change->replacement);

success:

    // Mark the file as dirty
    file->dirty = true;

    return true;

}

bool piece_chain_replace(PieceChain_t* file, size_t offset, const unsigned char* data, size_t len) {
    // A replacement is just a convenience shortcut for insertion and deletion
    if (piece_chain_delete(file, offset, len)) {
        return piece_chain_insert(file, offset, data, len);
    } else {
        return false;
    }
}

bool piece_chain_commit(PieceChain_t* file) {

    // Allocate a new revision only if there are pending changes not yet committed
    if (!list_empty(&file->pending_changes)) {
        Revision* rev = revision_alloc(file);
        if (rev == NULL) {
            return false;
        }
        file->current_revision = rev;
        
        // Move the changes from the temporary list to the revision
        rev->changes.next = file->pending_changes.next;
        rev->changes.prev = file->pending_changes.prev;
        file->pending_changes.next->prev = &rev->changes;
        file->pending_changes.prev->next = &rev->changes;
        list_init(&file->pending_changes);
    }
    
    // Invalidate piece cache
    cache_put(file, NULL);

    return true;
}

bool piece_chain_undo(PieceChain_t* file, size_t* pos) {

    // Commit any pending change
    if (!piece_chain_commit(file)) {
        return false;
    }

    if (list_first(&file->all_revisions, Revision, list) == file->current_revision) {
        return false;
    }

    size_t first_pos = file->size;
    
    // Revert all the changes in the last revision
    list_for_each_rev_member(c, &file->current_revision->changes, Change, list) {
        span_swap(file, &c->replacement, &c->original);
        *pos = c->pos;
        first_pos = MIN(first_pos, c->pos);
    }
    file->current_revision = list_prev(file->current_revision, Revision, list);

    return true;

}

bool piece_chain_redo(PieceChain_t* file, size_t* pos) {
    
    // Commit any pending change
    if (!piece_chain_commit(file)) {
        return false;
    }

    // Exit if there's nothing to redo
    if (list_last(&file->all_revisions, Revision, list) == file->current_revision) {
        return false;
    }

    size_t first_pos = file->size;
    
    // Reapply the changes in the revision
    Revision* rev = list_next(file->current_revision, Revision, list);
    list_for_each_member(c, &rev->changes, Change, list) {
        span_swap(file, &c->original, &c->replacement);
        *pos = c->pos;
        first_pos = MIN(first_pos, c->pos);
    }
    file->current_revision = rev;
    
    return true;

}

bool piece_chain_read_byte(PieceChain_t* file, size_t offset, unsigned char* out) {
    Piece* p;
    size_t p_offset;
    if (!piece_find(file, offset, &p, &p_offset)) {
        return false;
    }

    *out = p->data[p_offset];
    return true;
}

bool piece_chain_visit(PieceChain_t* file, size_t start, size_t len, bool (*visitor)(PieceChain_t*, size_t offset, const unsigned char* data, size_t len, void* user), void* user) {
    if (start >= file->size || len == 0) {
        return true;
    }

    // The current text is made up of the current active pieces
    size_t off = 0;
    list_for_each_member(p, &file->pieces, Piece, list) {
        if (off + p->size >= start) {
            size_t piece_start = off <= start ? start - off : 0;
            size_t piece_len = p->size - piece_start - (off + p->size >= start + len ? off + p->size - start - len : 0);
            if (!visitor(file, off + piece_start, p->data + piece_start, piece_len, user)) {
                return false;
            }
        }
        off += p->size;
    }

    return true;
}

PieceChainIterator_t* piece_chain_iter(PieceChain_t* file, size_t start, size_t len) {

    PieceChainIterator_t* it = calloc(1, sizeof(PieceChainIterator_t));
    if (it == NULL) {
        set_error(file, "Out of memory", ENOMEM);
        return NULL;
    }

    it->file = file;
    it->current_off = start;
    it->max_off = MIN(start + len, file->size);
    
    return it;

}

PieceChainIterator_t* piece_chain_iter_clone(PieceChainIterator_t* other) {
    if (other == NULL) {
        return NULL;
    }

    PieceChainIterator_t* it = malloc(sizeof(PieceChainIterator_t));
    if (it == NULL) {
        set_error(other->file, "Out of memory", ENOMEM);
        return NULL;
    }

    memcpy(it, other, sizeof(*it));
    return it;
}

bool piece_chain_iter_next(PieceChainIterator_t* it, const unsigned char** data, size_t* len) {
    
    if (it->current_off >= it->max_off) {
        return false;
    }
    
    // Find the piece containing the first byte if this is the first call to `next`
    if (it->current_piece == NULL) {
        size_t start = it->current_off;
        size_t off = 0;
        list_for_each_member(p, &it->file->pieces, Piece, list) {
            if (off + p->size > start) {
                size_t piece_start = off <= start ? start - off : 0;
                it->current_piece = p;
                *data = p->data + piece_start;
                *len = MIN(p->size - piece_start, it->max_off - start);
                break;
            }
            off += p->size;
        }

    } else {

        // Advance the iterator
        Piece* p = list_next(it->current_piece, Piece, list);
        it->current_piece = p;
        *data = p->data;
        *len = it->current_off + p->size > it->max_off ? it->max_off - it->current_off : p->size;
        
    }
    
    it->current_off += *len;
    return true;

}

void piece_chain_iter_free(PieceChainIterator_t* it) {
    free(it);
}