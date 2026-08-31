#!/bin/bash
# clang-tidy over the project's translation units, using the compile database of a configured build.
#   scripts/tidy.sh [preset] [extra run-clang-tidy args...]      default preset: release
# Headers are analyzed via the TUs that include them (HeaderFilterRegex in format/.clang-tidy).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."

preset="${1:-release}"
shift || true
db="build/${preset}/compile_commands.json"
if [[ ! -f "${db}" ]]; then
    echo "no ${db} — configure first (scripts/build.sh or cmake --preset ${preset})"
    exit 1
fi

exec run-clang-tidy -quiet -p "build/${preset}" -config-file=format/.clang-tidy \
    -header-filter='.*/(include/abt|apps)/.*' -j "$(nproc)" "$@" \
    "$(pwd)/(src|apps|test)/.*"
