#!/bin/bash
# compile.sh — Compile et (optionnellement) flash modbus_bridge_esp32
#
# Utilisation :
#   ./compile.sh            # compile seulement
#   ./compile.sh flash      # compile + flash (détecte /dev/ttyUSB0 ou /dev/ttyACM0)
#   ./compile.sh flash /dev/ttyUSB1   # compile + flash sur port spécifié
#
# Prérequis :
#   arduino-cli installé et dans le PATH
#   Board package "esp32 by Espressif" >= 3.0.0 installé :
#     arduino-cli core install esp32:esp32

set -e

SKETCH="modbus_bridge_esp32/modbus_bridge_esp32.ino"
FQBN="esp32:esp32:esp32-poe-iso"   # Olimex ESP32-POE-ISO
BUILD_DIR="/tmp/modbus_bridge_build"

# Détection auto du port série si non fourni
detect_port() {
    for p in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/ttyACM1; do
        [ -e "$p" ] && echo "$p" && return
    done
    echo ""
}

# Vérification arduino-cli
if ! command -v arduino-cli &>/dev/null; then
    echo "[ERREUR] arduino-cli introuvable."
    echo "  Installation : https://arduino.github.io/arduino-cli/latest/installation/"
    echo "  Puis : arduino-cli core install esp32:esp32"
    exit 1
fi

echo "=== Compilation ==="
echo "  Sketch : $SKETCH"
echo "  Board  : $FQBN"
mkdir -p "$BUILD_DIR"

arduino-cli compile \
    --fqbn "$FQBN" \
    --build-path "$BUILD_DIR" \
    "$SKETCH"

echo ""
echo "[OK] Compilation réussie."
echo "     Binaire : $BUILD_DIR/modbus_bridge_esp32.ino.bin"

# Flash si demandé
if [ "${1}" = "flash" ]; then
    PORT="${2}"
    if [ -z "$PORT" ]; then
        PORT=$(detect_port)
    fi
    if [ -z "$PORT" ]; then
        echo "[ERREUR] Aucun port série détecté. Précisez le port : $0 flash /dev/ttyUSBx"
        exit 1
    fi
    echo ""
    echo "=== Flash ==="
    echo "  Port   : $PORT"
    arduino-cli upload \
        --fqbn "$FQBN" \
        --port "$PORT" \
        --input-dir "$BUILD_DIR" \
        "$SKETCH"
    echo ""
    echo "[OK] Flash terminé."
fi
