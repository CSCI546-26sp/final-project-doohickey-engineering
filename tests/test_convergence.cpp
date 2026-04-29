#include <catch2/catch_test_macros.hpp>
#include "log_store.h"

// Ensure two nodes converge to same state after sync
TEST_CASE("Nodes converge to identical state", "[convergence]") {
    LogStore nodeA("node-1");
    LogStore nodeB("node-2");

    // Node A creates entries
    auto e1 = nodeA.append("hello");
    auto e2 = nodeA.append("world");

    // Node B merges from A
    nodeB.merge({e1, e2});

    REQUIRE(nodeA.get_hash() == nodeB.get_hash());
    REQUIRE(nodeA.all().size() == nodeB.all().size());
}