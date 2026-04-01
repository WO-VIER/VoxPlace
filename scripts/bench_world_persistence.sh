#!/usr/bin/env bash
set -euo pipefail

HOST=${HOST:-161.35.214.248}
PORT=${PORT:-28713}
REMOTE_ROOT=${REMOTE_ROOT:-/root/VoxPlace}
REMOTE_SERVER_BIN=${REMOTE_SERVER_BIN:-./build/VoxPlaceServer}
LOCAL_CLIENT_BIN=${LOCAL_CLIENT_BIN:-./build_release/VoxPlace}
LOCAL_STORAGE_BUILD_DIR=${LOCAL_STORAGE_BUILD_DIR:-build_release}
REMOTE_STORAGE_BUILD_DIR=${REMOTE_STORAGE_BUILD_DIR:-build_release}
LOCAL_STORAGE_BUILD_TYPE=${LOCAL_STORAGE_BUILD_TYPE:-Release}
REMOTE_STORAGE_BUILD_TYPE=${REMOTE_STORAGE_BUILD_TYPE:-Release}
STORAGE_BENCH_BIN_NAME=${STORAGE_BENCH_BIN_NAME:-VoxPlaceWorldStorageBench}
STORAGE_CHUNK_COUNT=${STORAGE_CHUNK_COUNT:-4096}
RENDER_DISTANCE=${RENDER_DISTANCE:-32}
MESH_WORKERS=${MESH_WORKERS:-1}
BENCH_SECONDS=${BENCH_SECONDS:-40}
BENCH_SPEED=${BENCH_SPEED:-25}
PROFILE_WORKERS=${PROFILE_WORKERS:-1}
RUN_STORAGE_LOCAL=${RUN_STORAGE_LOCAL:-1}
RUN_STORAGE_REMOTE=${RUN_STORAGE_REMOTE:-1}
RUN_E2E=${RUN_E2E:-1}
FORCE_E2E=${FORCE_E2E:-0}
LABEL=${LABEL:-world_persistence_$(date +%Y%m%d_%H%M%S)}

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

local_storage_bin_path() {
	printf '%s/%s/%s\n' "$PROJECT_ROOT" "$LOCAL_STORAGE_BUILD_DIR" "$STORAGE_BENCH_BIN_NAME"
}

remote_storage_bin_path() {
	printf '%s/%s/%s\n' "$REMOTE_ROOT" "$REMOTE_STORAGE_BUILD_DIR" "$STORAGE_BENCH_BIN_NAME"
}

has_local_gui() {
	[[ -n "${DISPLAY-}" || -n "${WAYLAND_DISPLAY-}" ]]
}

ensure_local_build_dir() {
	cmake -S "$PROJECT_ROOT" -B "$PROJECT_ROOT/$LOCAL_STORAGE_BUILD_DIR" \
		-DCMAKE_BUILD_TYPE="$LOCAL_STORAGE_BUILD_TYPE" >/dev/null
}

ensure_remote_build_dir() {
	remote "set -euo pipefail; \
		cmake -S '$REMOTE_ROOT' -B '$REMOTE_ROOT/$REMOTE_STORAGE_BUILD_DIR' \
			-DCMAKE_BUILD_TYPE='$REMOTE_STORAGE_BUILD_TYPE' >/dev/null"
}

sync_remote_storage_bench_files() {
	local files=(
		"CMakeLists.txt"
		"src/server/world_storage_bench.cpp"
	)

	for rel_path in "${files[@]}"; do
		remote "mkdir -p '$REMOTE_ROOT/$(dirname "$rel_path")'"
		scp "${ssh_opts[@]}" "$PROJECT_ROOT/$rel_path" "root@$HOST:$REMOTE_ROOT/$rel_path" >/dev/null
	done
}

ensure_local_storage_bench() {
	local bin
	bin=$(local_storage_bin_path)
	if [[ -x "$bin" ]]; then
		return
	fi

	log "building local $STORAGE_BENCH_BIN_NAME"
	ensure_local_build_dir
	cmake --build "$PROJECT_ROOT/$LOCAL_STORAGE_BUILD_DIR" \
		--target "$STORAGE_BENCH_BIN_NAME" -j "${JOBS:-8}"
}

ensure_remote_storage_bench() {
	local bin
	bin=$(remote_storage_bin_path)
	if remote "test -x '$bin'"; then
		return
	fi

	log "building remote $STORAGE_BENCH_BIN_NAME"
	sync_remote_storage_bench_files
	ensure_remote_build_dir
	remote "set -euo pipefail; cd '$REMOTE_ROOT'; \
		cmake --build '$REMOTE_STORAGE_BUILD_DIR' \
			--target '$STORAGE_BENCH_BIN_NAME' -j 2"
}

ensure_local_client() {
	local client_path="$PROJECT_ROOT/${LOCAL_CLIENT_BIN#./}"
	if [[ -x "$client_path" ]]; then
		return
	fi

	log "building local client binary"
	if [[ ! -f "$PROJECT_ROOT/build_release/CMakeCache.txt" ]]; then
		cmake -S "$PROJECT_ROOT" -B "$PROJECT_ROOT/build_release" \
			-DCMAKE_BUILD_TYPE=Release
	fi
	cmake --build "$PROJECT_ROOT/build_release" --target VoxPlace -j "${JOBS:-8}"
}

ensure_remote_server() {
	if remote "test -x '$REMOTE_ROOT/${REMOTE_SERVER_BIN#./}'"; then
		return
	fi

	log "building remote server binary"
	remote "set -euo pipefail; \
		if [[ ! -f '$REMOTE_ROOT/build/CMakeCache.txt' ]]; then \
			cmake -S '$REMOTE_ROOT' -B '$REMOTE_ROOT/build' -DCMAKE_BUILD_TYPE=RelWithDebInfo; \
		fi; \
		cd '$REMOTE_ROOT'; \
		cmake --build build --target VoxPlaceServer -j 2"
}

write_local_machine_info() {
	local output_path=$1
	python3 - "$output_path" <<'PY'
import os
import re
import subprocess
import sys

path = sys.argv[1]
cpu = subprocess.check_output(["bash", "-lc", "lscpu"], text=True)
model = "Local machine"
for line in cpu.splitlines():
    if line.startswith("Model name:"):
        model = line.split(":", 1)[1].strip()
        break

mem_kib = 0
with open("/proc/meminfo", "r", encoding="utf-8") as handle:
    for line in handle:
        if line.startswith("MemTotal:"):
            mem_kib = int(re.findall(r"\d+", line)[0])
            break

with open(path, "w", encoding="utf-8") as handle:
    handle.write(f"label={model}\n")
    handle.write(f"cpu_model={model}\n")
    handle.write(f"nproc={os.cpu_count() or 0}\n")
    handle.write(f"memory_gib={mem_kib / 1024 / 1024:.1f}\n")
PY
}

write_remote_machine_info() {
	local output_path=$1
	remote "python3 - <<'PY'
import os
import re

model = 'Remote machine'
with open('/proc/cpuinfo', 'r', encoding='utf-8') as handle:
    for line in handle:
        if line.startswith('model name'):
            model = line.split(':', 1)[1].strip()
            break

mem_kib = 0
with open('/proc/meminfo', 'r', encoding='utf-8') as handle:
    for line in handle:
        if line.startswith('MemTotal:'):
            mem_kib = int(re.findall(r'\\d+', line)[0])
            break

nproc = os.cpu_count() or 0
label = f'VPS {nproc} vCPU / ~{mem_kib / 1024 / 1024:.1f} GiB'
print(f'label={label}')
print(f'cpu_model={model}')
print(f'nproc={nproc}')
print(f'memory_gib={mem_kib / 1024 / 1024:.1f}')
PY" > "$output_path"
}

run_local_storage_bench() {
	ensure_local_storage_bench
	write_local_machine_info "$LOCAL_DIR/storage_local_machine.txt"
	log "running local storage micro-bench ($STORAGE_CHUNK_COUNT chunks)"
	(
		cd "$PROJECT_ROOT"
		"$(local_storage_bin_path)" "$STORAGE_CHUNK_COUNT" \
			| tee "$LOCAL_DIR/storage_local.log"
	)
}

run_remote_storage_bench() {
	ensure_remote_storage_bench
	write_remote_machine_info "$LOCAL_DIR/storage_remote_machine.txt"
	log "running remote storage micro-bench on $HOST ($STORAGE_CHUNK_COUNT chunks)"
	remote "set -euo pipefail; cd '$REMOTE_ROOT'; \
		'$(remote_storage_bin_path)' '$STORAGE_CHUNK_COUNT'" \
		| tee "$LOCAL_DIR/storage_remote.log"
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
	remote "set -euo pipefail; \
		pids=\$(pgrep -f '[V]oxPlaceServer.*--port $PORT' || true); \
		if [[ -z \"\$pids\" ]]; then \
			exit 0; \
		fi; \
		kill -INT \$pids >/dev/null 2>&1 || true; \
		for _ in \$(seq 1 20); do \
			pids=\$(pgrep -f '[V]oxPlaceServer.*--port $PORT' || true); \
			if [[ -z \"\$pids\" ]]; then \
				exit 0; \
			fi; \
			sleep 1; \
		done; \
		kill -TERM \$pids >/dev/null 2>&1 || true"
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
	server_pid=$(remote "pgrep -n -f '[V]oxPlaceServer.*--port $PORT'")
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
	scp "${ssh_opts[@]}" "root@$HOST:$REMOTE_DIR/"* "$LOCAL_DIR/" >/dev/null
}

write_summary() {
	python3 - "$LOCAL_DIR" <<'PY'
import json
import os
import re
import sys

base = sys.argv[1]
prepare_run_name = "prepare_db"
bench_run_name = "bench_modified_only"


def parse_key_value_file(path):
    data = {}
    with open(path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or "=" not in line:
                continue
            key, value = line.split("=", 1)
            data[key] = value
    return data


def parse_storage_log(path):
    metrics = {}
    with open(path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            timer_match = re.match(r"([^:\[]+): total=([0-9.]+) ms, per_chunk=([0-9.]+) ms", line)
            if timer_match:
                metrics[timer_match.group(1)] = float(timer_match.group(3))
    return {"metrics": metrics}


def parse_client(run_name):
    path = os.path.join(base, f"{run_name}_client.log")
    if not os.path.exists(path):
        return None

    client_fields = [
        "streamed_avg",
        "requests_window",
        "drops_window",
        "receives_window",
    ]
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


def parse_server(run_name):
    path = os.path.join(base, f"{run_name}_server.log")
    if not os.path.exists(path):
        return None

    server_fields = [
        "loaded_window",
        "generated_fresh_window",
        "snapshot_count",
        "saved_chunks_window",
        "save_batches_window",
    ]
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


def parse_world_count(run_name):
    path = os.path.join(base, f"{run_name}_world_count.txt")
    if not os.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8") as handle:
        raw = handle.read().strip()
    return int(raw) if raw else 0


summary = {"storage": {}, "prepare": {}, "e2e": {}}
lines = []
lines.append("# Resume Bench Persistance Monde")
lines.append("")
lines.append(f"- dossier local: `{base}`")
lines.append("")

storage_entries = []
for name in ("local", "remote"):
    log_path = os.path.join(base, f"storage_{name}.log")
    machine_path = os.path.join(base, f"storage_{name}_machine.txt")
    if not os.path.exists(log_path) or not os.path.exists(machine_path):
        continue
    machine = parse_key_value_file(machine_path)
    parsed = parse_storage_log(log_path)
    summary["storage"][name] = {
        "machine": machine,
        "parsed": parsed,
    }
    storage_entries.append((name, machine, parsed))

if storage_entries:
    lines.append("## Micro-Bench Stockage")
    lines.append("")
    lines.append("| Machine | Miss DB | Generation | Chargement DB | Ecriture batch DB |")
    lines.append("|---------|---------|------------|---------------|-------------------|")
    for _, machine, parsed in storage_entries:
        metrics = parsed["metrics"]
        lines.append(
            "| "
            + " | ".join(
                [
                    machine.get("label", "machine"),
                    f"{metrics.get('sqlite_load_miss_zstd_sections', 0.0):.4f} ms",
                    f"{metrics.get('generate', 0.0):.4f} ms",
                    f"{metrics.get('sqlite_load_zstd_sections', 0.0):.4f} ms",
                    f"{metrics.get('sqlite_save_zstd_sections_batch_txn', 0.0):.4f} ms",
                ]
            )
            + " |"
        )

    lines.append("")

prepare_world_count = parse_world_count(prepare_run_name)
bench_client = parse_client(bench_run_name)
bench_server = parse_server(bench_run_name)
bench_world_count = parse_world_count(bench_run_name)

if prepare_world_count is not None:
    summary["prepare"]["world_count_after"] = prepare_world_count

if bench_client is not None and bench_server is not None and bench_world_count is not None:
    loaded = bench_server["loaded_window"]
    generated = bench_server["generated_fresh_window"]
    total_resolved = loaded + generated
    loaded_share = loaded / total_resolved if total_resolved else 0.0
    db_complete_for_path = generated == 0.0
    summary["e2e"][bench_run_name] = {
        "client": bench_client,
        "server": bench_server,
        "world_count_after": bench_world_count,
        "loaded_share": loaded_share,
        "db_complete_for_path": db_complete_for_path,
    }

    lines.append("## Bench Parcours")
    lines.append("")
    lines.append("| Scenario | Recus | Requetes | Drops | Chunks streames moyen | Charges depuis DB | Regeneres | Couverture DB du trajet | Chunks en DB apres preparation | Chunks en DB apres bench |")
    lines.append("|----------|-------|-----------|-------|------------------------|-------------------|-----------|-------------------------|--------------------------------|--------------------------|")
    lines.append(
        "| "
        + " | ".join(
            [
                "Parcours bench en modified-only",
                f"{bench_client['receives_window_total']:.0f}",
                f"{bench_client['requests_window_total']:.0f}",
                f"{bench_client['drops_window_total']:.0f}",
                f"{bench_client['streamed_avg_mean']:.1f}",
                f"{bench_server['loaded_window']:.0f}",
                f"{bench_server['generated_fresh_window']:.0f}",
                "oui" if db_complete_for_path else "non",
                f"{prepare_world_count if prepare_world_count is not None else 0}",
                f"{bench_world_count}",
            ]
        )
        + " |"
    )

    lines.append("")
    lines.append("## Lecture")
    lines.append("")
    lines.append("- La preparation remplit la base avec les chunks du trajet de bench.")
    lines.append("- Le bench mesure ensuite uniquement `--modified-only-world`.")
    lines.append("- Si `Regeneres = 0`, alors la base couvrait bien tout le trajet demande.")
    lines.append("- Si `Regeneres > 0`, le bench a revele qu'il manquait encore des chunks en base.")
else:
    lines.append("## Bench Parcours")
    lines.append("")
    lines.append("- skipped")

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
	local should_run_e2e=$RUN_E2E

	log "using local dir $LOCAL_DIR"
	log "using remote dir $REMOTE_DIR"
	log "storage_chunk_count=$STORAGE_CHUNK_COUNT speed=$BENCH_SPEED seconds=$BENCH_SECONDS render_distance=$RENDER_DISTANCE"

	if (( should_run_e2e == 1 )) && (( FORCE_E2E != 1 )) && ! has_local_gui; then
		log "no local GUI detected, skipping end-to-end client run"
		should_run_e2e=0
	fi

	remote "mkdir -p '$REMOTE_DIR'"

	trap 'stop_remote_server >/dev/null 2>&1 || true' EXIT

	if (( RUN_STORAGE_LOCAL == 1 )); then
		run_local_storage_bench
	fi

	if (( RUN_STORAGE_REMOTE == 1 )); then
		run_remote_storage_bench
	fi

	if (( should_run_e2e == 1 )); then
		ensure_local_client
		ensure_remote_server

		reset_remote_pair "bench"
		run_scenario "prepare_db" "bench" "" "BenchPrepare" "BenchPass123"
		run_scenario "bench_modified_only" "bench" "--modified-only-world" "BenchRun" "BenchPass123"

		download_remote_artifacts
	fi

	write_summary
	log "summary written to $LOCAL_DIR/summary.md"
}

main "$@"
