#!/bin/bash
set -e

echo "[deploy] $(date) - Starting deployment..."

# Arrêter le serveur proprement via systemd
if systemctl is-active --quiet voxplace.service; then
    echo '[deploy] Stopping voxplace.service...'
    systemctl stop voxplace.service
    echo '[deploy] Service stopped.'
fi

# Tuer un éventuel serveur lancé à la main hors systemd.
STRAY_MATCH="/root/VoxPlace/build/VoxPlaceServer"
if pgrep -af "$STRAY_MATCH" >/dev/null 2>&1; then
    echo '[deploy] Found stray VoxPlaceServer process outside systemd:'
    pgrep -af "$STRAY_MATCH" || true
    echo '[deploy] Killing stray VoxPlaceServer process(es)...'
    pkill -f "$STRAY_MATCH" || true
    sleep 1
fi

# Nettoyer l'état failed éventuel du service avant redémarrage.
systemctl reset-failed voxplace.service || true

cd /root/VoxPlace
git pull origin main

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TRACY=OFF -DENABLE_GL_DEBUG=OFF
cmake --build . --target VoxPlaceServer -j2

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
