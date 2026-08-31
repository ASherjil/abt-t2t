#!/bin/bash
# Loopback run on the rig: exchange_sim_<backend> (cx0) <-DAC-> dut_<backend> (cx1), both on isolated
# cores, for a fixed duration, then a clean SIGINT shutdown so both binaries print their final report.
#
#   scripts/loopback_test.sh [seconds] [backend] [preset]     defaults: 10  verbs  release
#
# Needs root for the raw-packet QPs; re-execs itself under sudo. Logs land in results/loopback_<ts>/.
set -euo pipefail

duration="${1:-10}"
backend="${2:-verbs}"
preset="${3:-release}"

self="$(readlink -f "$0")"
cd "$(dirname "${self}")/.."

if [[ "${backend}" != "socket" && "${EUID}" -ne 0 ]]; then
    exec sudo --preserve-env=HOME "${self}" "${duration}" "${backend}" "${preset}"
fi

suffix="_${backend}"
if [[ "${backend}" == "socket" ]]; then
    suffix=""
fi
sim_bin="build/${preset}/apps/exchange_sim${suffix}"
dut_bin="build/${preset}/apps/dut${suffix}"
for bin in "${sim_bin}" "${dut_bin}"; do
    if [[ ! -x "${bin}" ]]; then
        echo "missing ${bin} — build the '${backend}' tier first (scripts/build.sh)"; exit 1
    fi
done


out="results/loopback_$(date +%Y%m%d_%H%M%S)"
mkdir -p "${out}"
sim_log="${out}/sim.log"
dut_log="${out}/dut.log"

sim_pid=""
dut_pid=""
grace=5

stop_one() {
    local pid="$1" name="$2"
    if [[ -z "${pid}" ]] || ! kill -0 "${pid}" 2>/dev/null; then
        return
    fi
    for sig in INT TERM KILL; do
        kill -"${sig}" "${pid}" 2>/dev/null || true
        for _ in $(seq 1 $((grace * 10))); do
            if ! kill -0 "${pid}" 2>/dev/null; then
                wait "${pid}" 2>/dev/null || true
                if [[ "${sig}" != "INT" ]]; then
                    echo "${name}: did not stop on SIGINT, needed SIG${sig} (backend hung?)"
                fi
                return
            fi
            sleep 0.1
        done
    done
}

stop_all() {
    trap - INT TERM
    stop_one "${dut_pid}" dut
    stop_one "${sim_pid}" exchange_sim
}
trap 'echo "interrupted — stopping"; stop_all; exit 130' INT TERM

echo "── loopback: ${sim_bin} <-> ${dut_bin}, ${duration}s, logs in ${out}/ ──"

"${sim_bin}" > "${sim_log}" 2>&1 &
sim_pid=$!
sleep 1
if ! kill -0 "${sim_pid}" 2>/dev/null; then
    echo "exchange_sim exited early:"; cat "${sim_log}"; exit 1
fi

"${dut_bin}" > "${dut_log}" 2>&1 &
dut_pid=$!
sleep 1
if ! kill -0 "${dut_pid}" 2>/dev/null; then
    echo "dut exited early:"; cat "${dut_log}"; stop_all; exit 1
fi

remaining=$((duration - 2))
if (( remaining > 0 )); then
    sleep "${remaining}"
fi
stop_all

chown -R "${SUDO_UID:-0}:${SUDO_GID:-0}" results 2>/dev/null || true

echo ""
echo "── dut (${dut_log}) ──"
grep -E "^\[dut \+" "${dut_log}" | tail -3
grep -vE "^\[dut \+" "${dut_log}"
echo ""
echo "── sim (${sim_log}) ──"
grep -E "^\[sim \+" "${sim_log}" | tail -3
grep -vE "^\[sim \+" "${sim_log}"
