#!/usr/bin/env bash
set -euo pipefail
LC_ALL=C

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

HOST=${HOST:-161.35.214.248}
PORT=${PORT:-28713}
BENCH_USER=${BENCH_USER:-BenchStatic}
BENCH_PASS=${BENCH_PASS:-BenchPass123}
PREGEN_USER=${PREGEN_USER:-BenchPrepare}
PREGEN_PASS=${PREGEN_PASS:-BenchPass123}
DURATION=${DURATION:-45}
SPEED=${SPEED:-40}
RENDER_DIST=${RENDER_DIST:-32}
WORKERS=${WORKERS:-0}
TIMESTAMP=${TIMESTAMP:-$(date +%Y%m%d_%H%M%S)}
RUN_LABEL=${RUN_LABEL:-fly_vps_full_db_${TIMESTAMP}}
OUTPUT_ROOT=${OUTPUT_ROOT:-"$PROJECT_ROOT/benchvps"}
OUTPUT_DIR="$OUTPUT_ROOT/$RUN_LABEL"
LOG_FILE="$OUTPUT_DIR/client.log"
PROFILE_JSON_FILE="$OUTPUT_DIR/profile.jsonl"
ANALYSIS_FILE="$OUTPUT_DIR/analysis.txt"
REMOTE_ROOT=${REMOTE_ROOT:-/root/VoxPlace}
REMOTE_SERVER_BIN=${REMOTE_SERVER_BIN:-./build/VoxPlaceServer}
REMOTE_PLAYER_DB=${REMOTE_PLAYER_DB:-$REMOTE_ROOT/voxplace_players_classic_gen.sqlite3}
REMOTE_WORLD_DB=${REMOTE_WORLD_DB:-$REMOTE_ROOT/voxplace_world_classic_gen.sqlite3}
REMOTE_OUTPUT_DIR=${REMOTE_OUTPUT_DIR:-$REMOTE_ROOT/benchvps/$RUN_LABEL}
PREPARE_FULL_DB=${PREPARE_FULL_DB:-1}
PREGEN_MAX_INFLIGHT=${PREGEN_MAX_INFLIGHT:-256}
PREGEN_EXTRA_TRAVEL_CHUNKS=${PREGEN_EXTRA_TRAVEL_CHUNKS:-8}
WAIT_IDLE_TIMEOUT=${WAIT_IDLE_TIMEOUT:-300}
WAIT_IDLE_STABLE_WINDOWS=${WAIT_IDLE_STABLE_WINDOWS:-2}
RESTORE_SERVICE=${RESTORE_SERVICE:-1}

ssh_opts=(
	-o StrictHostKeyChecking=accept-new
)

log() {
	printf '[bench-vps] %s\n' "$*"
}

print_usage() {
	cat <<EOF
Usage: $(basename "$0") [--help]

Benchmark fly contre le VPS avec préparation full DB.

Variables d'environnement:
  HOST=$HOST
  PORT=$PORT
  BENCH_USER=$BENCH_USER
  BENCH_PASS=<masqué>
  PREGEN_USER=$PREGEN_USER
  PREGEN_PASS=<masqué>
  DURATION=$DURATION
  SPEED=$SPEED
  RENDER_DIST=$RENDER_DIST
  WORKERS=$WORKERS
  TIMESTAMP=$TIMESTAMP
  RUN_LABEL=$RUN_LABEL
  OUTPUT_ROOT=$OUTPUT_ROOT
  REMOTE_ROOT=$REMOTE_ROOT
  REMOTE_SERVER_BIN=$REMOTE_SERVER_BIN
  REMOTE_PLAYER_DB=$REMOTE_PLAYER_DB
  REMOTE_WORLD_DB=$REMOTE_WORLD_DB
  PREPARE_FULL_DB=$PREPARE_FULL_DB
  PREGEN_MAX_INFLIGHT=$PREGEN_MAX_INFLIGHT
  PREGEN_EXTRA_TRAVEL_CHUNKS=$PREGEN_EXTRA_TRAVEL_CHUNKS
  WAIT_IDLE_TIMEOUT=$WAIT_IDLE_TIMEOUT
  WAIT_IDLE_STABLE_WINDOWS=$WAIT_IDLE_STABLE_WINDOWS
  RESTORE_SERVICE=$RESTORE_SERVICE

Sorties:
  client log   : \$OUTPUT_ROOT/<run>/client.log
  profile json : \$OUTPUT_ROOT/<run>/profile.jsonl
  analyse      : \$OUTPUT_ROOT/<run>/analysis.txt
  server log   : \$OUTPUT_ROOT/<run>/server.log
  pregen log   : \$OUTPUT_ROOT/<run>/pregen.log

Exemple:
  RENDER_DIST=32 SPEED=40 DURATION=45 ./scripts/$(basename "$0")
EOF
}

remote() {
	ssh "${ssh_opts[@]}" "root@$HOST" "$@"
}

if [[ $# -gt 1 ]]; then
	print_usage >&2
	exit 1
fi

if [[ $# -eq 1 ]]; then
	case "$1" in
		-h|--help|help)
			print_usage
			exit 0
			;;
		*)
			echo "Argument inconnu: $1" >&2
			print_usage >&2
			exit 1
			;;
	esac
fi

mkdir -p "$OUTPUT_DIR"

REMOTE_SERVICE_WAS_ACTIVE=0

cleanup() {
	local exit_code=$?
	remote "pkill -INT -f '[V]oxPlaceServer.*--port $PORT' >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
	sleep 1
	remote "pkill -TERM -f '[V]oxPlaceServer.*--port $PORT' >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
	if [[ "$RESTORE_SERVICE" == "1" && "$REMOTE_SERVICE_WAS_ACTIVE" == "1" ]]; then
		remote "systemctl start voxplace.service >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
	fi
	exit "$exit_code"
}

trap cleanup EXIT

ensure_local_binaries() {
	if [[ ! -x "$PROJECT_ROOT/build_release/VoxPlace" ]]; then
		log "binaire client release manquant, build en cours"
		(
			cd "$PROJECT_ROOT"
			make release
		)
	fi

	if [[ ! -x "$PROJECT_ROOT/build_release/VoxPlacePregen" ]]; then
		log "binaire pregen release manquant, build en cours"
		(
			cd "$PROJECT_ROOT"
			make release
		)
	fi
}

remote_service_state() {
	if remote "systemctl is-active --quiet voxplace.service"; then
		REMOTE_SERVICE_WAS_ACTIVE=1
	else
		REMOTE_SERVICE_WAS_ACTIVE=0
	fi
}

stop_remote_servers() {
	log "arrêt du serveur VPS existant"
	remote "systemctl stop voxplace.service >/dev/null 2>&1 || true"
	remote "pkill -INT -f '[V]oxPlaceServer.*--port $PORT' >/dev/null 2>&1 || true"
	sleep 1
	remote "pkill -TERM -f '[V]oxPlaceServer.*--port $PORT' >/dev/null 2>&1 || true"
}

start_remote_server_without_modified_only() {
	log "démarrage d'un serveur temporaire sans --modified-only-world"
	remote "mkdir -p '$REMOTE_OUTPUT_DIR'"
	remote "set -euo pipefail; cd '$REMOTE_ROOT'; \
		nohup stdbuf -oL -eL env VOXPLACE_PROFILE_WORKERS=1 \
		'$REMOTE_SERVER_BIN' --classic-gen --port '$PORT' \
		--db '$REMOTE_PLAYER_DB' \
		--world-db '$REMOTE_WORLD_DB' \
		> '$REMOTE_OUTPUT_DIR/server.log' 2>&1 < /dev/null & echo \$!" >/tmp/voxplace_remote_server_pid.txt

	local server_pid
	server_pid=$(cat /tmp/voxplace_remote_server_pid.txt)
	rm -f /tmp/voxplace_remote_server_pid.txt

	for _ in $(seq 1 30); do
		if remote "grep -q 'WorldServer listening' '$REMOTE_OUTPUT_DIR/server.log'"; then
			log "serveur VPS lancé (pid $server_pid)"
			return
		fi
		sleep 1
	done

	log "échec du démarrage du serveur VPS"
	remote "tail -n 40 '$REMOTE_OUTPUT_DIR/server.log'" || true
	exit 1
}

fetch_bench_user_chunk() {
	local escaped_user
	escaped_user=${BENCH_USER//\'/\'\'}
	local row
	row=$(remote "sqlite3 '$REMOTE_PLAYER_DB' \"select position_x, position_z from player_table where username = '$escaped_user';\"" | tail -n 1)
	if [[ -z "$row" ]]; then
		log "utilisateur de bench introuvable dans $REMOTE_PLAYER_DB: $BENCH_USER"
		exit 1
	fi

	local position_x=${row%%|*}
	local position_z=${row##*|}
	python3 - "$position_x" "$position_z" <<'PY'
import math
import sys
x = float(sys.argv[1])
z = float(sys.argv[2])
print(math.floor(x / 16.0), math.floor(z / 16.0))
PY
}

compute_travel_chunks() {
	python3 - "$SPEED" "$DURATION" "$PREGEN_EXTRA_TRAVEL_CHUNKS" <<'PY'
import math
import sys
speed = float(sys.argv[1])
duration = float(sys.argv[2])
extra = int(sys.argv[3])
travel_world = speed * duration
travel_chunks = math.ceil(travel_world / 16.0)
travel_chunks += extra
if travel_chunks < 1:
	travel_chunks = 1
print(travel_chunks)
PY
}

run_line_pregen() {
	local start_chunk_x=$1
	local start_chunk_z=$2
	local travel_chunks=$3

	log "préparation DB line-x depuis ($start_chunk_x,$start_chunk_z) sur $travel_chunks chunks"
	(
		cd "$PROJECT_ROOT"
		stdbuf -oL -eL "$PROJECT_ROOT/build_release/VoxPlacePregen" \
			"$HOST" "$PORT" "$PREGEN_USER" "$PREGEN_PASS" \
			line-x "$travel_chunks" "$RENDER_DIST" "$start_chunk_x" "$start_chunk_z" "$PREGEN_MAX_INFLIGHT" \
			| tee "$OUTPUT_DIR/pregen.log"
	)
}

wait_until_server_idle() {
	log "attente de fin de génération/sauvegarde avant bench"
	local deadline=$(( $(date +%s) + WAIT_IDLE_TIMEOUT ))
	local stable_count=0

	while [[ $(date +%s) -lt $deadline ]]; do
		local line
		line=$(remote "grep '\\[server-profile\\]' '$REMOTE_OUTPUT_DIR/server.log' | tail -n 1" || true)
		if [[ -z "$line" ]]; then
			sleep 2
			continue
		fi

		local generated ready tasks save_jobs dirty
		generated=$(printf '%s\n' "$line" | sed -n 's/.*generated_fresh_window=\([^ ]*\).*/\1/p')
		ready=$(printf '%s\n' "$line" | sed -n 's/.*ready_now=\([^ ]*\).*/\1/p')
		tasks=$(printf '%s\n' "$line" | sed -n 's/.*tasks_now=\([^ ]*\).*/\1/p')
		save_jobs=$(printf '%s\n' "$line" | sed -n 's/.*save_queue_jobs_now=\([^ ]*\).*/\1/p')
		dirty=$(printf '%s\n' "$line" | sed -n 's/.*dirty_queue_now=\([^ ]*\).*/\1/p')

		if [[ "$generated" == "0" &&
			  "$ready" == "0" &&
			  "$tasks" == "0" &&
			  "$save_jobs" == "0" &&
			  "$dirty" == "0" ]]; then
			stable_count=$((stable_count + 1))
			log "fenêtre idle détectée ($stable_count/$WAIT_IDLE_STABLE_WINDOWS)"
			if [[ $stable_count -ge $WAIT_IDLE_STABLE_WINDOWS ]]; then
				return
			fi
		else
			stable_count=0
			log "serveur encore actif: generated=$generated ready=$ready tasks=$tasks save_jobs=$save_jobs dirty=$dirty"
		fi

		sleep 2
	done

	log "timeout en attendant que le serveur devienne idle"
	exit 1
}

run_client_bench() {
	log "lancement du bench client sur VPS"
	(
		cd "$PROJECT_ROOT"
		stdbuf -oL -eL env \
			VOXPLACE_MESH_WORKERS="$WORKERS" \
			VOXPLACE_BENCH_FLY=1 \
			VOXPLACE_BENCH_FLY_SPEED="$SPEED" \
			VOXPLACE_BENCH_SECONDS="$DURATION" \
			VOXPLACE_PROFILE_WORKERS=1 \
			VOXPLACE_PROFILE_JSON=1 \
			VOXPLACE_PROFILE_JSON_PATH="$PROFILE_JSON_FILE" \
			VOXPLACE_RENDER_DISTANCE="$RENDER_DIST" \
			"$PROJECT_ROOT/build_release/VoxPlace" "$HOST" "$PORT" "$BENCH_USER" "$BENCH_PASS" \
			> "$LOG_FILE" 2>&1 || true
	)
}

copy_remote_server_log() {
	scp "${ssh_opts[@]}" "root@$HOST:$REMOTE_OUTPUT_DIR/server.log" "$OUTPUT_DIR/server.log" >/dev/null 2>&1 || true
}

run_analysis() {
	log "génération de l'analyse à partir du profile.jsonl"
	if [[ -f "$SCRIPT_DIR/analyze_profile_jsonl.py" ]]; then
		python3 "$SCRIPT_DIR/analyze_profile_jsonl.py" "$PROFILE_JSON_FILE" | tee "$ANALYSIS_FILE"
	else
		echo "Analyse ignorée: $SCRIPT_DIR/analyze_profile_jsonl.py introuvable" | tee "$ANALYSIS_FILE"
	fi
}

main() {
	echo "======================================================="
	echo "   BENCHMARK FLY VPS FULL DB"
	echo "======================================================="
	echo "Host            : $HOST:$PORT"
	echo "User            : $BENCH_USER"
	echo "Duration        : ${DURATION}s"
	echo "Speed           : $SPEED units/sec"
	echo "Render Distance : $RENDER_DIST chunks"
	echo "Mesh Workers    : ${WORKERS} (0=Auto)"
	echo "Prepare Full DB : $PREPARE_FULL_DB"
	echo "Output Dir      : $OUTPUT_DIR"
	echo "-------------------------------------------------------"

	ensure_local_binaries
	remote_service_state
	stop_remote_servers
	start_remote_server_without_modified_only

	if [[ "$PREPARE_FULL_DB" == "1" ]]; then
		local chunk_coords
		chunk_coords=$(fetch_bench_user_chunk)
		local start_chunk_x start_chunk_z
		start_chunk_x=$(printf '%s\n' "$chunk_coords" | awk '{print $1}')
		start_chunk_z=$(printf '%s\n' "$chunk_coords" | awk '{print $2}')
		local travel_chunks
		travel_chunks=$(compute_travel_chunks)
		log "joueur de bench récupéré à chunk=($start_chunk_x,$start_chunk_z)"
		log "travel_chunks calculés: $travel_chunks"
		run_line_pregen "$start_chunk_x" "$start_chunk_z" "$travel_chunks"
		wait_until_server_idle
	fi

	run_client_bench
	copy_remote_server_log
	run_analysis

	echo "Terminé."
	echo "Client log      : $LOG_FILE"
	echo "Profile JSONL   : $PROFILE_JSON_FILE"
	echo "Server log      : $OUTPUT_DIR/server.log"
	echo "Analysis        : $ANALYSIS_FILE"
}

main "$@"
