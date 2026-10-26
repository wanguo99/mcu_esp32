#!/usr/bin/env bash
# Source this file from Bash or Zsh to use idf.py in the current terminal.
if [ -n "${BASH_VERSION:-}" ]; then
    satellite_export_path="${BASH_SOURCE[0]}"
elif [ -n "${ZSH_VERSION:-}" ]; then
    satellite_export_path="${(%):-%x}"
else
    echo "Please source tools/export.sh from Bash or Zsh." >&2
    return 1
fi

export ESP32_SATELLITE_ROOT="$(cd -- "$(dirname -- "$satellite_export_path")/.." && pwd)"
export ESP_SDK_ROOT="${ESP_SDK_ROOT:-$HOME/EPS}"

if [ ! -f "$ESP_SDK_ROOT/export.sh" ]; then
    echo "ESP-IDF is not installed. Run: bash tools/setup.sh" >&2
    return 1
fi

unset satellite_export_path
. "$ESP_SDK_ROOT/export.sh" || return $?
satellite_expected_version="$(cat "$ESP32_SATELLITE_ROOT/tools/idf-version")"
if [ "v${ESP_IDF_VERSION:-}" != "$satellite_expected_version" ]; then
    echo "This project requires ESP-IDF $satellite_expected_version; found ${ESP_IDF_VERSION:-unknown}." >&2
    unset satellite_expected_version
    return 1
fi
unset satellite_expected_version
