# Vendored from ABTRDA3

`RingConcepts.hpp` is mirrored **verbatim** from
[ABTRDA3](https://github.com/ASherjil/ABTRDA3) `src/backends/common/`. It is the shared
transport ABI — the `TxRing` / `RxRing` concepts — that abt-t2t's transport `IoMode`
consumes so it stays compatible with ABTRDA3's backends (`tryReceive()` returns a span,
empty when no frame is ready).

Keep byte-identical with upstream (easy diff). Drift is caught at compile time: building
with `-DABT_WITH_TRANSPORT=ON` links the real backends, which must satisfy these exact concepts.
