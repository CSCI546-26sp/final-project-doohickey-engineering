#!/bin/bash

# Raft Consensus Local Testing Script
# Starts 3 Raft nodes locally and runs the test client

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BINARY_DIR="$BUILD_DIR"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  RAFT CONSENSUS LOCAL TEST SETUP${NC}"
echo -e "${BLUE}========================================${NC}\n"

# ===== STEP 1: BUILD =====
echo -e "${YELLOW}[STEP 1] Building project...${NC}"
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"
if [ ! -f "Makefile" ]; then
    echo "Running CMake..."
    cmake ..
fi

echo "Compiling..."
cmake --build . --config Release -j4

if [ ! -f "$BINARY_DIR/raft_server" ]; then
    echo -e "${RED}✗ Failed to build raft_server${NC}"
    exit 1
fi

if [ ! -f "$BINARY_DIR/test_raft_rpcs" ]; then
    echo -e "${RED}✗ Failed to build test_raft_rpcs${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Build successful${NC}\n"

# ===== STEP 2: CLEANUP OLD PROCESSES =====
echo -e "${YELLOW}[STEP 2] Cleaning up old processes...${NC}"

for port in 50053 50054 50055; do
    # Kill any process listening on these ports
    if command -v lsof &> /dev/null; then
        lsof -ti:$port | xargs kill -9 2>/dev/null || true
    elif command -v fuser &> /dev/null; then
        fuser -k $port/tcp 2>/dev/null || true
    fi
done

echo -e "${GREEN}✓ Cleanup complete${NC}\n"

# ===== STEP 3: START RAFT NODES =====
echo -e "${YELLOW}[STEP 3] Starting 3 Raft nodes...${NC}\n"

NODE_PIDS=()

# Node 1: auth-1 on port 50053
echo -e "${BLUE}Starting auth-1 on localhost:50053...${NC}"
"$BINARY_DIR/raft_server" "auth-1" "0.0.0.0:50053" \
    "auth-2=localhost:50054" "auth-3=localhost:50055" \
    > "$BUILD_DIR/auth-1.log" 2>&1 &
PID=$!
NODE_PIDS+=($PID)
echo "  PID: $PID"

# Node 2: auth-2 on port 50054
echo -e "${BLUE}Starting auth-2 on localhost:50054...${NC}"
"$BINARY_DIR/raft_server" "auth-2" "0.0.0.0:50054" \
    "auth-1=localhost:50053" "auth-3=localhost:50055" \
    > "$BUILD_DIR/auth-2.log" 2>&1 &
PID=$!
NODE_PIDS+=($PID)
echo "  PID: $PID"

# Node 3: auth-3 on port 50055
echo -e "${BLUE}Starting auth-3 on localhost:50055...${NC}"
"$BINARY_DIR/raft_server" "auth-3" "0.0.0.0:50055" \
    "auth-1=localhost:50053" "auth-2=localhost:50054" \
    > "$BUILD_DIR/auth-3.log" 2>&1 &
PID=$!
NODE_PIDS+=($PID)
echo "  PID: $PID"

echo ""

# ===== STEP 4: WAIT FOR NODES TO START =====
echo -e "${YELLOW}[STEP 4] Waiting for nodes to stabilize (5 seconds)...${NC}"

# Give nodes time to start and form a cluster
for i in {5..1}; do
    echo -n "  Waiting... $i"
    sleep 1
    echo -e "\r${NC}"
done

echo -e "${GREEN}✓ Nodes running${NC}\n"

# ===== STEP 5: CHECK NODES ARE RESPONSIVE =====
echo -e "${YELLOW}[STEP 5] Verifying nodes are responsive...${NC}"

all_ready=false
max_attempts=10
attempt=0

while [ $attempt -lt $max_attempts ]; do
    if nc -z localhost 50053 2>/dev/null && \
       nc -z localhost 50054 2>/dev/null && \
       nc -z localhost 50055 2>/dev/null; then
        all_ready=true
        break
    fi
    echo "  Attempt $((attempt+1))/$max_attempts..."
    sleep 1
    attempt=$((attempt+1))
done

if [ "$all_ready" = true ]; then
    echo -e "${GREEN}✓ All nodes are responsive${NC}\n"
else
    echo -e "${YELLOW}⚠ Nodes may not be fully ready, proceeding anyway...${NC}\n"
fi

# ===== STEP 6: RUN TEST CLIENT =====
echo -e "${YELLOW}[STEP 6] Running test client...${NC}\n"

"$BINARY_DIR/test_raft_rpcs" localhost:50053 localhost:50054 localhost:50055

TEST_EXIT=$?

echo ""

# ===== STEP 7: CLEANUP =====
echo -e "${YELLOW}[STEP 7] Cleaning up...${NC}"

for pid in "${NODE_PIDS[@]}"; do
    echo "  Stopping process $pid..."
    kill $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true
done

echo -e "${GREEN}✓ Cleanup complete${NC}\n"

# ===== SUMMARY =====
echo -e "${BLUE}========================================${NC}"
if [ $TEST_EXIT -eq 0 ]; then
    echo -e "${GREEN}✓ ALL TESTS PASSED${NC}"
else
    echo -e "${RED}✗ TESTS FAILED (exit code: $TEST_EXIT)${NC}"
fi
echo -e "${BLUE}========================================${NC}\n"

# ===== LOGS AVAILABLE =====
echo -e "${YELLOW}Node logs available at:${NC}"
echo "  - $BUILD_DIR/auth-1.log"
echo "  - $BUILD_DIR/auth-2.log"
echo "  - $BUILD_DIR/auth-3.log"
echo ""

exit $TEST_EXIT
