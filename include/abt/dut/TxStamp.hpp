#pragma once
//
// TX-completion timestamp contract. The DUT tags every order with its OUCH userRefNum; the NIC
// timestamps the frame as it leaves the wire and reports (userRef, sec, nsec) *asynchronously* —
// ef_vi via an EF_EVENT_TYPE_TX_WITH_TIMESTAMP eventq entry, DPDK via
// rte_eth_timesync_read_tx_timestamp. Because the stamp does not come back from the send call, it
// cannot ride TxRing::commit(); instead the session polls a TxStampSource for the next completion
// (status == 0 means "none ready yet") and joins it to the pending order by userRefNum. Whether
// that source is the datapath transport itself or a sidecar is a rig-wiring choice, so the session
// depends only on this concept — never on a concrete transport type.
//

#include <concepts>
#include <cstdint>

namespace abt::dut {

struct TxCompletion {
    std::uint32_t userRef;   // OUCH userRefNum the order was tagged with
    std::uint32_t sec;
    std::uint32_t nsec;
    std::uint32_t status;    // 0 = no completion available
};

template <typename S>
concept TxStampSource = requires(S s) {
    { s.pollTxTimestamp() } noexcept -> std::same_as<TxCompletion>;
};

}
