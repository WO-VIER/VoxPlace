#!/usr/bin/env bash
set -euo pipefail
LC_ALL=C

HOST=${HOST:-161.35.214.248}
PORT=${PORT:-28713}
USER="BenchGenSpawn"
PASS="BenchPass123"
DURATION=${DURATION:-30}
RENDER_DIST=${RENDER_DIST:-32}
WORKERS=${WORKERS:-0} # 0 = Auto

LOG_FILE="bench_spawn_results.log"

echo "======================================================="
echo "   BENCHMARK SPAWN (Test de Charge au Démarrage)"
echo "======================================================="
echo "Host            : $HOST:$PORT"
echo "Duration        : ${DURATION}s"
echo "Render Distance : $RENDER_DIST chunks"
echo "Mesh Workers    : ${WORKERS} (0=Auto)"
echo "-------------------------------------------------------"
echo "Lancement du client statique... (Patientez ${DURATION}s)"

stdbuf -oL -eL env \
    VOXPLACE_MESH_WORKERS=$WORKERS \
    VOXPLACE_BENCH_FLY=1 \
    VOXPLACE_BENCH_FLY_SPEED=0.000001 \
    VOXPLACE_BENCH_SECONDS=$DURATION \
    VOXPLACE_PROFILE_WORKERS=1 \
    VOXPLACE_RENDER_DISTANCE=$RENDER_DIST \
    ./build_release/VoxPlace "$HOST" "$PORT" "$USER" "$PASS" > "$LOG_FILE" 2>&1 || true

echo "Terminé. Analyse des résultats..."

python3 scripts/analyze_client_bottleneck.py "$LOG_FILE" "spawn"
