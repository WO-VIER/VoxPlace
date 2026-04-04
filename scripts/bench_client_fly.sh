#!/usr/bin/env bash
set -euo pipefail
LC_ALL=C

HOST=${HOST:-161.35.214.248}
PORT=${PORT:-28713}
USER="BenchStatic"
PASS="BenchPass123"
DURATION=${DURATION:-45}
SPEED=${SPEED:-40}
RENDER_DIST=${RENDER_DIST:-32}
WORKERS=${WORKERS:-0} # 0 = Auto

LOG_FILE="bench_fly_results.log"

echo "======================================================="
echo "   BENCHMARK FLY (Test de Charge en Mouvement)"
echo "======================================================="
echo "Host            : $HOST:$PORT"
echo "Duration        : ${DURATION}s"
echo "Speed           : $SPEED units/sec"
echo "Render Distance : $RENDER_DIST chunks"
echo "Mesh Workers    : ${WORKERS} (0=Auto)"
echo "-------------------------------------------------------"
echo "Lancement du client en arrière-plan... (Patientez ${DURATION}s)"

# On lance avec stdbuf pour avoir les logs en temps réel
stdbuf -oL -eL env \
    VOXPLACE_MESH_WORKERS=$WORKERS \
    VOXPLACE_BENCH_FLY=1 \
    VOXPLACE_BENCH_FLY_SPEED=$SPEED \
    VOXPLACE_BENCH_SECONDS=$DURATION \
    VOXPLACE_PROFILE_WORKERS=1 \
    VOXPLACE_RENDER_DISTANCE=$RENDER_DIST \
    ./build_release/VoxPlace "$HOST" "$PORT" "$USER" "$PASS" > "$LOG_FILE" 2>&1 || true

echo "Terminé. Analyse des résultats..."

python3 scripts/analyze_client_bottleneck.py "$LOG_FILE" "fly"
