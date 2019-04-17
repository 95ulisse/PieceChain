#ifndef __PIECE_CHAIN_HPP__
#define __PIECE_CHAIN_HPP__

#include <string>
#include <cstring>
#include <iostream>
#include <optional>

#include "PieceChain/PieceChain.h"

namespace piece_chain {



class PieceChainException : public std::runtime_error {
public:

    PieceChainException(const PieceChainError_t* e)
        : std::runtime_error(nullptr)
    {
        constexpr size_t s = sizeof(_what) / sizeof(_what[0]);
        snprintf(_what, s, "%s: %s.", e->message, strerror(e->err));
        _what[s - 1] = '\0';
    }

    virtual const char* what() const throw() override {
        return _what;
    }

private:
    char _what[256];
};



/** Specifies how to save the contents of a PieceChain to a file. */
enum class SaveMode {

    /** First try `Atomic` mode, then, if it fails, use `InPlace` mode. */
    Auto = SAVE_MODE_AUTO,

    /**
     * Save the contents to a temporary file, then move it to the destination using `rename(2)`.
     * 
     * This mode has the advantage of being resistent to I/O failures, but cannot be used
     * if the target file is either a symbolic or hard link and if there are errors in restoring
     * the original file permissions.
     */
    Atomic = SAVE_MODE_ATOMIC,

    /**
     * Regular file overwriting strategy: open a file, write to it, fsync, close.
     * If possible, use the `Atomic` mode, since the `InPlace` mode has the disadvantage of
     * possibly causing data loss in case of I/O error.
     */
    InPlace = SAVE_MODE_INPLACE

};



class PieceChainIterator {
public:
    using iterator_category = std::output_iterator_tag;
    using value_type = const std::pair<const unsigned char*, size_t>;
    using difference_type = std::ptrdiff_t;
    using pointer = const std::pair<const unsigned char*, size_t>*;
    using reference = const std::pair<const unsigned char*, size_t>&;

    PieceChainIterator(PieceChain_t* chain)
        : PieceChainIterator(chain, nullptr)
    {
    }

    PieceChainIterator(PieceChain_t* chain, PieceChainIterator_t* ptr)
        : _chain(chain),
          _ptr(ptr)
    {
        if (ptr != nullptr) {
            // Advance the iterator to load the first chunk
            ++(*this);
        }
    }

    ~PieceChainIterator() {
        destroy();
    }

    // Copy constructor and assignment

    PieceChainIterator(const PieceChainIterator& other) {
        PieceChainIterator_t* clone = nullptr;
        if (other._ptr != nullptr) {
            clone = piece_chain_iter_clone(other._ptr);
            if (clone == nullptr) {
                auto e = piece_chain_last_error(other._chain);
                throw PieceChainException(e);
            }
        }
        _chain = other._chain;
        _ptr = clone;
        _currentData = other._currentData;
    }

    PieceChainIterator& operator=(const PieceChainIterator& other) {
        destroy();

        PieceChainIterator_t* clone = nullptr;
        if (other._ptr != nullptr) {
            clone = piece_chain_iter_clone(other._ptr);
            if (clone == nullptr) {
                auto e = piece_chain_last_error(other._chain);
                throw PieceChainException(e);
            }
        }
        _chain = other._chain;
        _ptr = clone;
        _currentData = other._currentData;
    }

    // Movable
    PieceChainIterator(PieceChainIterator&&) = default;
    PieceChainIterator& operator=(PieceChainIterator&&) = default;

    bool operator==(const PieceChainIterator& other) const {
        return _ptr == other._ptr;
    }

    bool operator!=(const PieceChainIterator& other) const {
        return _ptr != other._ptr;
    }
    
    reference operator*() const {
        return _currentData;
    }

    pointer operator->() const {
        return &_currentData;
    }

    PieceChainIterator& operator++() {
        if (_ptr != nullptr) {
            const unsigned char* data;
            size_t len;
            if (piece_chain_iter_next(_ptr, &data, &len)) {
                _currentData = std::make_pair(data, len);
            } else {
                destroy();
            }
        }
        return *this;
    }

    PieceChainIterator operator++(int) {
        PieceChainIterator tmp(*this);
        ++(*this);
        return tmp;
    }

private:
    PieceChain_t* _chain;
    PieceChainIterator_t* _ptr;
    std::pair<const unsigned char*, size_t> _currentData;

    void destroy() {
        if (_ptr != nullptr) {
            piece_chain_iter_free(_ptr);
            _ptr = nullptr;
        }
    }
};



/**
 * A piece chain is a data structure that allows fast text insertion and deletion operations,
 * unlimited undo/redo and grouping of operations.
 */
class PieceChain {
public:

    inline PieceChain() {
        if ((_ptr = piece_chain_open(nullptr)) == nullptr) {
            throw std::system_error(errno, std::generic_category());
        }
    }

    inline explicit PieceChain(const std::string& path) {
        if ((_ptr = piece_chain_open(path.c_str())) == nullptr) {
            throw std::system_error(errno, std::generic_category());
        }
    }

    inline ~PieceChain() {
        piece_chain_destroy(_ptr);
    }

    // Non-copyable
    PieceChain(const PieceChain&) = delete;
    PieceChain& operator=(const PieceChain&) = delete;

    // Movable
    PieceChain(PieceChain&&) = default;
    PieceChain& operator=(PieceChain&&) = default;

    /** Returns the size (in bytes) of the data stored in this `PieceChain`. */
    inline size_t size() const {
        return piece_chain_size(_ptr);
    }

    /** Returns a boolean value indicating whether or not this `PieceChain` contains any data. */
    inline bool empty() const {
        return size() == 0;
    }

    /** Returns whether the contents of this piece chain have been modified or not since last save. */
    inline bool dirty() const {
        return piece_chain_dirty(_ptr);
    }

    /** Reads a single byte from the data. Access out of bounds throws a runtime_error. */
    inline unsigned char at(size_t offset) const {
        unsigned char out;
        if (piece_chain_read_byte(_ptr, offset, &out)) {
            return out;
        } else {
            throw std::runtime_error("Out of bounds.");
        }
    }

    /** Reads a single byte from the data. Access out of bounds throws a runtime_error. */
    inline unsigned char operator[](size_t offset) const {
        return at(offset);
    }

    /** Saves the contents of this `PieceChain` to a file. */
    inline void save(const std::string& path, SaveMode mode = SaveMode::Auto) {
        if (!piece_chain_save(_ptr, path.c_str(), (PieceChainSaveMode) mode)) {
            auto e = piece_chain_last_error(_ptr);
            throw PieceChainException(e);
        }
    }

    /** Inserts the given data at the given offset. */
    inline void insert(size_t offset, const unsigned char* data, size_t len) {
        if (!piece_chain_insert(_ptr, offset, data, len)) {
            auto e = piece_chain_last_error(_ptr);
            throw PieceChainException(e);
        }
    }

    /** Inserts the given data at the given offset. */
    inline void insert(size_t offset, const char* data, size_t len) {
        return insert(offset, (const unsigned char*) data, len);
    }

    /** Inserts the given data at the given offset. */
    inline void insert(size_t offset, const std::string& data) {
        return insert(offset, (const unsigned char*) data.c_str(), data.size());
    }

    /** Deletes a range of bytes. */
    inline void remove(size_t offset, size_t len) {
        if (!piece_chain_delete(_ptr, offset, len)) {
            auto e = piece_chain_last_error(_ptr);
            throw PieceChainException(e);
        }
    }

    /** Replaces a range of bytes with the given data. */
    inline void replace(size_t offset, const unsigned char* data, size_t len) {
        if (!piece_chain_replace(_ptr, offset, data, len)) {
            auto e = piece_chain_last_error(_ptr);
            throw PieceChainException(e);
        }
    }

    /** Replaces a range of bytes with the given data. */
    inline void replace(size_t offset, const char* data, size_t len) {
        return replace(offset, (const unsigned char*) data, len);
    }

    /** Replaces a range of bytes with the given data. */
    inline void replace(size_t offset, const std::string& data) {
        return replace(offset, (const unsigned char*) data.c_str(), data.size());
    }

    /** Commits any pending change in a new revision, snapshotting the current file status. */
    inline void commit() {
        if (!piece_chain_commit(_ptr)) {
            auto e = piece_chain_last_error(_ptr);
            throw PieceChainException(e);
        }
    }

    /** Undoes a recent modification. Returns the location of the last change, if the file changed. */
    inline std::optional<size_t> undo() {
        size_t out;
        if (piece_chain_undo(_ptr, &out)) {
            return out;
        } else {
            return std::nullopt;
        }
    }

    /** Redoes an undone modification. Returns the location of the last change, if the file changed. */
    inline std::optional<size_t> redo() {
        size_t out;
        if (piece_chain_redo(_ptr, &out)) {
            return out;
        } else {
            return std::nullopt;
        }
    }

    /**
     * Discards all the data stored in this `PieceChain`.
     * Note that this does NOT discard undo history.
     */
    inline void clear() {
        commit();
        remove(0, size());
        commit();
    }

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
    inline PieceChainIterator begin(size_t start, size_t len) const {
        if (auto it = piece_chain_iter(_ptr, start, len); it != nullptr) {
            return PieceChainIterator(_ptr, it);
        } else {
            auto e = piece_chain_last_error(_ptr);
            throw PieceChainException(e);
        }
    }

    /** Returns an iterator referring to the past-the-end element. */
    inline PieceChainIterator end() const {
        return PieceChainIterator(_ptr);
    }

private:
    PieceChain_t* _ptr;
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
