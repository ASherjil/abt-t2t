#!/bin/bash
# Download one of NASDAQ's public TotalView-ITCH 5.0 sample days into data/itch/ (resumable).
#   scripts/fetch_itch.sh [file]     default itch50_05_15.gz (15 May 2026, the day used here)
set -euo pipefail
name="${1:-itch50_05_15.gz}"
cd "$(dirname "$(readlink -f "$0")")/.."
mkdir -p data/itch
url="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/${name}"
out="data/itch/${name}"
echo "fetching ${url} -> ${out}"
curl -L --fail --retry 5 --retry-delay 5 -C - -o "${out}" "${url}"
ls -la "${out}"
