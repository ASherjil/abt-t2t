#!/bin/bash
# Configure, build, and test abt-t2t locally (workstation or rig; uses CMakePresets).
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

echo ""
echo "Exchange-sim backends (one binary per backend, all from apps/exchange_sim.cpp):"
echo "1) socket only            -> exchange_sim                                   (no NIC deps)"
echo "2) socket + dpdk          -> exchange_sim, exchange_sim_dpdk                (libdpdk)"
echo "3) socket + dpdk + verbs  -> exchange_sim, exchange_sim_dpdk, exchange_sim_verbs (libdpdk + rdma-core)"
read -rp "Backend tier [1/2/3] (default 1): " backend_choice
backend_choice="${backend_choice:-1}"

case "${backend_choice}" in
    1) backend="socket" ;;
    2) backend="dpdk"   ;;
    3) backend="verbs"  ;;
    *) echo "Invalid backend tier"; exit 1 ;;
esac

build_dir="build/${preset}"
echo ""
echo "── ${preset} / ${backend} → ${build_dir} ──"

if [[ "${clean_choice}" == "1" ]]; then
    echo "Cleaning ${build_dir}..."
    rm -rf "${build_dir}"
fi

echo "Configuring (ABT_SIM_BACKEND=${backend})..."
cmake --preset="${preset}" -DABT_SIM_BACKEND="${backend}"

echo "Building..."
cmake --build --preset="${preset}" -j"$(nproc)"

echo ""
echo "── Running tests ──"
ctest --preset="${preset}"

echo ""
echo "── Done: ${preset} / ${backend} — all tests passed ──"
