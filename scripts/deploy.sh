#!/bin/bash
set -e

echo "[deploy] $(date) - Starting deployment..."

# Arrêter l'ancien serveur proprement (SIGINT = shutdown clean)
OLD_PID=$(pgrep VoxPlaceServer 2>/dev/null || true)
if [ -n "$OLD_PID" ]; then
    echo "[deploy] Stopping server (PID $OLD_PID) with SIGINT..."
    kill -SIGINT "$OLD_PID"

    # Attendre le code de retour du processus (max 30s)
    WAITED=0
    while kill -0 "$OLD_PID" 2>/dev/null; do
        if [ "$WAITED" -ge 30 ]; then
            echo "[deploy] Server still alive after 30s, sending SIGKILL..."
            kill -9 "$OLD_PID" 2>/dev/null || true
            sleep 1
            break
        fi
        sleep 1
        WAITED=$((WAITED + 1))
    done

    # Récupérer le code de retour si possible
    wait "$OLD_PID" 2>/dev/null && echo "[deploy] Server exited cleanly." \
                                 || echo "[deploy] Server exited (code: $?)."
fi

cd /root/VoxPlace
git pull origin main

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TRACY=OFF -DENABLE_GL_DEBUG=OFF
make VoxPlaceServer -j1

strip VoxPlaceServer

# Relancer avec priorité max
# L'heuristique automatique choisit le bon nombre de workers
nice -n -20 nohup /root/VoxPlace/build/VoxPlaceServer --classic-gen --port 28713 \
    > /root/voxplace_server.log 2>&1 &
sleep 2

if pgrep VoxPlaceServer > /dev/null; then
    echo '[deploy] Server restarted successfully!'
    tail -5 /root/voxplace_server.log
else
    echo '[deploy] ERROR: Server failed to start!'
    cat /root/voxplace_server.log
    exit 1
fi
