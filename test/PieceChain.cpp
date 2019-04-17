#include <iostream>
#include <sstream>
#include <catch2/catch.hpp>
#include <PieceChain/PieceChain.hpp>

using namespace piece_chain;
using namespace std;

static bool chain_equals(const string& str, const PieceChain& chain) {
    ostringstream ss;
    ss << chain;
    return str == ss.str();
}

static bool chain_equals(const string& str, const PieceChain& chain, size_t from, size_t len) {
    ostringstream ss;
    for (auto it = chain.begin(from, len); it != chain.end(); ++it) {
        ss.write((const char*) it->first, it->second);
    }
    return str == ss.str();
}

TEST_CASE("Initial state", "[initialstate]") {
    PieceChain chain;

    REQUIRE(chain.size() == 0);
    REQUIRE(chain.empty());
}

TEST_CASE("Insert", "[edits]") {
    PieceChain chain;

    chain.insert(0, "hello", 5);
    REQUIRE(chain_equals("hello", chain));
    chain.insert(0, "<", 1);
    REQUIRE(chain_equals("<hello", chain));
    chain.insert(6, "world", 5);
    REQUIRE(chain_equals("<helloworld", chain));
    chain.insert(6, " ", 1);
    REQUIRE(chain_equals("<hello world", chain));
    chain.insert(12, ">", 1);
    REQUIRE(chain_equals("<hello world>", chain));
}

TEST_CASE("Delete", "[edits]") {
    PieceChain chain;

    chain.insert(0, "hello world", 11);
    chain.remove(0, 5);
    REQUIRE(chain_equals(" world", chain));
    chain.remove(1, 5);
    REQUIRE(chain_equals(" ", chain));
    chain.remove(0, 1);
    REQUIRE(chain_equals("", chain));
}

TEST_CASE("Insert and delete", "[edits]") {
    PieceChain chain;

    chain.insert(0, "hello", 5);          // "hello"
    chain.remove(0, 3);                   // "lo"
    chain.insert(1, "w", 1);              // "lwo"
    chain.insert(3, "rld", 3);            // "lworld"
    chain.remove(0, 1);                   // "world"
    chain.insert(0, "hello_", 6);         // "hello_world"
    chain.replace(5, " ", 1);             // "hello world"
    REQUIRE(chain_equals("hello world", chain));
}

TEST_CASE("Undo", "[undo]") {
    PieceChain chain;
    chain.insert(0, "hello", 5);

    size_t pos = *chain.undo();
    REQUIRE(pos == 0);
    REQUIRE(chain_equals("", chain));

    chain.insert(0, "hello", 5);
    chain.commit();
    chain.insert(5, " world", 6);

    pos = *chain.undo();
    REQUIRE(pos == 5);
    REQUIRE(chain_equals("hello", chain));

    pos = *chain.undo();
    REQUIRE(pos == 0);
    REQUIRE(chain_equals("", chain));

    REQUIRE_FALSE(chain.undo());
    REQUIRE(chain_equals("", chain));
}

TEST_CASE("Redo", "[undo]") {
    PieceChain chain;
    chain.insert(0, "hello", 5);

    REQUIRE_FALSE(chain.redo());
    REQUIRE(chain_equals("hello", chain));

    chain.insert(5, " world", 6);

    size_t pos;
    pos = *chain.undo();
    REQUIRE(pos == 5);
    REQUIRE(chain_equals("hello", chain));

    pos = *chain.redo();
    REQUIRE(pos == 5);
    REQUIRE(chain_equals("hello world", chain));

    REQUIRE(chain.undo());
    REQUIRE(chain.undo());
    REQUIRE(chain_equals("", chain));

    pos = *chain.redo();
    REQUIRE(pos == 0);
    REQUIRE(chain_equals("hello", chain));
    pos = *chain.redo();
    REQUIRE(pos == 5);
    REQUIRE(chain_equals("hello world", chain));
    
    REQUIRE_FALSE(chain.redo());
    REQUIRE(chain_equals("hello world", chain));
}

TEST_CASE("Undo and redo with insertions inbetween", "[undo]") {
    PieceChain chain;

    // Create a small history
    chain.insert(0, "hello", 5);  // "hello"
    chain.commit();
    chain.remove(0, 3);           // "lo"
    chain.commit();
    chain.insert(1, "w", 1);      // "lwo"
    chain.commit();
    chain.insert(3, "rld", 3);    // "lworld"
    chain.commit();
    chain.remove(0, 1);           // "world"
    chain.commit();
    chain.insert(0, "hello_", 6); // "hello_world"
    chain.commit();
    chain.replace(5, " ", 1);     // "hello world"
    chain.commit();
    REQUIRE(chain_equals("hello world", chain));

    // Start navigating revisions to test undoing of both insertion and deletions

    REQUIRE_FALSE(chain.redo());

    size_t pos;
    pos = *chain.undo();
    REQUIRE(pos == 5);
    REQUIRE(chain_equals("hello_world", chain));

    pos = *chain.undo();
    REQUIRE(pos == 0);
    REQUIRE(chain_equals("world", chain));

    pos = *chain.undo();
    REQUIRE(pos == 0);
    REQUIRE(chain_equals("lworld", chain));

    pos = *chain.redo();
    REQUIRE(pos == 0);
    REQUIRE(chain_equals("world", chain));

    pos = *chain.redo();
    REQUIRE(pos == 0);
    REQUIRE(chain_equals("hello_world", chain));

    pos = *chain.redo();
    REQUIRE(pos == 5);
    REQUIRE(chain_equals("hello world", chain));

    REQUIRE_FALSE(chain.redo());

    // Unroll to the beginning and then to the end to count the number of revisions

    int nrevisions = 0;
    while (chain.undo()) {
        nrevisions++;
    }
    REQUIRE(chain_equals("", chain));
    REQUIRE(nrevisions == 7);

    nrevisions = 0;
    while (chain.redo()) {
        nrevisions++;
    }
    REQUIRE(chain_equals("hello world", chain));
    REQUIRE(nrevisions == 7);

}

TEST_CASE("Can iterate portions of text", "[iterator]") {
    PieceChain chain;
    chain.insert(0, "hello world", 11);

    REQUIRE(chain_equals("lo wor", chain, 3, 6));
}

TEST_CASE("Can iterate portions of pieces", "[iterator]") {
    PieceChain chain;
    chain.insert(0, " world", 6);
    chain.insert(0, "hello", 5);

    REQUIRE(chain_equals("hello world", chain));
    REQUIRE(chain_equals("he", chain, 0, 2));
    REQUIRE(chain_equals("el", chain, 1, 2));
    REQUIRE(chain_equals("hello", chain, 0, 5));
    REQUIRE(chain_equals("hello wo", chain, 0, 8));
    REQUIRE(chain_equals("lo wo", chain, 3, 5));
    REQUIRE(chain_equals(" worl", chain, 5, 5));
    REQUIRE(chain_equals(" world", chain, 5, 6));
    REQUIRE(chain_equals("or", chain, 7, 2));
    REQUIRE(chain_equals("ld", chain, 9, 2));
}
