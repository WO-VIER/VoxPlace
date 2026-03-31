#!/bin/bash
set -e

echo "[deploy] $(date) - Starting deployment..."

# Arrêter le serveur proprement via systemd
if systemctl is-active --quiet voxplace.service; then
    echo '[deploy] Stopping voxplace.service...'
    systemctl stop voxplace.service
    echo '[deploy] Service stopped.'
fi

cd /root/VoxPlace
git pull origin main

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TRACY=OFF -DENABLE_GL_DEBUG=OFF
make VoxPlaceServer -j2

strip VoxPlaceServer

# Relancer via systemd
systemctl start voxplace.service
sleep 2

if systemctl is-active --quiet voxplace.service; then
    echo '[deploy] Server restarted successfully!'
    journalctl -u voxplace.service -n 5 --no-pager
else
    echo '[deploy] ERROR: Server failed to start!'
    journalctl -u voxplace.service -n 20 --no-pager
    exit 1
fi
