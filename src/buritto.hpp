#include <ctype.h>
#include <atomic>
#include <vector>


// TODO: if this doesn't work
// alternatively, make several segments; each segment has a read and write index; when the writer is full and enters a reader segment, the reader must read from the next segment; within the segments, the read index has a valid flag ... or maybe some bits define the segment, so when changing the segment, segment index and read index can be set atomically

// non data loss overruning ring buffer for non nullptr pointer
template <class T, int capacity>
class BuRiTTO {      // Buffer Ring To Trustily Overrun
public://private:
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
    
    std::atomic<std::uint64_t> m_readIndexPop {0};
    std::atomic<std::uint64_t> m_readIndexPush {0};
    std::atomic<std::uint64_t> m_writeIndex {0};
    
    std::atomic<std::uint64_t> counter {0};
    
    struct Debug {
        std::uint64_t id[10] { 0 };
    };
    
public:
    
    std::vector<Debug> pushId;
    std::vector<Debug> popId;
    
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
            m_pendingBufferPush->index = readIndex;
            m_pendingBufferPush = m_pendingActive.exchange(m_pendingBufferPush);
            
            if(m_pendingBufferPush->index+1 == readIndex && m_pendingBufferPush->valid) {
                overrun = true;
                outValue =  m_pendingBufferPush->value;
                m_readIndexPush = readIndex;
            }else {
                m_readIndexPush = (readIndex > m_pendingBufferPush->index ? readIndex : m_pendingBufferPush->index);
            }
        }
        
        data[m_writeIndex % RealCapacity] = inValue;
        m_writeIndex++;
        
        return !overrun;
    }
    
    bool pop(T& outValue) {
        
//         Debug debug;
//         
//         debug.id[1] = counter.fetch_add(1); //368
//         

        std::uint64_t writeIndex = m_writeIndex;
        std::uint64_t readIndex = m_readIndexPop;
        
        if(readIndex == writeIndex) { return false; }
        
        T tempVal = data[readIndex % RealCapacity];
        m_pendingBufferPop->valid = false;
        readIndex++;
        m_pendingBufferPop->index = readIndex;
        m_pendingBufferPop = m_pendingActive.exchange(m_pendingBufferPop);
        
        if(m_pendingBufferPop->index >= readIndex) {  // pendig overrun
            outValue =  m_pendingBufferPop->value;
            m_readIndexPop = m_pendingBufferPop->index;
        } else {
            outValue = tempVal;
            m_readIndexPop = readIndex;
        }
        
        return true;
    }
    
    bool empty() {
        return m_readIndexPop == m_writeIndex;  // this is save, we do not need to check the m_readIndexPush, because the only possibility to be greater than m_readIndexPop is when the BuRiTTO is not empty
    }
};
