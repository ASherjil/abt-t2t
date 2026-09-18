# Market data (not committed)

The day used for every result in this repo is NASDAQ TotalView-ITCH 5.0, 15 May 2026, from
NASDAQ's public sample directory <https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/>:

    itch50_05_15.gz        13 GB gzipped, 30 GB unpacked, 961 million messages

The directory lists it by upload date (June 2026), not trading day. The older samples there
are named by date (`01302020.NASDAQ_ITCH50.gz`); the 2026 files are not.

    scripts/fetch_itch.sh                     # downloads itch50_05_15.gz into data/itch/
    gunzip data/itch/itch50_05_15.gz          # the simulator reads the unpacked file

Format: "BinaryFILE", a 2-byte big-endian length before each raw ITCH message, no MoldUDP64
framing. Point `[replay] file` in `config/exchange_sim.toml` at the unpacked file. Nothing under
`data/itch/` is tracked by git.
