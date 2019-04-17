#ifndef __PIECE_CHAIN_H__
#define __PIECE_CHAIN_H__

#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/** Opaque structure representing a piece chain. */
typedef struct PieceChain_t PieceChain_t;

/** Opaque structure representing an iterator over the contents of a piece chain. */
typedef struct PieceChainIterator_t PieceChainIterator_t;

/** Description of an error occurred during the processing of one of the operations on a piece chain. */
typedef struct PieceChainError_t {
    const char* message;
    int err;
} PieceChainError_t;

enum PieceChainSaveMode {
    SAVE_MODE_AUTO = 0,
    SAVE_MODE_ATOMIC,
    SAVE_MODE_INPLACE
};

/** Creates a new PieceChain_t initialized with the contents of the given file. Pass NULL to create an empty piece chain. */
PieceChain_t* piece_chain_open(const char* path);

/** Destroys a piece chain and releases all the resources held. */
void piece_chain_destroy(PieceChain_t*);

/** Returns the size in bytes of the contents of a piece chain. */
size_t piece_chain_size(PieceChain_t*);

/** Returns whether the contents of this piece chain have been modified or not since last save. */
bool piece_chain_dirty(PieceChain_t*);

/** Returns a string containing a human-readable description of the last error in case a function fails. */
PieceChainError_t* piece_chain_last_error(PieceChain_t*);

/** Saves the contents of a piece chain to a file. */
bool piece_chain_save(PieceChain_t*, const char* path, enum PieceChainSaveMode);

/** Inserts a string at the given offset. */
bool piece_chain_insert(PieceChain_t*, size_t offset, const unsigned char* data, size_t len);

/** Deletes a range of bytes. */
bool piece_chain_delete(PieceChain_t*, size_t offset, size_t len);

/** Replaces a string with another. */
bool piece_chain_replace(PieceChain_t*, size_t offset, const unsigned char* data, size_t len);

/** Commits any pending change in a new revision, snapshotting the current status. */
bool piece_chain_commit(PieceChain_t*);

/** Undoes a recent modification. `*pos` contains the location of the last change, if the contents of the piece chain changed. */
bool piece_chain_undo(PieceChain_t*, size_t* pos);

/** Redoes an undone modification. `*pos` contains the location of the last change, if the contents of the piece chain changed. */
bool piece_chain_redo(PieceChain_t*, size_t* pos);

/** Reads a single byte from the piece chain. */
bool piece_chain_read_byte(PieceChain_t*, size_t offset, unsigned char* out);

/**
 * Visits a portion of the contents of this piece chain.
 * The visitor function may be called more than once with different fragments of contents.
 */
bool piece_chain_visit(
    PieceChain_t*,
    size_t start,
    size_t len,
    bool (*visitor)(PieceChain_t*, size_t offset, const unsigned char* data, size_t len, void* user),
    void* user
);

/**
 * Returns an iterator over the given section of a piece chain.
 * Altering the contents of the piece chain while an iterator is open will result in undefined behaviour.
 */
PieceChainIterator_t* piece_chain_iter(PieceChain_t*, size_t start, size_t len);

/** Clones an iterator, yielding a new one with exactly the same internal state. */
PieceChainIterator_t* piece_chain_iter_clone(PieceChainIterator_t*);

/**
 * Advances the iterator.
 * Returns `true` if there's more data to read, and `*data` and `*len` will contain
 * a pointer to the data and its length. Returns `false` and does not alter the pointers
 * if no more data is available.
 */
bool piece_chain_iter_next(PieceChainIterator_t*, const unsigned char** data, size_t* len);

/** Releases all the resources held by the given iterator. */
void piece_chain_iter_free(PieceChainIterator_t*);


#ifdef __cplusplus
}
#endif

#endif