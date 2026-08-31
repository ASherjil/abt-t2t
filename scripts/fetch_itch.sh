#!/bin/bash
# Download one of NASDAQ's public TotalView-ITCH 5.0 sample days into data/itch/ (resumable).
#   scripts/fetch_itch.sh [MMDDYYYY]     default 01302020
set -euo pipefail
day="${1:-01302020}"
cd "$(dirname "$(readlink -f "$0")")/.."
mkdir -p data/itch
url="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/${day}.NASDAQ_ITCH50.gz"
out="data/itch/${day}.NASDAQ_ITCH50.gz"
echo "fetching ${url} -> ${out}"
curl -L --fail --retry 5 --retry-delay 5 -C - -o "${out}" "${url}"
ls -la "${out}"
