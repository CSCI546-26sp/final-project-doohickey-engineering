# Locality-Based Messaging System — Data Plane

Narayan's data plane component for a distributed messaging system. Implements an append-only gossip log with Lamport clock ordering, CRDT set-union merges, and ACL enforcement via Venkat's control plane.

## Architecture

```
node-1 :50051 ──SyncLogs──► node-2 :50052
     │                           │
     └──── CheckAuthorization ───┘
                  │
             auth :50053
          (Venkat's IntegrationAuth)
```

Three roles across the team:

- **Narayan** — data plane: log storage, Lamport clock, CRDT merge (`DataPlaneGossip`)
- **Kriti** — sync & merge: gossip fanout, anti-entropy, peer discovery
- **Venkat** — control plane: Raft leader election, ACL membership, epoch certificates (`IntegrationAuth`)

## Project Structure

```
locality based messaging system/
├── docker/
│   ├── Dockerfile
│   ├── docker-compose.yml
│   └── scripts/
│       └── run_tests.sh
├── include/
│   ├── log_entry.h          # ChatEntry struct (internal log entry)
│   ├── lamport_clock.h      # Atomic Lamport clock
│   └── log_store.h          # LogStore interface
├── proto/
│   └── locality_messaging.proto   # Shared team contract
├── src/
│   ├── lamport_clock.cpp
│   ├── log_store.cpp        # append, read, merge, get_hash
│   ├── log_server.cpp       # DataPlaneGossip gRPC server
│   ├── log_client.cpp       # SyncLogs client
│   └── fake_auth_server.cpp # Stub for IntegrationAuth (until Venkat's is ready)
├── tests/
│   ├── test_lamport_clock.cpp
│   ├── test_log_store.cpp
│   └── test_grpc_integration.cpp
└── CMakeLists.txt
```

## Prerequisites

- Docker Desktop (Linux containers mode)
- Windows: PowerShell 5+

## Build

```powershell
cd docker
docker compose build
```

First build takes 10–20 minutes (gRPC compiles from source). Subsequent builds are cached and take under a minute.

## Run

**Start the fake auth service first (stands in for Venkat's control plane):**

```powershell
docker compose up auth
```

**Start a data plane node:**

```powershell
# node-1 on port 50051
docker compose up node1

# node-2 on port 50052
docker compose up node2

# node-3 on port 50053
docker compose up node3
```

**Start all three nodes at once:**

```powershell
docker compose up node1 node2 node3 auth
```

**Run the client (pushes a test batch to node-2):**

```powershell
docker compose --profile integration up client
```

**Stop everything:**

```powershell
docker compose down
```

## Run Tests

```powershell
docker compose up tests
```

Results are saved to `build/test-results/` as JUnit XML — compatible with GitHub Actions, Jenkins, and any CI that reads JUnit reports.

Run a specific suite:

```powershell
docker compose run tests bash -c "./build/run_tests [clock]"
docker compose run tests bash -c "./build/run_tests [store]"
docker compose run tests bash -c "./build/run_tests [concurrency]"
```

## Key Design Decisions

**Log entry format.** Each `ChatEntry` carries a `message_id` (`sender_id + "_" + lamport_time`), payload, Lamport timestamp, and epoch. The checksum covers the payload only — metadata fields like `lamport_time` are mutable on receive so including them in the checksum would break on merge.

**Lamport clock.** Implemented with `std::atomic<uint64_t>` for thread safety. On receive, the clock advances to `max(local, received) + 1`. On write, it increments before stamping the entry.

**CRDT merge.** Set-union semantics — dedup by `message_id`, sort by `lamport_time` with `message_id` as tiebreak for determinism across nodes. The local clock advances past any received timestamp.

**Auth enforcement.** Every incoming `SyncLogs` batch is checked per-message against Venkat's `IntegrationAuth.CheckAuthorization`. Unauthorized entries are silently dropped (the batch is not rejected wholesale). If the auth service is unreachable, the node fails closed.

**Thread safety.** `LogStore` uses a `std::mutex` on all public methods. The gRPC server runs handlers concurrently so this is required from Day 1.

## Proto Contract

The shared proto is `proto/locality_messaging.proto`. It defines three services:

| Service | Owner | Purpose |
|---|---|---|
| `DataPlaneGossip` | Narayan | Gossip log sync between nodes |
| `ControlPlaneRaft` | Venkat | Leader election, ACL mutations |
| `IntegrationAuth` | Venkat | Per-message authorization checks |

The `fake_auth_server` binary implements `IntegrationAuth` as a stub that always returns `is_authorized: true`. Replace it with Venkat's real service by changing the `auth` container command in `docker-compose.yml`.

## Known Limitations (Week 1 scope)

- `lamport_time` is `int32` in the proto — will overflow under sustained concurrent writes. Raise with team to change to `int64` before Week 2.
- Log storage is in-memory only. Persistence is out of scope until Week 5.
- `SyncLogsResponse.receiver_lamport_time` currently returns the log hash as a placeholder. Expose the actual clock value before Week 2 gossip integration.

## End-of-Week Sync Agenda

- Agree on `node_id` format with Kriti (currently `"node-1"`, `"node-2"` strings)
- Confirm `lamport_time` field type change (`int32` → `int64`)
- Verify `ChatEntry.message_id` format matches Kriti's gossip fanout expectations
- Share `LogStore::get_hash()` endpoint spec with Kriti for convergence checking
