# Locality-Based Messaging System

Distributed Systems Project — Raft + CRDT Data Plane

---

## Overview

This project implements a distributed messaging system that combines strong consistency (Raft control plane), eventual consistency (CRDT data plane), secure message enforcement (ACL + epochs), and fault tolerance with recovery.

The system demonstrates real-world distributed systems principles including consensus via Raft, log replication, failure recovery, concurrency, and performance benchmarking.

---

## Architecture

```
                +--------------------+
                |      Clients       |
                +--------+-----------+
                         | RPC
                         v
         +-------------------------------+
         |   Control Plane (Raft)        |
         |   - Leader election           |
         |   - Ordered log               |
         |   - ACL management            |
         +--------+----------------------+
                  | Apply
                  v
         +-------------------------------+
         |   Data Plane (LogStore)       |
         |   - Append-only log           |
         |   - Lamport ordering          |
         |   - CRDT merge                |
         +--------+----------------------+
                  |
                  v
         +-------------------------------+
         |   SyncLogs (Gossip)           |
         |   - Anti-entropy              |
         |   - Recovery                  |
         +-------------------------------+
```

---

## Components

### Data Plane 

- `LogStore` — append-only log with mutex-protected operations
- Lamport clock ordering with atomic increment and update
- CRDT merge using set-union semantics
- Deterministic log hashing via CRC32

### Control Plane (Raft)

- Leader election with randomised timeouts
- Log replication across nodes
- Commit index tracking
- State machine execution for ACL mutations

### Authorization (IntegrationAuth)

- Per-message validation on every gossip batch
- Epoch-based revocation — revoked members cannot inject messages
- Secure enforcement of ACL rules at merge time

---

## Project Structure

```
locality_based_messaging/
├── include/
│   ├── log_store.h
│   ├── lamport_clock.h
│   └── log_entry.h
├── src/
│   ├── log_store.cpp
│   ├── lamport_clock.cpp
│   ├── log_server.cpp
│   ├── log_client.cpp
│   ├── fake_auth_server.cpp
│   ├── partition_sim.cpp
│   ├── latency_bench.cpp
│   ├── crash_sim.cpp
│   └── correctness_eval.cpp
├── tests/
│   ├── test_log_store.cpp
│   ├── test_lamport_clock.cpp
│   ├── test_grpc_integration.cpp
│   └── test_full_system.cpp
├── app/
│   ├── latency.cpp
│   └── tput.cpp
├── bench/
│   ├── latency_plot.py
│   └── lat-tput.py
├── proto/
│   └── locality_messaging.proto
└── CMakeLists.txt
```

---

## Build Instructions

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

---

## Running the System

Start manually:

```bash
./build/fake_auth_server 127.0.0.1:50060
./build/raft_server node-1 127.0.0.1:50053 node-2=127.0.0.1:50054 node-3=127.0.0.1:50055
./build/raft_server node-2 127.0.0.1:50054 node-1=127.0.0.1:50053 node-3=127.0.0.1:50055
./build/raft_server node-3 127.0.0.1:50055 node-1=127.0.0.1:50053 node-2=127.0.0.1:50054
./build/log_server node-1 0.0.0.0:50051 127.0.0.1:50053
```

Run automated tests:

```bash
ctest --test-dir build -V
```

---

## Testing

### Unit Tests

- Lamport clock correctness — tick, update, concurrent uniqueness
- LogStore append, read, merge, hash, epoch behaviour

### Integration Tests

- gRPC SyncLogs handler
- Per-message auth enforcement
- Data plane correctness under concurrent writes

### Full System Test

- Raft leader election
- ACL add and revoke via Raft
- Crash recovery with re-election
- Auth enforcement after revocation

---

## Performance Evaluation

### Latency Benchmark

```bash
./build/latency_bench
```

Measures 1000 sequential operations across 5 runs, reporting average, min, and max latency.

### Throughput Benchmark

```bash
./build/tput <MaxClients> <PutRatio>
```

Measures throughput in ops/sec, latency percentiles (P50, P90, P99), and scalability across concurrent clients.

### Graph Generation

```bash
python3 bench/latency_plot.py
python3 bench/lat-tput.py
```

### Sample Results

| Metric      | Value                          |
|-------------|-------------------------------|
| Avg Latency | ~0.6 ms                       |
| P99 Latency | ~8 ms                         |
| Throughput  | ~1600 ops/sec (single client) |

---

## Key Design Decisions

**Lamport Clock.** Each write increments the clock before stamping the entry. On receive, the clock advances to `max(local, received) + 1`, preserving causal ordering without wall-clock synchronisation.

**CRDT Merge.** Set-union semantics with dedup by `message_id`. Entries are sorted by `lamport_time` with `message_id` as tiebreak, ensuring all nodes converge to identical log state regardless of message arrival order.

**Raft Integration.** Raft handles ordering and consensus for ACL mutations. LogStore handles storage and CRDT merge. The two planes are decoupled via the `IntegrationAuth` RPC boundary, allowing independent failure isolation.

**Fault Tolerance.** Crash-stop recovery via gossip resync. Leader re-election continues with a quorum of two nodes after any single node crash. Anti-entropy via `SyncLogs` ensures no entries are permanently lost.

---

## Limitations

- No persistent storage — log is in-memory only
- `lamport_time` uses `int32` in the proto — will overflow under sustained concurrent load
- Network partitions are simulated, not real
- Leader is a throughput bottleneck for write-heavy workloads

---

## Results Summary

- Deterministic convergence verified across 3 nodes
- Correct ACL enforcement before and after revocation
- Stable latency across repeated benchmark runs
- Throughput scales then saturates at expected client concurrency levels

---

## Conclusion

This system demonstrates strong consistency via Raft, eventual consistency via CRDT, robust recovery and fault tolerance, and realistic distributed system performance behaviour measured under both sequential and concurrent workloads.

---

## Future Work

- Persistent storage with disk-backed log
- True network partition simulation
- Dynamic cluster membership changes
- Load balancing reads across replicas
