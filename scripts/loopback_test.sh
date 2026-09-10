#!/bin/bash
# Loopback run on the rig: exchange_sim on the NIC named in config/exchange_sim.toml, dut on the NIC named
# in config/dut.toml, both on isolated cores, then a clean SIGINT shutdown so both binaries print their
# final report.
#
#   scripts/loopback_test.sh [seconds] [sim backend] [dut backend] [preset]
#       backends: socket | dpdk | verbs | ef_vi   presets: release | release-hw | relwithdebinfo
#       Everything is optional. With no backend words the backends follow the [transport] driver of each
#       config file (mlx5_core = verbs, sfc = ef_vi, i40e = dpdk). With no seconds the run lasts one
#       replay window (skip_to .. stop_at from config/exchange_sim.toml): the sim exits by itself when
#       loops = 1, or is stopped when it starts a second loop.
#       defaults: window run, backends from the configs, release
#   e.g. scripts/loopback_test.sh                       one replay window, backends from the configs
#        scripts/loopback_test.sh release-hw            same with the hardware-timestamp build
#        scripts/loopback_test.sh 150 verbs ef_vi       fixed 150 s, sim on verbs, DUT on ef_vi
#
# Needs root for the raw-packet QPs; re-execs itself under sudo. Logs land in results/loopback_<ts>/.
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    sed -n '2,16p' "$0"; exit 0
fi

duration=""
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
        ''|*[!0-9]*)
            echo "unknown argument '${word}' (seconds, backend: socket|dpdk|verbs|ef_vi, preset: release|release-hw|relwithdebinfo|debug)"; exit 1 ;;
        *)
            duration="${word}" ;;
    esac
done

self="$(readlink -f "$0")"
cd "$(dirname "${self}")/.."

toml_value() {
    sed -n "s/^${2}[[:space:]]*=[[:space:]]*\"\{0,1\}\([^\"#]*\)\"\{0,1\}.*/\1/p" "$1" | head -1 | sed 's/[[:space:]]*$//'
}
backend_of_driver() {
    case "$1" in
        mlx5_core|mlx5) echo verbs ;;
        sfc)            echo ef_vi ;;
        i40e)           echo dpdk ;;
        *)              echo "config $2: unknown transport driver '$1'" >&2; exit 1 ;;
    esac
}
tod_seconds() {
    IFS=: read -r h m sec <<< "$1"
    echo $(( 10#$h * 3600 + 10#$m * 60 + 10#${sec:-0} ))
}

sim_cfg="config/exchange_sim.toml"
dut_cfg="config/dut.toml"
if [[ -z "${backend}" ]]; then
    backend="$(backend_of_driver "$(toml_value "${sim_cfg}" driver)" "${sim_cfg}")"
    dut_backend="${dut_backend:-$(backend_of_driver "$(toml_value "${dut_cfg}" driver)" "${dut_cfg}")}"
fi
dut_backend="${dut_backend:-${backend}}"

skip_to="$(toml_value "${sim_cfg}" skip_to)"
stop_at="$(toml_value "${sim_cfg}" stop_at)"
speed="$(toml_value "${sim_cfg}" speed)"
loops="$(toml_value "${sim_cfg}" loops)"
window=""
if [[ -n "${skip_to}" && -n "${stop_at}" ]]; then
    window=$(( $(tod_seconds "${stop_at}") - $(tod_seconds "${skip_to}") ))
    if [[ -n "${speed}" && "${speed}" != "0" ]]; then
        window="$(awk -v w="${window}" -v s="${speed}" 'BEGIN { printf "%d", w / s + 0.5 }')"
    fi
fi
if [[ -z "${duration}" && -z "${window}" ]]; then
    echo "no seconds given and ${sim_cfg} has no skip_to/stop_at window"; exit 1
fi

if [[ ( "${backend}" != "socket" || "${dut_backend}" != "socket" ) && "${EUID}" -ne 0 ]]; then
    exec sudo --preserve-env=HOME "${self}" ${duration:+"${duration}"} "${backend}" "${dut_backend}" "${preset}"
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


# ef_vi: the in-order CTPIO writer (sfence per 64 B block) makes fallbacks architectural instead of a
# per-process lottery (abtrda3 Known_driver_issues.md 4.1). sudo resets the environment, so export here.
export EF_VI_CTPIO_MODE="${EF_VI_CTPIO_MODE:-in_order}"

out="results/loopback_$(date +%Y%m%d_%H%M%S)"
mkdir -p "${out}"
{
    echo "abt-t2t $(git rev-parse --short HEAD 2>/dev/null)$(git diff --quiet 2>/dev/null || echo '+dirty')"
    for d in "build/${preset}/_deps/abtrda3-src" "${FETCHCONTENT_SOURCE_DIR_ABTRDA3:-}"; do
        [[ -n "${d}" && -d "${d}/.git" ]] && echo "abtrda3 $(git -C "${d}" rev-parse --short HEAD)" && break
    done
    echo "sim backend ${backend}  dut backend ${dut_backend}  preset ${preset}  duration ${duration:-window}${duration:+s}  window ${skip_to:-none}..${stop_at:-none} (${window:-?}s at speed ${speed:-1}, loops ${loops:-0})  host $(hostname)  $(date -Is)"
    echo "sw_timing $(sed -n 's/^ABT_SW_TIMING:BOOL=//p' "build/${preset}/CMakeCache.txt")"
    echo "ef_vi_ctpio_mode ${EF_VI_CTPIO_MODE}"
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

if [[ -n "${duration}" ]]; then
    echo "── loopback: ${sim_bin} <-> ${dut_bin}, ${duration}s, logs in ${out}/ ──"
else
    echo "── loopback: ${sim_bin} <-> ${dut_bin}, replay window ${skip_to}..${stop_at} (${window}s), logs in ${out}/ ──"
fi

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

if [[ -n "${duration}" ]]; then
    remaining=$((duration - 2))
    if (( remaining > 0 )); then
        sleep "${remaining}"
    fi
else
    deadline=$(( SECONDS + window + 900 ))
    while kill -0 "${sim_pid}" 2>/dev/null && kill -0 "${dut_pid}" 2>/dev/null; do
        if grep -qE "^\[sim \+ *[0-9]+s\] loop=[1-9]" "${sim_log}"; then
            echo "replay window finished (second loop started), stopping"
            break
        fi
        if (( SECONDS >= deadline )); then
            echo "replay window did not finish within ${window}s + 15 min, stopping"
            break
        fi
        sleep 1
    done
    if ! kill -0 "${sim_pid}" 2>/dev/null; then
        wait "${sim_pid}" 2>/dev/null || true
        echo "exchange_sim finished the replay window"
    elif ! kill -0 "${dut_pid}" 2>/dev/null; then
        echo "dut exited before the replay window finished:"; tail -5 "${dut_log}"
    fi
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
