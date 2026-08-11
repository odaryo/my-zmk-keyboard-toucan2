#!/bin/bash
# Build ZMK firmware and flash it to a XIAO BLE half in one step.
#
# Usage:
#   ./scripts/flash.sh              # build + flash left (most common)
#   ./scripts/flash.sh right        # build + flash right
#   ./scripts/flash.sh reset        # build + flash settings_reset
#   ./scripts/flash.sh left --no-build   # skip build, just flash
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-left}"
VOLUME="/Volumes/XIAO-SENSE"

case "$TARGET" in
    left)  UF2="$REPO/firmware/toucan_left.uf2" ;;
    right) UF2="$REPO/firmware/toucan_right.uf2" ;;
    reset) UF2="$REPO/firmware/toucan_settings_reset.uf2" ;;
    *) echo "usage: $0 [left|right|reset] [--no-build]"; exit 1 ;;
esac

if [[ "${2:-}" != "--no-build" ]]; then
    echo "=== Building firmware (docker) ==="
    docker run --rm \
        -v zmk-workspace:/workspace \
        -v "$REPO":/workspace/repo \
        docker.io/zmkfirmware/zmk-build-arm:stable \
        bash /workspace/repo/scripts/build-in-container.sh
fi

[[ -f "$UF2" ]] || { echo "firmware not found: $UF2"; exit 1; }

echo ""
echo "=== Ready to flash: $(basename "$UF2") ==="
echo "対象のキーボード (${TARGET}) をUSB接続し、リセットボタンをダブルクリックしてください。"
echo "(${VOLUME} がマウントされるのを待っています... Ctrl+C で中断)"

until [[ -d "$VOLUME" ]]; do sleep 1; done

echo "Detected $VOLUME — copying..."
cp -X "$UF2" "$VOLUME/" 2>/dev/null || cp "$UF2" "$VOLUME/"
echo "Done! キーボードは自動的に再起動します。"
