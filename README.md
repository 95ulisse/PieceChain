# PieceChain [![Build Status](https://travis-ci.org/95ulisse/PieceChain.svg?branch=master)](https://travis-ci.org/95ulisse/PieceChain) [![codecov](https://codecov.io/gh/95ulisse/PieceChain/branch/master/graph/badge.svg)](https://codecov.io/gh/95ulisse/PieceChain)

This is an implementation of a piece chain.
A piece chain is a data structure that allows fast text insertion and deletion operations,
unlimited undo/redo and grouping of operations.

This is the same data structure used in [HEdit](https://github.com/95ulisse/hedit),
but extracted in a more easily reusable library and with a C++ wrapper.
This implementation is **heavily** inspired from [vis](https://github.com/martanne/vis).



## General overview

For the sake of simplicity, during the description of the structure, we will refer
to the contents of a piece chain with the word "text", but the piece chain has nothing
that prevents it from being used to store non-textual data.

The core idea is to keep the whole text in a linked list of pieces:

```
#1                #2        #3
+----------+ ---> +--+ ---> +------+
|This is so|      |me|      | text!|
+----------+ <--- +--+ <--- +------+
```

Evey time we make an insertion, we *replace* the pieces that we should modify with new ones.
Pieces are **immutable**, and they are always kept around for undo/redo support (look at the IDs).

```
#1                #2        #4           #5                         #6
+----------+ ---> +--+ ---> +-----+ ---> +-------------------+ ---> +-+
|This is so|      |me|      | text|      |, and now some more|      |!|
+----------+ <--- +--+ <--- +-----+ <--- +-------------------+ <--- +-+
```

This change is recorded as a pair of spans:
*"Replace the span ranging from piece `#2` to `#3`, with the span from `#4` to `#6`."*
To undo a change, just swap again the spans.

When we start with a new file, we start with an empty piece list, but when we open an existing
file, we can mmap it and use its contents as the first piece of the chain. The region can be mapped
read-only, because we will never need to change it (the piece chain is immutable, remember).

A final note on the management of the memory:
we need some kind of custom memory management, because we need to be able to keep track of which block of memory has been
mmapped and which one has been allocated on the heap, so that we can free them appropriately.
A piece does not own the data, it only has a pointer *inside* a memory block.

This is an example showing how a new insertion in the middle of an existing file is represented.

```
+-----------------------------------+ ---> +--------+
| Original file contents (mmap R/O) |      |new text|                     Memory blocks
+-----------------------------------+ <--- +--------+
^            ^                             ^
|            |                             |
|            +----------------------+      |
|                 +-----------------|------+
|                 |                 |
+----------+ ---> +----------+ ---> +----------+
| Piece #1 |      | Piece #2 |      | Piece #3 |                          Piece chain
+----------+ <--- +----------+ <--- +----------+
```



## Caching

While creating a piece is cheap, creating a piece for every single inserted char is a bit excessive,
so we make an exception to the rule: all memory blocks and pieces **except the last ones** are immutable.
When we insert a new sequence of bytes, we first check if they need to be appended to the last modified piece:
if it is the case, we can just extend the last piece without creating a new one. This has the disadvantage that
consecutive edits are not undoable separately.



## Undo / Redo

This implementation keeps a linear undo history, which means that if you undo a revision and then
modify the text, then the redo history is discarded.

The whole idea of spans + changes + revisions is to allow simple undoing of changes.
Changes are grouped in revisions, which are just a mean to undo a group of changes together
(think of the replacement of a char as a deletion followed by an insertion: we don't want to undo
the deletion and the insertion individually, but the replacement as a whole).