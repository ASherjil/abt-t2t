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

tidy="clang-tidy"
runner="run-clang-tidy"
for v in 23 22 21 20 19; do
    if command -v "clang-tidy-${v}" >/dev/null 2>&1; then
        tidy="clang-tidy-${v}"
        runner="run-clang-tidy-${v}"
        break
    fi
done
command -v "${runner}" >/dev/null 2>&1 || runner="run-clang-tidy"
echo "using $("${tidy}" --version | grep -o 'LLVM version [0-9.]*')" >&2

exec "${runner}" -clang-tidy-binary "$(command -v "${tidy}")" -quiet -p "build/${preset}" \
    -config-file=format/.clang-tidy \
    -header-filter='.*/src/(t2t|apps)/.*' -j "$(nproc)" "$@" \
    "$(pwd)/(src/t2t|src/apps|test)/.*"
