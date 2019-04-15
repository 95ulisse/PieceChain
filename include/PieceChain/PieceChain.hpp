#ifndef PIECE_CHAIN_HPP
#define PIECE_CHAIN_HPP

#include <string>
#include <iostream>
#include <optional>

/**
 * This is an implementation of a piece chain.
 * A piece chain is a data structure that allows fast text insertion and deletion operations,
 * unlimited undo/redo and grouping of operations.
 *
 * For the sake of simplicity, during the description of the structure, we will refer
 * to the contents of a piece chain with the word "text", but the piece chain has nothing
 * that prevents it from being used to store non-textual data.
 *
 * The core idea is to keep the whole text in a linked list of pieces:
 *
 * #1                #2        #3
 * +----------+ ---> +--+ ---> +------+
 * |This is so|      |me|      | text!|
 * +----------+ <--- +--+ <--- +------+
 *
 * Evey time we make an insertion, we *replace* the pieces that we should modify with new ones.
 * Pieces are **immutable**, and they are always kept around for undo/redo support (look at the IDs).
 *
 * #1                #2        #4           #5                         #6
 * +----------+ ---> +--+ ---> +-----+ ---> +-------------------+ ---> +-+
 * |This is so|      |me|      | text|      |, and now some more|      |!|
 * +----------+ <--- +--+ <--- +-----+ <--- +-------------------+ <--- +-+
 *
 * This change is recorded as a pair of spans:
 * "Replace the span ranging from piece #2 to #3, with the span from #4 to #6."
 * To undo a change, just swap again the spans.
 *
 * When we start with a new file, we start with an empty piece list, but when we open an existing
 * file, we can mmap it and use its contents as the first piece of the chain. The region can be mapped
 * read-only, because we will never need to change it (the piece chain is immutable, remember).
 *
 * A final note on the management of the memory:
 * we need some kind of custom memory management, because we need to be able to keep track of which block of memory has been
 * mmapped and which one has been allocated on the heap, so that we can free them appropriately.
 * A piece does not own the data, it only has a pointer *inside* a memory block.
 *
 * This is an example showing how a new insertion in the middle of an existing file is represented.
 *
 * +-----------------------------------+ ---> +--------+
 * | Original file contents (mmap R/O) |      |new text|                     Memory blocks
 * +-----------------------------------+ <--- +--------+
 * ^            ^                             ^
 * |            |                             |
 * |            +----------------------+      |
 * |                 +-----------------|------+
 * |                 |                 |
 * +----------+ ---> +----------+ ---> +----------+
 * | Piece #1 |      | Piece #2 |      | Piece #3 |                          Piece chain
 * +----------+ <--- +----------+ <--- +----------+
 *
 *
 *
 * Caching
 * =======
 *
 * While creating a piece is cheap, creating a piece for every single inserted char is a bit excessive,
 * so we make an exception to the rule: all memory blocks and pieces **except the last ones** are immutable.
 * When we insert a new sequence of bytes, we first check if they need to be appended to the last modified piece:
 * if it is the case, we can just extend the last piece without creating a new one. This has the disadvantage that
 * consecutive edits are not undoable separately.
 *
 *
 *
 * Undo / Redo
 * ===========
 *
 * This implementation keeps a linear undo history, which means that if you undo a revision and then
 * modify the text, then the redo history is discarded.
 *
 * The whole idea of spans + changes + revisions is to allow simple undoing of changes.
 * Changes are grouped in revisions, which are just a mean to undo a group of changes together
 * (think of the replacement of a char as a deletion followed by an insertion: we don't want to undo
 * the deletion and the insertion individually, but the replacement as a whole).
 *
 */



namespace piece_chain {



/** Specifies how to save the contents of a PieceChain to a file. */
enum class SaveMode {

    /** First try `Atomic` mode, then, if it fails, use `InPlace` mode. */
    Auto,

    /**
     * Save the contents to a temporary file, then move it to the destination using `rename(2)`.
     * 
     * This mode has the advantage of being resistent to I/O failures, but cannot be used
     * if the target file is either a symbolic or hard link and if there are errors in restoring
     * the original file permissions.
     */
    Atomic,

    /**
     * Regular file overwriting strategy: open a file, write to it, fsync, close.
     * If possible, use the `Atomic` mode, since the `InPlace` mode has the disadvantage of
     * possibly causing data loss in case of I/O error.
     */
    InPlace

};



class PieceChainIterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = const std::pair<const unsigned char*, size_t>;
    using difference_type = std::ptrdiff_t;
    using pointer = const std::pair<const unsigned char*, size_t>*;
    using reference = const std::pair<const unsigned char*, size_t>&;

    bool operator==(const PieceChainIterator& other) const;
    bool operator!=(const PieceChainIterator& other) const;
    
    value_type operator*() const;
    pointer operator->() const;

    PieceChainIterator& operator++();
    PieceChainIterator& operator--();
    PieceChainIterator operator++(int);
    PieceChainIterator operator--(int);
};



/**
 * A piece chain is a data structure that allows fast text insertion and deletion operations,
 * unlimited undo/redo and grouping of operations.
 */
class PieceChain {
public:

    PieceChain();
    explicit PieceChain(const std::string& path);

    PieceChain(const PieceChain&) = default;
    PieceChain(PieceChain&&) = default;
    PieceChain& operator=(const PieceChain&) = default;
    PieceChain& operator=(PieceChain&&) = default;

    ~PieceChain() = default;

    /** Returns the size (in bytes) of the data stored in this `PieceChain`. */
    size_t size() const;

    /** Returns a boolean value indicating whether or not this `PieceChain` contains any data. */
    inline bool empty() const {
        return size() == 0;
    }

    /** Reads a single byte from the data. Access out of bounds is undefined behaviour. */
    unsigned char at(size_t offset) const;

    /** Reads a single byte from the data. Access out of bounds is undefined behaviour. */
    inline unsigned char operator[](size_t offset) const {
        return at(offset);
    }

    /** Saves the contents of this `PieceChain` to a file. */
    void save(const std::string& path, SaveMode mode = SaveMode::Auto);

    /** Inserts the given data at the given offset. */
    void insert(size_t offset, const unsigned char* data, size_t len);

    /** Inserts the given data at the given offset. */
    inline void insert(size_t offset, const char* data, size_t len) {
        return insert(offset, (const unsigned char*) data, len);
    }

    /** Inserts the given data at the given offset. */
    inline void insert(size_t offset, const std::string& data) {
        return insert(offset, (const unsigned char*) data.c_str(), data.size());
    }

    /** Deletes a range of bytes. */
    void remove(size_t offset, size_t len);

    /** Replaces a range of bytes with the given data. */
    void replace(size_t offset, const unsigned char* data, size_t len);

    /** Replaces a range of bytes with the given data. */
    inline void replace(size_t offset, const char* data, size_t len) {
        return replace(offset, (const unsigned char*) data, len);
    }

    /** Replaces a range of bytes with the given data. */
    inline void replace(size_t offset, const std::string& data) {
        return replace(offset, (const unsigned char*) data.c_str(), data.size());
    }

    /** Commits any pending change in a new revision, snapshotting the current file status. */
    void commit();

    /** Undoes a recent modification. Returns the location of the last change, if the file changed. */
    std::optional<size_t> undo();

    /** Redoes an undone modification. Returns the location of the last change, if the file changed. */
    std::optional<size_t> redo();

    /** Discards all the data stored in this `PieceChain`. Note that this does NOT discard undo history. */
    void clear();

    /**
     * Returns an iterator over the given section of a file.
     * Altering the contents of the file while an iterator is open will result in undefined behaviour.
     */
    inline PieceChainIterator begin() const {
        return begin(0, size());
    }

    /**
     * Returns an iterator over the given section of a file.
     * Altering the contents of the file while an iterator is open will result in undefined behaviour.
     */
    PieceChainIterator begin(size_t from, size_t len) const;

    /** Returns an iterator referring to the past-the-end element. */
    PieceChainIterator end() const;

};

/** Writes the contents of the given `PieceChain` to the given stream. */
inline static std::ostream& operator<<(std::ostream& stream, const PieceChain& chain) {
    for (auto it : chain) {
        stream.write((const char*) it.first, it.second);
    }
    return stream;
}

} // namespace piece_chain

#endif
