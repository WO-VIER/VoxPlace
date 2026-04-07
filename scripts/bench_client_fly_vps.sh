#!/usr/bin/env bash
set -euo pipefail
LC_ALL=C

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

HOST=${HOST:-161.35.214.248}
PORT=${PORT:-28713}
USER="BenchStatic"
PASS="BenchPass123"
DURATION=${DURATION:-45}
SPEED=${SPEED:-40}
RENDER_DIST=${RENDER_DIST:-32}
WORKERS=${WORKERS:-0} # 0 = Auto
TIMESTAMP=${TIMESTAMP:-$(date +%Y%m%d_%H%M%S)}
RUN_LABEL=${RUN_LABEL:-fly_vps_${TIMESTAMP}}
OUTPUT_ROOT=${OUTPUT_ROOT:-"$PROJECT_ROOT/benchvps"}
OUTPUT_DIR="$OUTPUT_ROOT/$RUN_LABEL"
LOG_FILE="$OUTPUT_DIR/client.log"
PROFILE_JSON_FILE="$OUTPUT_DIR/profile.jsonl"
ANALYSIS_FILE="$OUTPUT_DIR/analysis.txt"

print_usage() {
	cat <<EOF
Usage: $(basename "$0") [--help]

Benchmark client fly contre le VPS.

Variables d'environnement:
  HOST=$HOST
  PORT=$PORT
  DURATION=$DURATION
  SPEED=$SPEED
  RENDER_DIST=$RENDER_DIST
  WORKERS=$WORKERS
  TIMESTAMP=$TIMESTAMP
  RUN_LABEL=$RUN_LABEL
  OUTPUT_ROOT=$OUTPUT_ROOT

Sorties:
  client log   : \$OUTPUT_ROOT/<run>/client.log
  profile json : \$OUTPUT_ROOT/<run>/profile.jsonl
  analyse      : \$OUTPUT_ROOT/<run>/analysis.txt

Exemple:
  HOST=1.2.3.4 DURATION=60 SPEED=50 ./scripts/$(basename "$0")
EOF
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

echo "======================================================="
echo "   BENCHMARK FLY VPS (Test de Charge en Mouvement)"
echo "======================================================="
echo "Host            : $HOST:$PORT"
echo "Duration        : ${DURATION}s"
echo "Speed           : $SPEED units/sec"
echo "Render Distance : $RENDER_DIST chunks"
echo "Mesh Workers    : ${WORKERS} (0=Auto)"
echo "Output Dir      : $OUTPUT_DIR"
echo "-------------------------------------------------------"
echo "Lancement du client en arrière-plan... (Patientez ${DURATION}s)"

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
		"$PROJECT_ROOT/build_release/VoxPlace" "$HOST" "$PORT" "$USER" "$PASS" \
		> "$LOG_FILE" 2>&1 || true
)

echo "Terminé. Analyse des résultats..."
echo "Client log      : $LOG_FILE"
echo "Profile JSONL   : $PROFILE_JSON_FILE"

if [[ -f "$SCRIPT_DIR/analyze_client_bottleneck.py" ]]; then
	python3 "$SCRIPT_DIR/analyze_client_bottleneck.py" "$LOG_FILE" "fly_vps" | tee "$ANALYSIS_FILE"
else
	echo "Analyse ignorée: $SCRIPT_DIR/analyze_client_bottleneck.py introuvable" | tee "$ANALYSIS_FILE"
fi
