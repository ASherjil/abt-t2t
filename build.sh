#!/bin/bash
# Configure, build, and test abt-t2t locally (workstation-only; uses CMakePresets).
set -euo pipefail

cd "$(dirname "$0")"

echo "1) Clean + Build"
echo "2) Build only"
read -rp "Choice [1/2] (default 2): " clean_choice
clean_choice="${clean_choice:-2}"

echo ""
echo "1) Release  (-O3, -march=x86-64-v3)"
echo "2) Debug    (ASan + UBSan)"
read -rp "Build type [1/2] (default 1): " type_choice
type_choice="${type_choice:-1}"

case "${type_choice}" in
    1) preset="release" ;;
    2) preset="debug"   ;;
    *) echo "Invalid build type"; exit 1 ;;
esac

build_dir="build/${preset}"
echo ""
echo "── ${preset} → ${build_dir} ──"

if [[ "${clean_choice}" == "1" ]]; then
    echo "Cleaning ${build_dir}..."
    rm -rf "${build_dir}"
fi

if [[ ! -f "${build_dir}/build.ninja" ]]; then
    echo "Configuring..."
    cmake --preset="${preset}"
fi

echo "Building..."
cmake --build --preset="${preset}" -j"$(nproc)"

echo ""
echo "── Running tests ──"
ctest --preset="${preset}"

echo ""
echo "── Done: ${preset} — all tests passed ──"
