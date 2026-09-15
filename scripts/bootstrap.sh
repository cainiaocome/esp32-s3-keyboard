#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"

python_bin="${IDF_PYTHON:-python3}"
if ! command -v "$python_bin" >/dev/null 2>&1; then
    echo "Python 3 is required." >&2
    exit 1
fi

if [[ -z "${IDF_PATH:-}" ]]; then
    echo "ESP-IDF is not active. Use the pinned Docker workflow in README.md or source an ESP-IDF v6.1 export.sh." >&2
    echo "Host bootstrap will still install the Python test dependency." >&2
fi

if "$python_bin" -m pip install -r tests/requirements.txt; then
    echo "Python test dependencies are ready."
else
    echo "Could not install Python dependencies. In a managed environment, install tests/requirements.txt manually." >&2
    exit 1
fi

if command -v idf.py >/dev/null 2>&1; then
    echo "ESP-IDF: $(idf.py --version 2>/dev/null || true)"
    echo "Target: ${IDF_TARGET:-esp32s3}"
else
    echo "idf.py: not found; firmware build remains unavailable until ESP-IDF v6.1 is active."
fi

echo "Bootstrap complete. Credentials are read only from the ignored .env/sdkconfig.defaults.local path."
