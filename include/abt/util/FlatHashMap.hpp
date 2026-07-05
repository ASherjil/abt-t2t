#pragma once
//
// Open-addressing hash map for integer keys — one contiguous array of {key,value} slots, linear
// probing, backward-shift deletion (Knuth 6.4 Algorithm R) so the table stays tombstone-free under
// constant insert/erase churn. This is the cache-friendly, allocation-free replacement for
// std::unordered_map on the hot path: a lookup touches one cache line and follows a short probe run
// instead of the two pointer chases (bucket then node) a chained map pays. One key value is
// reserved as the empty sentinel (default Key{} == 0, valid for ITCH order refs which start at 1).
//
// Declarations first (per project style); definitions at the bottom of the header.
//

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace abt::util {

template <class Key, class Value, Key Empty = Key{}>
class FlatHashMap {
    static_assert(std::is_integral_v<Key>, "FlatHashMap key must be an integral type");

public:
    explicit FlatHashMap(std::size_t capacityHint = 16);

    // Returns a pointer to the stored value (mutable), or nullptr if the key is absent. The pointer
    // is invalidated by any insertOrAssign that grows the table or by erase.
    [[nodiscard]] Value* find(Key key) noexcept;
    [[nodiscard]] const Value* find(Key key) const noexcept;

    // Insert the key or overwrite its value if already present. Grows (rehashes) past a 0.7 load
    // factor; that is the only path that allocates, and it does not happen in steady state.
    void insertOrAssign(Key key, const Value& value);

    bool erase(Key key) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    struct Slot {
        Key   key;
        Value value;
    };

    [[nodiscard]] static std::size_t nextPow2(std::size_t n) noexcept;
    [[nodiscard]] static std::uint64_t mix(std::uint64_t x) noexcept;
    [[nodiscard]] std::size_t slotFor(Key key) const noexcept;
    void grow();

    std::vector<Slot> m_slots;
    std::size_t       m_mask = 0;
    std::size_t       m_size = 0;
};

template <class Key, class Value, Key Empty>
FlatHashMap<Key, Value, Empty>::FlatHashMap(std::size_t capacityHint) {
    const std::size_t cap = nextPow2(capacityHint < 16 ? 16 : capacityHint);
    m_slots.assign(cap, Slot{Empty, Value{}});
    m_mask = cap - 1;
}

template <class Key, class Value, Key Empty>
Value* FlatHashMap<Key, Value, Empty>::find(Key key) noexcept {
    std::size_t i = slotFor(key);
    for (;;) {
        Slot& s = m_slots[i];
        if (s.key == key) {
            return &s.value;
        }
        if (s.key == Empty) {
            return nullptr;
        }
        i = (i + 1) & m_mask;
    }
}

template <class Key, class Value, Key Empty>
const Value* FlatHashMap<Key, Value, Empty>::find(Key key) const noexcept {
    std::size_t i = slotFor(key);
    for (;;) {
        const Slot& s = m_slots[i];
        if (s.key == key) {
            return &s.value;
        }
        if (s.key == Empty) {
            return nullptr;
        }
        i = (i + 1) & m_mask;
    }
}

template <class Key, class Value, Key Empty>
void FlatHashMap<Key, Value, Empty>::insertOrAssign(Key key, const Value& value) {
    if ((m_size + 1) * 10 >= (m_mask + 1) * 7) {
        grow();
    }
    std::size_t i = slotFor(key);
    for (;;) {
        Slot& s = m_slots[i];
        if (s.key == Empty) {
            s.key = key;
            s.value = value;
            ++m_size;
            return;
        }
        if (s.key == key) {
            s.value = value;
            return;
        }
        i = (i + 1) & m_mask;
    }
}

template <class Key, class Value, Key Empty>
bool FlatHashMap<Key, Value, Empty>::erase(Key key) noexcept {
    // Locate the key (stop at the first empty slot: the table has no tombstones).
    std::size_t hole = slotFor(key);
    for (;;) {
        if (m_slots[hole].key == Empty) {
            return false;
        }
        if (m_slots[hole].key == key) {
            break;
        }
        hole = (hole + 1) & m_mask;
    }
    // Backward-shift: pull following entries into the hole when they can legally move there, so no
    // tombstone is left behind. An entry at `scan` may fill `hole` unless its ideal slot lies
    // cyclically within (hole, scan].
    std::size_t scan = hole;
    for (;;) {
        scan = (scan + 1) & m_mask;
        if (m_slots[scan].key == Empty) {
            break;
        }
        const std::size_t ideal = slotFor(m_slots[scan].key);
        bool blocked = false;
        if (hole <= scan) {
            blocked = (hole < ideal && ideal <= scan);
        } else {
            blocked = (hole < ideal || ideal <= scan);
        }
        if (blocked) {
            continue;
        }
        m_slots[hole] = m_slots[scan];
        hole = scan;
    }
    m_slots[hole].key = Empty;
    m_slots[hole].value = Value{};
    --m_size;
    return true;
}

template <class Key, class Value, Key Empty>
void FlatHashMap<Key, Value, Empty>::clear() noexcept {
    for (Slot& s : m_slots) {
        s.key = Empty;
        s.value = Value{};
    }
    m_size = 0;
}

template <class Key, class Value, Key Empty>
std::size_t FlatHashMap<Key, Value, Empty>::size() const noexcept {
    return m_size;
}

template <class Key, class Value, Key Empty>
std::size_t FlatHashMap<Key, Value, Empty>::capacity() const noexcept {
    return m_mask + 1;
}

template <class Key, class Value, Key Empty>
std::size_t FlatHashMap<Key, Value, Empty>::nextPow2(std::size_t n) noexcept {
    std::size_t p = 1;
    while (p < n) {
        p <<= 1;
    }
    return p;
}

template <class Key, class Value, Key Empty>
std::uint64_t FlatHashMap<Key, Value, Empty>::mix(std::uint64_t x) noexcept {
    // fmix64 finalizer (MurmurHash3) — good avalanche so sequential order refs scatter.
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

template <class Key, class Value, Key Empty>
std::size_t FlatHashMap<Key, Value, Empty>::slotFor(Key key) const noexcept {
    return static_cast<std::size_t>(mix(static_cast<std::uint64_t>(key))) & m_mask;
}

template <class Key, class Value, Key Empty>
void FlatHashMap<Key, Value, Empty>::grow() {
    const std::size_t newCap = (m_mask + 1) * 2;
    std::vector<Slot> old = std::move(m_slots);
    m_slots.assign(newCap, Slot{Empty, Value{}});
    m_mask = newCap - 1;
    m_size = 0;
    for (const Slot& s : old) {
        if (s.key != Empty) {
            insertOrAssign(s.key, s.value);
        }
    }
}

}
