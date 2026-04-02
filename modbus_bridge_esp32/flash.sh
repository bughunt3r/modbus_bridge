#!/usr/bin/env bash
# flash.sh — Flash modbus_bridge_esp32 sur ESP32-POE-ISO via /dev/ttyUSB0
# Usage : ./flash.sh [port]   (défaut /dev/ttyUSB0)
#
# Via micro-USB (CH340T intégré, auto-reset) : brancher le micro-USB, lancer le script.
# Via adaptateur UART externe (sans RTS/DTR) :
#   1. IO0 → GND, presser RST1, lancer le script, retirer fil IO0-GND dès "Connecting..."

set -e

PORT="${1:-/dev/ttyUSB0}"
ESPTOOL="$HOME/.arduino15/packages/esp32/tools/esptool_py/5.1.0/esptool"
BUILD="/tmp/arduino_build_990073"
BOOT_APP="$HOME/.arduino15/packages/esp32/hardware/esp32/3.3.7/tools/partitions/boot_app0.bin"

echo "=== Flash ESP32-POE-ISO sur $PORT ==="

"$ESPTOOL" \
    --chip esp32 \
    --port "$PORT" \
    --baud 115200 \
    --before default_reset \
    --after hard-reset \
    write-flash \
    --flash-mode qio \
    --flash-freq 80m \
    --flash-size 4MB \
    0x1000  "$BUILD/modbus_bridge_esp32.ino.bootloader.bin" \
    0x8000  "$BUILD/modbus_bridge_esp32.ino.partitions.bin" \
    0xe000  "$BOOT_APP" \
    0x10000 "$BUILD/modbus_bridge_esp32.ino.bin"

echo "=== Flash terminé. Presser RST1 sur la carte. ==="
