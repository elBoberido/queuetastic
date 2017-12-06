#include <ctype.h>
#include <atomic>

template <class T, int capacity>
class BuRiTTO {      // Buffer Ring To Trustily Overrun
private:
    enum {
        Capacity = capacity,
        RealCapacity = Capacity
    };
    T data[RealCapacity] { 0 };
    
    struct PendingOverrun {
        T value;
        std::uint64_t index { 0 };
        bool valid { false };
    };
    
    PendingOverrun m_pendingBuffers[3];
    
    PendingOverrun* m_pendingBufferPop { &m_pendingBuffers[0] };
    PendingOverrun* m_pendingBufferPush { &m_pendingBuffers[1] };
    std::atomic<PendingOverrun*> m_pendingActive { &m_pendingBuffers[2] };
    
    std::atomic<std::uint64_t> m_writeIndex {0};
    std::atomic<std::uint64_t> m_readIndexPop {0};
    std::uint64_t m_readIndexPush {0};
    std::uint64_t m_oldReadIndexPendingPush {0};
    
public:
    BuRiTTO() = default;
    ~BuRiTTO() = default;
    
    bool push(const T inValue, T& outValue) {
        std::uint64_t writeIndex = m_writeIndex;
        std::uint64_t readIndex = m_readIndexPush;
        bool overrun = false;
        
        if(writeIndex - readIndex >= Capacity) {    // overrun will happen
            m_pendingBufferPush->valid = true;
            m_pendingBufferPush->value = data[readIndex % RealCapacity];
            readIndex++;
            m_oldReadIndexPendingPush = m_pendingBufferPush->index;
            m_pendingBufferPush->index = readIndex;
            m_pendingBufferPush = m_pendingActive.exchange(m_pendingBufferPush);
            
            if(m_pendingBufferPush->valid && m_pendingBufferPush->index > m_oldReadIndexPendingPush) {
                overrun = true;
                outValue =  m_pendingBufferPush->value;
                m_readIndexPush = readIndex;
            }else {
                m_readIndexPush = (readIndex > m_pendingBufferPush->index ? readIndex : m_pendingBufferPush->index);
            }
        }
        
        data[writeIndex % RealCapacity] = inValue;
        m_writeIndex++;
        
        return !overrun;
    }
    
    bool pop(T& outValue) {
        std::uint64_t writeIndex = m_writeIndex;
        std::uint64_t readIndex = m_readIndexPop;
        
        if(readIndex == writeIndex) { return false; }
        
        T tempVal = data[readIndex % RealCapacity];
        m_pendingBufferPop->valid = false;
        readIndex++;
        m_pendingBufferPop->index = readIndex;
        m_pendingBufferPop = m_pendingActive.exchange(m_pendingBufferPop);
        
        if(m_pendingBufferPop->index >= readIndex && m_pendingBufferPop->valid) {  // pendig overrun
            outValue =  m_pendingBufferPop->value;
            readIndex = m_pendingBufferPop->index;
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
