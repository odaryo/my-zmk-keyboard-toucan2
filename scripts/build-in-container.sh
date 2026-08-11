#!/bin/bash
# ZMK firmware build script — runs inside zmkfirmware/zmk-build-arm container.
# Workspace layout: /workspace (named volume), repo mounted at /workspace/repo
set -euo pipefail

cd /workspace
REPO=/workspace/repo

# west workspace topdir must contain the manifest repo at self.path ("config"),
# so sync the repo's config/ into the volume instead of init-ing in the repo.
mkdir -p /workspace/config
cp -a "$REPO/config/." /workspace/config/

if [ ! -d .west ]; then
    west init -l /workspace/config
fi
west update --fetch-opt=--filter=blob:none
west zephyr-export

# build <name> <shields> <snippet-or-empty> [extra cmake args...]
build() {
    local dir="$1" shield="$2" snippet="$3"; shift 3
    local wargs=()
    [ -n "$snippet" ] && wargs+=(-S "$snippet")
    west build -s zmk/app -d "build/$dir" -b seeeduino_xiao_ble "${wargs[@]}" -- \
        -DSHIELD="$shield" \
        -DZMK_CONFIG=/workspace/config \
        -DZMK_EXTRA_MODULES="$REPO" \
        "$@"
    cp "build/$dir/zephyr/zmk.uf2" "$REPO/firmware/toucan_${dir}.uf2"
}

mkdir -p "$REPO/firmware"

build left  "toucan_left rgbled_adapter nice_view_gem" studio-rpc-usb-uart -DCONFIG_ZMK_STUDIO=y
build right "toucan_right rgbled_adapter" ""
build settings_reset "settings_reset" ""

echo "=== BUILD COMPLETE ==="
ls -la "$REPO/firmware"
