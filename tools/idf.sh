#!/usr/bin/env bash
set -euo pipefail
satellite_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$satellite_root/tools/export.sh"
cd "$satellite_root"
exec python "$IDF_PATH/tools/idf.py" "$@"
