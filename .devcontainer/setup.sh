#!/bin/bash
set -e

echo "--- Installing uv ---"
curl -LsSf https://astral.sh/uv/install.sh | sh
export PATH="$HOME/.local/bin:$PATH"

echo "--- Installing pebble-tool ---"
uv tool install pebble-tool

echo "--- Installing Pebble SDK (downloads Moddable toolchain, ~500MB) ---"
yes | pebble sdk install latest

echo "--- Installing npm dependencies ---"
npm install

echo "--- Done. Run 'pebble build' to build the watchface. ---"
