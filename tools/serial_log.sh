#!/usr/bin/env bash
set -euo pipefail
mcu_tools_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$mcu_tools_dir/export.sh"
exec python "$mcu_tools_dir/serial_log.py" "$@"
