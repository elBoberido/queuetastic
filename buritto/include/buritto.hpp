// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2018 - 2023 Mathias Kraus <elboberido@m-hias.de>

#ifndef _BURITTO_HPP_
#define _BURITTO_HPP_

#include <atomic>
#include <cstdint>
#include <type_traits>

constexpr bool isPowerOfTwo(uint32_t v) {
    return v && ((v & (v - 1)) == 0);
}

// this is only correct if v is power of 2
constexpr uint32_t mask(uint32_t v) {
    return v - 1;
}

template <uint32_t Capacity>
typename std::enable_if<isPowerOfTwo(Capacity), uint32_t>::type index(uint64_t counter) {
    return static_cast<uint32_t>(counter & mask(Capacity));
}

template <uint32_t Capacity>
typename std::enable_if<!isPowerOfTwo(Capacity), uint32_t>::type index(uint64_t counter) {
    return static_cast<uint32_t>(counter % Capacity);
}

template <class T, uint32_t Capacity>
class BuRiTTO { // Buffer Ring To Trustily Overrun ... well, at least for almost 585 years with 1 push per nanosecond ... then the universe implodes
private:
    // TODO use a second array with of uint64_t m_slots[static_cast<uint64_t>(Capacity) + 2];
    //      m_data then holds all the data (>8 Byte and non trivialy copyable possible)
    //      m_slots are the "pointer" to the actual data which are also in the Transaction::value
    //      m_data will only be accessed once the ownership of the slot belongs to the thread
    T m_data[Capacity];

    enum class TaSource { POP, PUSH };

    // transactions are used for read counter synchronization and overrun handling
    struct Transaction {
        T        value;
        uint64_t counter {0};
        TaSource source {TaSource::POP};
    };

    // push and pop thread have a transaction they can operate on and the third transaction object is an exchange object
    Transaction m_ta[3];

    // transactions idices for m_ta
    // pop is used in the pop thread, overrun in the push thread and pending to exchange transactions
    uint8_t              m_taPop {0};
    uint8_t              m_taOverrun {1};
    std::atomic<uint8_t> m_taPending {2};

    // consecutive counter; in conjunction with Capacity this is used to calculate the access index to m_data
    std::atomic<uint64_t> m_writeCounter {0};
    std::atomic<uint64_t> m_readCounterPop {0};
    uint64_t              m_readCounterPush {0};

public:
    BuRiTTO()  = default;
    ~BuRiTTO() = default;

    bool push(const T inValue, T& outValue) {
        uint64_t readCounter  = m_readCounterPush;
        uint64_t writeCounter = m_writeCounter.load(std::memory_order_relaxed);
        bool     overrun      = false;

        if (writeCounter - readCounter >= Capacity) { // overrun might happen
            uint64_t oldPendingCounter = m_ta[m_taOverrun].counter;
            m_ta[m_taOverrun].source   = TaSource::PUSH;
            m_ta[m_taOverrun].value    = m_data[index<Capacity>(readCounter)];
            readCounter++;
            m_ta[m_taOverrun].counter = readCounter;
            m_taOverrun               = m_taPending.exchange(m_taOverrun, std::memory_order_acq_rel);

            if (m_ta[m_taOverrun].source == TaSource::PUSH && m_ta[m_taOverrun].counter > oldPendingCounter) { // overrun happend
                overrun  = true;
                outValue = m_ta[m_taOverrun].value;
            } else if (m_ta[m_taOverrun].counter > readCounter) {
                readCounter = m_ta[m_taOverrun].counter;
            }
            m_readCounterPush = readCounter;
        }

        m_data[index<Capacity>(writeCounter)] = inValue;
        m_writeCounter.store(++writeCounter, std::memory_order_release);

        return !overrun;
    }

    bool pop(T& outValue) {
        uint64_t readCounter  = m_readCounterPop.load(std::memory_order_relaxed);
        uint64_t writeCounter = m_writeCounter.load(std::memory_order_acquire);

        if (readCounter == writeCounter) { return false; }

        outValue             = m_data[index<Capacity>(readCounter)];
        m_ta[m_taPop].source = TaSource::POP;
        readCounter++;
        m_ta[m_taPop].counter = readCounter;
        m_taPop               = m_taPending.exchange(m_taPop, std::memory_order_acq_rel);

        // pendig overrun ... needs to be >= because the push thread might already have overwritten the value in m_data we stored in outValue
        if (m_ta[m_taPop].counter >= readCounter) {
            outValue    = m_ta[m_taPop].value;
            readCounter = m_ta[m_taPop].counter;
        }

        m_readCounterPop.store(readCounter, std::memory_order_relaxed);

        return true;
    }

    bool empty() {
        // this is save, we do not need to check the m_readCounterPush, because the only possibility to be greater than m_readCounterPop is when the BuRiTTO is
        // not empty
        return m_readCounterPop.load(std::memory_order_relaxed) == m_writeCounter.load(std::memory_order_relaxed);
    }
};

#endif // _BURITTO_HPP_
