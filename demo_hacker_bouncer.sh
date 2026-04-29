#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
PROTO_DIR="$ROOT_DIR/proto"
PROTO_FILE="$PROTO_DIR/locality_messaging.proto"
STATE_DIR="${HACKER_BOUNCER_STATE_DIR:-/tmp/hacker-bouncer-demo}"
LOG_DIR="$STATE_DIR/logs"
BACKUP_DIR="$STATE_DIR/backup"
PID_FILE="$STATE_DIR/pids.env"
SEED_FILE="$STATE_DIR/seeded"

RAFT_1_ADDR="127.0.0.1:50053"
RAFT_2_ADDR="127.0.0.1:50054"
RAFT_3_ADDR="127.0.0.1:50055"
AUTH_ADDR="127.0.0.1:50060"

MALLORY_DP_ADDR="127.0.0.1:60051"
BOB_DP_ADDR="127.0.0.1:60052"
ALICE_DP_ADDR="127.0.0.1:60053"

mkdir -p "$STATE_DIR" "$LOG_DIR" "$BACKUP_DIR"

require_binary() {
  local binary_path="$1"
  local friendly_name="$2"
  if [[ ! -x "$binary_path" ]]; then
    echo "Missing $friendly_name at $binary_path"
    exit 1
  fi
}

require_binary "$BUILD_DIR/fake_auth_server" "fake_auth_server"
require_binary "$BUILD_DIR/raft_server" "raft_server"
require_binary "$BUILD_DIR/log_server" "log_server"

if ! command -v grpcurl >/dev/null 2>&1; then
  echo "grpcurl is required for this demo script but was not found on PATH."
  echo "Install grpcurl, then rerun: bash demo_hacker_bouncer.sh setup"
  exit 1
fi

grpc() {
  grpcurl -plaintext -import-path "$PROTO_DIR" -proto "$PROTO_FILE" "$@"
}

seed_raft_state() {
  local node_id="$1"
  local state_file="$ROOT_DIR/raft_state_${node_id}.txt"
  if [[ -f "$state_file" && ! -f "$BACKUP_DIR/raft_state_${node_id}.txt" ]]; then
    cp "$state_file" "$BACKUP_DIR/raft_state_${node_id}.txt"
  fi
  cat > "$state_file" <<'EOF'
0
-
5
Alice
Bob
EOF
}

restore_raft_state() {
  for node_id in node-1 node-2 node-3; do
    if [[ -f "$BACKUP_DIR/raft_state_${node_id}.txt" ]]; then
      mv "$BACKUP_DIR/raft_state_${node_id}.txt" "$ROOT_DIR/raft_state_${node_id}.txt"
    else
      rm -f "$ROOT_DIR/raft_state_${node_id}.txt"
    fi
  done
}

is_pid_alive() {
  local pid="$1"
  [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null
}

load_pids() {
  if [[ -f "$PID_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$PID_FILE"
  fi
}

save_pids() {
  cat > "$PID_FILE" <<EOF
AUTH_PID=${AUTH_PID:-}
RAFT1_PID=${RAFT1_PID:-}
RAFT2_PID=${RAFT2_PID:-}
RAFT3_PID=${RAFT3_PID:-}
MALLORY_PID=${MALLORY_PID:-}
BOB_PID=${BOB_PID:-}
ALICE_PID=${ALICE_PID:-}
LEADER_ADDR=${LEADER_ADDR:-}
EOF
}

ensure_setup() {
  if [[ ! -f "$SEED_FILE" ]]; then
    echo "Run: ./demo_hacker_bouncer.sh setup"
    exit 1
  fi
  load_pids
  for pid_name in AUTH_PID RAFT1_PID RAFT2_PID RAFT3_PID MALLORY_PID BOB_PID ALICE_PID; do
    if ! is_pid_alive "${!pid_name:-}"; then
      echo "Demo is not running. Re-run: ./demo_hacker_bouncer.sh setup"
      exit 1
    fi
  done
}

setup_demo() {
  pkill -f "$BUILD_DIR/fake_auth_server" 2>/dev/null || true
  pkill -f "$BUILD_DIR/raft_server" 2>/dev/null || true
  pkill -f "$BUILD_DIR/log_server" 2>/dev/null || true

  : > "$LOG_DIR/auth.log"
  : > "$LOG_DIR/raft-1.log"
  : > "$LOG_DIR/raft-2.log"
  : > "$LOG_DIR/raft-3.log"
  : > "$LOG_DIR/mallory.log"
  : > "$LOG_DIR/bob.log"
  : > "$LOG_DIR/alice.log"

  seed_raft_state node-1
  seed_raft_state node-2
  seed_raft_state node-3

  echo "Starting auth backend and cluster..."
  "$BUILD_DIR/fake_auth_server" "$AUTH_ADDR" >"$LOG_DIR/auth.log" 2>&1 &
  AUTH_PID=$!

  "$BUILD_DIR/raft_server" node-1 "$RAFT_1_ADDR" node-2="$RAFT_2_ADDR" node-3="$RAFT_3_ADDR" >"$LOG_DIR/raft-1.log" 2>&1 &
  RAFT1_PID=$!
  "$BUILD_DIR/raft_server" node-2 "$RAFT_2_ADDR" node-1="$RAFT_1_ADDR" node-3="$RAFT_3_ADDR" >"$LOG_DIR/raft-2.log" 2>&1 &
  RAFT2_PID=$!
  "$BUILD_DIR/raft_server" node-3 "$RAFT_3_ADDR" node-1="$RAFT_1_ADDR" node-2="$RAFT_2_ADDR" >"$LOG_DIR/raft-3.log" 2>&1 &
  RAFT3_PID=$!

  "$BUILD_DIR/log_server" Mallory "$MALLORY_DP_ADDR" "$AUTH_ADDR" >"$LOG_DIR/mallory.log" 2>&1 &
  MALLORY_PID=$!
  "$BUILD_DIR/log_server" Bob "$BOB_DP_ADDR" "$AUTH_ADDR" >"$LOG_DIR/bob.log" 2>&1 &
  BOB_PID=$!
  "$BUILD_DIR/log_server" Alice "$ALICE_DP_ADDR" "$AUTH_ADDR" >"$LOG_DIR/alice.log" 2>&1 &
  ALICE_PID=$!

  LEADER_ADDR=""
  echo "WAITing for services..."
  sleep 3

  : > "$SEED_FILE"
  save_pids

  echo "Setup complete. Logs are in: $LOG_DIR"
#   echo "Run the step commands below in order:"
#   echo "  ./demo_hacker_bouncer.sh t0_add_mallory"
#   echo "  ./demo_hacker_bouncer.sh t1_send_msg"
#   echo "  ./demo_hacker_bouncer.sh t2_revoke_mallory"
#   echo "  ./demo_hacker_bouncer.sh t3_attack"
#   echo "  ./demo_hacker_bouncer.sh t4_bouncer_fires"
#   echo "  ./demo_hacker_bouncer.sh logs"
#   echo "  ./demo_hacker_bouncer.sh stop"
}

find_leader_for_add() {
  local resp
  for raft_addr in "$RAFT_1_ADDR" "$RAFT_2_ADDR" "$RAFT_3_ADDR"; do
    resp="$(grpc -d '{"user_id":"Mallory"}' "$raft_addr" locality_messaging.ControlPlaneRaft/AddUser || true)"
    if grep -q '"success": *true' <<<"$resp"; then
      # echo leader and raw response separated by pipe; caller will parse and persist LEADER_ADDR
      echo "$raft_addr|$resp"
      return 0
    fi
  done
  return 1
}

find_leader_for_revoke() {
  local resp
  for raft_addr in "$RAFT_1_ADDR" "$RAFT_2_ADDR" "$RAFT_3_ADDR"; do
    resp="$(grpc -d '{"user_id":"Mallory"}' "$raft_addr" locality_messaging.ControlPlaneRaft/RevokeUser || true)"
    if grep -q '"success": *true' <<<"$resp"; then
      echo "$raft_addr|$resp"
      return 0
    fi
  done
  return 1
}

mallory_payload='{"sender":{"node_id":"Mallory","address":"127.0.0.1:60051"},"logs":[{"message_id":"mallory-5-1","sender_id":"Mallory","payload":"msg(epoch=5)","lamport_time":5,"epoch":5}],"summary":{"node_id":"Mallory","entry_count":1,"max_lamport_time":5,"log_hash":0}}'

extract_log_hash() {
  local resp="$1"
  # extract numeric logHash from grpcurl JSON output
  echo "$resp" | sed -n 's/.*"logHash"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' || true
}

step_t0() {
  ensure_setup
  echo
  echo "> T0 - Mallory Joins"
  echo "> Admin runs AddUser(Mallory). Raft commits the ACL change and keeps the system at epoch 5."
  local add_resp
  local res
  res="$(find_leader_for_add)"
  if [[ -z "$res" ]]; then
    echo "Failed to locate a Raft leader for AddUser(Mallory)."
    exit 1
  fi
  # parse leader and response
  LEADER_ADDR="${res%%|*}"
  add_resp="${res#*|}"
  save_pids
  echo "$add_resp"
  sleep 1
  grpc -d '{}' "$LEADER_ADDR" locality_messaging.ControlPlaneRaft/ListUsers
  echo
  echo "--- Raft log highlights for T0 ---"
  grep -hE '\[ACL\] Added user:|Won Election! Becoming Leader' "$LOG_DIR"/raft-*.log || true
}

step_t1() {
  ensure_setup
  echo
  echo "> T1 - Normal Traffic"
  echo "> Mallory writes msg(epoch=5). Bob and Alice accept the SyncLogs payload and their logs converge."
  # send payload (summary.log_hash left as 0; servers compute the authoritative value)
  resp_bob="$(grpc -d "$mallory_payload" "$BOB_DP_ADDR" locality_messaging.DataPlaneGossip/SyncLogs || true)"
  echo "Bob SyncLogs response: $resp_bob"
  bob_log_hash=$(extract_log_hash "$resp_bob")
  if [[ -n "$bob_log_hash" ]]; then
    echo "Bob reported summary.log_hash: $bob_log_hash"
  fi

  resp_alice="$(grpc -d "$mallory_payload" "$ALICE_DP_ADDR" locality_messaging.DataPlaneGossip/SyncLogs || true)"
  echo "Alice SyncLogs response: $resp_alice"
  alice_log_hash=$(extract_log_hash "$resp_alice")
  if [[ -n "$alice_log_hash" ]]; then
    echo "Alice reported summary.log_hash: $alice_log_hash"
  fi
  # explicit epoch visibility for demo
  echo "Mallory epoch: 5"
  echo "--- Recent Bob/Alice log lines (showing any epoch info) ---"
  tail -n 10 "$LOG_DIR"/bob.log || true
  tail -n 10 "$LOG_DIR"/alice.log || true
  echo
  echo "--- Data-plane log highlights for T1 ---"
  grep -hE 'SyncLogs request received from Mallory|Incoming summary:|SyncLogs from Mallory accepted=1 rejected=0|Rejected message from Mallory' "$LOG_DIR"/bob.log "$LOG_DIR"/alice.log || true
}

step_t2() {
  ensure_setup
  echo
  echo "> T2 - Compromise Detected"
  echo "> Admin runs RevokeUser(Mallory). The Raft ACL removes Mallory and bumps the epoch to 6."
  local revoke_resp
  local res
  res="$(find_leader_for_revoke)"
  if [[ -z "$res" ]]; then
    echo "Failed to locate a Raft leader for RevokeUser(Mallory)."
    exit 1
  fi
  LEADER_ADDR="${res%%|*}"
  revoke_resp="${res#*|}"
  save_pids
  echo "Revoke RPC response: $revoke_resp"
  # print ListUsers (includes authoritative current_epoch)
  list_resp="$(grpc -d '{}' "$LEADER_ADDR" locality_messaging.ControlPlaneRaft/ListUsers || true)"
  echo "ListUsers response: $list_resp"
  sleep 1
  echo
  echo "--- Raft log highlights for T2 ---"
  # show lines that mention the revoke so the demo can point to the authoritative epoch
  grep -h 'Revoked user: Mallory' "$LOG_DIR"/raft-*.log || true
}

step_t3() {
  ensure_setup
  echo
  echo "> T3 - Attacker Injects"
  echo "> Mallory replays a stale epoch-5 payload and sends SyncLogs directly to Bob."
  # send the same payload; server will compute and report its summary.log_hash
  resp_bob_attack="$(grpc -d "$mallory_payload" "$BOB_DP_ADDR" locality_messaging.DataPlaneGossip/SyncLogs || true)"
  echo "Bob response to replay: $resp_bob_attack"
  attack_log_hash=$(extract_log_hash "$resp_bob_attack")
  if [[ -n "$attack_log_hash" ]]; then
    echo "Bob reported summary.log_hash for replay: $attack_log_hash"
  fi
  echo "Mallory epoch: 5"
  echo "--- Recent Bob log lines (showing rejection epoch if any) ---"
  tail -n 20 "$LOG_DIR"/bob.log || true
}

step_t4() {
  ensure_setup
  echo
  echo "> T4 - Bouncer Fires"
  echo "> Bob rejects the stale replay. The entry is dropped and Bob's log remains unchanged."
  echo
  echo "--- Bob data-plane rejection lines ---"
  grep -hE 'Rejected message from Mallory \(epoch=5\)|SyncLogs from Mallory accepted=0 rejected=1' "$LOG_DIR/bob.log" || true
  echo
  echo "--- Final log summary ---"
  final_resp="$(grpc -d '{"sender":{"node_id":"Observer","address":"127.0.0.1:60999"},"summary":{"node_id":"Observer","entry_count":0,"max_lamport_time":0,"log_hash":0}}' "$BOB_DP_ADDR" locality_messaging.DataPlaneGossip/SyncLogs || true)"
  echo "Observer final SyncLogs response: $final_resp"
}

show_logs() {
  echo "Logs directory: $LOG_DIR"
  for file in "$LOG_DIR"/*.log; do
    echo "--- $(basename "$file") ---"
    tail -n 20 "$file"
  done
}

stop_demo() {
  load_pids
  for pid_name in MALLORY_PID ALICE_PID BOB_PID RAFT3_PID RAFT2_PID RAFT1_PID AUTH_PID; do
    if is_pid_alive "${!pid_name:-}"; then
      kill "${!pid_name}" 2>/dev/null || true
    fi
  done
  wait 2>/dev/null || true
  restore_raft_state
  rm -f "$PID_FILE" "$SEED_FILE"
  echo "Stopped demo. Logs were in: $LOG_DIR"
}

status_demo() {
  if [[ -f "$PID_FILE" ]]; then
    load_pids
    echo "Logs: $LOG_DIR"
    echo "Auth PID: ${AUTH_PID:-missing}"
    echo "Raft PIDs: ${RAFT1_PID:-missing} ${RAFT2_PID:-missing} ${RAFT3_PID:-missing}"
    echo "Data PIDs: ${MALLORY_PID:-missing} ${BOB_PID:-missing} ${ALICE_PID:-missing}"
    echo "Leader addr: ${LEADER_ADDR:-unset}"
  else
    echo "Demo is not running. Use: ./demo_hacker_bouncer.sh setup"
  fi
}

usage() {
  cat <<EOF
Usage:
  ./demo_hacker_bouncer.sh setup
  ./demo_hacker_bouncer.sh t0_add_mallory
  ./demo_hacker_bouncer.sh t1_send_msg
  ./demo_hacker_bouncer.sh t2_revoke_mallory
  ./demo_hacker_bouncer.sh t3_attack
  ./demo_hacker_bouncer.sh t4_bouncer_fires
  ./demo_hacker_bouncer.sh logs
  ./demo_hacker_bouncer.sh status
  ./demo_hacker_bouncer.sh stop
EOF
}

cmd="${1:-}"
case "$cmd" in
  setup)
    setup_demo
    ;;
  t0_add_mallory)
    step_t0
    ;;
  t1_send_msg)
    step_t1
    ;;
  t2_revoke_mallory)
    step_t2
    ;;
  t3_attack)
    step_t3
    ;;
  t4_bouncer_fires)
    step_t4
    ;;
  logs)
    show_logs
    ;;
  status)
    status_demo
    ;;
  stop)
    stop_demo
    ;;
  "")
    usage
    ;;
  *)
    echo "Unknown command: $cmd"
    usage
    exit 1
    ;;
esac
