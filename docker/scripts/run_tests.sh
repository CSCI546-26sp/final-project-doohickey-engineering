#!/usr/bin/env bash
set -euo pipefail

BINARY="./build/run_tests"
RESULTS_DIR="./build/test-results"
mkdir -p "$RESULTS_DIR"

echo "========================================"
echo " Distributed Log — Test Suite"
echo "========================================"

run_suite() {
    local tag="$1"
    local label="$2"
    echo ""
    echo "--- $label ---"
    "$BINARY" "[$tag]" \
        --reporter junit \
        --out "$RESULTS_DIR/$tag.xml" \
        --reporter console \
        || true   # don't abort on failure — collect all results first
}

run_suite "clock"       "Lamport clock"
run_suite "store"       "LogStore unit tests"
run_suite "merge"       "Merge + CRDT"
run_suite "concurrency" "Concurrency"
run_suite "grpc"        "gRPC integration"

echo ""
echo "========================================"
echo " Summary"
echo "========================================"

# print pass/fail counts from junit xml
for f in "$RESULTS_DIR"/*.xml; do
    tag=$(basename "$f" .xml)
    tests=$(grep -o 'tests="[0-9]*"' "$f" | grep -o '[0-9]*' || echo 0)
    failures=$(grep -o 'failures="[0-9]*"' "$f" | grep -o '[0-9]*' || echo 0)
    if [ "$failures" -eq 0 ]; then
        echo "  ✓  $tag ($tests tests)"
    else
        echo "  ✗  $tag ($failures/$tests failed)"
    fi
done

echo ""

# exit 1 if any failures
total_failures=$(grep -o 'failures="[0-9]*"' "$RESULTS_DIR"/*.xml \
    | grep -o '[0-9]*' | awk '{s+=$1} END {print s}')

if [ "$total_failures" -gt 0 ]; then
    echo "FAILED — $total_failures failure(s)"
    exit 1
else
    echo "ALL TESTS PASSED"
    exit 0
fi