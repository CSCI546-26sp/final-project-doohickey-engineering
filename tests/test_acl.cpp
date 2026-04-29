#include <catch2/catch_test_macros.hpp>
#include "log_store.h"

// Simulate rejection behavior (data plane expects valid entries only)
TEST_CASE("Invalid epoch entries are rejected (simulated)", "[acl]") {
    LogStore store("node-1");

    ChatEntry valid;
    valid.message_id = "node-1_1";
    valid.sender_id = "node-1";
    valid.payload = "ok";
    valid.lamport_time = 1;
    valid.epoch = 0;

    ChatEntry invalid = valid;
    invalid.message_id = "node-1_2";
    invalid.epoch = 999;

    // In real system, invalid would be filtered BEFORE merge
    store.merge({valid});
    store.merge({invalid}); // assume filtered externally

    // Only valid entry should exist
    REQUIRE(store.all().size() == 1);
}