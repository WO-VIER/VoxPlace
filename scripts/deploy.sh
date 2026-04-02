#!/bin/bash
set -e

echo "[deploy] $(date) - Starting deployment..."

# Arrêter le serveur proprement via systemd
if systemctl is-active --quiet voxplace.service; then
    echo '[deploy] Stopping voxplace.service...'
    systemctl stop voxplace.service
    echo '[deploy] Service stopped.'
fi

# Tuer tout éventuel serveur lancé hors systemd ou encore vivant.
# On matche le nom du process, pas seulement le chemin complet, pour couvrir:
# - ./build/VoxPlaceServer
# - /root/VoxPlace/build/VoxPlaceServer
# - build_release/VoxPlaceServer
if pgrep -a -x VoxPlaceServer >/dev/null 2>&1; then
    echo '[deploy] Found existing VoxPlaceServer process(es):'
    pgrep -a -x VoxPlaceServer || true
    echo '[deploy] Killing existing VoxPlaceServer process(es)...'
    pkill -x VoxPlaceServer || true
    sleep 2
fi

# Libérer le port 28713 de force (au cas où un process fantôme l'occuperait encore)
if command -v fuser >/dev/null 2>&1; then
    echo '[deploy] Force-closing any process on port 28713 (UDP/TCP)...'
    fuser -k 28713/udp 28713/tcp 2>/dev/null || true
    sleep 1
fi

# Nettoyer l'état failed éventuel du service avant redémarrage.
systemctl reset-failed voxplace.service || true

cd /root/VoxPlace
git pull origin main

# Installer l'unité systemd versionnée dans le repo.
install -m 0644 scripts/voxplace.service /etc/systemd/system/voxplace.service
systemctl daemon-reload

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TRACY=OFF -DENABLE_GL_DEBUG=OFF
cmake --build . --target VoxPlaceServer -j2

strip VoxPlaceServer

# Relancer via systemd
systemctl start voxplace.service

# Attendre un peu plus que RestartSec=5 pour laisser systemd stabiliser le service.
WAIT_SECONDS=20
for ((i=1; i<=WAIT_SECONDS; i++)); do
    if systemctl is-active --quiet voxplace.service; then
        echo '[deploy] Server restarted successfully!'
        journalctl -u voxplace.service -n 10 --no-pager
        exit 0
    fi
    sleep 1
done

echo '[deploy] ERROR: Server failed to start or stabilize in time!'
journalctl -u voxplace.service -n 30 --no-pager
exit 1
