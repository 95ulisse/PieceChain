add_executable(PieceChainTest src/main.cpp)
target_link_libraries(PieceChainTest PRIVATE PieceChain)

add_test(NAME PieceChainTest COMMAND PieceChainTest)
