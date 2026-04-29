// Replay and Idempotence.

#include <catch2/catch_test_macros.hpp>
#include "log_store.h"

// Ensure merging same entries multiple times doesn't duplicate
TEST_CASE("Replay sync is idempotent", "[replay]") {
    LogStore store("node-1");

    // Create entries
    ChatEntry e1;
    e1.message_id = "node-1_1";
    e1.sender_id = "node-1";
    e1.payload = "hello";
    e1.lamport_time = 1;
    e1.epoch = 0;

    ChatEntry e2 = e1;
    e2.message_id = "node-1_2";
    e2.payload = "world";
    e2.lamport_time = 2;

    // Merge same batch multiple times
    store.merge({e1, e2});
    store.merge({e1, e2});
    store.merge({e1, e2});

    // Should not duplicate
    REQUIRE(store.all().size() == 2);
}