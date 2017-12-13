/*
 * Wait-free BuRiTTO (Buffer Ring To Trustily Overrun)
 * Copyright (C) 2017  Mathias Kraus <k.hias@gmx.de>
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

template <class T, int capacity>
class BuRiTTO {      // Buffer Ring To Trustily Overrun ... well, at least for almost 585 years with 1 push per nanosecond ... then the universe implodes
private:
    enum {
        Capacity = capacity
    };
    T data[Capacity];
    
    struct Transaction {
        T value;
        std::uint64_t index { 0 };
        bool valid { false };
    };
    
    Transaction m_transactions[3];
    
    Transaction* m_pendingPop { &m_transactions[0] };
    Transaction* m_pendingPush { &m_transactions[1] };
    std::atomic<Transaction*> m_pendingActive { &m_transactions[2] };
    
    std::atomic<std::uint64_t> m_writeIndex {0};
    std::atomic<std::uint64_t> m_readIndexPop {0};
    std::uint64_t m_readIndexPush {0};
    
public:
    BuRiTTO() = default;
    ~BuRiTTO() = default;
    
    bool push(const T inValue, T& outValue) {
        std::uint64_t readIndex = m_readIndexPush;
        std::uint64_t writeIndex = m_writeIndex.load(std::memory_order_relaxed);
        bool overrun = false;
        
        if(writeIndex - readIndex >= Capacity) {    // overrun will happen
            std::uint64_t m_oldPendingIndex = m_pendingPush->index;
            m_pendingPush->valid = true;
            m_pendingPush->value = data[readIndex % Capacity];
            readIndex++;
            m_pendingPush->index = readIndex;
            m_pendingPush = m_pendingActive.exchange(m_pendingPush, std::memory_order_release);
            
            if(m_pendingPush->valid && m_pendingPush->index > m_oldPendingIndex) {
                overrun = true;
                outValue =  m_pendingPush->value;
            }else if(m_pendingPush->index > readIndex) {
                readIndex = m_pendingPush->index;
            }
            m_readIndexPush = readIndex;
        }
        
        data[writeIndex % Capacity] = inValue;
        m_writeIndex.fetch_add(1, std::memory_order_release);
        
        return !overrun;
    }
    
    bool pop(T& outValue) {
        std::uint64_t readIndex = m_readIndexPop.load(std::memory_order_relaxed);
        std::uint64_t writeIndex = m_writeIndex.load(std::memory_order_relaxed);
        
        if(readIndex == writeIndex) { return false; }
        
        T tempVal = data[readIndex % Capacity];
        m_pendingPop->valid = false;
        readIndex++;
        m_pendingPop->index = readIndex;
        m_pendingPop = m_pendingActive.exchange(m_pendingPop, std::memory_order_release);
        
        if(m_pendingPop->index >= readIndex) {  // pendig overrun
            outValue =  m_pendingPop->value;
            readIndex = m_pendingPop->index;
        } else {
            outValue = tempVal;
        }
        m_readIndexPop.store(readIndex, std::memory_order_relaxed);
        
        return true;
    }
    
    bool empty() {
        return m_readIndexPop.load(std::memory_order_relaxed) == m_writeIndex.load(std::memory_order_relaxed);  // this is save, we do not need to check the m_readIndexPush, because the only possibility to be greater than m_readIndexPop is when the BuRiTTO is not empty
    }
};
