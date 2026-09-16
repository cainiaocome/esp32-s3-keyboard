#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"

: "${IDF_PATH:?IDF_PATH is not set; enter through the ESP-IDF development image}"
: "${IDF_PYTHON:?IDF_PYTHON is not set; use the published development image}"

command -v idf.py >/dev/null
command -v cmake >/dev/null
command -v ninja >/dev/null
command -v clang-format >/dev/null
"$IDF_PYTHON" -m pip --version >/dev/null

make bootstrap
make format
make lint
make test
make build

echo "Container development environment is ready."
