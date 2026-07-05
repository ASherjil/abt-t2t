# Vendored from ABTRDA3

`RxFrame.hpp` and `RingConcepts.hpp` are mirrored **verbatim** from
[ABTRDA3](https://github.com/ASherjil/ABTRDA3) `src/backends/common/`. They are the
shared transport ABI — the `TxRing` / `RxRing` concepts plus the `RxFrame` POD — that
abt-t2t's DPDK `IoMode` consumes so it stays compatible with ABTRDA3's `DPDK<>` backend.

Keep byte-identical with upstream (easy diff). Drift is caught at compile time: building
with `-DABT_WITH_TRANSPORT=ON` links the real `DPDK<>`, which must satisfy these exact concepts.
