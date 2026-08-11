#!/bin/bash
# 診断用: 右手側を USB ロギング付きでビルドする。
#
# エッジスクロール (toucan.dtsi の edge_scroll) のキャリブレーション用。
# 書き込んで USB で繋ぐと、指を置いた瞬間の絶対座標が
#   <dbg> tps43: Absolute position: x=..., y=...
# として出るので、パッドの端をタップして edge-min / edge-max を決める。
#
#   ./scripts/build-right-logging.sh          # ビルド (docker)
#   cp firmware/toucan_right_logging.uf2 /Volumes/XIAO-SENSE/
#   screen /dev/tty.usbmodem<番号> 115200     # 終了は Ctrl-A K y
#
# 注意:
# - ドライバのログレベルは CONFIG_INPUT_LOG_LEVEL で決まる (ZMK 側の設定ではない)。
# - CONFIG_ZMK_USB_LOGGING=y を conf に書くだけでは CDC ACM の DT ノードが無く
#   ビルドが通らないため、-S zmk-usb-logging スニペットを使う。
# - これは診断専用。通常は scripts/build-in-container.sh でビルドすること。
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"

if [ "${1:-}" != "--in-container" ]; then
    echo "=== Building right half with USB logging (docker) ==="
    exec docker run --rm \
        -v zmk-workspace:/workspace \
        -v "$REPO":/workspace/repo \
        docker.io/zmkfirmware/zmk-build-arm:stable \
        bash /workspace/repo/scripts/build-right-logging.sh --in-container
fi

cd /workspace
REPO=/workspace/repo

mkdir -p /workspace/config
cp -a "$REPO/config/." /workspace/config/

if [ ! -d .west ]; then
    west init -l /workspace/config
fi
west update --fetch-opt=--filter=blob:none
west zephyr-export

west build -s zmk/app -d build/right_logging -b seeeduino_xiao_ble -S zmk-usb-logging -- \
    -DSHIELD="toucan_right rgbled_adapter" \
    -DZMK_CONFIG=/workspace/config \
    -DZMK_EXTRA_MODULES="$REPO" \
    -DCONFIG_INPUT_LOG_LEVEL_DBG=y

cp build/right_logging/zephyr/zmk.uf2 "$REPO/firmware/toucan_right_logging.uf2"

echo "=== BUILD COMPLETE ==="
ls -la "$REPO/firmware/toucan_right_logging.uf2"
