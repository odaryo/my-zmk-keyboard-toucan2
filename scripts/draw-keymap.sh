#!/bin/bash
# Regenerate docs/keymap.svg from config/toucan.keymap with keymap-drawer.
# Run this after every keymap change so the README picture stays in sync.
#
# Usage:
#   ./scripts/draw-keymap.sh
#
# Requires uv (https://docs.astral.sh/uv/) — it fetches keymap-drawer on demand.
# With keymap-drawer already installed, set KEYMAP="keymap" to use it directly.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
KEYMAP="${KEYMAP:-uvx --from keymap-drawer keymap}"
CONFIG="$REPO/keymap_drawer.config.yaml"
OUT="$REPO/docs/keymap.svg"

mkdir -p "$(dirname "$OUT")"

# -d is required: the physical layout lives in the shield, not in the keymap.
$KEYMAP -c "$CONFIG" parse -z "$REPO/config/toucan.keymap" \
    | $KEYMAP -c "$CONFIG" draw \
        -d "$REPO/boards/shields/toucan/toucan.dtsi" -l default_layout \
        -o "$OUT" -

echo "wrote $OUT"
