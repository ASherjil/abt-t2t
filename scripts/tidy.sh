#!/bin/bash
# clang-tidy over the project, then clang-format. No arguments needed for the everyday case.
#   scripts/tidy.sh                 fix everything in place (tidy -fix, then format.sh)
#   scripts/tidy.sh --check         report only, change nothing
#   scripts/tidy.sh --preset debug  use another CMake preset's compile database (default: release)
#   scripts/tidy.sh -- <args>       pass extra args straight to run-clang-tidy
# The compile database is (re)generated first, so a fresh checkout or a CMake change is fine.
# Headers are analyzed via the TUs that include them (HeaderFilterRegex in format/.clang-tidy).
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."

preset="release"
mode="fix"
extra=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --check)  mode="check"; shift ;;
        --fix)    mode="fix"; shift ;;
        --preset) preset="$2"; shift 2 ;;
        --)       shift; extra=("$@"); break ;;
        -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
done

tidy="clang-tidy"
for v in 23 22 21 20 19; do
    if command -v "clang-tidy-${v}" >/dev/null 2>&1; then
        tidy="clang-tidy-${v}"
        break
    fi
done
suffix="${tidy#clang-tidy}"
runner="run-clang-tidy${suffix}"
apply="clang-apply-replacements${suffix}"
command -v "${runner}" >/dev/null 2>&1 || runner="run-clang-tidy"
command -v "${apply}"  >/dev/null 2>&1 || apply="clang-apply-replacements"
echo "using $("${tidy}" --version | grep -o 'LLVM version [0-9.]*') (${mode}, preset ${preset})" >&2

cmake --preset "${preset}" >/dev/null

fixargs=()
if [[ "${mode}" == "fix" ]]; then
    fixargs=(-fix -clang-apply-replacements-binary "$(command -v "${apply}")")
fi

"${runner}" -clang-tidy-binary "$(command -v "${tidy}")" -quiet -p "build/${preset}" \
    -config-file=format/.clang-tidy \
    -header-filter='.*/src/(t2t|apps)/.*' -j "$(nproc)" \
    "${fixargs[@]}" "${extra[@]}" \
    "$(pwd)/(src/t2t|src/apps|test)/.*" || true

if [[ "${mode}" == "fix" ]]; then
    scripts/format.sh
    echo "tidy + format applied; review with: git diff" >&2
fi
