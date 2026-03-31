#!/usr/bin/env bash
set -euo pipefail

HOST=${HOST:-161.35.214.248}
PORT=${PORT:-28713}
REMOTE_ROOT=${REMOTE_ROOT:-/root/VoxPlace}
REMOTE_SERVER_BIN=${REMOTE_SERVER_BIN:-./build/VoxPlaceServer}
LOCAL_CLIENT_BIN=${LOCAL_CLIENT_BIN:-./build_release/VoxPlace}
RENDER_DISTANCE=${RENDER_DISTANCE:-32}
MESH_WORKERS=${MESH_WORKERS:-1}
BENCH_SECONDS=${BENCH_SECONDS:-40}
BENCH_SPEED=${BENCH_SPEED:-25}
PROFILE_WORKERS=${PROFILE_WORKERS:-1}
LABEL=${LABEL:-vps_storage_$(date +%Y%m%d_%H%M%S)}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOCAL_DIR="$PROJECT_ROOT/bench/$LABEL"
REMOTE_DIR="$REMOTE_ROOT/bench/$LABEL"

mkdir -p "$LOCAL_DIR"

ssh_opts=(
	-o BatchMode=yes
	-o StrictHostKeyChecking=accept-new
)

log() {
	printf '[bench] %s\n' "$*"
}

remote() {
	ssh "${ssh_opts[@]}" "root@$HOST" "$@"
}

reset_remote_pair() {
	local pair_prefix=$1
	remote "set -euo pipefail; mkdir -p '$REMOTE_DIR'; rm -f \
		'$REMOTE_DIR/${pair_prefix}_players.sqlite3' \
		'$REMOTE_DIR/${pair_prefix}_players.sqlite3-shm' \
		'$REMOTE_DIR/${pair_prefix}_players.sqlite3-wal' \
		'$REMOTE_DIR/${pair_prefix}_world.sqlite3' \
		'$REMOTE_DIR/${pair_prefix}_world.sqlite3-shm' \
		'$REMOTE_DIR/${pair_prefix}_world.sqlite3-wal'"
}

stop_remote_server() {
	remote "set -euo pipefail; pkill -INT VoxPlaceServer >/dev/null 2>&1 || true; \
		for _ in \$(seq 1 20); do \
			if ! pgrep -x VoxPlaceServer >/dev/null 2>&1; then \
				exit 0; \
			fi; \
			sleep 1; \
		done; \
		pkill -TERM VoxPlaceServer >/dev/null 2>&1 || true"
}

start_remote_server() {
	local run_name=$1
	local pair_prefix=$2
	local extra_args=$3

	stop_remote_server
	remote "set -euo pipefail; mkdir -p '$REMOTE_DIR'; cd '$REMOTE_ROOT'; \
		nohup stdbuf -oL -eL env VOXPLACE_PROFILE_WORKERS='$PROFILE_WORKERS' \
		'$REMOTE_SERVER_BIN' --classic-gen --port '$PORT' \
		--db '$REMOTE_DIR/${pair_prefix}_players.sqlite3' \
		--world-db '$REMOTE_DIR/${pair_prefix}_world.sqlite3' \
		$extra_args \
		> '$REMOTE_DIR/${run_name}_server.log' 2>&1 < /dev/null & \
		echo \$!"

	for _ in $(seq 1 30); do
		if remote "grep -q 'WorldServer listening' '$REMOTE_DIR/${run_name}_server.log'"; then
			break
		fi
		sleep 1
	done

	local server_pid
	server_pid=$(remote "pgrep -n VoxPlaceServer")
	remote "set -euo pipefail; \
		nohup pidstat -dru -h -p '$server_pid' 1 120 > '$REMOTE_DIR/${run_name}_pidstat.log' 2>&1 < /dev/null & \
		nohup vmstat 1 120 > '$REMOTE_DIR/${run_name}_vmstat.log' 2>&1 < /dev/null &"
}

collect_remote_run_artifacts() {
	local run_name=$1
	local pair_prefix=$2
	remote "set -euo pipefail; \
		sqlite3 '$REMOTE_DIR/${pair_prefix}_world.sqlite3' 'select count(*) from world_chunk_table;' \
			> '$REMOTE_DIR/${run_name}_world_count.txt'; \
		ls -lh '$REMOTE_DIR/${pair_prefix}_world.sqlite3'* '$REMOTE_DIR/${pair_prefix}_players.sqlite3'* \
			> '$REMOTE_DIR/${run_name}_db_files.txt' 2>&1 || true"
}

run_client() {
	local run_name=$1
	local username=$2
	local password=$3

	(
		cd "$PROJECT_ROOT"
		stdbuf -oL -eL env \
			VOXPLACE_PROFILE_WORKERS="$PROFILE_WORKERS" \
			VOXPLACE_RENDER_DISTANCE="$RENDER_DISTANCE" \
			VOXPLACE_MESH_WORKERS="$MESH_WORKERS" \
			VOXPLACE_BENCH_FLY=1 \
			VOXPLACE_BENCH_FLY_SPEED="$BENCH_SPEED" \
			VOXPLACE_BENCH_SECONDS="$BENCH_SECONDS" \
			"$LOCAL_CLIENT_BIN" "$HOST" "$PORT" "$username" "$password" \
			| tee "$LOCAL_DIR/${run_name}_client.log"
	)
}

run_scenario() {
	local run_name=$1
	local pair_prefix=$2
	local extra_args=$3
	local username=$4
	local password=$5

	log "starting $run_name"
	start_remote_server "$run_name" "$pair_prefix" "$extra_args"
	run_client "$run_name" "$username" "$password"
	stop_remote_server
	collect_remote_run_artifacts "$run_name" "$pair_prefix"
}

download_remote_artifacts() {
	scp "${ssh_opts[@]}" "root@$HOST:$REMOTE_DIR/"* "$LOCAL_DIR/"
}

write_summary() {
	python - "$LOCAL_DIR" <<'PY'
import json
import math
import os
import re
import sys

base = sys.argv[1]
runs = ["mod_cold", "mod_warm", "all_cold", "all_warm"]

client_fields = [
    "streamed_avg",
    "requests_window",
    "requests_per_sec",
    "drops_window",
    "drops_per_sec",
    "receives_window",
    "receives_per_sec",
    "receive_request_ratio",
    "visible_avg",
    "meshed_chunks",
]
server_fields = [
    "loaded_window",
    "generated_fresh_window",
    "integrated_loaded_window",
    "integrated_generated_window",
    "queued_for_send_window",
    "snapshot_count",
    "saved_chunks_window",
    "save_batches_window",
    "dirty_marked_window",
]


def parse_client(run_name: str):
    path = os.path.join(base, f"{run_name}_client.log")
    values = {field: [] for field in client_fields}
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            if "[client-profile]" not in line:
                continue
            for field in client_fields:
                match = re.search(rf"{field}=([^ ]+)", line)
                if match:
                    values[field].append(float(match.group(1)))
    summary = {"windows": len(values["streamed_avg"])}
    for field, samples in values.items():
        summary[f"{field}_mean"] = sum(samples) / len(samples) if samples else 0.0
        if field.endswith("_window"):
            summary[f"{field}_total"] = sum(samples)
    return summary


def parse_server(run_name: str):
    path = os.path.join(base, f"{run_name}_server.log")
    values = {field: 0.0 for field in server_fields}
    windows = 0
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            if "[server-profile]" not in line or "clients=1" not in line:
                continue
            windows += 1
            for field in server_fields:
                match = re.search(rf"{field}=([^ ]+)", line)
                if match:
                    values[field] += float(match.group(1))
    values["windows"] = windows
    return values


def parse_world_count(run_name: str):
    path = os.path.join(base, f"{run_name}_world_count.txt")
    with open(path, "r", encoding="utf-8") as handle:
        raw = handle.read().strip()
    return int(raw) if raw else 0


summary = {}
for run_name in runs:
    summary[run_name] = {
        "client": parse_client(run_name),
        "server": parse_server(run_name),
        "world_count_after": parse_world_count(run_name),
    }


def row(run_name: str):
    client = summary[run_name]["client"]
    server = summary[run_name]["server"]
    return [
        run_name,
        f"{client['receives_window_total']:.0f}",
        f"{client['requests_window_total']:.0f}",
        f"{client['drops_window_total']:.0f}",
        f"{client['receive_request_ratio_mean']:.3f}",
        f"{client['streamed_avg_mean']:.1f}",
        f"{server['loaded_window']:.0f}",
        f"{server['generated_fresh_window']:.0f}",
        f"{server['snapshot_count']:.0f}",
        f"{server['saved_chunks_window']:.0f}",
        f"{summary[run_name]['world_count_after']}",
    ]


lines = []
lines.append(f"# Bench VPS Storage Summary")
lines.append("")
lines.append(f"- local dir: `{base}`")
lines.append("")
lines.append("| Run | Receives total | Requests total | Drops total | Receive/Request mean | Streamed mean | Loaded total | Fresh gen total | Snapshot sent total | Saved chunks total | World count after |")
lines.append("|-----|----------------|----------------|-------------|----------------------|---------------|--------------|-----------------|---------------------|--------------------|------------------|")
for run_name in runs:
    lines.append("| " + " | ".join(row(run_name)) + " |")

lines.append("")
lines.append("## Reading")
lines.append("")
lines.append("- `mod_cold` / `mod_warm`: same DB pair in `--modified-only-world` mode.")
lines.append("- `all_cold` / `all_warm`: same DB pair with full persistence enabled.")
lines.append("- Compare `mod_warm` vs `all_warm` first: that is the real revisit question.")
lines.append("- If `loaded total` stays near zero in `mod_warm`, the revisit does not benefit from world DB.")
lines.append("- If `all_warm` shows high `loaded total` and low `fresh gen total`, full persistence is doing useful work.")

summary_path = os.path.join(base, "summary.md")
with open(summary_path, "w", encoding="utf-8") as handle:
    handle.write("\n".join(lines) + "\n")

json_path = os.path.join(base, "summary.json")
with open(json_path, "w", encoding="utf-8") as handle:
    json.dump(summary, handle, indent=2)

print("\n".join(lines))
PY
}

main() {
	log "using local dir $LOCAL_DIR"
	log "using remote dir $REMOTE_DIR"
	log "speed=$BENCH_SPEED seconds=$BENCH_SECONDS render_distance=$RENDER_DISTANCE"

	if [[ ! -x "$PROJECT_ROOT/${LOCAL_CLIENT_BIN#./}" ]]; then
		printf 'missing local client binary: %s\n' "$LOCAL_CLIENT_BIN" >&2
		exit 1
	fi

	remote "mkdir -p '$REMOTE_DIR'"

	reset_remote_pair "mod"
	run_scenario "mod_cold" "mod" "--modified-only-world" "BenchModCold" "BenchPass123"
	run_scenario "mod_warm" "mod" "--modified-only-world" "BenchModWarm" "BenchPass123"

	reset_remote_pair "all"
	run_scenario "all_cold" "all" "" "BenchAllCold" "BenchPass123"
	run_scenario "all_warm" "all" "" "BenchAllWarm" "BenchPass123"

	download_remote_artifacts
	write_summary
	log "summary written to $LOCAL_DIR/summary.md"
}

main "$@"
