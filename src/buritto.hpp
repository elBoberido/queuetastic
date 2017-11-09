#include <atomic>


// TODO:
// two pop counter
// one in pop one in overflow
// when entering the function (push or pop), use the larger counter to copy the data and increment the own counter
// overflow has higher priority

// pending overrun must be a pointer to a struct with the value, index of the value and a valid flag


// alternatively, make several segments; each segment has a read and write index; when the writer is full and enters a reader segment, the reader must read from the next segment; within the segments, the read index has a valid flag ... or maybe some bits define the segment, so when changing the segment, segment index and read index can be set atomically

// non data loss overruning ring buffer for non nullptr pointer
template <class T, int capacity>
class BuRiTTO {      // Buffer Ring To Trustily Overrun
private:
    enum {
        Capacity = capacity,
        RealCapacity = Capacity + 1
    };
    T data[RealCapacity] { 0 };
    std::atomic<T*> pendingOverRunValue { nullptr };
    
    std::atomic<size_t> m_readIndexPop {0};
    std::atomic<size_t> m_readIndexPush {0};
    std::atomic<size_t> m_writeIndex {0};
    
public:
    
    BuRiTTO() = default;
    ~BuRiTTO() = default;
    
    bool push(const T inValue, T& outValue) {
        bool overrun = false;
        T* overRunValue { nullptr };
        
        size_t writeIndex = m_writeIndex;
        size_t readIndexPop = m_readIndexPop;   // read pop before push index, because pop is incremented in another thread
        size_t readIndexPush = m_readIndexPush;
        size_t readIndex = readIndexPush;
        // determine the currently valid read index
        if(readIndex < readIndexPop) { readIndex = readIndexPop; }  //TODO: catch index overrun bug, which happens after 2^64 reads
        
        
        // if there will be an overrun, set m_readIndexPush counter to m_readIndexPop counter if this is bigger and then check if the condition for overrun is still true
        // set the data value to m_pendingOverrun
        // in pop, if m_readIndexPush is equal or greater than m_readIndexPop and the m_pendingOverrun is not a nullptr, use this value and set m_readIndexPop to m_readIndexPush
        
        
        if(writeIndex - readIndex >= Capacity) {    // overrun will happen
            overRunValue = pendingOverRunValue.exchange(nullptr);
            if(overRunValue) {
                overrun = true;
                outValue = reinterpret_cast<T>(overRunValue);
            }
            if(readIndexPop > readIndexPush) {
                m_readIndexPush = readIndexPop + 1;
            } else{
                m_readIndexPush++;
            }
        }
        
        readIndex = m_readIndexPop;
        if(writeIndex - readIndex >= Capacity) {    // overrun will still happen
            T newPendingOverrunValue = data[readIndex % RealCapacity];
            pendingOverRunValue.store(reinterpret_cast<T*>(newPendingOverrunValue));
        }
        
        data[m_writeIndex % RealCapacity] = inValue;
        m_writeIndex++;
        return !overrun;
    }
    
    bool pop(T& outValue) {
        bool success = false;
        T tempVal;
        size_t readIndex = 0;
        size_t readIndexPush = 0;
        do {
            success = false;
            readIndexPush = m_readIndexPush;     // read push before pop index, because push is incremented in another thread
            readIndex = m_readIndexPop;
            
            if(readIndexPush > readIndex) {
                T* overRunValue = pendingOverRunValue.exchange(nullptr);
                if(overRunValue) { 
                    tempVal = reinterpret_cast<T>(overRunValue);
                    m_readIndexPop= readIndexPush;
                    success = true;
                    break;
                }
            }
            
            // determine the currently valid read index
            if(readIndex < readIndexPush) { readIndex = readIndexPush; }    //TODO: catch index overrun bug, which happens after 2^64 reads
            
            if(readIndex != m_writeIndex) {
                tempVal = data[readIndex % RealCapacity];
                m_readIndexPop.store(readIndex+1);
                success = true;
            }
        } while(readIndexPush != m_readIndexPush);  // there was an overrun
        
        if(success) { 
            outValue = tempVal; }
        return success;
    }
    
    bool empty() {
        return m_readIndexPop == m_writeIndex;  // this is save, we do not need to check the m_readIndexPush, because the only possibility to be greater than m_readIndexPop is when the BuRiTTO is not empty
    }
};
