/*
 * Wait-free BuRiTTO (Buffer Ring To Trustily Overflow)
 * Copyright (C) 2018  Mathias Kraus <k.hias@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <atomic>
#include <cstdint>

constexpr bool isPowerOfTwo(std::uint32_t v) {
    return v && ((v & (v - 1)) == 0);
}

// this is only correct if v is power of 2
constexpr std::uint32_t mask(std::uint32_t v) {
    return v - 1;
}

template<std::uint32_t Capacity>
typename std::enable_if<isPowerOfTwo(Capacity), std::uint32_t>::type
index(std::uint64_t counter)
{
    return static_cast<std::uint32_t>(counter & mask(Capacity));
}

template<std::uint32_t Capacity>
typename std::enable_if<!isPowerOfTwo(Capacity), std::uint32_t>::type
index(std::uint64_t counter)
{
    return static_cast<std::uint32_t>(counter % Capacity);
}

template <class T, std::uint32_t Capacity>
class BuRiTTO {      // Buffer Ring To Trustily Overflow ... well, at least for almost 585 years with 1 push per nanosecond ... then the universe implodes
private:
    T m_data[Capacity];
    
    enum class TaSource {
        POP,
        PUSH
    };
    
    // transactions are used for read counter synchronization and overflow handling
    struct Transaction {
        T value;
        std::uint64_t counter { 0 };
        TaSource source { TaSource::POP };
    };
    
    // push and pop thread have a transaction they can operate on and the third transaction object is an exchange object
    Transaction m_ta[3];
    
    // transactions idices for m_ta
    // pop is used in the pop thread, overflow in the push thread and pending to exchange transactions
    std::uint8_t m_taPop { 0 };
    std::uint8_t m_taOverflow { 1 };
    std::atomic<std::uint8_t> m_taPending { 2 };
    
    // consecutive counter; in conjunction with Capacity this is used to calculate the access index to m_data
    std::atomic<std::uint64_t> m_writeCounter { 0 };
    std::atomic<std::uint64_t> m_readCounterPop { 0 };
    std::uint64_t m_readCounterPush { 0 };
    
public:
    BuRiTTO() = default;
    ~BuRiTTO() = default;
    
    bool push(const T inValue, T& outValue) {
        std::uint64_t readCounter = m_readCounterPush;
        std::uint64_t writeCounter = m_writeCounter.load(std::memory_order_relaxed);
        bool overflow = false;
        
        if(writeCounter - readCounter >= Capacity) {    // overflow might happen
            std::uint64_t oldPendingCounter = m_ta[m_taOverflow].counter;
            m_ta[m_taOverflow].source = TaSource::PUSH;
            m_ta[m_taOverflow].value = m_data[index<Capacity>(readCounter)];
            readCounter++;
            m_ta[m_taOverflow].counter = readCounter;
            m_taOverflow = m_taPending.exchange(m_taOverflow, std::memory_order_acq_rel);   //TODO: why not release
            
            if(m_ta[m_taOverflow].source == TaSource::PUSH && m_ta[m_taOverflow].counter > oldPendingCounter) { // overflow happend
                overflow = true;
                outValue =  m_ta[m_taOverflow].value;
            }else if(m_ta[m_taOverflow].counter > readCounter) {
                readCounter = m_ta[m_taOverflow].counter;
            }
            m_readCounterPush = readCounter;
        }
        
        m_data[index<Capacity>(writeCounter)] = inValue;
        m_writeCounter.fetch_add(1, std::memory_order_release);
        
        return !overflow;
    }
    
    bool pop(T& outValue) {
        std::uint64_t readCounter = m_readCounterPop.load(std::memory_order_relaxed);
        std::uint64_t writeCounter = m_writeCounter.load(std::memory_order_acquire);    //TODO: why not relaxed
        
        if(readCounter == writeCounter) { return false; }
        
        outValue = m_data[index<Capacity>(readCounter)];
        m_ta[m_taPop].source = TaSource::POP;
        readCounter++;
        m_ta[m_taPop].counter = readCounter;
        m_taPop = m_taPending.exchange(m_taPop, std::memory_order_acq_rel); //TODO: why not release
        
        if(m_ta[m_taPop].counter >= readCounter) {  // pendig overflow ... needs to be >= because the push thread might already have overwritten the value in m_data we stored in outValue
            outValue =  m_ta[m_taPop].value;
            readCounter = m_ta[m_taPop].counter;
        }
        
        m_readCounterPop.store(readCounter, std::memory_order_relaxed);
        
        return true;
    }
    
    bool empty() {
        return m_readCounterPop.load(std::memory_order_relaxed) == m_writeCounter.load(std::memory_order_relaxed);  // this is save, we do not need to check the m_readCounterPush, because the only possibility to be greater than m_readCounterPop is when the BuRiTTO is not empty
    }
};
