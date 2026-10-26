#!/usr/bin/env bash
set -euo pipefail
mcu_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mcu_app="${MCU_APP:-satellite}"
mcu_board="${MCU_BOARD:-}"
mcu_modules="defaults"
mcu_modules_set=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        --app|--board|--modules)
            if [ "$#" -lt 2 ]; then echo "Missing value for $1" >&2; exit 2; fi
            case "$1" in
                --app) mcu_app="$2" ;;
                --board) mcu_board="$2" ;;
                --modules) mcu_modules="$2"; mcu_modules_set=true ;;
            esac
            shift 2 ;;
        *) break ;;
    esac
done
if [[ ! "$mcu_app" =~ ^[A-Za-z0-9_]+$ ]] || [ ! -f "$mcu_root/apps/$mcu_app/CMakeLists.txt" ] ||
   [ ! -f "$mcu_root/apps/$mcu_app/board.default" ]; then
    echo "Unknown app: $mcu_app" >&2; exit 2
fi
mcu_board="${mcu_board:-$(cat "$mcu_root/apps/$mcu_app/board.default")}"
if [[ ! "$mcu_board" =~ ^[A-Za-z0-9_]+$ ]] || [ ! -f "$mcu_root/boards/$mcu_board/board.cmake" ]; then
    echo "Unknown board: $mcu_board" >&2; exit 2
fi
source "$mcu_root/tools/export.sh"
mcu_build="$mcu_root/build/$mcu_app/$mcu_board"
if "$mcu_modules_set" && [ "$mcu_modules" != defaults ]; then
    if [[ ! "$mcu_modules" =~ ^[a-z_,\;]*$ ]]; then
        echo "Invalid module list: $mcu_modules" >&2; exit 2
    fi
    mcu_variant="${mcu_modules//[,;]/_}"
    mcu_build="$mcu_build-${mcu_variant:-none}"
fi
mcu_args=(-C "$mcu_root/apps/$mcu_app" -B "$mcu_build" -D "MCU_BOARD=$mcu_board" -D "MCU_MODULES=$mcu_modules")
exec python "$IDF_PATH/tools/idf.py" "${mcu_args[@]}" "$@"
