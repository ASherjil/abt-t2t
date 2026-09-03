#!/bin/bash
# Loopback run on the rig: exchange_sim_<backend> (cx0) <-DAC-> dut_<backend> (cx1), both on isolated
# cores, for a fixed duration, then a clean SIGINT shutdown so both binaries print their final report.
#
#   scripts/loopback_test.sh [seconds] [sim backend] [dut backend] [preset]
#       backends: socket | dpdk | verbs | ef_vi   presets: release | release-hw | relwithdebinfo
#       after the duration the words can come in any order; one backend name = both sides.
#       defaults: 10 s, verbs both sides, release
#   e.g. scripts/loopback_test.sh 150 verbs ef_vi        sim on the ConnectX-4 Lx, DUT on the X2522
#        scripts/loopback_test.sh 150 verbs              verbs loopback, both ConnectX-4 ports
#        scripts/loopback_test.sh 150 verbs ef_vi release-hw
#
# Needs root for the raw-packet QPs; re-execs itself under sudo. Logs land in results/loopback_<ts>/.
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    sed -n '2,12p' "$0"; exit 0
fi

duration="${1:-10}"
shift $(( $# > 0 ? 1 : 0 ))
backend=""
dut_backend=""
preset="release"
for word in "$@"; do
    case "${word}" in
        socket|dpdk|verbs|ef_vi)
            if [[ -z "${backend}" ]]; then
                backend="${word}"
            elif [[ -z "${dut_backend}" ]]; then
                dut_backend="${word}"
            else
                echo "too many backends: ${word}"; exit 1
            fi ;;
        release|release-hw|relwithdebinfo|debug)
            preset="${word}" ;;
        *)
            echo "unknown argument '${word}' (backend: socket|dpdk|verbs|ef_vi, preset: release|release-hw|relwithdebinfo|debug)"; exit 1 ;;
    esac
done
backend="${backend:-verbs}"
dut_backend="${dut_backend:-${backend}}"

self="$(readlink -f "$0")"
cd "$(dirname "${self}")/.."

if [[ ( "${backend}" != "socket" || "${dut_backend}" != "socket" ) && "${EUID}" -ne 0 ]]; then
    exec sudo --preserve-env=HOME "${self}" "${duration}" "${backend}" "${dut_backend}" "${preset}"
fi

bin_suffix() {
    if [[ "$1" == "socket" ]]; then
        echo ""
    else
        echo "_$1"
    fi
}
sim_bin="build/${preset}/apps/exchange_sim$(bin_suffix "${backend}")"
dut_bin="build/${preset}/apps/dut$(bin_suffix "${dut_backend}")"
for bin in "${sim_bin}" "${dut_bin}"; do
    if [[ ! -x "${bin}" ]]; then
        echo "missing ${bin} — build a tier that includes it first (scripts/build.sh)"; exit 1
    fi
done


out="results/loopback_$(date +%Y%m%d_%H%M%S)"
mkdir -p "${out}"
{
    echo "abt-t2t $(git rev-parse --short HEAD 2>/dev/null)$(git diff --quiet 2>/dev/null || echo '+dirty')"
    for d in "build/${preset}/_deps/abtrda3-src" "${FETCHCONTENT_SOURCE_DIR_ABTRDA3:-}"; do
        [[ -n "${d}" && -d "${d}/.git" ]] && echo "abtrda3 $(git -C "${d}" rev-parse --short HEAD)" && break
    done
    echo "sim backend ${backend}  dut backend ${dut_backend}  preset ${preset}  duration ${duration}s  host $(hostname)  $(date -Is)"
    echo "sw_timing $(sed -n 's/^ABT_SW_TIMING:BOOL=//p' "build/${preset}/CMakeCache.txt")"
    echo "hlog $(sed -n 's/^log_file *= *"\([^"]*\)".*/\1/p' config/dut.toml)  start_epoch $(date +%s)"
} > "${out}/versions.txt"
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
