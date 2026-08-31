#!/bin/bash
# Apply the repo .clang-format to every C++ source (include/, src/, apps/, test/).
#   scripts/format.sh          rewrite files in place
#   scripts/format.sh --check  exit non-zero if any file would change (CI / pre-commit)
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/.."

mapfile -t files < <(git ls-files 'include/*.hpp' 'src/*.cpp' 'src/*.hpp' 'apps/*.cpp' 'apps/*.hpp' 'test/*.cpp' 'test/*.hpp')
if [[ "${1:-}" == "--check" ]]; then
    clang-format --style=file --dry-run --Werror "${files[@]}"
else
    clang-format --style=file -i "${files[@]}"
fi
