#!/bin/bash
# Run this once to populate third_party/imgui
set -e
DIR="$(cd "$(dirname "$0")/.." && pwd)"
IMGUI="$DIR/third_party/imgui"
if [ ! -d "$IMGUI" ]; then
  git clone --depth=1 --branch v1.90.8 \
    https://github.com/ocornut/imgui.git "$IMGUI"
fi
echo "ImGui ready at $IMGUI"
