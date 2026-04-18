# Project Setup & Build Environment

This project implements a Raft-based consensus cluster for a distributed Access Control List (ACL). Due to the complexity of C++ dependencies like gRPC and Protocol Buffers, follow these instructions precisely.

---

## ⚠️ OS Requirements: Use Linux
**Do not attempt to build this project natively on Windows.** Compiling gRPC from source on Windows is notoriously slow (upwards of 60 minutes) and often fails due to character path limits. 

**Recommended Environment:** * Ubuntu Multipass VM
* Windows Subsystem for Linux (WSL2)
* Native Linux (Ubuntu 22.04+)

---

## 1. Environment Preparation
Before the first build, you must install the core Protocol Buffer development libraries and ensure the test script is executable.

Run these commands in your Linux terminal:

```bash
# Update and install required protobuf development headers
sudo apt update
sudo apt install -y libprotobuf-dev libprotoc-dev protobuf-compiler

# Grant execution permissions to the local test script
chmod +x test_raft_local.sh
```

---

## 2. The Build Process

### The "Build Cache" Rule
CMake aggressively caches project state. If you encounter obscure errors or version mismatches, you **must** nuke the build directory to start fresh:

```bash
rm -rf build
```

### ⏳ The "Initial Build Tax"
This project uses `FetchContent` to ensure all teammates use the exact same version of gRPC (v1.62.0). 
* **First Run:** The very first time you build, CMake will download and compile gRPC from source. This takes **30 minutes** on a standard VM. **Do not kill the process.**
* **Subsequent Runs:** Once cached, rebuilding your code changes will take **less than 10 seconds**.

```bash
cmake --build build/ -j4
```
---

## 3. Running the Tests
To compile the project, launch 3 Raft nodes, and verify the consensus logic (Leader Election, Log Replication, and User Revocation), run:

```bash
./test_raft_local.sh
```

### Verified Output
A successful run will conclude with:
`✓ ALL TESTS PASSED`

Logs for individual nodes (`auth-1`, `auth-2`, `auth-3`) can be found in the `build/` directory for debugging.

---

## 4. Troubleshooting
If CMake fails to find gRPC or Protobuf:
1. Ensure you ran the `apt install` commands in Step 1.
2. Run `rm -rf build` to clear the cache.
3. Check if any "ghost" configurations are interfering by running:
   `rm -rf ~/.local/lib/cmake/grpc`
