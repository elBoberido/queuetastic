// SPDX-License-Identifier: GPL-3.0-only
// SPDX-FileCopyrightText: © 2018 - 2023 Mathias Kraus <elboberido@m-hias.de>

#include "buritto.hpp"

#include "catch.hpp"

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

SCENARIO("BuRiTTO - Unittest") {
    constexpr std::uint32_t ContainerCapacity { 10 };
    using DataType = size_t;
    using BuRiTTO = BuRiTTO<DataType, ContainerCapacity>;
    
    constexpr DataType BuRiTTO_CounterStartValue { 0 };
    constexpr DataType BuRiTTO_InvalidValue { -1u };
    
    DataType dataCounter { BuRiTTO_CounterStartValue };
    DataType pushCounter { BuRiTTO_CounterStartValue };
    DataType outValue { BuRiTTO_InvalidValue };
    
    GIVEN("An BuRiTTO with a fixed capacity") {
        BuRiTTO buritto;
        
        WHEN("the buritto was just created") {
            THEN("it should be empty") {
                REQUIRE(buritto.empty() == true);
            }
            
            AND_WHEN("calling pop") {
                outValue = BuRiTTO_InvalidValue;
                bool popReturnValue = buritto.pop(outValue);
                THEN("it should not return data") {
                    REQUIRE(popReturnValue == false);
                    REQUIRE(outValue == BuRiTTO_InvalidValue);
                }
            }
        }
        
        WHEN("filling the buritto to the point before overrun") {
            bool pushReturnValue { false };
            bool emptyReturnValue { false };
            constexpr size_t ExtraCapacity = 1;
            for(auto i = 0u; i < ContainerCapacity + ExtraCapacity; i++) {
                outValue = BuRiTTO_InvalidValue;
                
                pushReturnValue = buritto.push(pushCounter, outValue);
                emptyReturnValue = buritto.empty();
                pushCounter++;
                
                if(pushReturnValue == false) { break; }
                if(emptyReturnValue == true) { break; }
                if(outValue != BuRiTTO_InvalidValue) { break; }
            }
            
            THEN("it should not overrun, return data or be empty") {
                REQUIRE(pushReturnValue == true);
                REQUIRE(outValue == BuRiTTO_InvalidValue);
                REQUIRE(emptyReturnValue == false);
            }
            
            AND_WHEN("pushing more data") {
                outValue = BuRiTTO_InvalidValue;
                pushReturnValue = buritto.push(pushCounter, outValue);
                emptyReturnValue = buritto.empty();
                pushCounter++;
                
                THEN("it should overrun, return data and not be empty") {
                    REQUIRE(pushReturnValue == false);
                    REQUIRE(outValue == dataCounter);
                    REQUIRE(emptyReturnValue == false);
                }
                
                dataCounter++;
                
                AND_WHEN("pop all data out") {
                    bool popReturnValue { false };
                    for(auto i = 0u; i < ContainerCapacity + ExtraCapacity; i++) {
                        outValue = BuRiTTO_InvalidValue;
                        
                        emptyReturnValue = buritto.empty();
                        popReturnValue = buritto.pop(outValue);
                        dataCounter++;
                        
                        if(popReturnValue == false) { break; }
                        if(emptyReturnValue == true) { break; }
                        if(outValue != dataCounter-1) { break; }
                    }
                    
                    THEN("it should always return data and not be empty") {
                        REQUIRE(popReturnValue == true);
                        REQUIRE(outValue == dataCounter-1);
                        REQUIRE(emptyReturnValue == false);
                    }
                    
                    AND_WHEN("all data is out") {
                        
                        THEN("it should be empty") {
                            REQUIRE(buritto.empty() == true);
                            outValue = BuRiTTO_InvalidValue;
                            popReturnValue = buritto.pop(outValue);
                            REQUIRE(popReturnValue == false);
                            REQUIRE(outValue == BuRiTTO_InvalidValue);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("BuRiTTO - Stress", "[.stress]") {
    constexpr std::uint32_t ContainerCapacity { 10 };
    using DataType = size_t;
    using BuRiTTO = BuRiTTO<DataType, ContainerCapacity>;
    
    constexpr DataType BuRiTTO_CounterStartValue { 0 };
    constexpr DataType BuRiTTO_InvalidValue { -1u };
    
    DataType dataCounter { BuRiTTO_CounterStartValue };
    DataType pushCounter { BuRiTTO_CounterStartValue };
    DataType popCounter { BuRiTTO_CounterStartValue };
    DataType outValue { BuRiTTO_InvalidValue };
    DataType overrunCounter {0};
    std::atomic<bool> pushThreadFinished {false};
    
    BuRiTTO buritto;
    
    std::vector<DataType> overrunData;
    std::vector<DataType> popData;
    
    std::mutex mtx;
    std::condition_variable condVar;
    bool run { false };
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    auto pushThread = std::thread([&] {
        {
            std::unique_lock<std::mutex> lock(mtx);
            condVar.wait(lock, [&]() -> bool { return run; });
        }
        std::cout << "Thread push: on CPU " << sched_getcpu() << std::endl;
        for(int i = 0; i < 1000000; i++) {
            DataType out {BuRiTTO_InvalidValue};
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
        {
            std::unique_lock<std::mutex> lock(mtx);
            condVar.wait(lock, [&]() -> bool { return run; });
        }
        std::cout << "Thread pop: on CPU " << sched_getcpu() << std::endl;
        while(!pushThreadFinished.load(std::memory_order_relaxed) || !buritto.empty()) {
            DataType out {BuRiTTO_InvalidValue};
            if(buritto.pop(out) == true) {
                if(out == BuRiTTO_InvalidValue) { std::cout << "Failure: popThread BuRiTTO should return data!" << std::endl; break; }
                popCounter++;
                popData.push_back(out);
            }else {
                if(out != BuRiTTO_InvalidValue) { std::cout << "Failure: popThread BuRiTTO should not return data!" << std::endl; break; }
            }
        }
    });
    
//     // set CPU affinity
//     // fastest option is when both threads run on the same core but with hyperthreading
//     cpu_set_t cpuset;
//     CPU_ZERO(&cpuset);
//     CPU_SET(0, &cpuset);
//     pthread_setaffinity_np(pushThread.native_handle(), sizeof(cpu_set_t), &cpuset);
//     CPU_ZERO(&cpuset);
//     CPU_SET(1, &cpuset);
//     pthread_setaffinity_np(popThread.native_handle(), sizeof(cpu_set_t), &cpuset);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    BENCHMARK("BuRiTTO push and pop 1 million values") {
        {
            std::unique_lock<std::mutex> lock(mtx);
            run = true;
        }
        condVar.notify_all();
        
        pushThread.join();
        popThread.join();
    }
    
    auto elapsedTime = std::chrono::high_resolution_clock::now() - startTime;
    std::cout << "duration: " << std::chrono::duration_cast<std::chrono::milliseconds> (elapsedTime).count() << "ms" << std::endl;
    
    std::cout << "push counter \t" << pushCounter << std::endl;
    std::cout << "overrun + pop \t" << overrunCounter + popCounter << std::endl;
    std::cout << "overrun counter \t" << overrunCounter << std::endl;
    std::cout << "pop counter \t" << popCounter << std::endl;
    
    std::cout << "overrun values: \t";
    int i = 0;
    for(auto& data: overrunData) {
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
    
    size_t overrunIndex = 0;
    size_t popIndex = 0;
    bool dataIntact = true;
    for(size_t i = BuRiTTO_CounterStartValue; i < pushCounter; i++) {
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
    
    CHECK(dataIntact);
    CHECK(pushCounter == (overrunCounter + popCounter));
}

TEST_CASE("BuRiTTO - Benchmark", "[!benchmark]") {
    constexpr std::uint32_t ContainerCapacity { 100000 };
    using DataType = size_t;
    using BuRiTTO = BuRiTTO<DataType, ContainerCapacity>;
    
    DataType outValue { 0 };
    DataType sum { 0 };
    
    const DataType expectedSum {
        [&] () -> DataType {
            DataType sum { 0 };
            for(auto i = 0u; i < ContainerCapacity; i++) {
                sum += i;
            }
            return sum;
        }()
    };
    
    DataType carray[ContainerCapacity];
    BENCHMARK("Fill fixed size array (not thread safe)") {
        for(auto i = 0u; i < ContainerCapacity; i++) {
            carray[i] = i;
        }
    }
    sum = 0;
    for(auto& element: carray) {
        sum += element;
    }
    REQUIRE( sum == expectedSum );
    
    std::mutex mtx;
    BENCHMARK("Fill fixed size array (protected with mutex)") {
        for(auto i = 0u; i < ContainerCapacity; i++) {
            std::lock_guard<std::mutex> lock(mtx);
            carray[i] = i;
        }
    }
    sum = 0;
    for(auto& element: carray) {
        sum += element;
    }
    REQUIRE( sum == expectedSum );
    

    BuRiTTO buritto;
    bool moreThanOneBenchmarkIteration = false;
    BENCHMARK("Fill BuRiTTO without overrun") {
        for(auto i = 0u; i < ContainerCapacity; i++) {
            // overrun happens if measured time for one iteration is to short and the benchmark runs several iterations
            moreThanOneBenchmarkIteration |= !buritto.push(i, outValue);
        }
    }

    REQUIRE( moreThanOneBenchmarkIteration == false );
    if(moreThanOneBenchmarkIteration) { buritto.pop(outValue); }

    sum = 0;
    outValue = 0;
    while(buritto.pop(outValue)) {
        sum += outValue;
        outValue = 0;
    }
    // this will fail it there are more than one iterations at the benchmark
    REQUIRE( sum == expectedSum );
    
    
    for(auto i = 0u; i < ContainerCapacity; i++) {
        buritto.push(i, outValue);
    }
    BENCHMARK("Fill BuRiTTO continuously overrunning") {
        for(auto i = 0u; i < ContainerCapacity; i++) {
            buritto.push(i, outValue);
        }
    }
    buritto.pop(outValue); // this is due to the fact the the buritto holds one value in the pending transaction
    sum = 0;
    outValue = 0;
    while(buritto.pop(outValue)) {
        sum += outValue;
        outValue = 0;
    }
    REQUIRE( sum == expectedSum );
}
