#include <iostream>
#include <atomic>
#include <thread>
#include <vector>

#include "buritto.hpp"

enum {
    BURITTO_CAPACITY = 10,
    BURITTO_REAL_CAPACITY = BURITTO_CAPACITY + 1
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
    for(int i = 0; i < BURITTO_REAL_CAPACITY; i++) {
        outValue = BuRiTTO_InvalidValue;
        if(buritto.push(pushCounter, outValue) == false) { std::cout << "1010 Failure: BuRiTTO should not overrun!" << std::endl; }
        pushCounter++;
        if(outValue != BuRiTTO_InvalidValue) { std::cout << "1020 Failure: BuRiTTO should not return data!" << std::endl; }
        
        if(buritto.empty()) { std::cout << "1030 Failure: BuRiTTO should not be empty!" << std::endl; }
    }
    
    outValue = BuRiTTO_InvalidValue;
    if(buritto.push(pushCounter, outValue) == true) { std::cout << "1040 Failure: BuRiTTO should overrun!" << std::endl; }
    pushCounter++;
    if(outValue != dataCounter) { std::cout << "1050 Failure: BuRiTTO should overrun and return data!" << std::endl; }
    dataCounter++;
    
    for(int i = 0; i < BURITTO_REAL_CAPACITY; i++) {
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
    
    for(int i = 0; i < 3 * BURITTO_REAL_CAPACITY; i++) {
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
    
    pushCounter = BuRiTTO_CounterStartValue;
    BuRiTTOData overrunCounter {0};
    BuRiTTOData popCounter {BuRiTTO_CounterStartValue};
    bool pushThreadFinished {false};
    
    std::vector<BuRiTTOData> overrunData;
    std::vector<BuRiTTOData> popData;
    
    auto pushThread = std::thread([&] {
        for(int i = 0; i < 1000000; i++) {
            BuRiTTOData out {BuRiTTO_InvalidValue};
            if(buritto.push(pushCounter, out) == true) {
                if(out != BuRiTTO_InvalidValue) { std::cout << "Failure: pushThread BuRiTTO should not return data!" << std::endl; break; }
            }else {
                if(out == BuRiTTO_InvalidValue) { std::cout << "Failure: pushThread BuRiTTO should return data!" << std::endl; break; }
                overrunCounter++;
                overrunData.push_back(out);
            }
            pushCounter++;
        }
        
        pushThreadFinished = true;
    });
    
    auto popThread = std::thread([&] {
        while(!pushThreadFinished || !buritto.empty()) {
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
    
    pushThread.join();
    popThread.join();
    
    std::cout << "push Counter \t" << pushCounter << std::endl;
    std::cout << "overrun + pop \t" << overrunCounter + popCounter << std::endl;
    std::cout << "overrun Counter \t" << overrunCounter << std::endl;
    std::cout << "pop Counter \t" << popCounter << std::endl;
    
    int i = 0;
    for(auto& data: overrunData) {
        std::cout << data << " ";
        i++;
        if(i > 100) { break; }
    }
    std::cout << std::endl;
    
    i = 0;
    for(auto& data: popData) {
        std::cout << data << " ";
        i++;
        if(i > 100) { break; }
    }
    std::cout << std::endl;
    
    int overrunIndex = 0;
    int popIndex = 0;
    bool dataIntact = true;
    for(int i = 0; i < pushCounter; i++) {
        if(overrunIndex < overrunData.size() && overrunData[overrunIndex] == i) {
            overrunIndex++;
        } else if(popIndex < popData.size() && popData[popIndex] == i) {
            popIndex++;
        } else {
            std::cout << "data loss detected at index: " << i << std::endl;
            dataIntact = false;
            break;
        }
    }
    
    std::cout << "Everything went fine? " << ((dataIntact && pushCounter == (overrunCounter + popCounter)) ? "yes" : "no") << std::endl;
    

    return 0;
}
