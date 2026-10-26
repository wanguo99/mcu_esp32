#!/usr/bin/env bash
# Installed as ~/EPS/export.sh by setup.sh. Source from Bash or Zsh.
export ESP_SDK_ROOT="${ESP_SDK_ROOT:-$HOME/EPS}"
export IDF_PATH="$ESP_SDK_ROOT/esp-idf"
export IDF_TOOLS_PATH="$ESP_SDK_ROOT/tools"
export PIP_CACHE_DIR="$ESP_SDK_ROOT/cache/pip"
export CCACHE_DIR="$ESP_SDK_ROOT/cache/ccache"

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "ESP-IDF source not found: $IDF_PATH" >&2
    return 1
fi

. "$IDF_PATH/export.sh"
