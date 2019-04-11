#include <catch2/catch.hpp>

TEST_CASE("Tests are run", "[PieceChain]") {
    REQUIRE(1 == 1);

    SECTION("Section1") {
        REQUIRE(2 == 2);
    }
    
    SECTION("Section2") {
        REQUIRE(3 == 3);
    }
}
