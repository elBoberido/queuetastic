#include <atomic>

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
    
    std::atomic<size_t> m_readIndex {0};
    std::atomic<size_t> m_writeIndex {0};
    
public:
    
    BuRiTTO() = default;
    ~BuRiTTO() = default;
    
    bool push(const T inValue, T& outValue) {
        bool overrun = false;
        
        size_t writeIndex = m_writeIndex;
        size_t readIndex = m_readIndex;
        T* overrunValue = { nullptr };
        
        if(writeIndex - readIndex >= Capacity) {    // overrun will happen
            T newPendingOverrunValue = data[readIndex % RealCapacity];
//             overrunValue = pendingOverRunValue.exchange(reinterpret_cast<T*>(newPendingOverrunValue));
            
            auto readIndexOld = readIndex;
            auto readIndexNext = readIndex + 1;
            while(!m_readIndex.compare_exchange_weak(readIndex, readIndexNext) && readIndex == readIndexOld) {};
            
            if(readIndex == readIndexOld) { overrunValue = pendingOverRunValue.exchange(reinterpret_cast<T*>(newPendingOverrunValue)); }
            
            // ================ check logic
            ///> this should not be relevant
            ///> bool newPendingOverrunFlag = readIndex == readIndexOld;  // this indicates just a new pending overrun
            
            if(overrunValue != nullptr) {      // a pop happend right after compare exchange and took the pending 
                outValue = reinterpret_cast<T>(overrunValue);
                overrun = true;
            }
            // ================
        }
        
        data[m_writeIndex % RealCapacity] = inValue;
        m_writeIndex++;
        return !overrun;
    }
    
    bool pop(T& valOut) {
        bool success = false;
        size_t readIndex = m_readIndex;
        size_t readIndexAfterPop;
        T temp;
        
        if(readIndex != m_writeIndex) {
            success = true;
            
            T* porValue = pendingOverRunValue.exchange(nullptr);
            if(porValue != nullptr) {
                temp = reinterpret_cast<T>(porValue);
                readIndexAfterPop = readIndex;
            }else{
                temp = data[readIndex % RealCapacity];
                readIndexAfterPop = m_readIndex.fetch_add(1);
            }

            if(readIndex != readIndexAfterPop) {                // the only other possibility this values are not equal is when there is an overrun
                T* porValue = pendingOverRunValue.exchange(nullptr);    // get the pending overrun; this will be either the same value we also got, or a different one if there were multiple overruns; if there were multiple overruns, our value was already returned by push, so it's correct to exchange with nullptr
                valOut = reinterpret_cast<T>(porValue);
            }else {
                valOut = temp;
            }
        }
        
        return success;
    }
    
    bool empty() {
        return m_readIndex == m_writeIndex;
    }
};
