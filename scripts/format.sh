#!/bin/bash
# Apply the repo .clang-format to every C++ source (src/, test/).
#   scripts/format.sh          rewrite files in place
#   scripts/format.sh --check  exit non-zero if any file would change (CI / pre-commit)
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."

mapfile -t files < <(git ls-files 'src/t2t/*.cpp' 'src/t2t/*.hpp' 'src/apps/*.cpp' 'src/apps/*.hpp' 'test/*.cpp' 'test/*.hpp')
if [[ "${1:-}" == "--check" ]]; then
    clang-format --style=file:format/.clang-format --dry-run --Werror "${files[@]}"
else
    clang-format --style=file:format/.clang-format -i "${files[@]}"
fi
