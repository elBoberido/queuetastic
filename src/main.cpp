#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <ctime>
#include <chrono>

#include "buritto.hpp"

enum {
    BURITTO_CAPACITY = 10
};

typedef size_t BuRiTTOData;

typedef BuRiTTO<BuRiTTOData, BURITTO_CAPACITY> MyBuRiTTo;

enum {
    BuRiTTO_CounterStartValue = static_cast<BuRiTTOData>(0),
    BuRiTTO_InvalidValue = static_cast<BuRiTTOData>(-1)
};

int main(int, char**) {
    
    MyBuRiTTo buritto;
    
    if(!buritto.empty()) { std::cout << "1000 Failure: BuRiTTO should be empty!" << std::endl; }
    
    BuRiTTOData dataCounter {BuRiTTO_CounterStartValue};
    BuRiTTOData pushCounter {BuRiTTO_CounterStartValue};
    BuRiTTOData outValue {BuRiTTO_InvalidValue};
    for(int i = 0; i < BURITTO_CAPACITY + 1; i++) {
        outValue = BuRiTTO_InvalidValue;
        if(buritto.push(pushCounter, outValue) == false) { std::cout << "1010 Failure: BuRiTTO should not overflow!" << std::endl; }
        pushCounter++;
        if(outValue != BuRiTTO_InvalidValue) { std::cout << "1020 Failure: BuRiTTO should not return data!" << std::endl; }
        
        if(buritto.empty()) { std::cout << "1030 Failure: BuRiTTO should not be empty!" << std::endl; }
    }
    
    outValue = BuRiTTO_InvalidValue;
    if(buritto.push(pushCounter, outValue) == true) { std::cout << "1040 Failure: BuRiTTO should overflow!" << std::endl; }
    pushCounter++;
    if(outValue != dataCounter) { std::cout << "1050 Failure: BuRiTTO should overflow and return data!" << std::endl; }
    dataCounter++;
    
    for(int i = 0; i < BURITTO_CAPACITY + 1; i++) {
        if(buritto.empty()) { std::cout << "1060 Failure: BuRiTTO should not be empty!" << std::endl; }
        outValue = BuRiTTO_InvalidValue;
        if(buritto.pop(outValue) == false) { std::cout << "1070 Failure: BuRiTTO should return data!" << std::endl; }
        if(outValue != dataCounter) { std::cout << "1080 Failure: BuRiTTO lost data! " << "Expected: " << dataCounter << " Actual: " << outValue << std::endl; }
        dataCounter++;
    }
    
    if(!buritto.empty()) { std::cout << "1090 Failure: BuRiTTO should be empty!" << std::endl; }
    outValue = BuRiTTO_InvalidValue;
    if(buritto.pop(outValue) == true) { std::cout << "1100 Failure: BuRiTTO should not return data!" << std::endl; }
    if(outValue != BuRiTTO_InvalidValue) { std::cout << "1110 Failure: BuRiTTO should not return data!" << std::endl; }
    
    for(int i = 0; i < 3 * (BURITTO_CAPACITY + 1); i++) {
        outValue = BuRiTTO_InvalidValue;
        if(buritto.push(pushCounter, outValue) == true) {
            if(outValue != BuRiTTO_InvalidValue) { std::cout << "1120 Failure: BuRiTTO should not return data!" << std::endl; }
        }else {
            if(outValue != dataCounter) { std::cout << "1130 Failure: BuRiTTO lost data! " << "Expected: " << dataCounter << " Actual: " << outValue << std::endl; }
            dataCounter++;
        }
        pushCounter++;
        if(buritto.empty()) { std::cout << "1140 Failure: BuRiTTO should not be empty!" << std::endl; }
    }
    
    outValue = BuRiTTO_InvalidValue;
    while(buritto.pop(outValue) == true) {
        if(outValue != dataCounter) { std::cout << "1160 Failure: BuRiTTO lost data! " << "Expected: " << dataCounter << " Actual: " << outValue << std::endl; }
        dataCounter++;
        outValue = BuRiTTO_InvalidValue;
    }
    if(outValue != BuRiTTO_InvalidValue) { std::cout << "1170 Failure: BuRiTTO should not return data!" << std::endl; }
    
    if(!buritto.empty()) { std::cout << "1180 Failure: BuRiTTO should be empty!" << std::endl; }
    if(pushCounter != dataCounter) { std::cout << "1190 Failure: BuRiTTO lost data!" << std::endl; }
    
    const int maxLoops = 1000;
    int loops = maxLoops;
    while(loops-- > 0) {
        pushCounter = BuRiTTO_CounterStartValue;
        BuRiTTOData overflowCounter {0};
        BuRiTTOData popCounter {BuRiTTO_CounterStartValue};
        std::atomic<bool> pushThreadFinished {false};
        
        std::vector<BuRiTTOData> overflowData;
        std::vector<BuRiTTOData> popData;
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        auto pushThread = std::thread([&] {
            std::cout << "Thread push: on CPU " << sched_getcpu() << std::endl;
            for(int i = 0; i < 1000000; i++) {
                BuRiTTOData out {BuRiTTO_InvalidValue};
                if(buritto.push(pushCounter, out) == true) {
                    if(out != BuRiTTO_InvalidValue) { std::cout << "Failure: pushThread BuRiTTO should not return data!" << std::endl; break; }
                }else {
                    if(out == BuRiTTO_InvalidValue) { std::cout << "Failure: pushThread BuRiTTO should return data!" << std::endl; break; }
                    overflowCounter++;
                    overflowData.push_back(out);
                }
                pushCounter++;
            }
            
            pushThreadFinished = true;
        });
        
        auto popThread = std::thread([&] {
            std::cout << "Thread pop: on CPU " << sched_getcpu() << std::endl;
            while(!pushThreadFinished.load(std::memory_order_relaxed) || !buritto.empty()) {
                BuRiTTOData out {BuRiTTO_InvalidValue};
                if(buritto.pop(out) == true) {
                    if(out == BuRiTTO_InvalidValue) { std::cout << "Failure: popThread BuRiTTO should return data!" << std::endl; break; }
                    popCounter++;
                    popData.push_back(out);
                }else {
                    if(out != BuRiTTO_InvalidValue) { std::cout << "Failure: popThread BuRiTTO should not return data!" << std::endl; break; }
                }
            }
        });
        
//         // set CPU affinity
//         // fastest option is when both threads run on the same core but with hyperthreading
//         cpu_set_t cpuset;
//         CPU_ZERO(&cpuset);
//         CPU_SET(0, &cpuset);
//         pthread_setaffinity_np(pushThread.native_handle(), sizeof(cpu_set_t), &cpuset);
//         CPU_ZERO(&cpuset);
//         CPU_SET(1, &cpuset);
//         pthread_setaffinity_np(popThread.native_handle(), sizeof(cpu_set_t), &cpuset);
        
        pushThread.join();
        popThread.join();
        
        std::cout << "loop: " << maxLoops - loops << std::endl;
        auto elapsedTime = std::chrono::high_resolution_clock::now() - startTime;
        std::cout << "duration: " << std::chrono::duration_cast<std::chrono::milliseconds> (elapsedTime).count() << "ms" << std::endl;
        
        std::cout << "push counter \t" << pushCounter << std::endl;
        std::cout << "overflow + pop \t" << overflowCounter + popCounter << std::endl;
        std::cout << "overflow counter \t" << overflowCounter << std::endl;
        std::cout << "pop counter \t" << popCounter << std::endl;
        
        std::cout << "overflow values: \t";
        int i = 0;
        for(auto& data: overflowData) {
            std::cout << data << " ";
            i++;
            if(i > 100) { break; }
        }
        std::cout << std::endl;
        
        std::cout << "pop values: \t";
        i = 0;
        for(auto& data: popData) {
            std::cout << data << " ";
            i++;
            if(i > 100) { break; }
        }
        std::cout << std::endl;
        
        size_t overflowIndex = 0;
        size_t popIndex = 0;
        bool dataIntact = true;
        for(size_t i = BuRiTTO_CounterStartValue; i < pushCounter; i++) {
            if(overflowIndex < overflowData.size() && overflowData[overflowIndex] == i) {
                overflowIndex++;
            } else if(popIndex < popData.size() && popData[popIndex] == i) {
                popIndex++;
            } else {
                std::cout << "data loss detected at index: " << i << std::endl;
                dataIntact = false;
                break;
            }
        }
        
        bool success = dataIntact && pushCounter == (overflowCounter + popCounter);
        std::cout << "Everything went fine? " << (success ? "yes" : "no") << std::endl;
        std::cout << "=========================" << std::endl;
        
        if(!success) { break; }
    }

    return 0;
}
