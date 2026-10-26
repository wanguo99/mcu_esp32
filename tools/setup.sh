#!/usr/bin/env bash
set -euo pipefail

satellite_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
idf_version="$(cat "$satellite_root/tools/idf-version")"
export ESP_SDK_ROOT="${ESP_SDK_ROOT:-$HOME/EPS}"
export IDF_PATH="$ESP_SDK_ROOT/esp-idf"
export IDF_TOOLS_PATH="$ESP_SDK_ROOT/tools"
export PIP_CACHE_DIR="$ESP_SDK_ROOT/cache/pip"
export PYTHONUNBUFFERED=1

for tool in git python3 cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing prerequisite: $tool" >&2
        exit 1
    fi
done

mkdir -p "$ESP_SDK_ROOT" "$IDF_TOOLS_PATH" "$PIP_CACHE_DIR"
if [ ! -d "$IDF_PATH" ]; then
    git clone --branch "$idf_version" --depth 1 --recursive --shallow-submodules \
        https://github.com/espressif/esp-idf.git "$IDF_PATH"
fi

installed_commit="$(git -C "$IDF_PATH" rev-parse HEAD)"
expected_commit="$(git -C "$IDF_PATH" rev-parse "$idf_version^{commit}")"
if [ "$installed_commit" != "$expected_commit" ]; then
    echo "$IDF_PATH is not at $idf_version. Existing SDK was preserved." >&2
    exit 1
fi
if [ -n "$(git -C "$IDF_PATH" status --porcelain --untracked-files=no --ignore-submodules)" ]; then
    echo "$IDF_PATH has local source changes. Existing SDK was preserved." >&2
    exit 1
fi
git -C "$IDF_PATH" submodule update --init --recursive --depth 1 --jobs 4

# Minimal Debian installs may have venv and pip but no ensurepip.
# Bootstrap pip into the isolated IDF environment using the existing system pip.
python_version="$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
idf_series="$(python3 -c 'import sys; print(".".join(sys.argv[1].lstrip("v").split(".")[:2]))' "$idf_version")"
idf_env="$IDF_TOOLS_PATH/python_env/idf${idf_series}_py${python_version}_env"
if ! python3 -c 'import ensurepip' >/dev/null 2>&1; then
    if [ ! -x "$idf_env/bin/python" ]; then
        python3 -m venv --without-pip "$idf_env"
    fi
    if ! "$idf_env/bin/python" -m pip --version >/dev/null 2>&1; then
        python3 -m pip --python "$idf_env/bin/python" install pip
    fi
    printf '%s\n' "$idf_series" > "$idf_env/idf_version.txt"
fi

bash "$IDF_PATH/install.sh" esp32s3

if [ -f "$ESP_SDK_ROOT/export.sh" ] && ! cmp -s "$satellite_root/tools/esp-env.sh" "$ESP_SDK_ROOT/export.sh"; then
    echo "Existing $ESP_SDK_ROOT/export.sh differs; it was preserved." >&2
    echo "Activate manually: export IDF_TOOLS_PATH=\"$IDF_TOOLS_PATH\"; source \"$IDF_PATH/export.sh\"" >&2
    exit 1
fi
cp "$satellite_root/tools/esp-env.sh" "$ESP_SDK_ROOT/export.sh"
echo "ESP-IDF installed. Activate with: source \"$ESP_SDK_ROOT/export.sh\""
