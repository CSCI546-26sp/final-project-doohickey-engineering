#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <set>
#include "log_store.h"

// ---------------------------------------------------------------
// Append
// ---------------------------------------------------------------
TEST_CASE("LogStore - append increments lamport_time", "[store]") {
    LogStore store("node-1");

    auto e1 = store.append("first");
    auto e2 = store.append("second");
    auto e3 = store.append("third");

    REQUIRE(e1.lamport_time == 1);
    REQUIRE(e2.lamport_time == 2);
    REQUIRE(e3.lamport_time == 3);
}

TEST_CASE("LogStore - append sets sender_id to node_id", "[store]") {
    LogStore store("node-42");
    auto e = store.append("hello");
    REQUIRE(e.sender_id == "node-42");
}

TEST_CASE("LogStore - append generates unique message_ids", "[store]") {
    LogStore store("node-1");
    std::set<std::string> ids;

    for (int i = 0; i < 100; i++) {
        auto e = store.append("payload");
        ids.insert(e.message_id);
    }
    REQUIRE(ids.size() == 100);
}

TEST_CASE("LogStore - append stores correct payload", "[store]") {
    LogStore store("node-1");
    auto e = store.append("my payload");
    REQUIRE(e.payload == "my payload");
}

TEST_CASE("LogStore - append stamps current epoch", "[store]") {
    LogStore store("node-1", /*epoch=*/3);
    auto e = store.append("data");
    REQUIRE(e.epoch == 3);
}

// ---------------------------------------------------------------
// Read
// ---------------------------------------------------------------
TEST_CASE("LogStore - read returns correct entry", "[store]") {
    LogStore store("node-1");
    auto appended = store.append("find me");

    auto result = store.read(appended.message_id);
    REQUIRE(result.has_value());
    REQUIRE(result->payload == "find me");
    REQUIRE(result->message_id == appended.message_id);
}

TEST_CASE("LogStore - read returns nullopt for unknown id", "[store]") {
    LogStore store("node-1");
    store.append("something");

    auto result = store.read("nonexistent_id");
    REQUIRE_FALSE(result.has_value());
}

// ---------------------------------------------------------------
// ReadRange
// ---------------------------------------------------------------
TEST_CASE("LogStore - read_range returns correct slice", "[store]") {
    LogStore store("node-1");
    for (int i = 0; i < 6; i++) store.append("entry " + std::to_string(i));

    auto slice = store.read_range(2, 4);
    REQUIRE(slice.size() == 3); // entries at index 2, 3, 4

    REQUIRE(slice[0].payload == "entry 2");
    REQUIRE(slice[1].payload == "entry 3");
    REQUIRE(slice[2].payload == "entry 4");
}

TEST_CASE("LogStore - read_range with from == to returns one entry", "[store]") {
    LogStore store("node-1");
    for (int i = 0; i < 5; i++) store.append("e" + std::to_string(i));

    auto slice = store.read_range(2, 2);
    REQUIRE(slice.size() == 1);
    REQUIRE(slice[0].payload == "e2");
}

TEST_CASE("LogStore - read_range beyond bounds clamps safely", "[store]") {
    LogStore store("node-1");
    store.append("only");

    auto slice = store.read_range(0, 100);
    REQUIRE(slice.size() == 1);
}

// ---------------------------------------------------------------
// Hash
// ---------------------------------------------------------------
TEST_CASE("LogStore - get_hash changes after append", "[store]") {
    LogStore store("node-1");
    uint32_t h1 = store.get_hash();

    store.append("something");
    uint32_t h2 = store.get_hash();

    REQUIRE(h1 != h2);
}

TEST_CASE("LogStore - two stores with same entries have same hash", "[store]") {
    LogStore a("node-1");
    LogStore b("node-1");

    a.append("alpha");
    a.append("beta");
    b.append("alpha");
    b.append("beta");

    REQUIRE(a.get_hash() == b.get_hash());
}

TEST_CASE("LogStore - different payloads produce different hash", "[store]") {
    LogStore a("node-1");
    LogStore b("node-1");

    a.append("foo");
    b.append("bar");

    REQUIRE(a.get_hash() != b.get_hash());
}

// ---------------------------------------------------------------
// Checksum
// ---------------------------------------------------------------
TEST_CASE("LogStore - checksum is non-zero for non-empty payload", "[store]") {
    LogStore store("node-1");
    auto e = store.append("hello");
    REQUIRE(e.checksum != 0);
}

TEST_CASE("LogStore - same payload produces same checksum", "[store]") {
    LogStore a("node-1");
    LogStore b("node-2"); // different node

    auto e1 = a.append("same data");
    auto e2 = b.append("same data");

    // checksum is over payload only — must match regardless of node
    REQUIRE(e1.checksum == e2.checksum);
}

TEST_CASE("LogStore - different payloads produce different checksum", "[store]") {
    LogStore store("node-1");
    auto e1 = store.append("aaa");
    auto e2 = store.append("bbb");
    REQUIRE(e1.checksum != e2.checksum);
}

// ---------------------------------------------------------------
// Merge
// ---------------------------------------------------------------
TEST_CASE("LogStore - merge adds new entries from peer", "[store][merge]") {
    LogStore local("node-1");
    local.append("local entry");

    LogEntry peer_entry;
    peer_entry.message_id   = "node-2_5";
    peer_entry.sender_id    = "node-2";
    peer_entry.payload      = "peer entry";
    peer_entry.lamport_time = 5;
    peer_entry.epoch        = 0;

    local.merge({peer_entry});

    auto all = local.all();
    REQUIRE(all.size() == 2);
}

TEST_CASE("LogStore - merge deduplicates by message_id", "[store][merge]") {
    LogStore store("node-1");

    LogEntry e;
    e.message_id   = "node-2_1";
    e.sender_id    = "node-2";
    e.payload      = "once";
    e.lamport_time = 1;
    e.epoch        = 0;

    store.merge({e});
    store.merge({e}); // same entry again

    REQUIRE(store.all().size() == 1);
}

TEST_CASE("LogStore - merge orders entries by lamport_time", "[store][merge]") {
    LogStore store("node-1");

    // insert out of order
    LogEntry late, early;
    late.message_id = "node-2_10"; late.sender_id = "node-2";
    late.payload = "late"; late.lamport_time = 10; late.epoch = 0;

    early.message_id = "node-2_2"; early.sender_id = "node-2";
    early.payload = "early"; early.lamport_time = 2; early.epoch = 0;

    store.merge({late, early});

    auto all = store.all();
    REQUIRE(all[0].lamport_time <= all[1].lamport_time);
}

TEST_CASE("LogStore - merge updates local lamport clock", "[store][merge]") {
    LogStore store("node-1"); // clock starts at 0

    LogEntry e;
    e.message_id   = "node-2_99";
    e.sender_id    = "node-2";
    e.payload      = "far ahead";
    e.lamport_time = 99;
    e.epoch        = 0;

    store.merge({e});

    // next local write must have clock > 99
    auto next = store.append("after merge");
    REQUIRE(next.lamport_time > 99);
}

// ---------------------------------------------------------------
// Epoch
// ---------------------------------------------------------------
TEST_CASE("LogStore - set_epoch updates stamped epoch on new entries", "[store]") {
    LogStore store("node-1", 0);
    auto e1 = store.append("before bump");
    REQUIRE(e1.epoch == 0);

    store.set_epoch(2);
    auto e2 = store.append("after bump");
    REQUIRE(e2.epoch == 2);
}

// ---------------------------------------------------------------
// Concurrency
// ---------------------------------------------------------------
TEST_CASE("LogStore - concurrent appends produce unique message_ids", "[store][concurrency]") {
    LogStore store("node-1");
    const int N = 500;

    std::vector<std::string> ids(N);
    std::vector<std::thread> threads;

    for (int i = 0; i < N; i++) {
        threads.emplace_back([&, i]() {
            ids[i] = store.append("payload " + std::to_string(i)).message_id;
        });
    }
    for (auto& t : threads) t.join();

    std::set<std::string> id_set(ids.begin(), ids.end());
    REQUIRE(id_set.size() == N);
}

TEST_CASE("LogStore - concurrent appends none are lost", "[store][concurrency]") {
    LogStore store("node-1");
    const int N = 500;
    std::vector<std::thread> threads;

    for (int i = 0; i < N; i++) {
        threads.emplace_back([&, i]() {
            store.append("entry " + std::to_string(i));
        });
    }
    for (auto& t : threads) t.join();

    REQUIRE(store.all().size() == N);
}