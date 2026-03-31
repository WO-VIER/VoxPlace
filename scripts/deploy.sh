#!/bin/bash
set -e

echo "[deploy] $(date) - Starting deployment..."

cd /root/VoxPlace
git pull origin main

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TRACY=OFF -DENABLE_GL_DEBUG=OFF
make VoxPlaceServer -j1

strip VoxPlaceServer

# Arrêter l'ancien serveur proprement
if pgrep VoxPlaceServer > /dev/null; then
    echo '[deploy] Stopping old server...'
    kill -SIGINT $(pgrep VoxPlaceServer)
    sleep 3
fi

# Relancer avec priorité max et 2 workers
nice -n -20 nohup env VOXPLACE_SERVER_WORKERS=2 \
    /root/VoxPlace/build/VoxPlaceServer --classic-gen --port 28713 \
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
