#include <ctype.h>
#include <atomic>

template <class T, int capacity>
class BuRiTTO {      // Buffer Ring To Trustily Overrun ... well, at least for almost 585 years with 1 push per nanosecond ... then the universe explodes
private:
    enum {
        Capacity = capacity
    };
    T data[Capacity] { 0 };
    
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
        std::uint64_t writeIndex = m_writeIndex;
        std::uint64_t readIndex = m_readIndexPush;
        bool overrun = false;
        
        if(writeIndex - readIndex >= Capacity) {    // overrun will happen
            std::uint64_t m_oldPendingIndex = m_pendingPush->index;
            m_pendingPush->valid = true;
            m_pendingPush->value = data[readIndex % Capacity];
            readIndex++;
            m_pendingPush->index = readIndex;
            m_pendingPush = m_pendingActive.exchange(m_pendingPush);
            
            if(m_pendingPush->valid && m_pendingPush->index > m_oldPendingIndex) {
                overrun = true;
                outValue =  m_pendingPush->value;
            }else if(m_pendingPush->index > readIndex) {
                readIndex = m_pendingPush->index;
            }
            m_readIndexPush = readIndex;
        }
        
        data[writeIndex % Capacity] = inValue;
        m_writeIndex++;
        
        return !overrun;
    }
    
    bool pop(T& outValue) {
        std::uint64_t writeIndex = m_writeIndex;
        std::uint64_t readIndex = m_readIndexPop;
        
        if(readIndex == writeIndex) { return false; }
        
        T tempVal = data[readIndex % Capacity];
        m_pendingPop->valid = false;
        readIndex++;
        m_pendingPop->index = readIndex;
        m_pendingPop = m_pendingActive.exchange(m_pendingPop);
        
        if(m_pendingPop->index >= readIndex) {  // pendig overrun
            outValue =  m_pendingPop->value;
            readIndex = m_pendingPop->index;
        } else {
            outValue = tempVal;
        }
        m_readIndexPop = readIndex;
        
        return true;
    }
    
    bool empty() {
        return m_readIndexPop == m_writeIndex;  // this is save, we do not need to check the m_readIndexPush, because the only possibility to be greater than m_readIndexPop is when the BuRiTTO is not empty
    }
};
